"""Create a portable VST3 folder containing Python, RVC and support models."""
import argparse,hashlib,json,pathlib,shutil,subprocess,sys,urllib.request,zipfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
PYTHON_URL='https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip'
PYTHON_SHA='4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3'
HF='https://huggingface.co/lj1995/VoiceConversionWebUI/resolve/'
WEIGHTS=[
 ('rmvpe/rmvpe.pt','0658a97f086c16951f55b4349c0b25503b6ffdba/rmvpe.pt','6d62215f4306e3ca278246188607209f09af3dc77ed4232efdd069798c4ec193'),
 ('hubert_base/pytorch_model.bin','1be9d36ece685661920e1a7cb36eb0437c1e5581/hubert_base/pytorch_model.bin','cc8c20f4b90a520757260197a3ff2505705a7adbd20ad9eeaa4e1a9b38442ef5'),
 ('hubert_base/config.json','1be9d36ece685661920e1a7cb36eb0437c1e5581/hubert_base/config.json',None),
 ('hubert_base/preprocessor_config.json','1be9d36ece685661920e1a7cb36eb0437c1e5581/hubert_base/preprocessor_config.json',None),
]

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def download(url,path,sha=None):
    path.parent.mkdir(parents=True,exist_ok=True)
    if path.is_file() and sha and digest(path)==sha:return
    print('Downloading',path.name,flush=True)
    with urllib.request.urlopen(url,timeout=90) as source,path.with_suffix(path.suffix+'.part').open('wb') as target:
        shutil.copyfileobj(source,target,1024*1024)
    temp=path.with_suffix(path.suffix+'.part')
    if sha and digest(temp)!=sha:raise RuntimeError('Checksum mismatch: '+path.name)
    temp.replace(path)

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=pathlib.Path,required=True);p.add_argument('--destination',type=pathlib.Path,default=ROOT/'dist');p.add_argument('--local-runtime',type=pathlib.Path);a=p.parse_args()
    source=a.build/'MetamorphRebuild_artefacts'/'Release'/'VST3'/'Metamorph Rebuild.vst3'
    if not source.is_dir():raise SystemExit('Build the Release VST3 target first.')
    package=a.destination.resolve()/'Metamorph-Rebuild-Windows-x64'
    plugin=package/source.name
    shutil.copytree(source,plugin,dirs_exist_ok=True)
    resources=plugin/'Contents'/'Resources'
    for name in ('worker','rvc'):shutil.copytree(ROOT/name,resources/name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
    if a.local_runtime:
        for name in ('runtime','assets'):shutil.copytree(a.local_runtime/name,resources/name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
    else:
        archive=ROOT/'dependencies'/'python-embed.zip';download(PYTHON_URL,archive,PYTHON_SHA)
        runtime=resources/'runtime';runtime.mkdir(parents=True,exist_ok=True)
        with zipfile.ZipFile(archive) as z:z.extractall(runtime)
        (runtime/'python312._pth').write_text('python312.zip\n.\nLib/site-packages\nimport site\n',encoding='ascii')
        subprocess.run([sys.executable,'-m','pip','install','--disable-pip-version-check','--only-binary=:all:','--target',str(runtime/'Lib'/'site-packages'),'-r',str(ROOT/'worker'/'requirements.txt')],check=True)
        for name,url,sha in WEIGHTS:download(HF+url,resources/'assets'/name,sha)
    for name in ('README.md','LICENSE','THIRD_PARTY.md'):shutil.copy2(ROOT/name,package/name)
    (resources/'assets'/'WEIGHTS-NOTICE.txt').write_text('RVC support weights: https://huggingface.co/lj1995/VoiceConversionWebUI\nRepository license MIT. No target voice model is included.\n'+json.dumps(WEIGHTS,indent=2),encoding='utf-8')
    records=[]
    for path in sorted(package.rglob('*')):
        if path.is_file():records.append({'path':path.relative_to(package).as_posix(),'size':path.stat().st_size,'sha256':digest(path)})
    (package/'SHA256-MANIFEST.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
    print('Package ready:',package,flush=True)
    return package

if __name__=='__main__':main()
