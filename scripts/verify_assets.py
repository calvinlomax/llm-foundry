#!/usr/bin/env python3
"""Independently verify copied Ollama layers without changing the originals."""
import argparse
import hashlib
import json
from pathlib import Path

def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as file:
        for block in iter(lambda: file.read(8 * 1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()

def verify(manifest_path, weights_root, metadata_root):
    manifest = json.loads(Path(manifest_path).read_text())
    layers = manifest.get('layers') or []
    if not layers:
        raise ValueError('REMOTE_ONLY_MODEL: no local weight layers')
    entries = ([manifest['config']] if manifest.get('config') else []) + layers
    verified = []
    weights = set()
    for entry in entries:
        digest = entry.get('digest', '')
        if not digest.startswith('sha256:') or len(digest) != 71 or any(c not in '0123456789abcdef' for c in digest[7:]):
            raise ValueError('Invalid SHA-256 digest')
        model = entry.get('mediaType', '').split(';', 1)[0].strip() == 'application/vnd.ollama.image.model'
        path = Path(weights_root if model else metadata_root) / ('sha256-' + digest[7:])
        if path.stat().st_size != entry['size'] or sha256(path) != digest[7:]:
            raise ValueError(f'Size or digest mismatch: {path}')
        verified.append({'media_type': entry['mediaType'], 'sha256': digest[7:], 'bytes': path.stat().st_size})
        if model:
            weights.add(path.resolve())
    if len(weights) != 1:
        raise ValueError('Exactly one distinct local model layer is supported')
    model = next(iter(weights))
    with model.open('rb') as handle:
        if handle.read(4) != b'GGUF':
            raise ValueError('Model layer has no GGUF signature')
    return {'manifest_sha256': sha256(manifest_path), 'layers': verified, 'model': str(model)}

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', default='metadata/manifest.json')
    p.add_argument('--weights-root', default='.')
    p.add_argument('--metadata-root', default='metadata')
    args = p.parse_args()
    print(json.dumps(verify(args.manifest, args.weights_root, args.metadata_root), indent=2))
