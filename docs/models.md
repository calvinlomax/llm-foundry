# Models, registry, and provenance

The registry lives at `FOUNDRY_HOME` when set, otherwise
`$HOME/.local/share/cinder-foundry`. It contains:

```text
registry/<name>.json       Versioned model record
artifacts/<sha256>.gguf    Weights copied only with --copy
```

Names allow 1–96 ASCII letters, digits, dots, underscores, and hyphens and cannot
start with a dot. A name already assigned to different bytes is rejected. Reimport
of the same bytes is repeatable. Writes use private temporary files, fsync, and
rename so readers do not observe partial JSON. A per-record file lock serializes
concurrent writers before identity checks and commit. Garbage collection is not
implemented in this preview.

A record has `schema_version: 1`, `name`, absolute `path`, weight `sha256`, `mode`,
`imported_at`, and `inspection`. Ollama imports also retain `provenance.manifest`,
`manifest_sha256`, and each metadata layer's original descriptor and text (hex
for embedded-NUL byte content). Licenses, system prompts, templates, stop parameters,
and unknown manifest fields are retained. No remote-only entry is silently
converted into a local model.

All referenced layers are size-checked and SHA-256 checked at import. Direct GGUF
imports are hashed as well. Reference mode leaves weights in their original path;
copy mode copies and rehashes the result before committing. References are not
immutable filesystem objects: if you modify a referenced file later, reimport
and compare its current `inspect` hash before trusting the recorded identity.
Loads structurally validate and ask the backend to check tensors, but do not
rehash every previously imported weight file. Do not modify Ollama cache blobs.

The reader supports little-endian GGUF v2/v3. It bounds metadata to 64 MiB,
individual strings to 16 MiB, metadata elements to one million, and tensors to
100,000. It validates shape arithmetic, alignment, data boundaries, and recognized
block sizes. Unknown tensor types remain visible with `type_name: "unknown"` and
`all_tensor_extents_validated: false`; execution must be validated by the backend.
The complete tensor inventory and metadata are retained in inspection JSON.
Tokenizer data can be embedded and does not require a separate tokenizer file.

Null/missing/empty layers and config-only entries return `remote_only_model`.
Invalid hashes, missing files, truncation, and partial downloads fail explicitly.
More than one distinct model layer and sharded GGUF are unsupported. Safetensors,
conversion, downloads, training, adapters, multimodal inputs, and remote providers
are outside this release. File size and a model tag do not establish compatibility.

The canonical chat formatter is an exact translation of the initial artifact's
embedded SmolLM2 template. It injects that template's default system message only
when the first message is not a system message. Foreign Ollama Go templates are
preserved without being executed. Other embedded chat templates return unsupported
until they receive their own golden tests. Special-token spellings in user content
are preserved; this formatting layer does not provide a security boundary between
trusted and untrusted conversation text.
