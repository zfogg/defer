# defer

> Work in progress: the standalone extraction and release packaging are not complete. Do not use these builds as a drop-in replacement yet.

`defer` provides block-scoped cleanup for C and Objective-C, and a native C++ scope guard. The `defer-tool` source transformer uses LLVM 19 or newer and libclang. Cleanup is registered when execution reaches `defer(...)` and runs in reverse order on supported block exits. It does not run on `longjmp` or process termination. Unsupported cases produce an error.

## Use without CMake

Build the tool as described below, then transform and compile a source file:

```sh
defer-tool examples/hello_world/main.c build/hello.c -- -Icpp/include
clang build/hello.c -Icpp/include -o build/hello
./build/hello
```

Or transform a stream (the filename determines language and relative include resolution):

```sh
defer-tool --filename input.c -- -std=c17 -Icpp/include < input.c > output.c
```

Use `defer-tool --help` for batch manifests, compilation databases, diagnostics, and other options. The Python implementation can be invoked as `uv run python python/defer.py` and requires an LLVM libclang shared library.

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

For a release archive, CMake can download the appropriate host archive and generate the module during configuration. Set `DEFER_ARCHIVE_URL` to a versioned release ZIP and `DEFER_ARCHIVE_SHA256` to its published checksum:

```cmake
set(_defer_dir "${CMAKE_BINARY_DIR}/defer-release")
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

## Building and testing

Install CMake 3.25+, Ninja, Clang, LLVM development libraries and libclang 19 or newer. On Windows set `DEFER_LLVM_DEV_ROOT` to an LLVM development archive with `include`, `lib`, and `bin` directories.

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /your/install/prefix
```

`make build`, `make install`, `make format`, and `make format-check` provide shortcuts. The install includes the CLI, SDK libraries and headers, and the CMake module. A normal development build uses a shared libclang; release builds link it statically. Build the two-language documentation separately with `cmake -S docs -B build/docs` followed by `cmake --build build/docs --target docs`.
