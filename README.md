# Cinder Foundry

<p align="center"><strong>Local language model inference through a C17 API, terminal, and C desktop app.</strong></p>

<p align="center">
  <a href="LICENSE"><img src="docs/assets/badges/license.svg" alt="License: MIT"></a>
  <a href="CHANGELOG.md"><img src="docs/assets/badges/version.svg" alt="Version: 0.1.0-dev"></a>
  <a href="docs/releasing.md"><img src="docs/assets/badges/status.svg" alt="Status: preview"></a>
  <a href="docs/api.md"><img src="docs/assets/badges/language.svg" alt="C standard: C17"></a>
  <a href="docs/gui.md"><img src="docs/assets/badges/gui.svg" alt="GUI: GTK4"></a>
  <a href="docs/backend-revision.txt"><img src="docs/assets/badges/backend.svg" alt="Backend: llama.cpp"></a>
</p>

<p align="center">
  <a href="docs/installation.md">Install</a> /
  <a href="docs/cli.md">CLI guide</a> /
  <a href="docs/gui.md">Desktop guide</a> /
  <a href="docs/api.md">C API</a> /
  <a href="docs/benchmarking.md">Benchmarks</a> /
  <a href="docs/compatibility.md">Compatibility</a> /
  <a href="docs/roadmap.md">Roadmap</a>
</p>

<p align="center">
  <a href="THIRD_PARTY_NOTICES.md">Third-party notices</a> /
  <a href="CONTRIBUTING.md">Contributing</a> /
  <a href="SECURITY.md">Security</a> /
  <a href="CODE_OF_CONDUCT.md">Code of conduct</a> /
  <a href="CHANGELOG.md">Changelog</a>
</p>

Cinder Foundry is the implementation of the [LLM Foundry development plan](llm-foundry-development-plan.md).
It uses a pinned llama.cpp compute backend behind an opaque C API. The registry,
GGUF inspector, memory planner, CLI, and GTK4 GUI are written in C17. Inference
runs offline and never falls back to a remote provider.

**Status: 0.1.0 development preview.** The copied SmolLM2 artifact has passed local
CPU integration tests. See [validation evidence](docs/validation.md) for the exact
checks and [release gates](docs/releasing.md) for unfinished distribution work.
This is a source preview, not a signed, portable desktop binary release. No
performance improvement over another runtime is claimed.

The display name is provisional and distinguishes this project from the
[unrelated MosaicML LLM Foundry](https://github.com/mosaicml/llm-foundry).
The executable and C API retain the `foundry` name. Do not install the
`llm-foundry` package from PyPI to obtain this project.

## What works

- Native GGUF and copied Ollama-manifest imports, streamed SHA-256 verification,
  reference/copy modes, atomic registry records, and retained source metadata.
- Bounded GGUF v2/v3 inspection, tensor shapes/types, explicit compatibility errors,
  and a conservative memory estimate with an optional hard planning budget.
- CPU raw completion, greedy or seeded sampling, streaming UTF-8, cross-token stop
  strings, cancellation, tokenization, and JSON timing records.
- Persistent loaded weights for CLI and GUI chat, using the verified SmolLM2
  template. Each turn reprocesses full structured history with a fresh KV state.
- A C-only GTK4 app with import, model selection, inspection, planning, settings,
  streaming chat, Stop, New Chat, Copy, Export, and timing displays.
- A shared C SDK, CMake package, C sample, synthetic tests, real-model tests,
  sanitizer configuration, CI workflows, and benchmark/tuning scripts.

Metal can be selected at build time; it remains an unverified platform combination
in this preview. Advanced template families, sharded models, automatic tuning
profile application, portable GTK bundling, custom compute kernels, and a server
are tracked in the [roadmap](docs/roadmap.md).

## Build

Prerequisites: CMake 3.24+ (3.25+ for the supplied presets), Ninja, C17/C++17
compilers, Python 3.10+ for tests, and GTK4 4.10+ with pkg-config for the GUI.

```sh
# Inside a clone of this repository:
git submodule update --init --recursive

# macOS development dependencies:
brew install cmake ninja gtk4 pkgconf

# Ubuntu 24.04 development dependencies instead:
# sudo apt-get install build-essential cmake ninja-build pkg-config libgtk-4-dev python3

cmake --preset release
cmake --build --preset release
ctest --preset release
```

CPU is the default. `cmake --preset cpu` deliberately builds without the GUI.
`cmake --preset metal` requests Metal on a compatible macOS machine. Configuration
fails if the required GUI dependency or pinned backend is missing.
See [installation](docs/installation.md) for offline builds, sanitizers, installation,
C SDK consumers, and macOS application bundles.

## Import and inspect

For the assets described in the development plan:

```sh
# Optional: keep all development imports inside this ignored project directory.
export FOUNDRY_HOME="$PWD/models/registry"

./build/release/foundry import ollama \
  --manifest metadata/manifest.json --weights-root . --metadata-root metadata \
  --name smollm2
./build/release/foundry inspect smollm2 --json
./build/release/foundry plan smollm2 --context 2048
```

For another complete local GGUF file:

```sh
./build/release/foundry import gguf --path '/path/to/model.gguf' --name my-model
# Add --copy to keep an independently verified copy in the registry's artifact store.
```

Reference imports preserve the original file and require it to remain at that
path. Inspecting a file is not proof that the compute backend supports it.
See [models and provenance](docs/models.md) and the [compatibility matrix](docs/compatibility.md).

## Generate and chat

```sh
./build/release/foundry run smollm2 --raw \
  --prompt 'The capital of France is' --max-tokens 32 --metrics-json
./build/release/foundry run smollm2 --chat \
  --prompt 'Explain a hash table.' --max-tokens 128
./build/release/foundry chat smollm2 --system 'Answer concisely.'
./build/release/foundry-gui
```

Raw text goes to stdout. Backend logs, errors, and `--metrics-json` records go to
stderr. Ctrl+C cancels a CLI request; Stop cancels a GUI request. Cancellation
waits for a supported backend interruption point. In CLI chat, use `/new` to
clear history and `/quit` to exit. In the GUI, use Ctrl+Enter to send a multiline
prompt. Conversations are saved only when you choose Export.

See the [CLI reference](docs/cli.md), [GUI guide](docs/gui.md), and
[C API reference](docs/api.md).

## Measure

```sh
./build/release/foundry bench smollm2 --repetitions 7 --warmups 2
python3 scripts/bench_matrix.py --model '/path/to/model.gguf' --mode warm-model
python3 scripts/tune.py --model '/path/to/model.gguf' --budget-seconds 300
```

The harness saves raw samples, hashes, settings, and environment fields under
ignored local directories. Process load, prefill, decoding, and first content
latency use different clocks. Missing observations are marked unavailable.
Compare aligned workloads and repeated samples; these tools do not prove a
speedup by themselves. Read [benchmark methodology](docs/benchmarking.md).

## Project documentation

[Architecture](docs/architecture.md) · [Installation](docs/installation.md) ·
[Model compatibility](docs/compatibility.md) · [Troubleshooting](docs/troubleshooting.md) ·
[Development](CONTRIBUTING.md) · [Security](SECURITY.md) ·
[Release checklist](docs/releasing.md) · [Changelog](CHANGELOG.md)

Original source is [MIT licensed](LICENSE). Dependencies and model licenses are
separate; see [third-party notices](THIRD_PARTY_NOTICES.md). Model weights and
private prompts are excluded from this repository.
