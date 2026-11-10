# Defer semantics

Include `defer.h` and put `defer(action);` directly in a compound block. The
cleanup is registered when execution reaches that statement and runs when that
block exits. Multiple actions run in reverse order of registration, with inner
blocks cleaned first. Actions evaluate their expressions at exit, using the
current values of referenced variables.

```c
FILE *file = fopen(path, "r");
if (!file) return 1;
defer(fclose(file));
```

Cleanup runs on fallthrough, `return`, `break`, `continue`, and supported
goto exits. A return expression is evaluated and converted before cleanup.
`longjmp`, process termination, and other nonlocal termination do not run
cleanup. The transformer rejects control flow it cannot safely analyze, and
an untransformed C use fails compilation.

Use `defer_named(handle, action)` and `defer_cancel(handle)` when transferring
ownership. A handle is a lexical name, not a C variable: it cannot be copied,
stored, or passed to a function. Canceling it more than once is harmless.
Re-entering a loop body registers new actions.

C and Objective-C source files require transformation; C++11 and later use
native scope guards, including in headers. C++ cleanup also runs during
exception unwinding and must not throw. Objective-C uses `@try`/`@finally`
and requires a zero-cost exception runtime and `-fobjc-exceptions` for both
transformation and compilation. Included C header functions use per-source
Clang VFS overlays. C source files included by another source are unsupported.

Every defer must be a direct statement within braces. Add braces to conditional
and loop bodies and place switch-case cleanup inside a braced block. Inline
assembly in a function with defer, computed goto, jumping into a protected
block, and backward jumps that may register the same cleanup repeatedly are
rejected. Cleanup may not contain another defer, return, goto, break, continue,
or `__COUNTER__`. Wrapping macros are supported where their expansion is
unambiguous; token pasting, stringification, and other opaque macro forms are
rejected.

The CMake integration uses the compilation database and transforms files in
parallel batches. It keeps generated sources in the build directory, retains
relative include lookup from the original source directory, and tracks header
dependencies. Ninja checks source eligibility on each build so adding the first
defer call to an existing file causes regeneration automatically. Makefiles
use conservative reconfiguration. `DEFER_HEADER_OVERLAYS=AUTO` detects header
cleanup; `ON` forces the per-source overlay path. Add external wrapper roots to
`DEFER_WRAPPER_HEADER_DIRS`, and restrict scanned source roots with
`DEFER_SOURCE_DIRS` when appropriate.

The CLI supports a single input/output pair, redirected input with a required
`--filename`, and `--batch` manifests. Batch rows are tab-separated input and
output paths, optionally followed by a JSON array of per-file Clang arguments;
alternatively supply `--compilation-database` for all rows. Use `--explain
source.c:line` to inspect cleanup decisions and `--show-transformed
source.c:line` to inspect generated code. `--jobs` controls batch concurrency.
Run `defer-tool --help` for the complete interface.
