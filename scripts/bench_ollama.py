#!/usr/bin/env python3
"""Local Ollama diagnostic baseline. Prefix cache state is unknown."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import statistics
import time
import urllib.request

def request(endpoint, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request('http://127.0.0.1:11434/api/' + endpoint, data=data,
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=600) as response:
        return json.load(response)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--model', default='smollm2:latest')
    p.add_argument('--output-root', type=Path, default=Path('benchmarks/runs'))
    a = p.parse_args()
    info = request('show', {'model': a.model})
    if info.get('remote_model') or info.get('remote_host'):
        raise SystemExit('REMOTE_ONLY_MODEL: this benchmark requires local weights')
    out = a.output_root / ('ollama-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
    out.mkdir(parents=True)
    payload = {'model': a.model, 'prompt': 'Explain how a hash table handles collisions in plain English.',
               'raw': True, 'stream': False, 'keep_alive': '10m',
               'options': {'temperature': 0, 'seed': 42, 'num_ctx': 2048, 'num_predict': 128}}
    (out / 'request.json').write_text(json.dumps(payload, indent=2))
    (out / 'environment.json').write_text(json.dumps({'version': request('version'), 'model': info,
          'cache_state': 'unknown; repeated prefixes may be cached', 'first_token_latency': None}, indent=2))
    rates = []
    for index in range(6):
        start = time.perf_counter(); result = request('generate', payload)
        result['client_wall_seconds'] = time.perf_counter() - start
        result['warmup'] = index == 0
        duration, count = result.get('eval_duration', 0), result.get('eval_count', 0)
        rate = count * 1e9 / duration if duration > 0 and count > 0 else None
        result['decode_tokens_per_second'] = rate
        (out / f'run-{index}.json').write_text(json.dumps(result, indent=2))
        if result.get('error') or not result.get('done') or rate is None:
            raise RuntimeError('Invalid benchmark response; inspect saved result')
        if index:
            rates.append(rate)
    (out / 'summary.json').write_text(json.dumps({'median_decode_tokens_per_second': statistics.median(rates),
          'samples': rates, 'cache_state': 'unknown'}, indent=2))
    print(out)

if __name__ == '__main__':
    main()
