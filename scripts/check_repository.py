#!/usr/bin/env python3
"""Check source boundaries, repository hygiene, and local documentation links."""
import json
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
errors = []
for path in (root / 'src/gui').rglob('*'):
    if path.is_file() and path.suffix not in {'.c', '.h'}:
        errors.append(f'GUI application source must be C17: {path}')
commands = root / 'build/release/compile_commands.json'
if commands.exists():
    gui_commands = [c for c in json.loads(commands.read_text()) if '/src/gui/' in c['file']]
    if not gui_commands:
        errors.append('No GUI compile commands found')
    for command in gui_commands:
        if not command['file'].endswith('.c') or '-std=c17' not in command['command']:
            errors.append(f'GUI source is not compiled as C17: {command["file"]}')
paths = list(root.glob('*.md')) + list((root / 'docs').rglob('*.md'))
for path in paths:
    for target in re.findall(r'\]\(([^)]+)\)', path.read_text()):
        if '://' in target or target.startswith('#'):
            continue
        target = target.split('#', 1)[0]
        if target and not (path.parent / target).exists():
            errors.append(f'Broken local link: {path.relative_to(root)} -> {target}')
for asset in ['metadata/manifest.json', 'sha256-test', 'models/example.gguf', 'build/example', 'benchmarks/runs/example.json']:
    p = subprocess.run(['git', 'check-ignore', '-q', asset], cwd=root)
    if p.returncode:
        errors.append(f'Local asset is not ignored: {asset}')
tracked = subprocess.run(['git', 'ls-files', '-z'], cwd=root, capture_output=True, check=True).stdout.decode().split('\0')
for path in filter(None, tracked):
    if path.startswith(('metadata/', 'models/', 'build/', 'benchmarks/runs/', 'sha256-')):
        errors.append(f'Local asset is tracked: {path}')
    full = root / path
    if full.is_file() and full.stat().st_size > 1024 * 1024 and not path.startswith('vendor/'):
        errors.append(f'Unexpected large source file: {path}')
if errors:
    raise SystemExit('\n'.join(errors))
print('C17 GUI boundary, asset exclusions, source size, and documentation links passed')
