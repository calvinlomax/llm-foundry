# Security policy

This is a development preview and has no security support SLA. Use trusted local
model files. The bounded native parser rejects many malformed containers; it is
not a substitute for a security audit of the importer or compute dependencies.
Foundry does not provide a sandbox for hostile model files or model-generated text.

The native product does not open a listener, download models, or invoke remote
providers. Generated text is displayed as plain text and is never executed.
Conversation export and clipboard copy are explicit user actions. Registry JSON
contains local paths, original model metadata, templates, and licenses. Optional
benchmark records can contain prompt text and file paths; review them before sharing.

Report ordinary defects through the bug template using a minimal synthetic
reproduction. For a sensitive vulnerability, use GitHub's private vulnerability
reporting on the published repository when enabled. If it is not enabled, contact
the repository owner through a private channel before sending exploit details;
do not assume an email address or security response promise that is not published.
Maintainers should enable private vulnerability reporting before public launch.

Never attach private model weights, credentials, user conversations, or personal
metadata to an issue. Dependency advisories and backend updates require review and
revalidation under the pinned-revision policy.
