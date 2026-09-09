# Initial development environment

Observed locally on 2026-09-09. This is an implementation/test environment, not a
promise of portability to every machine with similar specifications.

| Field | Observation |
| --- | --- |
| OS | macOS 26.6.2, build 25G83 |
| Architecture | native ARM64 / Darwin |
| Physical RAM | 8,589,934,592 bytes (8 GiB) |
| Logical CPUs | 8 |
| Compiler | Apple clang 21.0.0, clang-2100.1.1.101 |
| CMake | 4.4.3 |
| Ninja | 1.13.2 |
| Python | 3.14.5 |
| GTK | 4.22.4 |
| pkg-config provider | pkgconf 3.0.6 |
| clang-format | 23.1.0 |
| Ollama executable | 0.33.3 |
| Backend pin | `f3f1a8f2760f28325a5ec20c05b171e5b7c83a29` |
| Initially available disk space | approximately 326 GiB |
| Initial power observation | Battery, 100%, discharging; later benchmark conditions must be recorded separately |

Development dependencies were installed with Homebrew. CPU execution was validated;
Metal execution was not measured. GUI window startup required access to the normal
macOS desktop session: the filesystem/process sandbox could compile the program
but could not create the AppKit window. The installed GUI smoke test passed outside
that display restriction.

`scripts/environment.py` records a new JSON inventory. A denied/unsupported system
query is retained as unavailable rather than inferred. Keep full machine-local
logs under `benchmarks/runs/` and review them before sharing. CPU-intensive builds
were running during early smoke generations; those timings are not a controlled
performance baseline.
