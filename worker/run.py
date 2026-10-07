import argparse,json,os,pathlib,sys,traceback
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))

def write_status(path, **values):
    temp=path.with_suffix('.tmp')
    temp.write_text(json.dumps(values),encoding='utf-8')
    temp.replace(path)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('request');args=parser.parse_args()
    request_path=pathlib.Path(args.request).resolve()
    req=json.loads(request_path.read_text(encoding='utf-8'))
    status=request_path.parent/'status.json'
    os.environ['NUMBA_CACHE_DIR']=str(request_path.parent/'numba-cache')
    sys.pycache_prefix=str(request_path.parent/'pycache')
    try:
        write_status(status,state='loading',progress=0.01,message='Loading voice model...')
        import numpy as np
        import soundfile as sf
        from engine import VoiceEngine
        audio,rate=sf.read(req['input'],dtype='float32',always_2d=True)
        if rate<8000 or rate>192000 or not 0<len(audio)<=rate*180 or audio.shape[1]>2 or not np.isfinite(audio).all():
            raise ValueError('Use a finite mono/stereo recording up to 3 minutes, at 8–192 kHz.')
        audio=audio.mean(axis=1)
        engine=VoiceEngine(req['model'],req['resources'])
        def progress(value):write_status(status,state='converting',progress=.1+.85*value,message='Transforming voice...')
        output=engine.convert(audio,rate,float(req.get('pitch',0)),progress)
        sf.write(req['output'],output,rate,subtype='FLOAT')
        write_status(status,state='done',progress=1,message='Transformation ready',samples=len(output),sample_rate=rate)
        return 0
    except Exception as exc:
        write_status(status,state='error',progress=0,message=str(exc))
        traceback.print_exc()
        return 1

if __name__=='__main__':sys.exit(main())
