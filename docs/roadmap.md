# Implementation status and roadmap

The original plan remains the design/acceptance runbook. This preview implements
the product path but does not mark every multi-week milestone complete.

| Milestone | Current implementation | Remaining evidence or work |
| --- | --- | --- |
| M0 | Verified local layers; pinned direct CPU backend; direct text generation | Ollama baseline and fully aligned multi-runtime comparison |
| M1 | C API, runtime/model/session ownership, raw CLI, CPU generation, cancellation | Broader low-level error/OOM stress coverage and GPU verification |
| M2 | Native registry/import, inspection, verified initial chat, planner, JSON metrics | Automatic profile/import-default precedence, more templates |
| M2G | C17 GTK import/model/chat/settings/metrics, worker queue, Stop/New Chat/close/export | Manual accessibility/visual audit, tuning panel, richer resource reporting, portable desktop bundle |
| M3 | Native benchmark samples, Python envelope, bounded thread search and independent evaluation | Automatic profile application/invalidation, cross-runtime rotation, controlled repeated comparison and optimization evidence |
| M4 | Initial artifact tested; backend-reported raw GGUF scope explicit | Second distinct architecture and exact fixture matrix; shards/conversion later |
| M5 | CMake install/export, SDK sample, desktop development bundle, notices, CI, GitHub docs | Hosted CI evidence, separate-machine/offline install, portable GTK dependencies, signing/notarization and release review |

Optional later tracks remain unimplemented: an experimental all-C architecture
engine, local HTTP serving, remote adapters, training, multimodal input, model
marketplaces, automatic downloading, and distributed execution. They must not
block corrections to the initial product runtime or be advertised as available.

Next bounded tasks: add Golden Jinja/tokenizer
fixtures for another template; add real-memory observations; implement explicit
compatible profile loading with override tests; add the GUI tuning panel after
that contract is stable; complete clean-machine desktop packaging.
