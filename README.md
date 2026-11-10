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
defer_targets(app)
```

The module uses batch transformation and the compilation database, supports incremental rebuilds and header overlays, and requires single-config Ninja or Unix Makefiles. Include `defer.h` in source files. See `examples/hello_world_cmake` for an example.

## Building and testing

Install CMake 3.25+, Ninja, Clang, LLVM development libraries and libclang 19 or newer. On Windows set `DEFER_LLVM_DEV_ROOT` to an LLVM development archive with `include`, `lib`, and `bin` directories.

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /your/install/prefix
```

`make build`, `make install`, `make format`, and `make format-check` provide shortcuts. The install includes the CLI, SDK libraries and headers, and the CMake module. The native tool currently requires libclang at runtime.
