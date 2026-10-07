"""Recover validated resources, not source code, from a user-supplied PE binary."""
import argparse, collections, hashlib, html, io, json, pathlib, re, struct, sys, zlib
import xml.etree.ElementTree as ET

def extract(source, destination):
    source, destination = pathlib.Path(source), pathlib.Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    data = source.read_bytes()
    items = []
    seen = set()
    def save(kind, offset, payload, **meta):
        digest = hashlib.sha256(payload).hexdigest()
        if (kind,digest) in seen: return
        seen.add((kind,digest))
        name = f'{kind}/{offset:08x}.{kind}'
        path = destination/name; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        items.append(dict(path=name,offset=offset,size=len(payload),sha256=digest,**meta))
    for m in re.finditer(re.escape(b'\x89PNG\r\n\x1a\n'),data):
        start=m.start(); pos=start+8; valid=False; width=height=0
        for _ in range(5000):
            if pos+12>len(data): break
            size=struct.unpack_from('>I',data,pos)[0]; kind=data[pos+4:pos+8]
            if size>32*1024*1024 or pos+12+size>len(data): break
            body=data[pos+8:pos+8+size]
            crc=struct.unpack_from('>I',data,pos+8+size)[0]
            if zlib.crc32(kind+body)&0xffffffff!=crc: break
            if pos==start+8:
                if kind!=b'IHDR' or size!=13: break
                width,height=struct.unpack_from('>II',body)
            pos+=12+size
            if kind==b'IEND': valid=True; break
        if valid: save('png',start,data[start:pos],width=width,height=height)
    for m in re.finditer(rb'<svg\b',data):
        end=data.find(b'</svg>',m.start(),m.start()+4*1024*1024)
        if end<0: continue
        payload=data[m.start():end+6]
        try:
            root=ET.fromstring(payload)
            if not root.tag.endswith('svg'): continue
            save('svg',m.start(),payload,viewBox=root.get('viewBox',''))
        except ET.ParseError: pass
    for sig,kind in [(b'OTTO','otf'),(b'\x00\x01\x00\x00','ttf')]:
        for m in re.finditer(re.escape(sig),data):
            start=m.start()
            if start+12>len(data): continue
            n=struct.unpack_from('>H',data,start+4)[0]
            if not 4<=n<=80 or start+12+16*n>len(data): continue
            tables=[]; end=12+16*n
            for i in range(n):
                tag,checksum,offset,length=struct.unpack_from('>4sIII',data,start+12+16*i)
                if not re.fullmatch(rb'[A-Za-z0-9 /]{4}',tag) or length>20*1024*1024 or offset<12+16*n or start+offset+length>len(data): break
                end=max(end,offset+length); tables.append(tag)
            if len(tables)==n and b'head' in tables and b'name' in tables:
                save(kind,start,data[start:start+end])
    strings=[]
    for m in re.finditer(rb'[\x09\x0a\x0d\x20-\x7e]{6,}',data):
        text=m.group().decode('ascii')
        strings.append(f'{m.start():08x}\t{text}')
    (destination/'strings-ascii.txt').write_text('\n'.join(strings),encoding='utf-8')
    names=sorted({m.group().decode() for m in re.finditer(rb'[A-Za-z_][A-Za-z0-9_. -]{1,100}\.(?:png|svg|ttf|otf|jpg|xml|onnx|pth)\b',data)})
    (destination/'resource-names.txt').write_text('\n'.join(names),encoding='utf-8')
    # PE sections retain encrypted machine code. They are forensic data, not build inputs.
    sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[3]/'work'/'deps'))
    try:
        import pefile
        pe=pefile.PE(data=data,fast_load=True)
        sections=[]
        for i,s in enumerate(pe.sections):
            name=s.Name.rstrip(b'\0').decode(errors='replace')
            filename=f'raw_sections/{i:02d}_{re.sub("[^A-Za-z0-9_.-]","_",name)}.bin'
            p=destination/filename;p.parent.mkdir(exist_ok=True);p.write_bytes(s.get_data())
            sections.append(dict(name=name,path=filename,rva=s.VirtualAddress,offset=s.PointerToRawData,size=s.SizeOfRawData,entropy=s.get_entropy()))
    except ImportError: sections=[]
    report=dict(input_name=source.name,input_sha256=hashlib.sha256(data).hexdigest(),resources=items,sections=sections,counts=dict(collections.Counter(pathlib.Path(x['path']).suffix for x in items)),limitation='Extracted graphics and binary data do not recover encrypted DSP or original C++/UI source.')
    (destination/'manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    cards=''.join(f'<article><img loading="lazy" src="{html.escape(x["path"])}"><p>{html.escape(x["path"])}<br>{x["size"]:,} bytes</p></article>' for x in items if x['path'].endswith(('.svg','.png')))
    (destination/'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Metamorph recovered assets</title><style>body{background:#181b24;color:#eee;font:15px system-ui;margin:32px}main{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:16px}article{background:#303642;padding:16px;overflow:hidden}img{width:100%;height:170px;object-fit:contain}p{font:12px monospace}h1{font-size:28px}</style><h1>Recovered Metamorph graphics</h1><p>Validated embedded resources. This gallery is not the original plugin UI or recovered application source.</p><main>'+cards+'</main>',encoding='utf-8')
    print(json.dumps({'counts':report['counts'],'resources':len(items),'output':str(destination)}))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('destination');a=p.parse_args();extract(a.source,a.destination)
