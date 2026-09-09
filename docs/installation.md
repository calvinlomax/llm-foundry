# Installation and builds

The tested development environment is recorded in [environment.md](environment.md).
Only the tested combinations in [compatibility.md](compatibility.md) are claimed.

## Source checkout

Clone this repository using its actual GitHub URL and `--recurse-submodules`, or
run `git submodule update --init --recursive` inside an existing clone. The
backend gitlink and `docs/backend-revision.txt` must agree. CMake never fetches or
updates dependencies during configuration. A backend revision mismatch is a
hard error. cJSON 1.7.19 source and its license are committed directly.

An offline build requires the repository, initialized backend checkout, compilers,
CMake, and GTK development dependencies to be available before disconnecting.
Inference, import, inspection, and desktop chat make no network requests.

## Configure

| Preset | Result |
| --- | --- |
| `release` | Release CPU library, CLI, tests, GTK app |
| `cpu` | Release CPU library, CLI, tests; GUI deliberately omitted |
| `metal` | Release library, CLI, GTK app with requested Metal support |
| `sanitize` | CPU Debug build; Foundry's code uses AddressSanitizer and UndefinedBehaviorSanitizer |

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Presets require CMake 3.25+. With CMake 3.24, use ordinary `-S . -B build/release`
configuration instead. On Linux the documented desktop dependency target is
Ubuntu 24.04 or a distribution supplying GTK 4.10+. GUI builds intentionally fail
when GTK is absent; choose the CPU preset for an explicitly headless build.
The GUI uses dynamically linked system GTK. No Python, JavaScript, HTTP service,
or WebView is needed for installed native inference. Python is used for tests
and optional development measurements; set `-DBUILD_TESTING=OFF` to omit tests.

## Install and use the SDK

```sh
cmake --install build/release --prefix "$PWD/build/install"
./build/install/bin/foundry version
./build/install/bin/foundry-example '/path/to/model.gguf'
```

The install contains `bin/foundry`, `bin/foundry-gui`, `lib/libfoundry_core`, the
public header, a CMake package, licenses, and the C sample. llama.cpp and cJSON are
linked into the shared Foundry library. The library still depends on the system
C++ runtime and any selected platform frameworks. Executable install RPATHs point
to the adjacent library directory. Inspect actual dependencies with `otool -L`
on macOS or `ldd` on Linux.

A consumer CMake project can use:

```cmake
cmake_minimum_required(VERSION 3.24)
project(my_foundry_app LANGUAGES C)
find_package(Foundry 0.1 CONFIG REQUIRED)
add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE Foundry::foundry)
```

Set `CMAKE_PREFIX_PATH` to the installation prefix. `main.c` can start from
[`examples/basic.c`](../examples/basic.c). No upstream C++ types appear in the
public header.

## macOS desktop bundle

The GUI build also creates `build/release/foundry-gui.app`. Installation places
that bundle at the install prefix alongside `lib/`; keep that layout intact.
The separate `bin/foundry-gui` entry remains useful for development diagnostics.
The bundle has an application identifier and Info.plist, but still relies on the
installed GTK distribution. It is **a development bundle**, not a portable,
signed or notarized release. Moving only the `.app` to another machine is not a
supported installation method in this preview. Portable GTK resources, a macOS
`.icns` icon, signing, notarization, and clean-machine verification remain release
gates. The source provides a scalable icon and Linux desktop launcher.

`cpack --config build/release/CPackConfig.cmake -B dist` creates a development
installation archive. Do not describe that archive as portable before finishing
[the release checklist](releasing.md).

## Real-model and sanitizer checks

```sh
cmake --preset release -DFOUNDRY_TEST_MODEL='/absolute/path/model.gguf'
cmake --build --preset release
ctest --preset release
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

The real-model suite is optional so CI does not need private weights. Its chat
checks currently target the exact SmolLM2 template in the compatibility matrix.
The sanitizer preset instruments Foundry's code; it does not claim complete
sanitizer coverage of all third-party compute code or GPU kernels.
