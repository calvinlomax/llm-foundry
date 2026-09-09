# Benchmark methodology

No speedup is claimed. The implemented harness captures measurements that can be
reviewed and repeated. Do not compare a cold process to a warm service as though
they measured the same thing, or attribute an upstream engine difference to Foundry.

## Clocks and counters

| Metric | Definition |
| --- | --- |
| `load_ms` | Model handle loading: path resolution, structural inspection, optional planning, backend loading/tensor checking; excludes session creation |
| `prefill_ms` | Synchronized backend processing of the complete prompt |
| `decode_ms` | Generation loop including sampling, detokenization, synchronized evaluation of each emitted token, stop/UTF-8 handling, and callbacks |
| `first_token_ms` | Generate entry until first emitted content bytes; includes tokenization/prefill and buffering; excludes model loading; zero if no content |
| `total_ms` | Generate entry to final cleanup preparation; excludes weight/session creation |
| `generated_tokens` | Actual non-EOG tokens evaluated, including bytes withheld by stop filtering |
| `cached_prompt_tokens` | Zero for this implementation's full-reset requests |
| `decode_tokens_per_second` | Actual generated tokens divided by decode interval; null if unavailable |

Foundry evaluates the last emitted token into KV state. A direct completion tool
may count only N−1 decode evaluations for N output tokens. Align this convention
before calculating overhead. Ollama durations are nanoseconds; its non-streaming
endpoint does not provide client first-content latency. Metrics marked unavailable
must stay null rather than become zero-valued observations.

## Modes

`foundry bench MODEL --warmups 2 --repetitions 7` retains weights and resets KV and
sampler state for each request. `scripts/bench_matrix.py --mode warm-model` wraps
that native command and saves its raw JSON and logs. `--mode process-cold` starts
a fresh process per sample, including two warm-ups. OS file pages may still be
cached; this is not a cold-disk measurement. There is no cached-prefix Foundry mode
yet. Multi-turn chat also reprocesses full history.

```sh
python3 scripts/environment.py > benchmarks/runs/environment.json
python3 scripts/bench_matrix.py --model '/path/model.gguf' \
  --context 2048 --max-tokens 128 --repetitions 7 --warmups 2 \
  --power-state 'AC power; background applications closed'
python3 scripts/bench_ollama.py --model smollm2:latest
```

The Ollama script calls only `127.0.0.1:11434`, checks for remote model fields,
saves the request, model info, version, raw responses, and five measured samples
after one warm-up. Prefix-cache state is unknown and explicitly labeled. It does
not unload models automatically; stop the Ollama model before direct/Foundry
measurements to avoid competing allocations.

Archive compiler versions, CMakeCache.txt, source/backend revisions, weight and
tokenizer metadata hashes, actual token IDs, requested/actual placement logs,
power state, memory/swap observations, and prompts only when safe to share.
`bench_matrix.py` captures hashes and identifies unavailable observations; add
platform observations that cannot be collected automatically. Paths and prompt
text appear in saved command arguments. Results are ignored by Git by default.

Use the same tokenized prompt set and output budget, run two warm-ups plus at
least seven measurements, and repeat in a second session. For multi-runtime
comparisons, rotate runtime order manually across rounds. The current wrapper
does not automate cross-runtime rotation or infer a comparison from unrelated
samples. Compare medians and ranges; seven samples do not support a reliable p95.
Benchmark final settings again after builds and other CPU-intensive tasks finish.

## Bounded tuning

`scripts/tune.py` explores CPU thread counts 1, 2, 4, and the logical CPU count,
subject to a wall-time budget and per-subprocess timeout. It keeps weights, context,
seed, greedy sampling, and output limit fixed. Candidates receive two warm-ups and
three search samples. The selected candidate must pass seven evaluation samples
on a separate prompt before a winner is reported. Failures and timeouts are saved.
There is no automatic application of an unvalidated setting.

The profile records model digest, backend revision/capabilities, machine/OS, context,
objective, and the candidate/evaluation samples. The identity is intended for
review; profiles are applied manually with explicit CLI flags. Do not reuse a
profile across materially different hardware, builds, power conditions, or model
bytes. Automatic cache invalidation/application and GUI comparison are roadmap items.
