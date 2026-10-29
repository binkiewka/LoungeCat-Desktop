#!/usr/bin/env python3
"""Do not package a working tree that differs from the claimed source commit."""
import subprocess
status = subprocess.check_output(['git', 'status', '--porcelain'], text=True)
if status.strip():
    raise SystemExit('Commit source changes before packaging; the source archive must match the compiled tree.\n' + status)
