# defer

[GitHub](https://github.com/zfogg/defer) · [Documentation](https://zfogg.github.io/defer/) · [C++ and C API](https://zfogg.github.io/defer/cpp/) · [Python API](https://zfogg.github.io/defer/python/)

> Work in progress: the standalone extraction and release packaging are not complete. Do not use these builds as a drop-in replacement yet.

`defer` provides block-scoped cleanup for C and Objective-C, and a native C++ scope guard. The `defer-tool` source transformer uses LLVM 19 or newer and libclang. Cleanup is registered when execution reaches `defer(...)` and runs in reverse order on supported block exits. It does not run on `longjmp` or process termination. Unsupported cases produce an error.

## Build and install

Install CMake 3.25 or newer, Ninja, Clang, and LLVM development libraries and libclang 19 or newer. On Linux or macOS, configure and build from the repository root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /your/install/prefix
```

On Windows, install the Visual Studio C++ build tools and a compatible Clang toolchain, CMake, and Ninja. Use a developer PowerShell for your target architecture so the Windows SDK and linker are available. Download a `clang+llvm-*-x86_64-pc-windows-msvc.tar.xz` or `clang+llvm-*-aarch64-pc-windows-msvc.tar.xz` development archive from [LLVM releases](https://github.com/llvm/llvm-project/releases), as appropriate for your machine. Extract it and point `DEFER_LLVM_DEV_ROOT` at the extracted directory containing `include/`, `lib/`, and `bin/libclang.dll`:

```powershell
scoop install llvm cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++ `
  -DDEFER_LLVM_DEV_ROOT="C:/path/to/llvm-dev"
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "C:/path/to/defer-install"
```

The Scoop LLVM package provides the compiler; use the separate LLVM development archive for the headers and libraries required by this build. Match their LLVM versions and target architecture.

`make build`, `make install`, `make format`, and `make format-check` provide shortcuts where Make is available. The install includes the CLI, SDK libraries and headers, and the CMake module. A normal development build uses a shared libclang; release builds link it statically. Build the two-language documentation separately with `cmake -S docs -B build/docs` followed by `cmake --build build/docs --target docs`.

Build configuration options can be passed to CMake as `-DNAME=VALUE`:

| Option | Purpose |
| --- | --- |
| `DEFER_LLVM_DEV_ROOT` | LLVM development directory; required on Windows and for static LLVM builds. |
| `DEFER_HOST_CXX` | Host compiler used to locate `llvm-config` for a shared-library build on Unix. |
| `DEFER_STATIC_LIBCLANG` | Link LLVM and libclang statically; default `OFF`. |
| `DEFER_FULLY_STATIC_EXECUTABLE` | Link the CLI fully statically, for a musl build; default `OFF`. |
| `BUILD_TESTING` | Build the native tests; default `ON`. |
| `CMAKE_BUILD_TYPE` | Select the configuration, such as `Debug` or `Release`, with Ninja. |
| `CMAKE_CXX_COMPILER` | Select the C++ compiler. |
| `CMAKE_INSTALL_PREFIX` | Default installation directory; `cmake --install --prefix` overrides it. |

## Use without CMake

Build and install the tool as described above, add the installation's `bin` directory to your `PATH`, then transform and compile a source file:

```sh
defer-tool examples/hello_world/main.c build/hello.c -- -Icpp/include
clang build/hello.c -Icpp/include -o build/hello
./build/hello
```

Or transform a stream (the filename determines language and relative include resolution):

```sh
defer-tool --filename input.c -- -std=c17 -Icpp/include < input.c > output.c
```

Use `defer-tool --help` for batch manifests, compilation databases, diagnostics, and other options. The Python implementation can be invoked as `uv run --frozen python -m defer` and requires an LLVM libclang shared library.

### Command-line flags

The native tool accepts the following flags. Pass compiler options after `--`; alternatively, provide `--compilation-database` to recover them for each input. The Python implementation supports the shared transformation flags below, with differences noted after the table.

| Flag | Purpose |
| --- | --- |
| `--help`, `--version` | Show help or version information. |
| `--filename PATH` | Set the virtual source filename for redirected standard input; required in that mode. |
| `--compilation-database PATH` | Read compiler arguments from a database directory or `compile_commands.json`. |
| `--batch MANIFEST` | Transform source/output pairs from a manifest, optionally with per-file JSON argument arrays. |
| `--depfile PATH` | Write header dependencies. |
| `--dependency-stamps` | Write per-output dependency stamps. |
| `--incremental` | Reuse unchanged batch transformations; requires `--batch` and `--dependency-stamps`. |
| `--shared-cache DIR` | Share cached transformations between builds; requires `--incremental`. |
| `--jobs N` | Limit concurrent transformations; defaults to available CPU cores. |
| `--preamble` | Experimentally reuse Clang preambles for validation. |
| `--header-overlay PATH` | Write a Clang header overlay for one source. |
| `--header-overlays` | Write per-source Clang header overlays in batch mode. |
| `--libclang PATH` | Check the loaded libclang library against the specified path. |
| `--explain SOURCE:LINE` | Explain the cleanup at a source location. |
| `--show-transformed SOURCE:LINE` | Show the transformed function at a source location. |
| `--scan` | List source files that may contain defer calls. |
| `--scan-input PATH` | With `--scan`, read source paths from a UTF-8 file. |
| `--plan-records PATH` | Plan source/output records for generated CMake build rules. |
| `--plan-stage STAGE` | Set the stage for batch planning. |
| `--plan-output PATH` | Write the generated CMake build rules. |
| `--build-dir DIR` | Set the build directory for batch planning. |
| `cmake-config --write-file PATH` | Write the embedded CMake module to a file; omit `--write-file` to print it. |

The native executable also exposes LLVM's general command-line options in `--help`. The Python implementation accepts `--dependency-stamp PATH` for a single output and uses `--libclang PATH` to select the shared library. Its batch mode supports `--batch`, `--libclang`, `--compilation-database`, `--depfile`, `--dependency-stamps`, and `--header-overlays`; it does not implement the native tool's scanning, planning, concurrency, preamble, or incremental cache flags.

## Use with CMake

Obtain `Defer.cmake` from the release binary, including when shell redirection is unavailable:

```sh
defer-tool cmake-config --write-file Defer.cmake
```

Set `CMAKE_EXPORT_COMPILE_COMMANDS` to `ON`, point `DEFER_NATIVE_BINARY` at the host tool, include the generated module, and call `defer_targets` after creating the targets:

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(DEFER_NATIVE_BINARY "/path/to/defer-tool")
include("${CMAKE_CURRENT_SOURCE_DIR}/Defer.cmake")
add_executable(app main.c)
target_include_directories(app PRIVATE "/path/to/defer/include")
defer_targets(app)
```

The module uses batch transformation and the compilation database, supports incremental rebuilds and header overlays, and requires single-config Ninja or Unix Makefiles. Include `defer.h` in source files. See `examples/hello_world_cmake` for an example.

The module also accepts these CMake variables:

| Variable | Purpose |
| --- | --- |
| `DEFER_SOURCE_ROOT` | Root of sources to transform; defaults to `CMAKE_SOURCE_DIR`. |
| `DEFER_SOURCE_DIRS` | Semicolon-separated directories relative to the source root; defaults to `.`. |
| `DEFER_BACKEND` | `NATIVE` (default) or `PYTHON`. |
| `DEFER_NATIVE_BINARY` | Path to the host executable; otherwise it is found on `PATH`. |
| `DEFER_HEADER_OVERLAYS` | `AUTO` (default), `ON`, or `OFF`. |
| `DEFER_SHARED_CACHE` | Directory for the native shared transformation cache. |
| `DEFER_WRAPPER_HEADER_DIRS` | Additional directories containing defer wrapper macros. |
| `DEFER_EXTRA_INCLUDE_DIRS` | Additional include directories supplied to the transformer. |
| `DEFER_TOOL_ROOT` | Standalone source checkout used by the Python backend. |
| `DEFER_PYTHON_COMMAND` | Python command used by the Python backend. |
| `DEFER_LIBCLANG_PATH` | Shared libclang library used by the Python backend; defaults to `LIBCLANG_LIBRARY_FILE` if set. |

For a release archive, CMake can download the appropriate host archive and generate the module during configuration. Set `DEFER_ARCHIVE_URL` to a versioned release ZIP and `DEFER_ARCHIVE_SHA256` to its published checksum:

```cmake
set(_defer_dir "${CMAKE_BINARY_DIR}/defer-release/${DEFER_ARCHIVE_SHA256}")
file(MAKE_DIRECTORY "${_defer_dir}")
if(NOT EXISTS "${_defer_dir}/defer.zip")
  file(DOWNLOAD "${DEFER_ARCHIVE_URL}" "${_defer_dir}/defer.zip"
       EXPECTED_HASH "SHA256=${DEFER_ARCHIVE_SHA256}" TLS_VERIFY ON)
else()
  file(SHA256 "${_defer_dir}/defer.zip" _defer_archive_hash)
  if(NOT _defer_archive_hash STREQUAL DEFER_ARCHIVE_SHA256)
    message(FATAL_ERROR "Cached defer archive does not match DEFER_ARCHIVE_SHA256")
  endif()
endif()
if(CMAKE_HOST_WIN32)
  set(DEFER_NATIVE_BINARY "${_defer_dir}/bin/defer-tool.exe")
else()
  set(DEFER_NATIVE_BINARY "${_defer_dir}/bin/defer-tool")
endif()
if(NOT EXISTS "${DEFER_NATIVE_BINARY}")
  file(ARCHIVE_EXTRACT INPUT "${_defer_dir}/defer.zip" DESTINATION "${_defer_dir}")
endif()
execute_process(COMMAND "${DEFER_NATIVE_BINARY}" cmake-config --write-file "${_defer_dir}/Defer.cmake"
                COMMAND_ERROR_IS_FATAL ANY)
include("${_defer_dir}/Defer.cmake")
# After add_executable(app ...):
target_include_directories(app PRIVATE "${_defer_dir}/include")
defer_targets(app)
```

Choose the ZIP for the **build host**, including for cross compilation. Set `CMAKE_EXPORT_COMPILE_COMMANDS=ON` before configuring targets. The module's `DEFER_SOURCE_DIRS` and `DEFER_WRAPPER_HEADER_DIRS` options control which project and external headers it scans. The [documentation site](https://zfogg.github.io/defer/) describes scope, restrictions, and the SDK.

Header discovery skips nested virtual environments, `node_modules`, VCS metadata, and the current build directory. If a needed wrapper is inside one of those trees, list its directory explicitly in `DEFER_WRAPPER_HEADER_DIRS`.

## C++ SDK

Include `api.h` and link the static or shared SDK library to transform an in-memory source buffer:

```cpp
#include <api.h>

auto generated = defer_tool::transform_source("src/example.c", source, {"-std=c17", "-Iinclude"});
```

The virtual filename determines language detection, diagnostics, and relative include lookup. Transformation failures throw an exception; the API does not overwrite the source file. Release archives include the SDK libraries, public headers, and the Clang C headers used by the lower-level API.

For a C ABI suitable for shared-library boundaries, include the same `api.h` and call `defer_transform_source`. Release every returned output or error with `defer_free_buffer`, which frees it in the library that allocated it. The C++ API exposes a C++ ABI, so use a compatible compiler, standard library, and architecture when linking it. The Windows release libraries use the static MSVC runtime (`MultiThreaded` in CMake). The bundled static SDK archive contains its LLVM and libclang implementation; platform system libraries such as threads and `dl` may still need to be linked by the consumer.
