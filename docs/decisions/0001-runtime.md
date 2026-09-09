# ADR 0001: C17 product runtime with a pinned compute adapter

Status: accepted for the development preview.

The product needs correct offline inference and a native C API before custom
kernels can be justified. Use C17 for the public API, runtime coordination,
importer, planner, CLI, and GTK application. Keep llama.cpp behind a single C++
adapter with exception translation and opaque C-facing types. Pin its exact Git
commit and fail configuration on revision drift.

The GUI calls the API through a worker thread and never spawns the CLI or an HTTP
server. GGUF inspection and registry management require no Python installation.
Python remains appropriate for development verification, tuning, and measurement.

The initial compatibility claim is one verified SmolLM2 artifact on CPU. Other
raw decoder models may load through the pinned backend; that is a backend-reported
capability until tested. Chat accepts only the exact verified template. The
project display name is provisionally Cinder Foundry to avoid the already known
MosaicML LLM Foundry identity conflict.

Custom kernels, broad architecture claims, cached-prefix execution, serving, and
portable desktop releases follow their own evidence gates. This decision makes
no speed claim about replacing a front end.
