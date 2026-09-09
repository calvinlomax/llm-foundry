# Compatibility matrix — schema 1

Compatibility is a combination of artifact, architecture, tensor representation,
tokenizer, template, backend revision, and hardware. GGUF inspection alone does
not establish executability. Backend-reported capability is separate from a
Foundry-tested combination.

| Artifact | Representation | Backend / platform | Evidence | Status |
| --- | --- | --- | --- | --- |
| Local SmolLM2, `4d2396b16114669389d7555c15a1592aad584750310f648edad5ca8c4eccda17` | GGUF v3, llama architecture, Q8_0 and F32, 1,711,376,384 parameters | llama.cpp `f3f1a8f2760f28325a5ec20c05b171e5b7c83a29`; macOS 26.6.2 ARM64 CPU | Direct/Foundry raw output parity; API integration; GTK worker generation/cancellation | Locally tested development combination |
| Synthetic single-tensor GGUF fixtures | F32 structural fixtures; no executable architecture weights | macOS CPU test suite; CI configuration for macOS/Linux | Import, hash, path, copy, malformed input, and planner regressions | Inspection/import only |
| Other single-file decoder-only GGUF models | Backend-dependent | Pinned llama.cpp | Load-time backend checks only | Backend-reported, unverified |
| Metal execution | Backend-dependent | Optional macOS build | No published runtime evidence in this preview | Unverified |
| Linux CPU / GTK | Source build configured in CI | Ubuntu 24.04 workflow | Workflow supplied; local Linux run not performed here | Pending CI evidence |

The SmolLM2 inspection reports 24 layers, embedding width 2048, 32 attention heads,
32 KV heads, trained context 8192, and vocabulary size 49,152. Weight bytes:
1,820,414,656. The name embedded in the container is `Smollm2 1.7B 8k Mix7 Ep2 v2`.
All copied manifest/config/system/template/license/parameter layer hashes were
verified; model weights are not included in this repository.

Chat formatting is restricted to the exact verified SmolLM2 embedded template.
The parser's conventional memory-estimator allowlist is not a model-support claim.
There is no broad architecture-family claim until at least two distinct families
pass the versioned test matrix.

Unsupported: sharded GGUF, safetensors conversion, encoder/embedding-only execution,
multimodal input, adapters, remote providers, training, distributed execution,
a local server, and an experimental custom C compute engine. Unknown chat templates
fail explicitly; raw completion may remain available if the backend can load them.
