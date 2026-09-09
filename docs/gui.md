# Desktop guide

Build the `release` preset and launch `./build/release/foundry-gui`, or use the
installed macOS development bundle described in [installation.md](installation.md).
The GUI directly links to the C API and requires no terminal inference process,
Python runtime, server, WebView, or browser.

## First run

1. Expand **Import model** and set an import name, such as `smollm2`.
2. For a direct file, choose **Import GGUF** and select the complete local model.
3. For the copied Ollama assets, enter the weights directory and metadata directory
   as absolute paths, then choose **Import Ollama manifest** and select the manifest.
4. Choose **Inspect** and **Memory plan** to review the artifact and estimate.
5. Choose **Load**. Wait for Ready before sending a prompt.
6. Enter text in the multiline prompt box and choose **Send**, or press Return. Shift+Return inserts a new line (Ctrl+Return also sends).

Imports use reference mode. The CLI also exposes copy mode. **Refresh library**
updates the model picker. You can enter a local file path directly in the model
field. **Unload** releases weights and retains the conversation for Copy or Export until
the next Load or New Chat. Import and inspection run on the worker thread,
so hashing large model files does not block GTK event processing.

## Conversation and settings

Stop or Escape requests cancellation and displays Cancelling until the backend
acknowledges it. Partial responses remain visible and in the structured history.
New Chat clears history/KV state while retaining weights. Copy uses the system
clipboard. Export explicitly saves role/content JSON; chats are never saved
automatically. User and assistant messages appear in separate, selectable blocks.
Assistant responses stream as text and render a basic Markdown subset when complete
or stopped: headings, bullets, bold, emphasis, inline code, and fenced code blocks.
Raw HTML is displayed literally; tables, images, and syntax highlighting are not supported.

The window opens maximized, with dark mode enabled by default. Use the header
switch for light mode. Drag the sidebar divider to resize the layout. Import controls
collapse separately from generation settings. Inspect and Memory plan fill the
sidebar's model information panel. New Chat, Copy, and Export sit in a secondary
toolbar; the composer grows with wrapped or multiline text up to 200 pixels, then
scrolls. Shortcut hints appear in tooltips. Stop appears only during generation;
a spinner indicates model loading, import, inspection, or generation activity.

Context, CPU threads, and GPU placement are locked while a model is loaded.
Unload to edit them, then Load to apply them. The model picker is also locked while
loaded, so the selected model always matches the model receiving your messages.
GPU settings are disabled when no GPU backend is available. Top-p and seed are
disabled for greedy sampling (temperature zero).
Maximum output, temperature, top-p, and seed apply to the next request. Settings
are validated by the same API as CLI requests; a context overflow is an error and
does not silently truncate the conversation. Generation settings and the selected
model, import paths/name, sidebar width, expanded sections, and appearance are persisted under the platform's GLib configuration directory in
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

The compact status bar shows throughput, last-request token usage, loaded model,
and peak application memory (the process high-water mark, including more than model
weights). The context meter below generation settings uses actual input plus output
tokens from the last request; it does not estimate an unsent draft. Expand Diagnostics
for load, first-content, prefill, and decode timings and a bounded activity log.
The memory plan remains an estimate and reserve, not an allocation guarantee.
Model information is rendered as a summary with readable parameter counts, binary
file sizes, context length, and vocabulary size. Architecture details and file
provenance (including the selectable full path and SHA-256) are collapsible.
Memory plan adds a separate breakdown beside the retained model summary; its bar
compares estimated memory with total system RAM, not available memory. Missing
fields display a quiet dash. Full tensor/tokenizer inventories and original
license/provenance remain available through CLI inspection and the registry JSON.

The tuning comparison panel, richer resource observations, automatic profile
selection, macOS `.icns` icon, and portable GTK bundling remain tracked work.
Automated tests cover the worker's loading, streaming, cancellation, reset, and
close lifecycle, plus Markdown rendering/escaping. Run `./build/release/foundry-test-ui`
in a desktop session for widget state, Return handling, composer resizing, and
preference round-trip checks. This test uses a temporary registry and configuration. The `--smoke-test` development flag opens the window briefly and
requests a clean shutdown. Manual keyboard/accessibility and visual review remain
separate release checks; a successful compile alone does not establish them.

## Launcher and full diagnostics

Double-click `cinder.command` at the repository root to open the release GUI. It
runs no tests, benchmarks, imports, or dependency installation. If the executable
has not been built yet, it configures and builds the release preset once.

After unloading the model, choose **Run diagnostics** to the right of the
Diagnostics expander. A separate window streams stdout and stderr, shows a spinner,
and reports pass, failure, or cancellation. **Cancel** or closing the popup stops
the command and its child processes. Closing the application also cancels the run.
The main window's runtime controls are disabled while the checks run.

The button invokes the executable `cinder-diagnostics.command` at the repository
root; you can also double-click that file to run it in Terminal. It contains the
old launch pipeline: dependency setup, asset verification, build, CTest, import,
inspection, memory planning, tokenization, raw/chat inference, environment capture,
benchmarking, and the 300-second tuning search. It does not open another GUI.
Builds use `build/diagnostics`, imports use `models/diagnostics-registry`, and logs,
inspection, environment, and benchmark results go to a timestamped directory under
`benchmarks/runs`. Tuning profiles remain under `profiles/local`. The popup retains
the latest 2,000 lines; the complete output is saved in `diagnostics.log`.

Run `./build/release/foundry-test-diagnostics` in a desktop session to check output
streaming, UTF-8 boundaries, failure reporting, repeat runs, bounded log rendering,
and cancellation of child processes using short isolated fixture commands.
