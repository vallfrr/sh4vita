import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

import shader_cache_bundle as bundle


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sh4-gpu-bundle-')
        self.root = Path(self.temp.name)/'export'
        self.root.mkdir()
        stage = struct.pack('<I',0) + b'GXP\0' + bytes(32)
        self.program = struct.pack('<I',len(stage)) + stage + struct.pack('<I',len(stage)) + stage
        self.manifest = dict(format=1,cache_abi=bundle.abi(),exe_sha256=bundle.EXE_SHA256,
                             compiler=copy.deepcopy(bundle.CONFIG),stage_backend_revision='000a0001',files=[])
        self.add('programs/0123456789abcdef.bin','program',self.program)
        self.add('stages/v/00/cg2-0000000000001234.gxp','stage',
                 struct.pack('<IIQQ',0x32434753,len(stage),0x1234,0)+stage)
        self.write_manifest()

    def tearDown(self):
        self.temp.cleanup()

    def add(self,name,kind,data):
        p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        self.manifest['files'].append(dict(path=name,kind=kind,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))

    def write_manifest(self):
        (self.root/'manifest.json').write_text(json.dumps(self.manifest),encoding='utf-8')

    def test_export_to_versioned_bundle_preserves_gpu_payload(self):
        out=Path(self.temp.name)/'ready'
        bundle.prepare(self.root,out)
        self.assertEqual(bundle.validate(out)['format'],2)
        self.assertEqual(bundle.unwrap((out/'programs/0123456789abcdef.bin').read_bytes()),self.program)
        self.assertEqual((self.root/'programs/0123456789abcdef.bin').read_bytes(),self.program)

    def test_wrong_executable_renderer_and_compiler_reject_before_output(self):
        for key,value in (('exe_sha256','0'*64),('cache_abi','other'),('compiler',dict(bundle.CONFIG,optimization=1))):
            good=copy.deepcopy(self.manifest)
            self.manifest[key]=value;self.write_manifest()
            with self.assertRaises(ValueError):bundle.prepare(self.root,Path(self.temp.name)/'bad')
            self.assertFalse((Path(self.temp.name)/'bad').exists())
            self.manifest=good;self.write_manifest()

    def test_changed_record_and_recomputed_file_hash_still_require_payload_checksum(self):
        out=Path(self.temp.name)/'ready';manifest=bundle.prepare(self.root,out)
        p=out/'programs/0123456789abcdef.bin';data=bytearray(p.read_bytes());data[-1]^=1;p.write_bytes(data)
        with self.assertRaises(ValueError):bundle.validate(out)
        manifest['files'][0]['sha256']=hashlib.sha256(data).hexdigest()
        (out/'manifest.json').write_text(json.dumps(manifest),encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'checksum'):bundle.validate(out)

    def test_save_game_file_and_paths_cannot_be_packaged(self):
        p=self.root/'save/sh4.sav';p.parent.mkdir();p.write_bytes(b'existing progress')
        with self.assertRaisesRegex(ValueError,'unlisted'):bundle.validate(self.root,1)
        self.manifest['files'].append(dict(path='save/sh4.sav',kind='program',bytes=p.stat().st_size,
                                          sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
        self.write_manifest()
        with self.assertRaisesRegex(ValueError,'non-cache'):bundle.validate(self.root,1)
        self.assertEqual(p.read_bytes(),b'existing progress')


if __name__=='__main__':
    unittest.main()
