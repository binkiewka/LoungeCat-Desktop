#!/usr/bin/env python3
"""Fail closed before publication if tag/source/artifact identity does not match."""
from pathlib import Path
import argparse,hashlib,json,re,tarfile
p=argparse.ArgumentParser()
p.add_argument('--dist',type=Path,required=True)
p.add_argument('--tag',required=True)
p.add_argument('--commit',required=True)
a=p.parse_args();d=a.dist
m=json.loads((d/'package-manifest.json').read_text())
assert re.fullmatch(r'v\d+\.\d+\.\d+',a.tag),'Expected a stable vMAJOR.MINOR.PATCH tag'
assert a.tag=='v'+m['version'],'Tag and packaged version disagree'
assert re.fullmatch(r'[0-9a-f]{40}',a.commit),'Invalid source commit'
assert a.commit==m['sourceCommit']==(d/'SOURCE_COMMIT').read_text().strip(),'Source revision mismatch'
expected={f'LoungeCat-{m["version"]}-x86_64.AppImage',f'loungecat_{m["version"]}_amd64.deb',f'LoungeCat-{m["version"]}-source.tar.gz'}
assert {e['file'] for e in m['artifacts']}==expected,'Incomplete/unexpected release artifact set'
sums=[]
for e in m['artifacts']:
    path=d/e['file']
    assert path.stat().st_size==e['bytes'] and hashlib.sha256(path.read_bytes()).hexdigest()==e['sha256'],'Artifact identity mismatch: '+e['file']
    sums.append(f'{e["sha256"]}  {e["file"]}')
assert set((d/'SHA256SUMS').read_text().splitlines())==set(sums),'SHA256SUMS mismatch'
with tarfile.open(d/f'LoungeCat-{m["version"]}-source.tar.gz') as source:
    names={n.split('/',1)[1] for n in source.getnames() if '/' in n}
    assert {'CMakeLists.txt','LICENSE','src/main.cpp','packaging/build.sh'}<=names,'Matching source archive incomplete'
print('Release tag, source revision, source archive and all artifact checksums verified.')
