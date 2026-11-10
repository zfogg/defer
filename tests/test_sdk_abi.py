"""Exercise the stable C ABI from outside the SDK's compiler runtime."""
from __future__ import annotations

import ctypes
import os
import shutil
import subprocess
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


def test_c_abi_rejects_invalid_control_flow_without_output(sdk: ctypes.CDLL) -> None:
    source = b'#include "defer.h"\nvoid f(void) { defer(return); }\n'
    arguments = (ctypes.c_char_p * 1)(f"-I{ROOT / 'cpp' / 'include'}".encode())
    output, error, length = ctypes.c_void_p(), ctypes.c_void_p(), ctypes.c_size_t(17)
    assert sdk.defer_transform_source(b"example.c", source, len(source), arguments, 1,
                                      ctypes.byref(output), ctypes.byref(length), ctypes.byref(error)) != 0
    try:
        assert not output.value and length.value == 0 and error.value
        assert b"defer" in ctypes.string_at(error).lower()
    finally:
        sdk.defer_free_buffer(error)


def test_c_abi_rejects_missing_clang_argument(sdk: ctypes.CDLL) -> None:
    source = b"void f(void) {}\n"
    arguments = (ctypes.c_char_p * 1)(None)
    output, error, length = ctypes.c_void_p(), ctypes.c_void_p(), ctypes.c_size_t()
    assert sdk.defer_transform_source(b"example.c", source, len(source), arguments, 1,
                                      ctypes.byref(output), ctypes.byref(length), ctypes.byref(error)) != 0
    try:
        assert not output.value and error.value
        assert b"argument" in ctypes.string_at(error).lower()
    finally:
        sdk.defer_free_buffer(error)


def test_c_program_can_link_and_call_shared_sdk(tmp_path: Path) -> None:
    library = os.environ.get("DEFER_SDK_LIBRARY")
    compiler = shutil.which("clang")
    if not library or not compiler:
        pytest.skip("shared SDK and Clang are required")
    shared = Path(library).resolve()
    link_library = shared.with_suffix(".lib") if os.name == "nt" else shared
    source = tmp_path / "consumer.c"
    source.write_text(
        '#include "defer_api.h"\n#include <string.h>\n'
        'int main(int argc, char **argv) {\n'
        '  const char *input = "#include \\\"defer.h\\\"\\nvoid f(void) { defer((void)0); }\\n";\n'
        '  const char *flags[] = {argv[1]}; char *output = 0, *error = 0; size_t size = 0;\n'
        '  if (argc != 2 || defer_transform_source("api.c", input, strlen(input), flags, 1, &output, &size, &error)) return 1;\n'
        '  int failed = !output || !strstr(output, "__defer_active_");\n'
        '  defer_free_buffer(output); defer_free_buffer(error); return failed;\n}\n',
        encoding="utf-8",
    )
    executable = tmp_path / ("consumer.exe" if os.name == "nt" else "consumer")
    args = [compiler, str(source), f"-I{ROOT / 'cpp' / 'include'}", str(link_library), "-o", str(executable)]
    if os.name != "nt":
        args.append(f"-Wl,-rpath,{shared.parent}")
    subprocess.run(args, capture_output=True, text=True, check=True)
    environment = os.environ.copy()
    environment["PATH"] = f"{shared.parent}{os.pathsep}{environment.get('PATH', '')}"
    subprocess.run([executable, f"-I{ROOT / 'cpp' / 'include'}"], env=environment, check=True)
