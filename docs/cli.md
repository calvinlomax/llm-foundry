# CLI reference

Run `foundry --help` for the executable's current syntax. Examples below assume
`build/release` is on PATH and your `FOUNDRY_HOME` points to the intended registry.

| Command | Behavior |
| --- | --- |
| `foundry import gguf --path FILE --name ID [--copy]` | Verify/inspect and register a complete single-file GGUF |
| `foundry import ollama --manifest FILE --weights-root DIR --metadata-root DIR --name ID [--copy]` | Verify all manifest layers; preserve source metadata |
| `foundry list --json` | JSON object with a `models` array |
| `foundry inspect MODEL --json` | Structural metadata, SHA-256, tensor inventory; potentially large JSON |
| `foundry plan MODEL --context 2048` | Advisory JSON estimate, uncertainty, requested placement |
| `foundry run MODEL --prompt TEXT [--raw|--chat]` | One-shot generation; raw is the default |
| `foundry chat MODEL [--system TEXT]` | Loaded model retained between turns; `/new` and `/quit` |
| `foundry tokenize MODEL --prompt TEXT [--chat]` | Canonical input token IDs as a JSON array |
| `foundry bench MODEL --warmups 2 --repetitions 7` | Loaded weights, full fresh-KV requests; JSON samples |
| `foundry backends --json` | Pinned backend version and discoverable capabilities |
| `foundry version` | Foundry version and C ABI |

A model can be an ID, positional file path, or `--model PATH`. Quote paths and
prompts containing spaces. `--prompt-file FILE` accepts UTF-8 text up to 16 MiB;
`--prompt-file -` reads stdin. Embedded NUL bytes are rejected in prompt files.

Generation settings: `--context`, `--max-tokens`, `--batch-size`, `--threads`,
`--temperature`, `--top-p`, `--seed`, `--gpu-layers`, `--memory-budget`, and `--stop`.
The API reference defines defaults/ranges. `--backend llama` and `--backend auto`
select the only compute adapter; `--backend cpu` also forces zero GPU layers.
`--gpu-layers -1` requests automatic device placement. CPU is otherwise the default.

`--raw` disables chat formatting and does not interpret token-like spellings as
special tokens. `--chat` renders the supported embedded template. `--system` is
used by chat formatting, including `run --chat` and `tokenize --chat`.
The GUI and CLI use the same formatter. Raw run does not apply a system message.

`--metrics-json` emits a single final JSON object to stderr after a run, or one
per CLI chat turn. Other backend stderr lines are logs; consumers should select
JSON objects with `event == "metrics"`. `bench` writes its entire result object to
stdout and does not stream generated text. No measurements contain a fabricated
observed-memory value.

Exit codes: 0 success, 2 invalid invocation/configuration, 130 cancellation,
1 other failure. Ctrl+C and SIGTERM are observed by a cancellation-monitor thread.
The callback and backend abort mechanism stop at supported boundaries; loading
may finish before a request can be interrupted. Backend/model errors include a
short typed diagnostic and may require the detailed stderr load log.

Profiles are an explicit development workflow in this preview:
`python3 scripts/tune.py --model FILE`. The script prints a revalidated `--threads`
setting and records its identity; the CLI does not automatically load a profile.
Imported Ollama parameters are preserved as provenance, not silently applied as
native options. Effective settings are therefore explicit CLI values followed by
framework defaults. Automatic profile/import-default precedence is a future gate.
