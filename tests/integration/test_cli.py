#!/usr/bin/env python3
"""Synthetic fixtures exercise bounded parsing and atomic native registration."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
def string(s):
    b = s.encode()
    return struct.pack('<Q', len(b)) + b

def fixture():
    metadata = {'general.architecture': 'llama', 'general.name': 'Synthetic fixture',
                'llama.block_count': 2, 'llama.embedding_length': 16,
                'llama.attention.head_count': 2, 'llama.attention.head_count_kv': 1,
                'llama.context_length': 4096, 'tokenizer.ggml.model': 'gpt2'}
    b = b'GGUF' + struct.pack('<IQQ', 3, 1, len(metadata))
    for key, value in metadata.items():
        b += string(key)
        b += struct.pack('<I', 8) + string(value) if isinstance(value, str) else struct.pack('<II', 4, value)
    b += string('synthetic.weight') + struct.pack('<IQIQ', 1, 32, 0, 0)
    b += b'\0' * ((-len(b)) % 32)
    return b + struct.pack('<32f', *([0.25] * 32))

class NativeCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='foundry test ')
        self.root = Path(self.temp.name)
        self.env = dict(os.environ, FOUNDRY_HOME=str(self.root / 'registry home'))
        self.model = self.root / 'tiny model.gguf'
        self.model.write_bytes(fixture())
    def tearDown(self):
        self.temp.cleanup()
    def run_cli(self, *args, success=True):
        p = subprocess.run([str(BINARY), *map(str, args)], env=self.env, capture_output=True, text=True, timeout=30)
        self.assertEqual(p.returncode == 0, success, p.stderr)
        return p
    def test_reference_import_and_identity(self):
        first = json.loads(self.run_cli('import', 'gguf', '--path', self.model, '--name', 'tiny').stdout)
        self.assertEqual(first['sha256'], hashlib.sha256(fixture()).hexdigest())
        self.run_cli('import', 'gguf', '--path', self.model, '--name', 'tiny')
        inspected = json.loads(self.run_cli('inspect', 'tiny', '--json').stdout)
        self.assertEqual(inspected['tensor_count'], 1)
        self.assertEqual(inspected['tensors'][0]['bytes'], 128)
        self.assertEqual(len(json.loads(self.run_cli('list').stdout)['models']), 1)
        plan = json.loads(self.run_cli('plan', 'tiny', '--memory-budget', 1).stdout)
        self.assertFalse(plan['fits_budget'])
        self.assertEqual(plan['kv_bytes'], 2048 * 2 * 1 * (8 * 2 + 8 * 2))
    def test_copy_survives_source_removal(self):
        record = json.loads(self.run_cli('import', 'gguf', '--path', self.model, '--name', 'copied', '--copy').stdout)
        self.model.unlink()
        self.run_cli('inspect', 'copied')
        self.assertTrue(Path(record['path']).is_file())
    def test_null_and_config_only(self):
        manifest = self.root / 'manifest.json'
        for doc in ({'layers': None}, {'config': {}}, {'layers': []}):
            manifest.write_text(json.dumps(doc))
            p = self.run_cli('import', 'ollama', '--manifest', manifest, '--name', 'remote', success=False)
            self.assertIn('remote_only_model', p.stderr)
    def test_manifest_hash_and_unknown_metadata(self):
        content = fixture(); digest = hashlib.sha256(content).hexdigest()
        blob = self.root / ('sha256-' + digest); blob.write_bytes(content)
        license_bytes = b'A synthetic license for testing.'
        license_hash = hashlib.sha256(license_bytes).hexdigest()
        (self.root / ('sha256-' + license_hash)).write_bytes(license_bytes)
        layers = [{'mediaType': 'application/vnd.ollama.image.model', 'digest': 'sha256:' + digest, 'size': len(content)},
                  {'mediaType': 'application/vnd.ollama.image.license', 'digest': 'sha256:' + license_hash, 'size': len(license_bytes)}]
        manifest = self.root / 'manifest.json'; manifest.write_text(json.dumps({'layers': layers, 'custom': 42}))
        args = ('import', 'ollama', '--manifest', manifest, '--weights-root', self.root, '--metadata-root', self.root, '--name', 'manifest')
        self.run_cli(*args)
        record = json.loads((Path(self.env['FOUNDRY_HOME']) / 'registry/manifest.json').read_text())
        self.assertEqual(record['provenance']['manifest']['custom'], 42)
        self.assertEqual(record['provenance']['layers'][0]['text'], license_bytes.decode())
        blob.write_bytes(content[:-1] + b'x')
        self.assertIn('SHA-256 mismatch', self.run_cli(*args, success=False).stderr)
    def test_malformed_container(self):
        for data in (b'', b'GGUF', b'nope' + b'\0' * 30, fixture()[:-1], b'GGUF' + struct.pack('<IQQ', 3, 2**63, 1)):
            self.model.write_bytes(data)
            self.run_cli('inspect', self.model, success=False)
    def test_concurrent_name_conflict(self):
        alternate = self.root / 'different.gguf'
        alternate.write_bytes(fixture()[:-4] + struct.pack('<f', 0.5))
        processes = [subprocess.Popen([str(BINARY), 'import', 'gguf', '--path', str(path), '--name', 'race'],
                         env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                     for path in (self.model, alternate)]
        results = [(process, process.communicate(timeout=30)) for process in processes]
        self.assertEqual(sum(process.returncode == 0 for process, _ in results), 1)
        self.assertTrue(any('different bytes' in stderr for _, (_, stderr) in results))
        self.run_cli('inspect', 'race')

    def test_bad_inputs(self):
        self.run_cli('plan', self.model, '--context', '-1', success=False)
        self.run_cli('plan', self.model, '--temperature', 'nan', success=False)
        self.run_cli('import', 'gguf', '--path', self.model, '--name', '../escape', success=False)
        self.run_cli('run', self.model, '--max-tokens', '0', '--prompt', 'test', success=False)
        manifest = self.root / 'bad.json'
        manifest.write_text('{"layers":[{"mediaType":"application/vnd.ollama.image.model","digest":"sha256:../../bad","size":1}]}')
        self.run_cli('import', 'ollama', '--manifest', manifest, '--name', 'bad', success=False)

if __name__ == '__main__':
    unittest.main()
