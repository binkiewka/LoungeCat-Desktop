#!/usr/bin/env python3
"""Retain Debian copyright/license notices for the actual bundled ELF closure."""
from pathlib import Path
import json,subprocess,sys
stage=Path(sys.argv[1]); dest=stage/'usr/share/doc/loungecat/dependency-licenses'
dest.mkdir(parents=True,exist_ok=True)
inventory={}
for bundled in sorted(stage.rglob('*.so*')):
    if not bundled.is_file(): continue
    candidates=list(Path('/usr/lib/x86_64-linux-gnu').glob(bundled.name))
    candidates+=list(Path('/usr/lib/x86_64-linux-gnu/qt6/plugins').rglob(bundled.name))
    candidates+=list(Path('/lib/x86_64-linux-gnu').glob(bundled.name))
    for candidate in candidates:
        found=subprocess.run(['dpkg-query','-S',str(candidate)],capture_output=True,text=True)
        if found.returncode: continue
        package=found.stdout.split(': ',1)[0].split(':',1)[0]
        notice=Path('/usr/share/doc')/package/'copyright'
        if not notice.exists(): continue
        (dest/(package+'.copyright')).write_bytes(notice.read_bytes())
        inventory[str(bundled.relative_to(stage))]=package
        break
    else:
        raise SystemExit('Cannot find dependency license notice for '+str(bundled))
(dest/'inventory.json').write_text(json.dumps(inventory,indent=2)+'\n')
# Debian copyright files may refer to these shared full license texts.
common=Path('/usr/share/common-licenses')
for p in common.iterdir():
    if p.is_file(): (dest/p.name).write_bytes(p.read_bytes())
