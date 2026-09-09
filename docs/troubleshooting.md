# Troubleshooting

| Symptom | Check and action |
| --- | --- |
| CMake says backend missing | Run `git submodule update --init --recursive` |
| Revision mismatch | Restore the committed backend gitlink; upgrades must update the pin and tests together |
| GTK not found | Install GTK4 4.10+ and pkg-config; check `pkg-config --modversion gtk4`; choose the CPU preset only for a deliberate headless build |
| `remote_only_model` | The manifest contains no local model layer; obtain actual local weights separately |
| Hash/size mismatch | Restore the original copied layer; do not edit a model blob to make its metadata agree |
| Invalid/truncated GGUF | Confirm a complete download, matching hash, GGUF version, and required shards; shards are currently unsupported |
| Model loads but chat fails | Only the verified SmolLM2 embedded template is accepted; use `run --raw` for other loadable models |
| Context exceeded | Reduce prompt/history or output budget, or choose a valid larger context within trained limits; no automatic truncation occurs |
| Memory budget exceeded | Reduce context/batch demand or choose a smaller model; file size alone does not establish fit |
| GPU unavailable | CPU is the default build; build the Metal preset on a compatible machine and inspect actual backend logs |
| Different output across builds | Compare model hashes, rendered token IDs, settings, and kernel/platform differences; inspect near ties before assuming incorrectness |
| Benchmark unexpectedly slow | Stop other model runtimes and builds, align warm/cold/cache state, power state, context, and threads |
| GUI seems stuck closing | Loading/hashing may need to finish before shutdown; Stop can interrupt inference at supported boundaries |
| Desktop launch fails from another machine | The development bundle depends on system GTK and adjacent Foundry libraries; portable bundling is not finished |
| Registry ID not found | Check `FOUNDRY_HOME`, the saved record, and the original path for reference imports |
| CLI JSON mixed with logs | Run metrics use stderr beside backend logs; select JSON objects with `event: "metrics"` |

Use `foundry version`, `foundry backends --json`, inspection JSON, the exact command,
and a redacted backend log when reporting a bug. Do not include private prompts
or model weights. See [the security policy](../SECURITY.md) for sensitive reports.
