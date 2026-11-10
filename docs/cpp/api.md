# Native API

The source transformer rejects control flow whose cleanup behavior cannot be
proven. The C++ API throws an exception; the C ABI returns a nonzero status
and an optional diagnostic, with buffers released by the SDK.

```{c:autodoc} defer_api.h
```

```{cpp:autodoc} transform.h
```

```{cpp:autodoc} api.h
```

```{cpp:autodoc} driver.h
```
