"""Offline RVC inference. No account, network service, or licensing runtime.

RVC architecture is supplied separately under its MIT license. The inference
integration is informed by the user's AGPL VocalMorph distribution.
"""
import math
import os
from pathlib import Path
import sys

os.environ['HF_HUB_OFFLINE']='1'
os.environ['TORCH_FORCE_WEIGHTS_ONLY_LOAD']='1'

def resample(audio, old, new):
    import numpy as np
    from scipy.signal import resample_poly
    if old==new: return np.asarray(audio,dtype=np.float32)
    divisor=math.gcd(int(old),int(new))
    return resample_poly(audio,int(new)//divisor,int(old)//divisor).astype(np.float32)

class VoiceEngine:
    def __init__(self, model, resources):
        import torch
        model=Path(model); resources=Path(resources)
        if model.suffix.lower()!='.pth' or not model.is_file():
            raise ValueError('Select an RVC voice .pth file.')
        if model.stat().st_size>512*1024*1024:
            raise ValueError('Voice checkpoint exceeds the 512 MB limit.')
        assets=Path(os.environ.get('METAMORPH_ASSETS',str(resources/'assets')))
        self.rmvpe=assets/'rmvpe'/'rmvpe.pt'
        self.hubert=assets/'hubert_base'
        if not self.rmvpe.is_file() or not (self.hubert/'config.json').is_file():
            raise ValueError('Support models are missing. Install the full Windows package.')
        sys.path.insert(0,str(resources/'rvc'))
        from infer.module.models import SynthesizerTrnMs256NSFsid,SynthesizerTrnMs768NSFsid
        from infer.rmvpe import RMVPE
        from infer.hubert import HubertModelWithFinalProj
        from transformers import AutoFeatureExtractor
        torch.set_num_threads(max(1,min(4,(os.cpu_count() or 2)//2)))
        checkpoint=torch.load(model,map_location='cpu',weights_only=True)
        if not isinstance(checkpoint,dict) or not isinstance(checkpoint.get('weight'),dict):
            raise ValueError('Expected an RVC inference checkpoint with weight and config fields.')
        self.version=checkpoint.get('version','v1')
        if self.version not in ('v1','v2') or checkpoint.get('f0',1)!=1:
            raise ValueError('Supported models are RVC v1/v2 with F0 (pitch) support.')
        config=checkpoint.get('config')
        if not isinstance(config,(list,tuple)) or len(config)!=18:
            raise ValueError('Unsupported RVC model configuration.')
        config=list(config)
        for i,limit in ((0,4097),(1,128),(2,512),(3,1024),(4,4096),(5,16),(6,24),(7,31),(13,2048),(16,1024)):
            if not isinstance(config[i],int) or not 0<config[i]<=limit:
                raise ValueError('Unsupported RVC architecture size.')
        if not isinstance(config[10],(list,tuple)) or not 1<=len(config[10])<=8:
            raise ValueError('Invalid RVC residual block configuration.')
        if not isinstance(config[12],(list,tuple)) or not 1<=len(config[12])<=8 or any(not isinstance(x,int) or not 1<=x<=32 for x in config[12]):
            raise ValueError('Invalid RVC upsampling configuration.')
        weights=checkpoint['weight']
        if any(not isinstance(v,torch.Tensor) for v in weights.values()):
            raise ValueError('The checkpoint contains non-tensor weights.')
        if sum(v.numel()*v.element_size() for v in weights.values())>512*1024*1024:
            raise ValueError('The model exceeds the supported tensor size.')
        speaker=weights.get('emb_g.weight')
        if speaker is None or speaker.ndim!=2 or not 1<=speaker.shape[0]<=1000:
            raise ValueError('Invalid RVC speaker embedding.')
        config[-3]=speaker.shape[0]
        self.rate=int(config[-1])
        if self.rate not in (32000,40000,48000):
            raise ValueError('Supported model rates are 32, 40, and 48 kHz.')
        ctor=SynthesizerTrnMs768NSFsid if self.version=='v2' else SynthesizerTrnMs256NSFsid
        self.net=ctor(*config,is_half=False)
        del self.net.enc_q
        status=self.net.load_state_dict(weights,strict=False)
        if status.missing_keys or any(not k.startswith('enc_q.') for k in status.unexpected_keys):
            raise ValueError('Checkpoint tensors do not match the RVC architecture.')
        self.net=self.net.float().eval()
        self.net.remove_weight_norm()
        self.encoder=HubertModelWithFinalProj.from_pretrained(str(self.hubert),local_files_only=True,use_safetensors=(self.hubert/'model.safetensors').is_file(),attn_implementation='eager').float().eval()
        self.normalize=AutoFeatureExtractor.from_pretrained(str(self.hubert),local_files_only=True).do_normalize
        self.pitch=RMVPE(str(self.rmvpe),is_half=False,device='cpu')
        self.torch=torch

    def convert_chunk(self, audio, rate, semitones):
        import numpy as np
        torch=self.torch
        x=resample(audio,rate,16000)
        x=np.pad(x,(0,max(0,1600-len(x))))
        torch.manual_seed(0)
        with torch.inference_mode():
            source=torch.from_numpy(x.copy()).float()[None]
            if self.normalize: source=(source-source.mean())/(source.var(unbiased=False)+1e-7).sqrt()
            if self.version=='v1': features=self.encoder.final_proj(self.encoder(source,output_hidden_states=True).hidden_states[9])
            else: features=self.encoder(source).last_hidden_state
            length=len(x)//160
            features=torch.nn.functional.interpolate(features.transpose(1,2),size=length,mode='nearest').transpose(1,2)
            f0=self.pitch.infer_from_audio(torch.from_numpy(x.copy()),thred=.03)[:length]
            f0=np.pad(f0,(0,max(0,length-len(f0))))*2**(semitones/12)
            mel=1127*np.log1p(f0/700)
            low=1127*np.log1p(50/700);high=1127*np.log1p(1100/700)
            coarse=np.rint(np.clip((mel-low)*254/(high-low)+1,1,255)).astype(np.int64)
            y=self.net.infer(features,torch.tensor([length]),torch.from_numpy(coarse)[None],torch.from_numpy(f0.astype(np.float32))[None],torch.tensor([0]))[0][0,0].float().numpy()
        y=resample(y,self.rate,rate)
        return np.pad(y,(0,max(0,len(audio)-len(y))))[:len(audio)]

    def convert(self,audio,rate,semitones=0,progress=lambda fraction:None):
        import numpy as np
        if not math.isfinite(semitones) or not -24<=semitones<=24:
            raise ValueError('Pitch must be between -24 and +24 semitones.')
        # Use context on both sides; adjacent core regions are kept at exact length.
        size=int(rate*6);context=int(rate*.3);result=np.empty_like(audio)
        for start in range(0,len(audio),size):
            end=min(len(audio),start+size);lo=max(0,start-context);hi=min(len(audio),end+context)
            wet=self.convert_chunk(audio[lo:hi],rate,semitones)
            result[start:end]=wet[start-lo:end-lo]
            progress(end/len(audio))
        if not np.isfinite(result).all(): raise ValueError('Inference produced invalid audio.')
        return np.clip(result,-1,1).astype(np.float32)
