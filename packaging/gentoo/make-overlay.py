#!/usr/bin/env python3
"""Produce an immutable source recipe and a complete local Gentoo overlay."""
from pathlib import Path
import argparse, hashlib, shutil, tarfile, xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--version',required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
assert a.source.name==f'LoungeCat-{a.version}-source.tar.gz'
root=a.output/'overlay'; package=root/'net-irc/loungecat'
package.mkdir(parents=True,exist_ok=True);(root/'metadata').mkdir(exist_ok=True);(root/'profiles').mkdir(exist_ok=True)
(root/'profiles/repo_name').write_text('loungecat\n')
(root/'metadata/layout.conf').write_text('masters = gentoo\nthin-manifests = true\nsign-manifests = false\n')
recipe=Path('packaging/gentoo/loungecat.ebuild').read_bytes()
(package/f'loungecat-{a.version}.ebuild').write_bytes(recipe)
(a.output/f'loungecat-{a.version}.ebuild').write_bytes(recipe)
(package/'metadata.xml').write_text('''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE pkgmetadata SYSTEM "https://www.gentoo.org/dtd/metadata.dtd">
<pkgmetadata>
  <maintainer type="person"><email>binkiewka@users.noreply.github.com</email><name>LoungeCat maintainer</name></maintainer>
  <use><flag name="alsa">Enable ALSA notification sounds</flag></use>
  <upstream><remote-id type="github">binkiewka/LoungeCat-Desktop</remote-id></upstream>
</pkgmetadata>
''')
source=a.source.read_bytes()
(package/'Manifest').write_text(f'DIST {a.source.name} {len(source)} BLAKE2B {hashlib.blake2b(source).hexdigest()} SHA512 {hashlib.sha512(source).hexdigest()}\n')
with tarfile.open(a.output/f'LoungeCat-{a.version}-gentoo-overlay.tar.gz','w:gz') as t:
    t.add(root,arcname='loungecat-overlay')
print('Generated standalone ebuild and complete overlay with exact source digest.')
