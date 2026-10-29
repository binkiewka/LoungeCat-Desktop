#!/usr/bin/env python3
"""Join platform artifacts only when version, source and source digests agree."""
from pathlib import Path
import argparse, hashlib, json, shutil

def merge(inputs, output):
    output.mkdir(parents=True,exist_ok=True)
    manifests={name:json.loads((inputs/name/'package-manifest.json').read_text()) for name in ['debian','rpm','gentoo']}
    first=manifests['debian']; version=first['version']; commit=first['sourceCommit']
    assert all(m['version']==version and m['sourceCommit']==commit for m in manifests.values()), 'Platform source/version mismatch'
    assert all((inputs/name/'SOURCE_COMMIT').read_text().strip()==commit for name in manifests), 'Platform SOURCE_COMMIT mismatch'
    entries=[]
    for name,m in manifests.items():
        for e in m['artifacts']:
            file=e['file']; assert Path(file).name==file and file not in {x['file'] for x in entries}
            source=inputs/name/file
            assert source.stat().st_size==e['bytes'] and hashlib.sha256(source.read_bytes()).hexdigest()==e['sha256'], 'Platform artifact changed'
            shutil.copy2(source,output/file);entries.append(e)
        inventory={'debian':'build-dependencies.tsv','rpm':'rpm-build-dependencies.tsv','gentoo':'gentoo-build-dependencies.tsv'}[name]
        source=inputs/name/inventory; assert source.is_file() and source.stat().st_size>0
        shutil.copy2(source,output/inventory)
        entries.append({'file':inventory,'bytes':source.stat().st_size,'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
    source=output/f'LoungeCat-{version}-source.tar.gz'
    assert manifests['gentoo']['sourceSha256']==hashlib.sha256(source.read_bytes()).hexdigest(), 'Gentoo recipe uses different source'
    combined={'version':version,'architecture':'x86_64','sourceCommit':commit,
        'buildBaselines':{n:m['buildBaseline'] for n,m in manifests.items()},'artifacts':sorted(entries,key=lambda e:e['file'])}
    (output/'package-manifest.json').write_text(json.dumps(combined,indent=2)+'\n')
    (output/'SOURCE_COMMIT').write_text(commit+'\n')
    (output/'SHA256SUMS').write_text(''.join(f"{e['sha256']}  {e['file']}\n" for e in combined['artifacts']))
    return combined

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();m=merge(a.inputs,a.output)
    print(f"Aggregated {len(m['artifacts'])} source-matched release files.")
