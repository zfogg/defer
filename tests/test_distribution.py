"""Distribution-facing commands must agree between implementations."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(params=["python", "python-module", "native"])
def command(request: pytest.FixtureRequest) -> list[str]:
    if request.param == "python":
        return [sys.executable, str(ROOT / "python/defer.py")]
    if request.param == "python-module":
        return [sys.executable, "-m", "defer"]
    binary = os.environ.get("DEFER_TEST_BINARY")
    if not binary:
        pytest.skip("set DEFER_TEST_BINARY to test the native tool")
    return [binary]


def test_cmake_config_stdout_and_write_file(command: list[str], tmp_path: Path) -> None:
    expected = (ROOT / "Defer.cmake").read_bytes().replace(b"\r\n", b"\n")
    result = subprocess.run([*command, "cmake-config"], capture_output=True, check=True)
    assert result.stdout.replace(b"\r\n", b"\n") == expected
    destination = tmp_path / "Defer.cmake"
    result = subprocess.run([*command, "cmake-config", "--write-file", str(destination)], capture_output=True, check=True)
    assert result.stdout == b""
    assert destination.read_bytes().replace(b"\r\n", b"\n") == expected


def test_cmake_config_rejects_invalid_arguments(command: list[str]) -> None:
    result = subprocess.run([*command, "cmake-config", "--write-file"], capture_output=True, check=False)
    assert result.returncode != 0
    assert not result.stdout


def test_direct_transform_finds_builtin_headers(command: list[str], tmp_path: Path) -> None:
    compiler = shutil.which("clang")
    if not compiler:
        pytest.skip("Clang is required")
    source = tmp_path / "source.c"
    output = tmp_path / "output.c"
    source.write_text('#include <stdio.h>\n#include "defer.h"\nint main(void) { defer(puts("done")); return 0; }\n', encoding="utf-8")
    include = f"-I{ROOT / 'cpp' / 'include'}"
    result = subprocess.run([*command, str(source), str(output), "--", "-std=c17", include],
                            cwd=tmp_path, capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    subprocess.run([compiler, str(output), include, "-o", str(executable)], capture_output=True, text=True, check=True)
    assert subprocess.check_output([executable], text=True).strip() == "done"


def test_module_and_executable_transform_standard_input(command: list[str], tmp_path: Path) -> None:
    source = b'#include "defer.h"\nvoid f(void) { defer((void)0); }\n'
    result = subprocess.run(
        [*command, "--filename", str(tmp_path / "virtual.c"), "--", "-I", str(ROOT / "cpp" / "include")],
        cwd=tmp_path,
        input=source,
        capture_output=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    assert b"__defer_active_" in result.stdout
