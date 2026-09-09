# GitHub publication and release checklist

The source tree is prepared for an initial **development preview** repository.
No remote repository, push, release, or public package has been created by this
implementation task. Choose the actual GitHub owner/repository and confirm the
provisional project identity before publication.

## Publish the source preview

- Review README, compatibility, validation evidence, license, third-party notices,
  security reporting, and the development-preview label.
- Inspect `git status --short` and staged files. Never add weights, copied metadata,
  build trees, machine-local paths in benchmark logs, or private prompts.
- Include the pinned submodule gitlink and `.gitmodules`; verify a recursive clone.
- Create the repository under the intended GitHub account and set its remote.
  Push only after reviewing the concrete staged/committed content.
- Enable Actions and private vulnerability reporting. Replace the provisional
  community/security contact guidance when maintainers publish a contact channel.
- Run the macOS and Linux CI workflows and link actual run evidence. Do not add
  passing badges before a workflow has actually passed in the public repository.

The normal source `.zip` from GitHub does not include initialized submodules.
Document recursive clone instructions prominently. Keep release evidence small
and redistributable; private local measurements belong outside Git.

## Gate a binary release

- [ ] Freeze the project/app identity and version; update the header, CMake, and changelog.
- [ ] Verify the exact backend commit and retain all dependency notices.
- [ ] Pass the synthetic, sanitizer, real-model, template/token, lifecycle, and GUI suites.
- [ ] Exercise long conversations, Unicode, Stop, New Chat, settings changes, and close races manually.
- [ ] Audit keyboard navigation, accessibility, scrolling, first-run import, and error recovery.
- [ ] Verify GPU placement/performance separately on each advertised GPU platform.
- [ ] Publish only tested architecture/template/platform matrix rows.
- [ ] Archive repeated, aligned benchmark evidence with clock definitions and no speed claim beyond the evidence.
- [ ] Package GTK libraries, loaders, schemas, fonts/icons, resources, and licenses when promising portability.
- [ ] Complete the macOS application icon, library paths, signing/notarization, and current distribution requirements.
- [ ] Install on a separate machine without development/Homebrew prefixes and run the C sample.
- [ ] Import permitted local weights, disconnect networking, and run CLI plus GUI inference.
- [ ] Confirm runtime libraries resolve outside the source/build tree.
- [ ] Produce versioned archives and SHA-256 checksums; inspect archive contents before upload.

Current CPack archives and the `.app` are **development installations** with system
GTK dependencies. They are not a substitute for the separate-machine portability
gate. Do not tag a production release while representing unchecked items as done.
