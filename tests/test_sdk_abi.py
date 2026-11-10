"""Exercise the stable C ABI from outside the SDK's compiler runtime."""
from __future__ import annotations

import ctypes
import os
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture
def sdk() -> ctypes.CDLL:
    library = os.environ.get("DEFER_SDK_LIBRARY")
    if not library:
        pytest.skip("set DEFER_SDK_LIBRARY to the shared SDK")
    dll_directory = None
    if os.name == "nt":
        dll_directory = os.add_dll_directory(str(Path(library).resolve().parent))
    lib = ctypes.CDLL(library)
    if dll_directory is not None:
        dll_directory.close()
    lib.defer_transform_source.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t,
                                           ctypes.POINTER(ctypes.c_char_p), ctypes.c_size_t,
                                           ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t),
                                           ctypes.POINTER(ctypes.c_void_p)]
    lib.defer_transform_source.restype = ctypes.c_int
    lib.defer_free_buffer.argtypes = [ctypes.c_void_p]
    lib.defer_free_buffer.restype = None
    return lib


def test_c_abi_success_and_error_ownership(sdk: ctypes.CDLL) -> None:
    source = b'#include "defer.h"\nvoid f(void) { defer((void)0); }\n'
    arguments = (ctypes.c_char_p * 1)(f"-I{ROOT / 'cpp' / 'include'}".encode())
    output, error, length = ctypes.c_void_p(), ctypes.c_void_p(), ctypes.c_size_t()
    assert sdk.defer_transform_source(b"example.c", source, len(source), arguments, 1,
                                      ctypes.byref(output), ctypes.byref(length), ctypes.byref(error)) == 0
    try:
        assert output.value and not error.value
        assert b"__defer_active_" in ctypes.string_at(output, length.value)
    finally:
        sdk.defer_free_buffer(output)
    output, error, length = ctypes.c_void_p(), ctypes.c_void_p(), ctypes.c_size_t()
    assert sdk.defer_transform_source(b"", source, len(source), arguments, 1,
                                      ctypes.byref(output), ctypes.byref(length), ctypes.byref(error)) != 0
    try:
        assert not output.value and length.value == 0 and error.value
        assert b"filename" in ctypes.string_at(error)
    finally:
        sdk.defer_free_buffer(error)
