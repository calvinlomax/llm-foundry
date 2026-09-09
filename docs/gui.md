# Desktop guide

Build the `release` preset and launch `./build/release/foundry-gui`, or use the
installed macOS development bundle described in [installation.md](installation.md).
The GUI directly links to the C API and requires no terminal inference process,
Python runtime, server, WebView, or browser.

## First run

1. Set an import name, such as `smollm2`.
2. For a direct file, choose **Import GGUF** and select the complete local model.
3. For the copied Ollama assets, enter the weights directory and metadata directory
   as absolute paths, then choose **Import Ollama manifest** and select the manifest.
4. Choose **Inspect** and **Memory plan** to review the artifact and estimate.
5. Choose **Load**. Wait for Ready before sending a prompt.
6. Enter text in the multiline prompt box and choose **Send**, or press Ctrl+Enter.

Imports use reference mode. The CLI also exposes copy mode. **Refresh library**
updates the model picker. You can enter a local file path directly in the model
field. **Unload** releases weights. Import and inspection run on the worker thread,
so hashing large model files does not block GTK event processing.

## Conversation and settings

Stop or Escape requests cancellation and displays Cancelling until the backend
acknowledges it. Partial responses remain visible and in the structured history.
New Chat clears history/KV state while retaining weights. Copy uses the system
clipboard. Export explicitly saves role/content JSON; chats are never saved
automatically. The display uses plain text, and transcripts remain selectable.

Context, CPU threads, and GPU placement require unloading/reloading in the GUI.
Maximum output, temperature, top-p, and seed apply to the next request. Settings
are validated by the same API as CLI requests; a context overflow is an error and
does not silently truncate the conversation. Generation settings and the selected
model are persisted under the platform's GLib configuration directory in
`cinder-foundry/preferences.ini`; raw conversation content is excluded.

The initial formatter supports the verified SmolLM2 template. Its default system
message is preserved; a custom system-message editor is not yet exposed in the
GUI. The CLI and C API support explicit system messages.

## Runtime behavior and limits

GTK widget access stays on the main thread. One dedicated worker owns all runtime
handles. It copies callback bytes into a bounded 128-event queue, and the UI drains
queued text about every 40 ms. IDs reject stale events. Closing requests cancellation
and waits without blocking GTK processing for the worker to release resources.
Loading cannot be interrupted through the current public API, so close can wait
for model loading or file hashing. There is one active job per window.

Timing displays label load, first emitted content, prefill, decode, token counts,
and throughput. Observed memory is explicitly unavailable. The plan includes an
estimate and reserve, not an allocation guarantee. Metadata is shown in a compact
view; full tensor/tokenizer inventories and original license/provenance are
available through CLI inspection and the registry JSON.

The tuning comparison panel, richer resource observations, automatic profile
selection, macOS `.icns` icon, and portable GTK bundling remain tracked work.
Automated tests cover the worker's loading, streaming, cancellation, reset, and
close lifecycle. The `--smoke-test` development flag opens the window briefly and
requests a clean shutdown. Manual keyboard/accessibility and visual review remain
separate release checks; a successful compile alone does not establish them.
