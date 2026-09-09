#!/usr/bin/env python3
"""Development reference: Python digest verification followed by native import."""
import argparse
import subprocess
from verify_assets import verify
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--foundry', default='build/release/foundry')
p.add_argument('--manifest', default='metadata/manifest.json')
p.add_argument('--weights-root', default='.')
p.add_argument('--metadata-root', default='metadata')
p.add_argument('--name', default='smollm2')
a = p.parse_args()
verify(a.manifest, a.weights_root, a.metadata_root)
subprocess.run([a.foundry, 'import', 'ollama', '--manifest', a.manifest,
                '--weights-root', a.weights_root, '--metadata-root', a.metadata_root,
                '--name', a.name], check=True, timeout=600)
