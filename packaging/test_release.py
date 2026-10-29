import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest


def module(name):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(name+'.py'))
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result

verify=module('verify-release').verify
merge=module('merge-release').merge

class ReleaseIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.commit='a'*40;self.version='2.0.1'
        for kind in ['debian','rpm','gentoo']:(self.root/'inputs'/kind).mkdir(parents=True)
        d=self.root/'inputs/debian'
        self.tar(d/'LoungeCat-2.0.1-source.tar.gz',{
            'LoungeCat-2.0.1-source/CMakeLists.txt':b'project(LoungeCat VERSION 2.0.1 LANGUAGES CXX)',
            'LoungeCat-2.0.1-source/LICENSE':b'GPL', 'LoungeCat-2.0.1-source/src/main.cpp':b'main',
            'LoungeCat-2.0.1-source/packaging/build.sh':b'build'})
        (d/'LoungeCat-2.0.1-x86_64.AppImage').write_bytes(b'test package identity')
        (d/'loungecat_2.0.1_amd64.deb').write_bytes(b'test package identity')
        (self.root/'inputs/rpm/loungecat-2.0.1-1.fc44.x86_64.rpm').write_bytes(b'test package identity')
        g=self.root/'inputs/gentoo';recipe=b'EAPI=8\n';(g/'loungecat-2.0.1.ebuild').write_bytes(recipe)
        source=(d/'LoungeCat-2.0.1-source.tar.gz').read_bytes()
        manifest=f'DIST LoungeCat-2.0.1-source.tar.gz {len(source)} BLAKE2B {hashlib.blake2b(source).hexdigest()} SHA512 {hashlib.sha512(source).hexdigest()}\n'.encode()
        self.tar(g/'LoungeCat-2.0.1-gentoo-overlay.tar.gz',{'loungecat-overlay/net-irc/loungecat/loungecat-2.0.1.ebuild':recipe,'loungecat-overlay/net-irc/loungecat/Manifest':manifest})
        for kind in ['debian','rpm','gentoo']:
            folder=self.root/'inputs'/kind
            files=list(folder.iterdir())
            inventory={'debian':'build-dependencies.tsv','rpm':'rpm-build-dependencies.tsv','gentoo':'gentoo-build-dependencies.tsv'}[kind]
            (folder/inventory).write_text('dependency\t1\n')
            m={'version':self.version,'sourceCommit':self.commit,'buildBaseline':kind,
               'artifacts':[{'file':f.name,'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in files]}
            if kind=='gentoo':m['sourceSha256']=hashlib.sha256(source).hexdigest()
            (folder/'package-manifest.json').write_text(json.dumps(m));(folder/'SOURCE_COMMIT').write_text(self.commit+'\n')
        self.out=self.root/'dist';merge(self.root/'inputs',self.out)

    @staticmethod
    def tar(path,files):
        with tarfile.open(path,'w:gz') as archive:
            for name,data in files.items():
                entry=tarfile.TarInfo(name);entry.size=len(data);archive.addfile(entry,io.BytesIO(data))

    def test_complete_set_verifies(self):
        self.assertEqual(len(verify(self.out,'v2.0.1',self.commit)['artifacts']),9)

    def test_missing_rpm_blocks_publication(self):
        p=self.out/'package-manifest.json';m=json.loads(p.read_text());m['artifacts']=[e for e in m['artifacts'] if not e['file'].endswith('.rpm')];p.write_text(json.dumps(m))
        with self.assertRaises(AssertionError):verify(self.out,'v2.0.1',self.commit)

    def test_changed_artifact_blocks_publication(self):
        (self.out/'loungecat_2.0.1_amd64.deb').write_bytes(b'changed')
        with self.assertRaises(AssertionError):verify(self.out,'v2.0.1',self.commit)

    def test_wrong_tag_blocks_publication(self):
        with self.assertRaises(AssertionError):verify(self.out,'v2.0.0',self.commit)

    def test_mixed_platform_commits_block_aggregation(self):
        p=self.root/'inputs/rpm/package-manifest.json';m=json.loads(p.read_text());m['sourceCommit']='b'*40;p.write_text(json.dumps(m))
        with self.assertRaises(AssertionError):merge(self.root/'inputs',self.root/'other')

    def test_gentoo_source_digest_must_match(self):
        p=self.root/'inputs/gentoo/package-manifest.json';m=json.loads(p.read_text());m['sourceSha256']='0'*64;p.write_text(json.dumps(m))
        with self.assertRaises(AssertionError):merge(self.root/'inputs',self.root/'other')
