#!/usr/bin/env python3
"""Capture reproducible native samples; never infer speedups from mismatched modes."""
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

def capture(command, timeout):
    start = time.perf_counter()
    result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    return result, time.perf_counter() - start

def summary(samples):
    valid = [s['decode_tokens_per_second'] for s in samples if not s.get('warmup') and s.get('status') == 'ok' and s.get('decode_tokens_per_second') is not None]
    return {'valid_samples': len(valid), 'median_decode_tokens_per_second': statistics.median(valid) if valid else None,
            'min_decode_tokens_per_second': min(valid) if valid else None, 'max_decode_tokens_per_second': max(valid) if valid else None}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--foundry', default='build/release/foundry')
    p.add_argument('--model', required=True, help='GGUF path (needed for content hashing)')
    p.add_argument('--mode', choices=['warm-model', 'process-cold'], default='warm-model')
    p.add_argument('--context', type=int, default=2048)
    p.add_argument('--max-tokens', type=int, default=128)
    p.add_argument('--threads', type=int, default=4)
    p.add_argument('--gpu-layers', type=int, default=0)
    p.add_argument('--repetitions', type=int, default=7)
    p.add_argument('--warmups', type=int, default=2)
    p.add_argument('--timeout', type=float, default=600)
    p.add_argument('--prompt', default='The capital of France is')
    p.add_argument('--power-state', default='unrecorded')
    p.add_argument('--output-root', type=Path, default=Path('benchmarks/runs'))
    a = p.parse_args()
    if a.repetitions < 1 or a.warmups < 0 or a.timeout <= 0:
        p.error('Invalid repetition, warmup, or timeout value')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    out = a.output_root / ('foundry-' + stamp); out.mkdir(parents=True)
    options = ['--model', str(Path(a.model).resolve()), '--prompt', a.prompt, '--context', str(a.context),
               '--max-tokens', str(a.max_tokens), '--threads', str(a.threads), '--gpu-layers', str(a.gpu_layers), '--temperature', '0', '--seed', '42']
    record = {'schema_version': 1, 'run_id': stamp, 'timestamp_utc': datetime.now(timezone.utc).isoformat(),
              'machine': platform.machine(), 'os': platform.platform(), 'processor': platform.processor(),
              'power_state': a.power_state, 'model_sha256': sha256(a.model),
              'prompt_sha256': hashlib.sha256(a.prompt.encode()).hexdigest(), 'mode': a.mode,
              'source_revision': None,
              'memory_method': 'unavailable', 'swap_observations': None, 'build_settings': 'archive CMakeCache.txt and compiler versions alongside this run'}
    revision = subprocess.run(['git', 'rev-parse', 'HEAD'], capture_output=True, text=True)
    if revision.returncode == 0:
        record['source_revision'] = revision.stdout.strip()
    record['tokenizer_provenance'] = {'storage': 'embedded GGUF', 'container_sha256': record['model_sha256']}
    token_result, _ = capture([a.foundry, 'tokenize', *options], a.timeout)
    if token_result.returncode:
        raise RuntimeError(token_result.stderr)
    token_ids = json.loads(token_result.stdout)
    record['prompt_token_ids_sha256'] = hashlib.sha256(json.dumps(token_ids, separators=(',', ':')).encode()).hexdigest()
    record['prompt_tokens'] = len(token_ids)
    samples = []
    rounds = 1 if a.mode == 'warm-model' else a.warmups + a.repetitions
    for index in range(rounds):
        command = [a.foundry, 'bench' if a.mode == 'warm-model' else 'run', *options]
        if a.mode == 'warm-model':
            command += ['--warmups', str(a.warmups), '--repetitions', str(a.repetitions)]
        else:
            command += ['--metrics-json']
        (out / f'command-{index}.json').write_text(json.dumps(command, indent=2))
        try:
            result, wall = capture(command, a.timeout)
        except subprocess.TimeoutExpired as exc:
            record['error'] = f'timeout: {exc.timeout} seconds'
            break
        (out / f'stdout-{index}.txt').write_text(result.stdout)
        (out / f'stderr-{index}.txt').write_text(result.stderr)
        if a.mode == 'warm-model':
            result_samples = json.loads(result.stdout)['samples'] if result.stdout else []
        else:
            result_samples = []
            for line in result.stderr.splitlines():
                try:
                    value = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(value, dict) and value.get('event') == 'metrics':
                    value.update(warmup=index < a.warmups, process_wall_seconds=wall)
                    result_samples.append(value)
        samples.extend(result_samples)
        if result.returncode:
            record['error'] = f'process exit {result.returncode}'
            break
    record['samples'] = samples
    record['summary'] = summary(samples)
    (out / 'result.json').write_text(json.dumps(record, indent=2))
    print(out / 'result.json')
    if record.get('error') or record['summary']['valid_samples'] != a.repetitions:
        raise SystemExit('Benchmark incomplete; inspect saved failure record')

if __name__ == '__main__':
    main()
