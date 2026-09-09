# LLM Foundry development plan

Build a local inference framework with a C API, a terminal interface, a required C-native inference GUI written entirely in C17, explicit model compatibility, and hardware-specific tuning. Start with the SmolLM2 files already copied into `~/Desktop/llm-foundry`. Establish correct inference and reproducible measurements before attempting custom compute kernels.

This is an implementation runbook, not an already implemented framework. The setup and baseline commands below use existing tools. Commands invoking `foundry` are acceptance targets that become runnable only after the stated implementation stage. Run the stages in order. No command in this document has been executed on your Mac, and no speedup has been measured there.

## 1 Scope and engineering decisions

The first release should import a local model, explain its memory requirements and compatibility, run text inference offline through both a CLI and desktop GUI, and benchmark candidate settings. It should retain the model's identity and original metadata. It should report unsupported models accurately.

The GUI application source must be C-only. Use GTK4 through its C API, construct widgets and callbacks in `.c` files, and link directly to the shared Foundry C API. No Electron, browser/WebView, JavaScript, Python GUI, C++ GUI source, or HTTP server is required for desktop inference. This requirement concerns the GUI application's implementation language: GTK's macOS platform integration and the initial llama.cpp compute dependency contain platform code and C++ respectively. It does not mean the entire transitive dependency stack is pure C. GTK supplies native desktop windows and input integration, although its widgets are GTK-rendered rather than standard AppKit controls. [GTK C API](https://docs.gtk.org/gtk4/)

Use C17 for the public API, CLI, registry, and runtime coordination. Use llama.cpp behind an adapter for the initial compute implementation. Its library exposes a C-style interface, but its implementation includes C++. Link through CMake so the C++ runtime and hardware libraries are resolved correctly. Python is acceptable for development scripts, import utilities, benchmarking, and evaluation. It should not sit in the per-token execution loop. [llama.cpp build documentation](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md)

Keep two development tracks distinct:

| Track | Deliverable | Purpose |
| --- | --- | --- |
| Product runtime | Model importer, C API, CLI, memory planner, backend adapter, tuning, benchmarks | Build a usable platform with broad support through existing engines |
| Experimental runtime | Scalar reference kernels, quantized kernels, architecture implementation | Investigate whether specialized execution can improve a measured bottleneck |

Do not make the product wait for the experimental runtime. An all-C implementation of selected architectures is possible, but universal architecture support and competitive kernels are separate, substantial projects.

“Any model” is the long-term interface goal. Compatibility actually depends on architecture, operators, weight format, quantization, tokenizer, positional encoding, templates, and available hardware. Support must be a versioned capability matrix. A file extension or open license alone does not establish executability.

Use `llm-foundry` as the existing local folder and `foundry` as a provisional executable name. Before public distribution, choose a distinct project and package identity: MosaicML already has an unrelated project called LLM Foundry. Do not install `llm-foundry` from a package index expecting it to be this framework. [Existing MosaicML project](https://github.com/mosaicml/llm-foundry)

## 2 Starting assets and constraints

The following facts come from your terminal output. They have not been independently checked on disk.

| Asset | Location relative to the project | Interpretation |
| --- | --- | --- |
| SmolLM2 weights | `sha256-4d2396b16114669389d7555c15a1592aad584750310f648edad5ca8c4eccda17` | Manifest reports 1.695 GiB and a model layer |
| Model manifest | `metadata/manifest.json` | Identifies the copied layers |
| Configuration and other metadata | `metadata/sha256-*` | Config, system prompt, template, license, parameters |

Do not infer exact parameter count, tensor types, quantization, or architecture from the rounded size or `latest` tag. Verify the actual container and metadata first. An Ollama blob can be a model container, a small metadata file, or another layer type. The manifest links them by digest. [Ollama manifest format](https://pkg.go.dev/github.com/ollama/ollama/server/internal/manifest)

The cloud model entries shown earlier have no local weight layers and cannot supply local inference weights. Gemma4 is not part of this initial project import. The incomplete download files are also outside the starting dataset.

Initial platform assumption: macOS on the Mac used in your terminal transcript. Detect architecture and RAM before choosing settings. Use Apple Metal only on a compatible machine. Begin with context 2,048 and one request at a time. These are provisional test settings, not an assertion that every model or machine supports them.

## 3 Deliverables and release gates

| Milestone | Required deliverables | Completion gate |
| --- | --- | --- |
| M0 Reproducible baseline | Verified assets, machine report, pinned backend, Ollama and direct backend measurements | Same local weights load and generate in the direct backend |
| M1 Minimal runtime | C API, model/session ownership, raw completion CLI, CPU and supported GPU execution | Offline generation, clean cancellation, explicit errors |
| M2 Usable framework | Import, inspect, chat formatting, memory planning, JSON metrics | Model can be registered and run without hand-editing internal files |
| M2G C desktop inference | C-only GTK4 application, model picker, streaming chat, settings, memory and timing displays | Offline GUI inference, responsive Stop, correct lifecycle and CLI parity |
| M3 Measured optimization | Controlled benchmark suite, profiles, tuning cache | Repeatable benefit or documented parity with the baseline |
| M4 Broader support | Multiple verified architecture families, backend capability reporting | Each advertised combination passes compatibility fixtures |
| M5 Distributable release | Install package, C SDK example, documentation, CI, release evidence | Clean installation and offline smoke test on a separate machine |

An honest M3 outcome can be “performance parity with better inspection and reproducibility.” A speed claim requires evidence. Picking particular virtual addresses or replacing a CLI front end does not eliminate matrix computation.

## 4 Stage 0 Prepare the workspace

**Objective:** preserve the copied assets and establish reproducible development tools.

Run these inventory commands in Terminal:

```bash
cd "$HOME/Desktop/llm-foundry"
pwd
sw_vers
uname -m
sysctl -n hw.memsize
xcode-select -p
clang --version
git --version
cmake --version
python3 --version
ollama --version
df -h .
```

If developer tools are missing, run `xcode-select --install`, finish the installer, and rerun the checks. If CMake is missing and Homebrew is already installed, run `brew install cmake`. Otherwise install CMake using its official distribution. Use Python 3.10 or later for this plan's development scripts. Record the versions actually selected.

Create the workspace directories and initialize Git only if this folder is not already inside a repository:

```bash
cd "$HOME/Desktop/llm-foundry"
mkdir -p include/foundry src/core src/cli src/backends/llama
mkdir -p tests/unit tests/integration tests/fixtures
mkdir -p scripts docs/decisions models/smollm2 profiles benchmarks/runs vendor
if ! git rev-parse --show-toplevel >/dev/null 2>&1; then
  git init
fi
git rev-parse --show-toplevel
```

The reported repository root should be the intended project. If it is a parent repository, decide whether to use that repository before adding a submodule. Preserve any existing source files. Append these ignore rules if they are not already present:

```bash
cat >> .gitignore <<'EOF'

# Local model assets and generated output
/sha256-*
/metadata/
/models/
/build*/
/.venv/
/benchmarks/runs/
*.gguf
*.safetensors
*.partial*
EOF
```

Keep redistributable synthetic test fixtures in `tests/fixtures`, not the ignored model directory. Check `git status --short` before every commit. Raw prompts from personal work and multi-gigabyte weight files should not enter the source repository.

Create `docs/environment.md` with the inventory output, available disk space, power mode, and native architecture. On Apple Silicon, avoid mixing an x86 Python or shell with an ARM backend build unless deliberately testing translation.

**Gate:** tools run, source root is known, local weights remain present, and large assets are ignored.

## 5 Stage 1 Verify and normalize SmolLM2

**Objective:** establish exactly which bytes the framework will execute.

Run this validation from the project root. It streams SHA-256 checks instead of reading the entire weight file into Python memory. It creates a descriptive symlink only after validation. It does not alter the model bytes.

```bash
python3 - <<'PY'
import hashlib
import json
from pathlib import Path

root = Path.cwd()
manifest_path = root / "metadata/manifest.json"
manifest = json.loads(manifest_path.read_text())
layers = manifest.get("layers") or []
entries = ([manifest["config"]] if manifest.get("config") else []) + layers
weight_paths = []

for entry in entries:
    digest = entry.get("digest", "")
    if not digest.startswith("sha256:"):
        raise SystemExit(f"Unsupported digest: {digest!r}")
    expected = digest.split(":", 1)[1]
    if len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
        raise SystemExit("Invalid SHA-256 digest")
    filename = "sha256-" + expected
    media = (entry.get("mediaType") or "").split(";", 1)[0].strip()
    is_weights = media == "application/vnd.ollama.image.model"
    path = (root if is_weights else root / "metadata") / filename
    if not path.is_file():
        raise SystemExit(f"Missing file: {path}")
    if "size" in entry and path.stat().st_size != entry["size"]:
        raise SystemExit(f"Size mismatch: {path.name}")
    digestor = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(8 * 1024 * 1024), b""):
            digestor.update(block)
    if digestor.hexdigest() != expected:
        raise SystemExit(f"Hash mismatch: {path.name}")
    print(f"Verified {media}: {path.name}")
    if is_weights:
        weight_paths.append(path)

if len(weight_paths) != 1:
    raise SystemExit("This initial importer expects one model layer")
model = weight_paths[0]
with model.open("rb") as handle:
    magic = handle.read(4)
if magic != b"GGUF":
    raise SystemExit(f"Expected GGUF signature, found {magic!r}; inspect format before proceeding")
alias = root / "models/smollm2/model.gguf"
alias.parent.mkdir(parents=True, exist_ok=True)
if alias.exists() or alias.is_symlink():
    if alias.resolve() != model.resolve():
        raise SystemExit(f"Existing alias points elsewhere: {alias}")
else:
    alias.symlink_to(Path("../..") / model.name)
print(f"Validated GGUF container signature and alias: {alias}")
PY
```

The four-byte signature is a format check, not full structural validation. The backend must still validate the model. This script is intentionally specific to the single model layer in your copied SmolLM2 manifest. General tensor-layer and sharded imports are later work.

Implement `scripts/inspect_import.py` next. It should parse the manifest, use a maintained GGUF reader or the pinned backend metadata API, and write `models/smollm2/model.json`. The GGUF Python package is supplied with llama.cpp. Pin it to the same source revision if used. [GGUF Python tools](https://github.com/ggml-org/llama.cpp/blob/master/gguf-py/README.md)

Record these fields:

| Field group | Required values |
| --- | --- |
| Identity | Source model/tag, weight SHA-256, original manifest SHA-256, import timestamp, local paths |
| Architecture | Architecture identifier, layer count, embedding width, attention heads, KV heads, head dimensions, trained context |
| Representation | Container version, tensor count, tensor names/shapes/types, actual file bytes, quantization metadata |
| Tokenization | Vocabulary size, tokenizer type, BOS/EOS identifiers, relevant special tokens, add-BOS behavior |
| Formatting | Embedded chat template if present, Ollama template bytes, system prompt, stop conditions, parameters |
| Provenance | License files, conversion history if known, inspection tool and version |

Retain unknown fields. Reject missing required values instead of inventing architecture defaults. Tokenizer information may be embedded in GGUF rather than supplied as a separate file. Do not assume an Ollama template can be passed directly to another template engine.

**Gate:** all copied digests match, GGUF loads structurally, and inspection identifies the actual architecture and tensor types. Save failures as importer test fixtures with synthetic content.

## 6 Stage 2 Build and freeze the direct backend

**Objective:** prove that the existing model runs before writing your own execution wrapper.

For a new project without an existing backend checkout:

```bash
git submodule add https://github.com/ggml-org/llama.cpp.git vendor/llama.cpp
git -C vendor/llama.cpp rev-parse HEAD > docs/backend-revision.txt
```

This selects the revision fetched at setup time and records its exact commit. Commit the submodule pointer with the project. Subsequent builds use `git submodule update --init --recursive`, not an automatic update to upstream HEAD. If the folder already exists, inspect and reuse it rather than rerunning `submodule add`.

On an Apple Silicon Mac, build with Metal:

```bash
cmake -S vendor/llama.cpp -B build/backend-metal \
  -DCMAKE_BUILD_TYPE=Release -DGGML_METAL=ON
cmake --build build/backend-metal --config Release --parallel 2
```

Build a separate CPU configuration for comparison:

```bash
cmake -S vendor/llama.cpp -B build/backend-cpu \
  -DCMAKE_BUILD_TYPE=Release -DGGML_METAL=OFF
cmake --build build/backend-cpu --config Release --parallel 2
```

Use the upstream build guide for another platform. Limit build concurrency initially to reduce memory pressure. Record the CMake cache, compiler, source SHA, and build log. A successful configuration must actually enable the requested backend. Treat “unused variable” or unavailable-backend messages as a configuration issue. [Backend build options](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md)

Select the raw completion binary using the names found in your pinned revision:

```bash
FOUNDRY_COMPLETION=""
for candidate in build/backend-metal/bin/llama-completion build/backend-metal/bin/llama-cli; do
  if [ -x "$candidate" ]; then
    FOUNDRY_COMPLETION="$candidate"
    break
  fi
done
if [ -n "$FOUNDRY_COMPLETION" ]; then
  "$FOUNDRY_COMPLETION" --help > docs/backend-completion-help.txt
else
  printf '%s\n' 'Completion binary not found. Inspect the pinned build targets.'
fi
```

Do not continue with an empty variable. Check the saved help for the flags used below. Current upstream documents raw completion using `llama-completion` and `-no-cnv`; older builds may expose a different executable. [Completion tool documentation](https://github.com/ggml-org/llama.cpp/blob/master/tools/completion/README.md)

```bash
"$FOUNDRY_COMPLETION" \
  -m models/smollm2/model.gguf \
  -no-cnv -p "The capital of France is" \
  -n 32 -c 2048 --temp 0 --seed 42 -ngl 99
```

`-ngl 99` requests extensive GPU layer offload. It is not proof of placement. Inspect the load log and record actual offload. Repeat with the CPU binary and `-ngl 0`. Use raw completion for the initial equivalence test so hidden system prompts and templates do not confound it.

**Gate:** the same model produces sensible raw completion on at least one backend, the load log records architecture and types, and unsupported placement is detected. If loading fails, fix compatibility or select a verified upstream revision before implementing the Foundry wrapper.

## 7 Stage 3 Capture an Ollama baseline

**Objective:** preserve a reference measurement before optimization.

Ensure Ollama is running and `smollm2:latest` is available. These commands use the local API and do not invoke cloud models:

```bash
ollama list
ollama ps
curl --fail --silent --show-error http://localhost:11434/api/version
```

Create `scripts/bench_ollama.py` with the following content. It performs one warm-up and five measured requests. It saves raw responses and computes decode throughput from Ollama's timing fields. The API expresses durations in nanoseconds. [Ollama generate API](https://docs.ollama.com/api/generate)

```python
import json
import statistics
import time
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
out = Path("benchmarks/runs") / ("ollama-" + stamp)
out.mkdir(parents=True)
payload = {
    "model": "smollm2:latest",
    "prompt": "Explain how a hash table handles collisions in plain English.",
    "raw": True,
    "stream": False,
    "keep_alive": "10m",
    "options": {"temperature": 0, "seed": 42, "num_ctx": 2048, "num_predict": 128},
}
(out / "request.json").write_text(json.dumps(payload, indent=2))
rates = []
for index in range(6):
    request = urllib.request.Request(
        "http://localhost:11434/api/generate",
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"},
    )
    start = time.perf_counter()
    with urllib.request.urlopen(request, timeout=600) as response:
        result = json.load(response)
    if result.get("error") or not result.get("done"):
        raise RuntimeError(result)
    result["client_wall_seconds"] = time.perf_counter() - start
    result["warmup"] = index == 0
    duration = result.get("eval_duration", 0)
    count = result.get("eval_count", 0)
    rate = count * 1e9 / duration if duration > 0 and count > 0 else None
    result["decode_tokens_per_second"] = rate
    (out / f"run-{index}.json").write_text(json.dumps(result, indent=2))
    if index and rate is not None:
        rates.append(rate)
    print(index, "warmup" if index == 0 else "measured", rate)
if len(rates) != 5:
    raise RuntimeError("Expected five valid decode measurements")
print("Median decode tokens/s:", statistics.median(rates))
print("Results:", out)
```

Run it from the project root:

```bash
python3 scripts/bench_ollama.py
ollama ps
```

This is a diagnostic baseline, not the final comparison harness. Repeated prompts may reuse cached prefix computation. Do not report these runs as uncached prefill measurements. Non-streaming timing does not measure first-token latency. An early EOS means fewer than 128 generated tokens; record the actual count.

Before direct-backend tests, unload the Ollama model with `ollama stop smollm2:latest` so two runtimes do not compete for the same memory. Keep power mode, other applications, and workload fixed. Do not infer that Ollama is simply an unoptimized interpreter: local inference uses native compute backends, and engine selection can vary by model and release. Record the installed version and backend logs rather than assuming every version follows one engine path.

**Gate:** five valid measurements, original requests/responses retained locally, hardware placement recorded, and no unsupported speed claim.

## 8 Stage 4 Establish the repository contracts

Create the following files. Write interfaces and ownership rules before implementing the decode loop.

| Path | Responsibility |
| --- | --- |
| `CMakeLists.txt` | C17 project, C++ link support, pinned backend, CLI/library/test targets |
| `include/foundry/foundry.h` | Public C API, opaque handles, error codes, ABI version |
| `src/core/model.c` | Import registry, model identity, loading lifecycle |
| `src/core/session.c` | Per-session context and cancellation |
| `src/core/config.c` | Validated options and precedence |
| `src/core/metrics.c` | Monotonic timing and structured event records |
| `src/core/planner.c` | Memory estimates and placement requests |
| `src/backends/backend.h` | Internal adapter interface and capability description |
| `src/backends/llama/adapter.cpp` | Only layer that depends directly on upstream inference API details |
| `src/cli/main.c` | Argument parsing, stdout/stderr contracts, process exit codes |
| `src/gui/` | C17 GTK4 application, runtime worker, views, settings, desktop actions |
| `scripts/inspect_import.py` | Development importer and inspection prototype |
| `scripts/bench_ollama.py` | Baseline measurement above |
| `scripts/bench_matrix.py` | Later cross-runtime benchmark orchestration |
| `docs/decisions/0001-runtime.md` | C API, C++ backend, format support, scope decisions |
| `docs/compatibility.md` | Tested combinations and unsupported cases |

Use a backend adapter rather than sprinkling upstream calls through the core. llama.cpp exposes model/context operations, tokenization, decoding, and sampling through its public header. Consult the pinned header when implementing because signatures evolve. [Public inference API](https://github.com/ggml-org/llama.cpp/blob/master/include/llama.h)

Specify the following proposed API operations in `foundry.h`: runtime create/destroy, capabilities query, model inspect/load/unload, session create/reset/destroy, tokenize, prefill, decode one token, generate with callback, request cancellation, and retrieve last error. Use opaque model and session handles. Define explicit status codes for unsupported architecture, missing artifact, invalid config, corrupt model, insufficient memory, backend failure, and cancellation.

Required ownership rules:

1. A runtime owns backend initialization and shutdown.
2. A model handle owns immutable weights and model metadata.
3. A session owns KV state, sampler state, token history, and temporary buffers.
4. A session retains its model. Reject unload while references remain, or defer unload safely.
5. Callbacks receive borrowed text bytes valid only for the documented callback duration.
6. Errors must not throw C++ exceptions across the C ABI. Translate exceptions within the adapter.
7. v0.1 supports one active generation per session. Do not advertise concurrent use of a handle.

Set config precedence to explicit CLI/API settings, then a compatible saved tuning profile, then model import defaults, then conservative framework defaults. Print the resolved effective configuration in JSON so a run can be reproduced.

**Gate:** a C-only sample application links successfully to the library, creates/destroys an empty runtime, and receives a structured unsupported-model error.

## 9 Stage 5 Implement model loading and memory planning

Implement loading in this order:

1. Resolve registered model ID to artifact paths and digest.
2. Reject partial downloads and missing shards.
3. Validate container, metadata sizes, tensor boundaries, and supported types through the backend.
4. Query capabilities for the actual architecture and requested device.
5. Compute a preliminary memory estimate.
6. Load weights through the adapter and record actual placement.
7. Allocate the context and reusable workspace.
8. Record actual allocation observations and readiness.
9. On any failure, release all resources already acquired and return a typed error.

For the initial adapter, let the backend own weight mapping and device allocation. Mapping the same file again in Foundry just to control addresses adds complexity and can duplicate memory. Read-only mappings and planned buffer reuse are useful; hard-coded addresses are not a speed feature. Never use destructive fixed mappings to force placement.

For a conventional transformer with a KV cache, begin with this approximate cache budget:

`KV bytes ≈ sequences × context tokens × layers × KV heads × (key head dimension × key element bytes + value head dimension × value element bytes)`

Add weights, compute workspace, allocator/backend overhead, logits buffers, and a safety reserve. Quantized caches have block metadata and alignment. Hybrid, recurrent, sliding-window, and other architectures need different state models. On unified memory, do not double-count a shared allocation as separate CPU and GPU copies, but do account for genuine staging or repacking buffers.

Implement a dry-run plan showing estimated bytes and uncertainty. The plan must not claim a model fits just because its file size is below installed RAM. Record both total machine memory and runtime availability. Begin with an explicit reserve policy that is configurable and tested under pressure.

If the requested plan does not fit, report the cause and alternatives: smaller context, fewer concurrent sessions, a supported lower-precision representation, or a smaller model. Do not silently reduce context or switch backend during a benchmark. Interactive `--auto` mode may adjust settings only while showing the resolved changes.

**Gate:** load/unload cycles succeed, a deliberately excessive context request fails cleanly, and the actual device choice is visible.

## 10 Stage 6 Implement correct raw generation

Build the generation path incrementally:

1. Tokenize UTF-8 input with the model's tokenizer and explicit special-token policy.
2. Check prompt length plus generation budget against context capacity.
3. Prefill the prompt in supported batches with correct positions and sequence IDs.
4. Obtain logits for the last prompt token.
5. Select the next token. Implement greedy decoding first, then seedable sampling through the backend.
6. Decode the selected token into the context so the next step sees it.
7. Reuse KV state. Do not recompute the entire prompt per generated token.
8. Stop on end-of-generation tokens, configured limit, stop sequence, error, or cancellation.
9. Stream text while handling UTF-8 fragments that span token boundaries.
10. Return final counters and reason for termination.

Define empty-prompt behavior deliberately. Avoid adding BOS twice. Support stop strings that span multiple generated tokens and hold back enough output bytes to avoid leaking a matched stop sequence. Keep output text on stdout and progress/errors on stderr.

Implement one-shot `run` first, then a persistent `chat` session that keeps the model and session alive. A one-shot process reloads the model on each invocation, which is not comparable to a warm Ollama service for end-to-end latency.

Proposed acceptance commands after this stage:

```bash
./build/release/foundry run --model models/smollm2/model.gguf \
  --raw --prompt "The capital of France is" \
  --max-tokens 32 --context 2048 --temperature 0 --backend llama
./build/release/foundry run --model models/smollm2/model.gguf \
  --raw --prompt "Explain binary search." --max-tokens 128 --metrics-json
```

Specify command semantics and implement these flags before claiming these commands work. `--metrics-json` should emit a final structured metrics record on stderr or a specified file, not corrupt the text stream.

**Gate:** greedy token IDs match the direct backend under the same build, input tokens, settings, and deterministic conditions. Across different hardware kernels, use numeric/logit tolerances and investigate near-tied logits rather than promising universal bitwise output identity.

## 11 Stage 7 Implement registration and chat metadata

The prototype importer now becomes a supported command. Keep the Python implementation as a development reference and implement native import/inspection in the core library so the GUI and installed CLI can operate without Python. Bare `foundry` commands in this document assume the built executable is on PATH; during development, substitute `./build/release/foundry`.

Implement these proposed commands:

```bash
foundry import ollama --manifest metadata/manifest.json \
  --weights-root . --metadata-root metadata --name smollm2
foundry inspect smollm2 --json
foundry plan smollm2 --context 2048 --backend auto
foundry run smollm2 --prompt "Explain a hash table." --max-tokens 128
foundry chat smollm2
```

The importer must handle missing or null `layers`, config-only cloud entries, duplicate references, shared blobs, absent files, invalid digests, and incomplete downloads. The `layers: null` exception encountered earlier becomes a regression test. A config-only remote reference should produce `REMOTE_ONLY_MODEL`, not an empty local model that fails later.

Use content-addressed artifact identity, atomic registry writes, and a versioned JSON schema. Separate import metadata from mutable run configuration. Preserve license and provenance. Support copy and reference modes explicitly. Never modify a blob in Ollama's cache in place.

For chat formatting, first inspect both the embedded GGUF template and copied Ollama template. Select a canonical formatter and record why. Ollama and the backend may use different template languages. Translate only a tested subset or use a known matching model template. Preserve system messages, role markers, turn delimiters, generation prompt, stop tokens, and special-token rules. Reject an unsupported template instead of quietly using an arbitrary generic one.

Create golden tests for a system message, one user turn, multiple turns, empty content, Unicode, and user content containing token-like strings. Compare resulting token IDs against a trusted renderer. Raw completion and chat mode must remain separate modes.

**Gate:** import is repeatable, paths with spaces work, cloud entries receive a useful error, and chat rendering matches the verified model convention.

## 12 Stage 7A Build the required C inference GUI

**Objective:** provide a complete desktop inference workflow without requiring the user to run a server or open Terminal. This is a required release milestone, not an optional future interface. Start the window shell after Stage 4 and integrate live inference after Stage 7.

### Toolkit and build boundary

Use GTK4 and GLib through C headers. Pin a tested stable GTK4 version in the build record and specify a minimum supported version in CMake. Do not automatically adopt a development version shown by online API documentation. Prefer GTK 4.10 or newer if using the asynchronous `GtkFileDialog` API. Construct the first release's UI directly in C so there is no separate UI scripting layer.

On a Mac with Homebrew installed, install the development dependencies and verify their discovery:

```bash
brew install gtk4 pkgconf
pkg-config --modversion gtk4
pkg-config --cflags --libs gtk4
mkdir -p src/gui tests/gui
```

Use the selected GTK distribution's macOS packaging instructions when producing the final bundle. A Homebrew-linked development executable is not automatically a portable application. [GTK macOS distribution guidance](https://www.gtk.org/docs/installations/macos/)

Add a `foundry_core` library target, a `foundry` CLI target, and a `foundry-gui` target. The GUI target's source list must contain only `.c` files. Compile them as C17. Final linkage can require the C++ linker because the shared runtime includes the compute adapter; this must not cause GUI source to be compiled as C++.

Proposed CMake integration after the listed files and core target exist:

```cmake
option(FOUNDRY_BUILD_GUI "Build the C desktop inference application" ON)
if(FOUNDRY_BUILD_GUI)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.10)
  add_executable(foundry-gui
    src/gui/main.c
    src/gui/window.c
    src/gui/model_view.c
    src/gui/chat_view.c
    src/gui/settings_view.c
    src/gui/metrics_view.c
    src/gui/runtime_worker.c
    src/gui/session_store.c)
  set_target_properties(foundry-gui PROPERTIES
    C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
    LINKER_LANGUAGE CXX)
  target_link_libraries(foundry-gui PRIVATE foundry_core PkgConfig::GTK4)
endif()
```

GUI builds must fail clearly if GTK is missing. Headless users may deliberately set `FOUNDRY_BUILD_GUI=OFF`, but the product release must ship and test the GUI.

### Required screens and controls

| View | Required controls | Required behavior |
| --- | --- | --- |
| Model library | Import local weights/manifest, choose registered model, inspect, load, unload | Show name, actual precision, file size, local path and load status; reject remote-only entries clearly |
| Chat | Conversation transcript, multiline prompt, Send, Stop, New Chat, Copy, Export | Stream text, preserve role boundaries, support keyboard-only use and Unicode |
| Generation settings | Context, maximum new tokens, temperature, top-p, seed, backend, device preference | Validate ranges against capabilities and display the effective settings for the next run |
| Model details | Architecture, tensor representation, tokenizer/template origin, license, hash | Show trusted inspection data without guessing from the filename |
| Resource and timing panel | Estimated memory, observed memory with method label, load time, first-token time, tokens/s, token counts | Use actual runtime events; display unavailable metrics as unavailable |
| Tuning and benchmark view | Run bounded tuning, select objective, compare saved results, export JSON | Enable after Stage 9; show environment and quality differences alongside speed |

Keep the initial window simple: a model sidebar, central conversation, and collapsible settings/metrics pane. Use standard GTK text and list widgets. Start with safe plain text rather than interpreting generated HTML or introducing a web renderer. Long transcripts should remain selectable and scrollable. Only autoscroll when the user is already near the bottom.

The first-run flow is: open the application, select Import, choose the copied SmolLM2 manifest and artifact location, review model details and memory estimate, Load, enter a prompt, then Send. Support direct GGUF import too, but make missing chat metadata explicit. A cloud-only manifest should explain that local weights are absent.

### Runtime and UI threading

Keep all GTK widget access on the main UI thread. Perform model loading and inference on a dedicated worker. GTK documents moving long-running work away from the UI thread and delivering results back through the main context. [GTK threading guidance](https://docs.gtk.org/gtk4/section-threading.html)

Implement the worker in `runtime_worker.c` using GLib threading/queues or a documented C threading abstraction. Use the exact same Foundry C API as the CLI; do not spawn the CLI and parse its text, call Python, or send prompts to an HTTP service.

Implement typed events for load progress, model ready, prefill started, token bytes, metrics, completion, cancellation, and failure. Each event carries a session ID and generation ID. The worker owns runtime handles. It copies callback text before returning because the callback memory is borrowed. The UI receives owned event payloads and frees them after consuming them.

Use a bounded queue and a main-thread timer/source to drain it. Coalesce output updates roughly every 30–50 ms rather than appending a widget update for every token. Retain text in order and never drop token bytes to keep a metrics display current. Coalesce redundant metrics separately. Backpressure waits must be cancellation-aware so a closing window cannot strand the worker.

Implement state transitions explicitly: Empty, Loading, Ready, Generating, Cancelling, Failed, and Closing. Disable incompatible actions during loading/generation. Stop sets a thread-safe cancellation request checked between supported backend operations. Do not promise instant interruption of a GPU kernel. Display Cancelling until acknowledgement arrives. No second generation may start on the same session before cleanup.

On window close, request cancellation, stop new submissions, wait for worker acknowledgement without blocking UI event processing, release session/model/runtime handles in order, remove sources, and quit. Discard late events whose generation IDs no longer match. Do not dereference destroyed widgets from queued callbacks.

### Settings and conversation lifecycle

Persist non-sensitive preferences such as window size, selected model ID, and default generation settings through a C/GLib implementation. Use an application configuration directory rather than writing inside the weight folder. Store raw conversations only through an explicit Save/Export action in the first release. New Chat clears token/KV history and visible conversation while keeping weights loaded.

Changing sampling options applies to the next generation. Changing backend, device, or context allocation requires an acknowledged session/model reconfiguration. Make this distinction visible. Never silently change the running session underneath a worker.

Do not feed GUI-rendered text back as the conversation source. Maintain structured roles/content in the core and render from that state. This preserves parity with CLI chat and allows exports to remain independent of widget formatting.

### Implementation order and acceptance

1. Compile a C-only application shell with `GtkApplication`, a window, and Quit action.
2. Add model list and asynchronous file selection without loading weights.
3. Connect native import/inspection and show typed failures.
4. Add the worker lifecycle and load/unload progress.
5. Add prompt submission and streaming plain-text output.
6. Add Stop, cancellation acknowledgement, safe close, and stale-event handling.
7. Add validated settings and New Chat state reset.
8. Add metrics from shared runtime events and model details.
9. Add conversation export, clipboard, keyboard shortcuts, focus order, and accessible labels.
10. Add the tuning view after the tuning engine exists, using the same job/event contract.
11. Package a macOS `.app` with GTK and runtime resources and test Finder launch.

Proposed build/run commands after implementation:

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DFOUNDRY_BUILD_GUI=ON
cmake --build build/release --target foundry foundry-gui --parallel 2
./build/release/foundry-gui
```

Keep the same model and sampling configuration for a GUI-versus-CLI test. Compare core token IDs, stop reason, and counters. Measure GUI-visible first-token time separately from core first-token time. Profile rendering overhead on a long answer and transcript. A GUI adds display work and must not be marketed as a source of reduced inference compute.

Test resize during generation, repeated Send clicks, Stop during prefill and decoding, model switch attempts, file selection cancellation, missing model on restart, Unicode input methods, clipboard, long transcripts, and closing during load or generation. Check keyboard navigation, screen-reader labels, light/dark theme contrast, and display scaling on the target Mac. Test the actual window on macOS; a headless Linux build is not sufficient GUI validation.

**Gate:** all GUI application files compile as C17; no web/Python/C++ UI layer is present; the app imports and runs SmolLM2 offline, stays responsive, cancels safely, and matches the CLI's model behavior. No public release is complete without this gate.

## 13 Stage 8 Build a fair benchmark harness

This is the prerequisite for every speed claim. Implement `scripts/bench_matrix.py` and a shared JSON results schema. Preserve raw observations rather than only summary numbers.

Compare three configurations separately:

| Configuration | What it measures |
| --- | --- |
| Installed Ollama | Existing user experience and installed engine behavior |
| Pinned direct llama.cpp | Compute baseline without Foundry's coordination layer |
| Foundry using that same pinned backend | Framework overhead and tuning decisions |

For the first two, verify the weight hash is identical to the copied model and capture backend versions. If the engines differ, label that explicitly. A newer backend improvement is not evidence that Foundry's memory layout caused the gain.

Required result fields: run ID, UTC timestamp, machine/OS, power state, model and tokenizer hashes, backend and source revision, compiler/build settings, effective options, prompt hash, prompt token count, cached prompt token count, generated token count, requested and actual placement, load time, time to first token, prefill time, decode time, end-to-end time, memory measurement method, memory values, swap observations, stop reason, and error status.

Define the clocks precisely. Client time to first token starts when the request is submitted and ends at the first content token, excluding status-only streaming events. Runtime prefill starts when uncached prompt processing begins. Decode throughput is actual generated tokens divided by decode time under the engine's documented timing convention. Do not directly compare differently defined counters without noting the difference.

Create three benchmark modes:

1. **Fresh model process:** model is unloaded before each measured load. OS file pages may still be cached. Label this “process cold,” not “cold disk.”
2. **Warm model, fresh session:** weights remain loaded, prompt/KV state is cleared, and cache reuse is disabled or verified absent. Use this for controlled prefill and decode measurements.
3. **Warm conversation or cached prefix:** intentionally reuse state. Report cache hits and saved prefill work separately.

Use raw completion with matching input token IDs for compute comparisons. Measure chat formatting and HTTP/CLI overhead separately. Do not force EOS suppression in only one runtime. Track early termination and avoid treating different output lengths as identical end-to-end workloads.

Start with 128, 512, and 1,024 input tokens and 128 generated tokens where context permits. Add longer contexts only after memory planning passes. For each case, use two warm-ups and at least seven measured runs. Rotate runtime order across rounds. Use the same small prompt set across runtimes, with fresh sessions for uncached tests. Repeat finalists in a second session to detect thermal or background-load effects.

Report median, range or interquartile range, and raw samples. Seven runs are too few for a stable tail-latency claim. Collect a larger sample if p95 latency matters. Prompt throughput, decode throughput, and end-to-end latency must remain separate columns.

The upstream benchmark tool supplies distinct prompt-processing and token-generation cases and machine-readable output. Use it for backend microbenchmarks, not as a substitute for full request timing. [llama-bench documentation](https://github.com/ggml-org/llama.cpp/blob/master/tools/llama-bench/README.md)

Existing-tool diagnostic commands, after checking the pinned binary's help:

```bash
build/backend-metal/bin/llama-bench --help
build/backend-metal/bin/llama-bench \
  -m models/smollm2/model.gguf -p 512 -n 128 -r 7 -ngl 99 -o json \
  > benchmarks/runs/backend-metal.json
build/backend-cpu/bin/llama-bench \
  -m models/smollm2/model.gguf -p 512 -n 128 -r 7 -ngl 0 -o json \
  > benchmarks/runs/backend-cpu.json
```

Use timestamped names in the final harness so measurements are not overwritten. Verify actual placement and comparable thread settings. The benchmark's token workloads are not necessarily the same as the Ollama text prompt above.

**Gate:** one repeatable comparison report with aligned conditions, no missing critical metadata, and no claim based on a single run.

## 14 Stage 9 Optimize settings before replacing kernels

Profile first. Determine whether the target case is dominated by loading, uncached prefill, token decoding, memory movement, prompt formatting, or queuing. Use native profiling tools appropriate to the machine and include backend/device synchronization in timing where required.

Run a bounded search rather than every combination:

| Parameter | Initial candidates | Constraint |
| --- | --- | --- |
| Device placement | CPU, full supported GPU offload, one intermediate placement | Verify actual allocation and placement |
| CPU threads | 1, 2, 4, then a supported machine-specific count | Tune prefill and decode separately |
| Prompt batch size | 64, 128, 256 | Respect memory and backend limits |
| Context allocation | 1,024, 2,048, 4,096 | Only compare equal-context settings within a case |
| Attention implementation | Backend default and supported optimized option | Compatibility test before timing |
| KV precision | Default, then supported lower-precision option | Quality and memory evaluation required |

Keep weights and quality settings constant for the first search. Distinguish a faster configuration from a lower-quality configuration. Existing quantized weights should not be repeatedly requantized into lower precision and treated as equivalent to conversion from original higher-precision weights.

Implement `foundry tune smollm2 --objective decode --budget-seconds 300` only after the harness works. Candidate subprocesses need timeouts, resource cleanup, and failure records. Rank valid candidates by the requested objective under the memory budget. Use separate tuning and evaluation prompt sets. Revalidate the winner with a longer independent measurement.

Cache the selected profile by model digest, backend revision, hardware identity, OS/runtime compatibility, context class, and objective. Invalidate it when those inputs change. Profiles must be inspectable and overridable.

Suggested project gates, explicitly chosen rather than measured: framework-only overhead below 5% for a warm single-session case, and any advertised optimization improvement at least 10% in the named metric across repeat sessions without violating correctness or declared quality bounds. Change these targets if measurement noise or the product goal warrants it. Never present them as predicted speedups.

**Gate:** retain an optimization only when the profile explains the bottleneck and repeat measurements show the benefit. Revert or leave experimental any change that fails.

## 15 Stage 10 Add a portable experimental C engine

Start this track only when the product runtime and benchmark suite are stable. Its first milestone is correct inference for one verified architecture and representation, not universal performance superiority.

Implementation sequence:

1. Define tensor views with dimensions, strides, storage type, block layout, and checked byte bounds.
2. Implement aligned allocation and a per-session arena with clear lifetimes. Use the platform allocator's returned address.
3. Build scalar reference kernels for matrix-vector multiplication, normalization, softmax, positional rotation, residual addition, and the model's activation functions.
4. Validate each kernel on small deterministic arrays against a trusted reference, including extreme values and numerical stability.
5. Implement one verified architecture's forward pass, including exact tensor naming, dimensions, attention layout, and position conventions.
6. Implement incremental attention state and compare full-prefill results to incremental decoding results.
7. Add the actual tokenizer or reuse a verified tokenizer component. A correct matrix engine with a mismatched tokenizer is still incorrect inference.
8. Support the precise tensor types found in the SmolLM2 inspection. Implement every required quantization block layout and any mixed-precision tensors.
9. Compare layer outputs and logits against the trusted backend before generating long text.
10. Profile. Add SIMD and parallelism only to measured hotspots, keeping scalar code as the correctness oracle.
11. Evaluate fused dequantization and dot products to avoid materializing the whole model as float32.
12. Add the engine behind the same adapter interface with an explicit supported-capability list.

Do not expand the full quantized model to float32 as the default path. Quantized storage saves memory only if the execution path preserves those savings. A temporary tiny float model is useful for mathematical validation, but obtaining original high-precision weights is a separate download and disk/RAM decision.

For performance investigations, measure representative tensor shapes and an end-to-end workload. A faster isolated dot product can lose overall because of conversion, synchronization, or cache behavior. Speculative decoding is optional later research: it requires compatible draft/target handling, correct acceptance logic, and enough draft acceptance to pay for its overhead. Treat it as an experiment, not a guaranteed acceleration switch.

**Gate:** architecture-specific correctness passes, numerical tolerances are documented, and the custom engine has a measured reason to be selected. Otherwise keep it as an educational or research backend.

## 16 Stage 11 Expand model compatibility

Extend support along explicit dimensions. A second checkpoint of the same architecture tests a new artifact, not a new architecture implementation.

For each candidate model:

1. Select a small enough local checkpoint and retain its license/provenance.
2. Inspect format, architecture, tokenizer, required operators, and precision.
3. Check backend capabilities before downloading or converting large assets where possible.
4. Import into the versioned registry and validate all referenced artifacts.
5. Verify tokenization and chat formatting with golden cases.
6. Run raw completion, multi-turn chat, context-boundary, cancellation, and memory tests.
7. Record quality checks and representative performance measurements.
8. Add the exact tested model digest, backend revision, platform, and limitations to the matrix.

Prioritize: more GGUF models in the initial backend; then multiple architecture families; then sharded GGUF; then a controlled safetensors conversion/import path. Safetensors contains tensors, not a universal executable architecture. Its config and tokenizer assets must also be understood. Conversion is an import-time job with its own memory/disk budget and recorded tool revision.

Add another backend only when it solves a concrete unsupported model, hardware, or performance case. Keep capabilities visible through `foundry backends --json`. Separate “backend reports support,” “Foundry tested support,” and “experimental” states.

Defer vision/audio inputs, embeddings, adapters, mixture-of-experts specialization, distributed inference, and training until text inference is reliable. Each can later be introduced as an explicit capability. A remote provider adapter must be visibly remote and must not silently replace a failed local execution path.

**Gate:** at least two genuinely different architecture families pass the published test matrix before describing the framework as broadly model-compatible.

## 17 Stage 12 Add optional local serving

This stage follows the CLI and persistent chat session. It is useful for comparing a warm service to Ollama and for other applications to call the runtime.

Implement a local-only server bound to `127.0.0.1` by default. Specify health, capabilities, model inspection, generation, and streaming generation endpoints. Implement one active generation initially with a bounded queue. Document status codes, cancellation, timeouts, output event order, and final metrics.

Separate immutable shared model weights from per-request mutable state. Enforce aggregate context/KV budgets across active sessions. A client disconnect must release or cancel its request. Avoid unbounded prompt bodies and token budgets. Do not shell-interpolate model paths or prompts. Network exposure and authentication belong to a later deliberate feature decision.

If adding compatibility with another API, publish exactly which endpoints and fields work. Avoid claiming full compatibility based on one successful request. Measure client overhead separately from backend computation.

**Gate:** bounded requests succeed, cancelled/disconnected requests clean up, concurrent sessions do not mix state, and overload produces a clear response rather than exhausting memory.

## 18 Stage 13 Test and package the release

The following tests protect actual failure modes and form the release suite:

| Area | Required evidence |
| --- | --- |
| Import | Null layers, config-only models, invalid hashes, truncated container, missing shards, paths with spaces |
| C API | Ownership, repeated create/destroy, failure cleanup, errors across C/C++ boundary |
| Generation | BOS/EOS, long prompt, exact context boundary, UTF-8 fragments, stop sequences, cancellation |
| Chat | Canonical rendered token IDs across roles and turns |
| C GUI | C17 source boundary, CLI parity, responsiveness, cancellation/close races, keyboard/accessibility, clean desktop launch |
| Numerical correctness | Same-backend parity plus tolerant reference checks for custom kernels |
| Memory | Repeated sessions stabilize, OOM is handled, requested plan fits aggregate limits |
| Performance | Raw samples, reproducible environment, release comparison report |
| Distribution | Clean build/install, executable starts, libraries resolve, offline generation works |

Use AddressSanitizer and UndefinedBehaviorSanitizer on CPU debug builds of your native code. Keep GPU performance tests in release builds without sanitizer overhead. Test malformed inputs without requesting huge allocations. Set timeouts so corrupt models or stalled subprocesses cannot hang CI indefinitely.

Implement CMake targets for `foundry`, the library, and CTest tests. Proposed commands after those targets exist:

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel 2
ctest --test-dir build/release --output-on-failure
```

CI should run a small CPU suite with synthetic or appropriately licensed tiny fixtures. Do not upload the private local model just to make CI work. Run the real SmolLM2 integration and performance suite on a suitable machine and archive its result identifiers. Add macOS ARM and Linux CPU builds before making cross-platform claims. GPU tests require matching hardware, not just successful compilation.

Package the CLI, required desktop GUI, required dynamic libraries/framework resources, C header, example C caller, licenses, version information, and installation instructions. For macOS, include a `.app` bundle, application identity, icon, GTK resources, and working runtime library paths. Test on a machine without your Homebrew prefixes. Plan signing/notarization for public distribution and verify current platform requirements at that stage. Verify runtime library search paths outside the build tree. Do not redistribute third-party weights by default. Preserve notices for bundled dependencies. Pin and document the dependency revision used for each release.

Create README sections for install, first import, raw completion, chat, memory planning, benchmark interpretation, compatibility, and troubleshooting. Document unsupported features plainly. A reproducibility bundle should include build settings, hashes, commands, and benchmark JSON, without private prompts unless intentionally included.

**Gate:** install into a fresh location, run the bundled C sample, import local weights, disconnect networking, and generate successfully. Release only after all claimed platform/capability checks pass.

## 19 Work breakdown and scheduling

These are planning estimates for one developer working full-time and learning parts of the native inference stack. They are not delivery guarantees. Backend/API changes or unfamiliar GPU tooling can expand them considerably.

| Work package | Depends on | Estimated focused days | Main artifact |
| --- | --- | ---: | --- |
| Workspace and asset verification | None | 1–2 | Environment record and validated import |
| Backend build and baselines | Verification | 2–3 | Pinned build and baseline JSON |
| API, ownership, loader, raw CLI | Baselines | 5–8 | M1 runtime |
| Registry, chat, memory planner | Raw CLI | 5–8 | M2 framework |
| Required C GUI and desktop integration | API shell, then M2 | 8–14 | M2G desktop application |
| Controlled benchmark harness | Raw CLI | 3–5 | Comparison report |
| Profiling and bounded tuning | Benchmark harness | 4–7 | M3 tuning profiles |
| Multiple architectures and import hardening | M2 | 4–8 | M4 compatibility matrix |
| Packaging, CI, documentation | Stable product path | 3–5 | M5 release |
| Optional local API | Stable sessions | 3–6 | Local service |
| Experimental C compute engine | Correct baseline and harness | 15–30+ | Research backend |

Expect roughly 35–60 focused days for the core product path including the required C GUI, before the optional service and custom engine. At ten hours per week, convert that to a substantially longer calendar schedule. Keep the first milestone small enough to finish before broadening support.

Use one issue per work package with these fields: problem, input assumptions, exact files, interface contract, implementation steps, tests, acceptance command, benchmark effect, and rollback. Each issue should end in a reviewable commit. Keep dependency upgrades separate from your own performance changes so results remain attributable.

Suggested initial issues, in execution order:

1. Verify copied SmolLM2 assets and save the machine inventory.
2. Build and pin the backend with CPU and supported GPU configurations.
3. Save Ollama and direct-backend baselines.
4. Define C API, backend interface, error model, and ownership.
5. Implement loader and one-session raw greedy generation.
6. Add CLI arguments, cancellation, stdout/stderr rules, and JSON metrics.
7. Add registry/import and the null-layer regression case.
8. Implement verified chat formatting and persistent chat.
9. Implement memory planning and explicit placement reporting.
10. Implement the C-only GTK shell and native import/model views.
11. Add worker-driven GUI inference, settings, cancellation, metrics, and chat export.
12. Finish the controlled benchmark matrix and evaluate framework and GUI overhead.

The initial product includes the terminal tool, embeddable runtime, and C desktop inference GUI. Defer a marketplace, remote service, automatic model downloader, or elaborate visual customization until these issues pass.

## 20 Troubleshooting decisions

| Symptom | First check | Next action |
| --- | --- | --- |
| Model fails to load | Weight hash, signature, architecture, backend revision | Fix compatibility before debugging generation |
| Fluent but wrong or strange output | Template, special tokens, sampling, exact model identity | Compare raw tokens and first-step logits |
| Foundry slower than Ollama | Warm/cold status, device placement, backend version, context | Align conditions and profile the remaining difference |
| GPU enabled but no speed gain | Actual offload, workload size, synchronization | Compare native profiling and CPU performance |
| High memory or swapping | KV budget, duplicate model loads, quantization expansion | Reduce tested allocation and remove unnecessary copies |
| Prefill appears impossibly fast | Cached prompt count and reused session | Rerun with a fresh verified context |
| Different generated text | Prompt tokens, seed, greedy settings, numerical differences | Compare logits with tolerances and inspect near ties |
| Cloud entry imports as empty | Missing or null weight layers | Return a remote-only capability error |
| New backend release breaks build | Pinned header/API differences | Keep working pin and migrate in a separate branch |

## 21 Final definition of done

- [ ] The existing SmolLM2 weight file and copied metadata are verified and traceable.
- [ ] `foundry inspect`, `plan`, `run`, and `chat` work for the initial model.
- [ ] A small C program can use the public API without depending on internal C++ types.
- [ ] The C17-only GUI imports models, streams chat, exposes settings and metrics, and calls the same runtime directly.
- [ ] The GUI remains responsive during loading/generation and safely handles Stop, New Chat, and closing.
- [ ] The GUI launches from a clean desktop installation without a browser, Python runtime, or local HTTP server.
- [ ] Inference works offline with explicit backend and placement reporting.
- [ ] Context, tokenizer, template, and stopping behavior are correct.
- [ ] Import errors and memory failures are actionable and recover cleanly.
- [ ] Measurements distinguish loading, prefill, decoding, cache reuse, and client overhead.
- [ ] Optimizations are accepted on repeated evidence, with quality tradeoffs disclosed.
- [ ] Advertised model families have passed the versioned compatibility matrix.
- [ ] A fresh installation reproduces the smoke test and benchmark procedure.
- [ ] Public project naming is distinct from the existing MosaicML project.

The first concrete target is simple: the copied SmolLM2 model generates correct text through a C API and CLI. The required desktop milestone brings that same verified inference path into a C-only GUI, with enough instrumentation to explain every subsequent performance change.
