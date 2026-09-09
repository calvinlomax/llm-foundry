#!/usr/bin/env python3
"""Bounded CPU thread search; explicit profiles, independent winner revalidation."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time
from verify_assets import sha256

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--foundry', default='build/release/foundry')
p.add_argument('--model', required=True)
p.add_argument('--objective', choices=['decode', 'prefill'], default='decode')
p.add_argument('--budget-seconds', type=float, default=300)
p.add_argument('--context', type=int, default=2048)
p.add_argument('--max-tokens', type=int, default=64)
p.add_argument('--output-root', type=Path, default=Path('profiles/local'))
a = p.parse_args()
if a.budget_seconds <= 0:
    p.error('Budget must be positive')
started = time.monotonic(); deadline = started + a.budget_seconds
out = a.output_root / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'); out.mkdir(parents=True)
cap = subprocess.run([a.foundry, 'backends', '--json'], capture_output=True, text=True, check=True, timeout=max(.1, deadline-time.monotonic()))
identity = {'model_sha256': sha256(a.model), 'backend': json.loads(cap.stdout), 'hardware': platform.machine(),
            'processor': platform.processor(), 'logical_cpus': os.cpu_count(), 'os': platform.platform(),
            'context': a.context, 'objective': a.objective, 'foundry_version': '0.1.0-dev'}
key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
record = {'schema_version': 1, 'identity': identity, 'cache_key': key, 'candidates': [], 'winner': None,
          'application': 'manual; use the printed --threads setting explicitly', 'budget_seconds': a.budget_seconds}

def measure(threads, prompt, repetitions, label):
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        return {'threads': threads, 'error': 'budget exhausted'}
    command = [a.foundry, 'bench', '--model', a.model, '--context', str(a.context), '--max-tokens', str(a.max_tokens),
               '--temperature', '0', '--seed', '42', '--gpu-layers', '0', '--threads', str(threads),
               '--prompt', prompt, '--warmups', '2', '--repetitions', str(repetitions)]
    result = {'threads': threads, 'command': command}
    try:
        process = subprocess.run(command, capture_output=True, text=True, timeout=remaining)
        (out / (label + '-stderr.txt')).write_text(process.stderr)
        (out / (label + '-stdout.json')).write_text(process.stdout)
        if process.returncode:
            raise ValueError(f'exit {process.returncode}')
        samples = [s for s in json.loads(process.stdout)['samples'] if not s['warmup']]
        if len(samples) != repetitions or any(s['status'] != 'ok' for s in samples):
            raise ValueError('incomplete measurements')
        scores = [s['decode_tokens_per_second'] if a.objective == 'decode' else s['prompt_tokens'] * 1000 / s['prefill_ms'] for s in samples]
        if any(score is None or score <= 0 for score in scores):
            raise ValueError('invalid throughput')
        result.update(score=statistics.median(scores), samples=samples)
    except (subprocess.TimeoutExpired, ValueError, KeyError, ZeroDivisionError) as error:
        result['error'] = str(error)
    return result

for threads in sorted(set(n for n in (1, 2, 4, os.cpu_count() or 1) if n <= (os.cpu_count() or 1))):
    candidate = measure(threads, 'A hash table handles collisions by', 3, f'threads-{threads}')
    record['candidates'].append(candidate)
    if time.monotonic() >= deadline:
        break
valid = [c for c in record['candidates'] if 'score' in c]
if valid:
    best = max(valid, key=lambda c: c['score'])
    evaluation = measure(best['threads'], 'Binary search finds a number in a sorted list by', 7, 'evaluation')
    record['evaluation'] = evaluation
    if 'score' in evaluation:
        record['winner'] = {'threads': best['threads'], 'evaluation_score': evaluation['score']}
record['elapsed_seconds'] = time.monotonic() - started
(out / 'profile.json').write_text(json.dumps(record, indent=2))
print(out / 'profile.json')
if record['winner']:
    print('Revalidated setting: --threads', record['winner']['threads'])
else:
    raise SystemExit('No revalidated winner within budget; current settings are unchanged')
