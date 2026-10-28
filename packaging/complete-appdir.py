#!/usr/bin/env python3
"""Bundle non-driver runtime closure that linuxdeploy's desktop blacklist skips."""
from pathlib import Path
import re,subprocess,sys
stage=Path(sys.argv[1]); deploy=sys.argv[2]
# glibc/loader and vendor graphics dispatch must remain from the host. Everything
# else in the actual ELF dependency closure is part of this portable bundle.
host={'ld-linux-x86-64.so.2','libc.so.6','libm.so.6','libresolv.so.2','libdl.so.2',
      'libpthread.so.0','librt.so.1','libGL.so.1','libEGL.so.1','libOpenGL.so.0',
      'libGLX.so.0','libGLdispatch.so.0'}
required={}
for file in [stage/'usr/bin/loungecat',*sorted((stage/'usr/lib').glob('*.so*')),
             *sorted((stage/'usr/plugins').rglob('*.so'))]:
    if not file.is_file() or file.read_bytes()[:4]!=b'\x7fELF':continue
    result=subprocess.run(['ldd',str(file)],text=True,capture_output=True,check=True)
    assert 'not found' not in result.stdout,result.stdout
    for name,path in re.findall(r'^\s*(\S+)\s+=>\s+(/\S+)',result.stdout,re.M):
        if name in host or (stage/'usr/lib'/name).exists():continue
        required[name]=path
if required:
    command=[deploy,'--appdir',str(stage)]
    for name,path in sorted(required.items()):command+=['--library',path]
    print('Explicitly bundling non-driver runtime dependencies:',', '.join(sorted(required)),flush=True)
    subprocess.run(command,check=True)
