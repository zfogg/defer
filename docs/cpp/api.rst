Native API
==========

The source transformer rejects control flow whose cleanup behavior cannot be
proven. The C++ API throws an exception; the C ABI returns a nonzero status
and an optional diagnostic, with buffers released by the SDK.

C API
-----

.. c:autodoc:: defer_api.h

C++ API
-------

.. cpp:autodoc:: api.h

.. cpp:autodoc:: transform.h

.. cpp:autodoc:: driver.h
