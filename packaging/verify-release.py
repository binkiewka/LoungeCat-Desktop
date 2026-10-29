#!/usr/bin/env python3
"""Fail closed before publication if tag/source/artifact identity does not match."""
from pathlib import Path
import argparse,hashlib,json,re,tarfile

def verify(d,tag,commit):
    m=json.loads((d/'package-manifest.json').read_text());version=m['version']
    assert re.fullmatch(r'v\d+\.\d+\.\d+',tag),'Expected a stable vMAJOR.MINOR.PATCH tag'
    assert tag=='v'+version,'Tag and packaged version disagree'
    assert re.fullmatch(r'[0-9a-f]{40}',commit),'Invalid source commit'
    assert commit==m['sourceCommit']==(d/'SOURCE_COMMIT').read_text().strip(),'Source revision mismatch'
    expected={f'LoungeCat-{version}-x86_64.AppImage',f'loungecat_{version}_amd64.deb',
        f'LoungeCat-{version}-source.tar.gz',f'loungecat-{version}-1.fc44.x86_64.rpm',
        f'loungecat-{version}.ebuild',f'LoungeCat-{version}-gentoo-overlay.tar.gz',
        'build-dependencies.tsv','rpm-build-dependencies.tsv','gentoo-build-dependencies.tsv'}
    assert len(m['artifacts'])==len(expected) and {e['file'] for e in m['artifacts']}==expected,'Incomplete/unexpected release artifact set'
    sums=[]
    for e in m['artifacts']:
        path=d/e['file']
        assert path.stat().st_size==e['bytes'] and hashlib.sha256(path.read_bytes()).hexdigest()==e['sha256'],'Artifact identity mismatch: '+e['file']
        sums.append(f"{e['sha256']}  {e['file']}")
    assert len((d/'SHA256SUMS').read_text().splitlines())==len(sums)
    assert set((d/'SHA256SUMS').read_text().splitlines())==set(sums),'SHA256SUMS mismatch'
    source=d/f'LoungeCat-{version}-source.tar.gz'; prefix=f'LoungeCat-{version}-source/'
    with tarfile.open(source) as archive:
        names=set(archive.getnames())
        assert {prefix+n for n in ['CMakeLists.txt','LICENSE','src/main.cpp','packaging/build.sh']}<=names,'Matching source archive incomplete'
        cmake=archive.extractfile(prefix+'CMakeLists.txt').read().decode()
        assert re.search(r'project\(LoungeCat VERSION ([0-9.]+)',cmake)[1]==version,'Source archive version mismatch'
    with tarfile.open(d/f'LoungeCat-{version}-gentoo-overlay.tar.gz') as overlay:
        root='loungecat-overlay/net-irc/loungecat/'
        assert overlay.extractfile(root+f'loungecat-{version}.ebuild').read()==(d/f'loungecat-{version}.ebuild').read_bytes(), 'Standalone recipe/overlay mismatch'
        fields=overlay.extractfile(root+'Manifest').read().decode().split()
        data=source.read_bytes()
        assert fields==['DIST',source.name,str(len(data)),'BLAKE2B',hashlib.blake2b(data).hexdigest(),'SHA512',hashlib.sha512(data).hexdigest()], 'Gentoo source Manifest mismatch'
    return m

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--dist',type=Path,required=True);p.add_argument('--tag',required=True);p.add_argument('--commit',required=True)
    a=p.parse_args();verify(a.dist,a.tag,a.commit)
    print('Tag, source, all package checksums and Gentoo source Manifest verified.')
