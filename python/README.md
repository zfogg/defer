# Python defer tool

[GitHub](https://github.com/zfogg/defer) · [Documentation](https://zfogg.github.io/defer/) · [Python API](https://zfogg.github.io/defer/python/) · [C++ and C API](https://zfogg.github.io/defer/cpp/)

The Python implementation transforms C and Objective-C cleanup statements using
libclang. It supports the same single-file and batch interfaces as the native
`defer-tool`, and is useful for development, inspection, and comparing the two
implementations. C++ sources use native scope guards.

## Run the tool

From the repository root, use the locked uv project and set
`LIBCLANG_LIBRARY_FILE` if your LLVM 19 or newer shared library is not found
by the platform loader:

```sh
uv sync --frozen
uv run --frozen python python/defer.py input.c output.c -- -std=c17 -Iinclude
```

For a stream, provide a virtual filename for diagnostics and relative includes:

```sh
uv run --frozen python python/defer.py --filename input.c -- -std=c17 -Iinclude < input.c > output.c
```

Use `--batch manifest.txt` with per-file arguments or a compilation database to
transform multiple files in one process. Run `python/defer.py --help` for the
complete interface. The project-level README describes CMake integration and
installation.

## Development

Run the Python tests with `uv run --frozen pytest tests`, and check changes with
`uv run --frozen ruff check python tests` and `uv run --frozen ty check python`.
Set `DEFER_TEST_BINARY` to the native executable to include cross-implementation
checks, and `LIBCLANG_LIBRARY_FILE` to its matching libclang shared library.
