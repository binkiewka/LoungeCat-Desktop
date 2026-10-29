#!/usr/bin/env python3
from pathlib import Path
import argparse, hashlib, json, re, subprocess
p=argparse.ArgumentParser()
p.add_argument('--kind', choices=['rpm','gentoo'], required=True)
p.add_argument('--dist', type=Path, required=True)
p.add_argument('--source', type=Path)
a=p.parse_args(); d=a.dist
version=re.search(r'project\(LoungeCat VERSION ([0-9.]+)',Path('CMakeLists.txt').read_text())[1]
commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
files=sorted(d.glob('*.rpm')) if a.kind=='rpm' else sorted([*d.glob('*.ebuild'),*d.glob('*-gentoo-overlay.tar.gz')])
expected={f'loungecat-{version}-1.fc44.x86_64.rpm'} if a.kind=='rpm' else {f'loungecat-{version}.ebuild',f'LoungeCat-{version}-gentoo-overlay.tar.gz'}
assert {f.name for f in files}==expected, files
m={'version':version,'architecture':'x86_64','kind':a.kind,'sourceCommit':commit,
   'buildBaseline':'Fedora 44' if a.kind=='rpm' else 'Gentoo amd64 glibc / EAPI 8',
   'artifacts':[{'file':f.name,'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in files]}
if a.source:m['sourceSha256']=hashlib.sha256(a.source.read_bytes()).hexdigest()
(d/'package-manifest.json').write_text(json.dumps(m,indent=2)+'\n')
(d/'SOURCE_COMMIT').write_text(commit+'\n')
(d/'SHA256SUMS').write_text(''.join(f"{e['sha256']}  {e['file']}\n" for e in m['artifacts']))
