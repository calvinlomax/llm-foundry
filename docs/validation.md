# Development validation evidence

Executed locally on 2026-09-09 using the environment in [environment.md](environment.md).
These are development checks on one macOS ARM64 machine, not completion of all
release gates or evidence of broader platform/architecture support.

## Completed checks

| Check | Result |
| --- | --- |
| Independent Python SHA-256 verification of all copied manifest layers | Passed; model and all five non-weight layers match their descriptors |
| Native Ollama import and preservation of metadata | Passed for the original copied model |
| Direct pinned llama.cpp CPU generation | Passed; raw `The capital of France is` generates a completion beginning ` Paris.` |
| Foundry raw completion parity | Same 16 generated-token text as direct backend, ignoring the direct tool's terminal-added trailing newlines |
| Release CTest suite | 8/8 tests passed, including native core/import CLI, SDK, real model, GTK version, and real-model GUI worker |
| Synthetic importer regression cases | Valid import, known tensor bounds, null/config-only entries, copy mode, hashes, bad paths/options, truncation, oversized counts, and concurrent conflicting name import |
| C API with real model | Load/session ownership, busy unload, raw generation, callback cancellation, reset/reuse, low-level tokenization/prefill/decode, chat render, context rejection |
| Chat golden cases | Exact explicit-system multi-turn Unicode output; exact default-system/empty-user output and its canonical token IDs |
| AddressSanitizer / UndefinedBehaviorSanitizer CPU suite | 4/4 tests passed on Foundry code; no sanitizer finding in those cases |
| GTK worker with actual model | Load, streamed response, cancellation acknowledgement, reset, orderly release |
| Installed GUI desktop startup | `foundry-gui --smoke-test` opened and closed the GTK window successfully in the normal macOS desktop session |
| CMake install and SDK consumer | Installed CLI/sample start; a separate C-only CMake consumer resolves `Foundry::foundry` and generates with local weights |
| Runtime dependency inspection | Installed CLI uses `@rpath/libfoundry_core.1.dylib`; core depends on system C/C++ libraries and Accelerate |
| Benchmark harness | Two warm-ups and seven measured raw-completion requests each generated 32 tokens; JSON samples and explicit missing observations were retained |
| Bounded tuning harness | Completed CPU thread search and seven independent evaluation samples within a 60-second diagnostic budget; reported `--threads 4` without automatically applying it |

Local command/test logs live under ignored `build/` and `benchmarks/runs/`.
Initial smoke timings overlapped other development work and are not a benchmark
comparison. The benchmark harness also retained a zero-output early-EOS case as
an incomplete decode measurement rather than inventing throughput or suppressing
EOS. Use partial raw-completion prompts or explicit chat formatting for suitable
workloads. Repeated harness samples are diagnostic evidence, not a speed claim.

The local Ollama executable is present, but its API was not listening at
`127.0.0.1:11434` during validation. No Ollama timing baseline or comparison is
claimed. The script is supplied for a deliberately started local service.

## Limits of this evidence

Hosted GitHub CI has not been run because no remote repository has been created.
Linux execution, Metal inference, a second architecture family, manual desktop
accessibility/visual review, extended memory-pressure tests, independent Jinja
renderer token fixtures, and a clean-machine portable GTK install remain open.
The standalone development `.app` depends on system GTK and an adjacent library
installation. It is not a signed/notarized portable application release.

The initial model remains outside Git. Its weight SHA-256 is
`4d2396b16114669389d7555c15a1592aad584750310f648edad5ca8c4eccda17` and the pinned
backend is `f3f1a8f2760f28325a5ec20c05b171e5b7c83a29`.
See [release gates](releasing.md) before changing the preview's status.
