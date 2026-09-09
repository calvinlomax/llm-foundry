# C API and ownership

The contract is [`include/foundry/foundry.h`](../include/foundry/foundry.h).
Version `0.1.0-dev` exposes ABI version 1. This is a development ABI: rebuild
consumers when updating this preview. Public structures do not provide forward
size negotiation, so do not mix headers and libraries from different revisions.

A runtime initializes the backend. A model owns immutable weights and retains a
reference to its runtime. A session owns KV state, sampler, cancellation state,
and temporary buffers. Version 0.1 allows one session per model. Handles require
external serialization; **only `foundry_request_cancel` may be called concurrently
with a session operation**. Do not destroy handles from callbacks or while an
operation is running. There is no aggregate scheduler across independent runtimes.

Destroy a session, unload its model, then destroy its runtime. Unload/destroy
return `FOUNDRY_BUSY` when owned references remain. Failed creation leaves its
output handle null. `foundry_session_destroy(NULL)` and null unload/destroy are
safe. C++ exceptions are translated inside the backend adapter.

## Operations

| Family | Operations | Contract |
| --- | --- | --- |
| Runtime | `foundry_runtime_create`, `foundry_runtime_destroy`, `foundry_capabilities_json` | Capabilities JSON is borrowed static storage |
| Model | `foundry_model_load`, `foundry_model_unload` | Accept a registered ID or a complete local path |
| Session | `foundry_session_create`, `foundry_session_reset`, `foundry_session_destroy` | Reset clears KV state, sampler, end state, and cancellation |
| Low-level inference | `foundry_tokenize`, `foundry_prefill`, `foundry_decode` | Prefill before decoding; validate complete prompt+output budget yourself |
| Streaming | `foundry_generate`, `foundry_request_cancel` | Generate resets KV history; cancelled sessions need explicit reset before reuse |
| Chat | `foundry_chat_render` | Render structured system/user/assistant messages, then generate with `parse_special=true` |
| Inspection | `foundry_inspect`, `foundry_plan` | Inspection does not prove executability; plan reports uncertainty |
| Registry | `foundry_import_gguf`, `foundry_import_ollama`, `foundry_registry_list`, `foundry_resolve` | Native import; output JSON strings are allocated |
| Utilities | `foundry_sha256_file`, `foundry_free`, `foundry_last_error`, `foundry_status_string` | Hash streams file bytes; last session error is borrowed |

Free allocated text, token arrays, and JSON with `foundry_free`. `foundry_decode`
returns allocated byte fragments that can split UTF-8; `foundry_generate` buffers
fragments into valid UTF-8, replaces invalid/incomplete byte sequences with U+FFFD,
and suppresses matched stop strings across tokens. Callback bytes are borrowed,
not NUL terminated, and valid only until the callback returns. Returning false
cancels generation. A callback may be delayed to avoid exposing a partial stop
sequence or UTF-8 character.

Default configuration is context 2048, max output 128, batch 128, four CPU threads,
greedy temperature 0, top-p 0.95, seed 0, F16 KV, and CPU placement. `gpu_layers=-1`
chooses available GPU offload or CPU; a positive request errors when no GPU is
available. Explicit CPU placement disables GPU devices and operation offload.
A context above the model's training context is rejected. Full prompt plus output
budget must fit. No automatic truncation or context reduction occurs.

`memory_budget=0` leaves planning advisory; a nonzero byte limit is enforced
against the conservative estimate before model/context allocation. It is not an
OS memory reservation. Other applications and independent runtimes can consume
memory after a successful plan. Changing GPU placement requires reloading weights.
The session copies the stop string; other configuration fields are value types.

Raw generation adds tokenizer-required special tokens but parses special token
spellings only if explicitly requested. An empty string succeeds only if the
model's tokenizer produces at least one token; otherwise it is an invalid prompt.
The verified SmolLM2 chat renderer preserves its embedded default system message.
Ollama template bytes are retained as provenance, not executed as another language.
The canonical renderer allows token-like content as supplied; it is not a trust
boundary or prompt-injection filter.

## Errors and metrics

Every status-returning operation can receive a caller-owned `foundry_error`.
Use the returned status as authoritative; an optional error message supplies
context. Session errors are also available through `foundry_last_error` after
session operations. Never retain that borrowed pointer beyond session destruction.
The error enum distinguishes invalid configuration, missing files, corruption,
unsupported capability, memory budget failure, backend failure, busy handles,
remote-only models, I/O errors, context limits, cancellation, and EOG.

`FOUNDRY_END_OF_GENERATION` is a normal low-level decode result. High-level generate
maps EOS to `FOUNDRY_OK` and `metrics.stop_reason="eos"`. Other reasons include
`token_limit`, `stop_sequence`, `cancelled`, and `error`. Counters report actual
tokens, even when output is withheld by a stop filter. Timing clocks and their
limits are defined in [benchmarking.md](benchmarking.md).
