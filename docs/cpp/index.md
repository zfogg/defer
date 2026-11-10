# defer C++ SDK

The SDK exposes the transformation engine used by `defer-tool`. C++ consumers
include the headers in `cpp/include` and link the static or shared SDK library.
The C marker header `defer.h` defines the `defer`, `defer_named`, and
`defer_cancel` macros. See the [README](https://github.com/zfogg/defer#readme)
for transformation and build instructions.

```{toctree}
:maxdepth: 2

semantics
api
```
