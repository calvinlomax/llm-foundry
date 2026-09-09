# Third-party notices

Foundry's original source is licensed under the MIT license in [LICENSE](LICENSE).
No model weights, tokenizer datasets, or copied Ollama metadata are distributed.
Model licenses remain separate and are retained by the importer.

| Dependency | Version / revision | License | Distribution |
| --- | --- | --- | --- |
| llama.cpp and its bundled ggml | `f3f1a8f2760f28325a5ec20c05b171e5b7c83a29` | MIT; see upstream notices for bundled components | Pinned Git submodule; statically linked into Foundry's shared library |
| cJSON | 1.7.19 | MIT | Source and license in `vendor/cjson/` |
| GTK4 | Minimum 4.10; development validation uses 4.22.4 | LGPL-2.1-or-later | System development dependency; dynamically linked GUI |
| GLib, Pango, Cairo, and GTK platform dependencies | Supplied by the system's GTK distribution | Respective upstream licenses | System dependencies; retain their notices if bundling |
| Apple Accelerate / Metal | System SDK | Apple system framework terms | System frameworks on macOS |

The CMake install includes the llama.cpp and cJSON license texts. The GUI's
system dependencies are not copied into the development installation. A portable
binary bundle must include the applicable notices and satisfy each dependency's
redistribution requirements before publication. Upstream references:
[llama.cpp](https://github.com/ggml-org/llama.cpp),
[cJSON](https://github.com/DaveGamble/cJSON/tree/v1.7.19),
[GTK](https://www.gtk.org/).
