# Native C++ tool and SDK

[GitHub](https://github.com/zfogg/defer) · [Documentation](https://zfogg.github.io/defer/) · [C++ and C API](https://zfogg.github.io/defer/cpp/) · [Python API](https://zfogg.github.io/defer/python/)

The native `defer-tool` transforms C and Objective-C sources using libclang.
C++11 and later use a native scope guard. Cleanup is registered at the point of
a `defer()` statement, runs in reverse order when its block exits, and fails
closed when the transformer cannot prove that a control-flow path is safe.

## Transform and compile

```sh
defer-tool input.c output.c -- -std=c17 -I/path/to/defer/include
clang output.c -I/path/to/defer/include -o program
```

For redirected input, `--filename` specifies the virtual source path:

```sh
defer-tool --filename input.c -- -std=c17 -Iinclude < input.c > output.c
```

`defer-tool cmake-config --write-file Defer.cmake` emits the self-contained
CMake integration. Set `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, include the module,
and call `defer_targets(...)` after creating your targets. The project-level
README includes download and configuration examples.

## SDK

Include `api.h` for both the C ABI and the C++ source-buffer API.
The C ABI returns an error code and uses `defer_free_buffer` to release output
and diagnostics in the library that allocated them. The C++ API throws on
failure. Static and shared SDK libraries expose both interfaces.

The pages below document scope, supported control flow, and the API.
