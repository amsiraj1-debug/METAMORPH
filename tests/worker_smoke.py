"""Exercise the complete packaged worker using random RVC weights (not a voice)."""
import argparse,json,os,pathlib,subprocess,sys,tempfile

def main():
    p=argparse.ArgumentParser();p.add_argument('resources',type=pathlib.Path);p.add_argument('--model',type=pathlib.Path);args=p.parse_args()
    resources=args.resources.resolve();sys.path.insert(0,str(resources/'worker'));sys.path.insert(0,str(resources/'rvc'))
    import numpy as np
    import soundfile as sf
    import torch
    from infer.module.models import SynthesizerTrnMs768NSFsid
    folder=pathlib.Path(tempfile.mkdtemp(prefix='metamorph-smoke-'))
    config=[1025,32,32,32,64,2,2,3,0.0,'1',[3,7,11],[[1,3,5],[1,3,5],[1,3,5]],[10,8,2,2],128,[20,16,4,4],1,32,32000]
    model=args.model
    if not model:
        torch.manual_seed(1);net=SynthesizerTrnMs768NSFsid(*config,is_half=False)
        model=folder/'synthetic-test.pth';torch.save({'config':config,'weight':net.state_dict(),'version':'v2','f0':1},model)
        del net
    rate=24000;t=np.arange(rate)/rate;source=(.12*np.sin(2*np.pi*160*t)).astype(np.float32)
    sf.write(folder/'input.wav',source,rate,subtype='FLOAT')
    request={'input':str(folder/'input.wav'),'output':str(folder/'output.wav'),'resources':str(resources),'model':str(model.resolve()),'pitch':0}
    (folder/'request.json').write_text(json.dumps(request),encoding='utf-8')
    subprocess.run([sys.executable,'-B',str(resources/'worker'/'run.py'),str(folder/'request.json')],check=True,timeout=600)
    audio,actual_rate=sf.read(folder/'output.wav',dtype='float32')
    assert actual_rate==rate and len(audio)==len(source) and np.isfinite(audio).all()
    assert float(np.sqrt(np.mean((audio-source)**2)))>1e-5,'Worker returned unchanged audio'
    print('PASS: full RVC worker, exact audio length/rate, finite output and nontrivial conversion')

if __name__=='__main__':main()
