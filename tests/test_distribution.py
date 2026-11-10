"""Distribution-facing commands must agree between implementations."""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(params=["python", "native"])
def command(request: pytest.FixtureRequest) -> list[str]:
    if request.param == "python":
        return [sys.executable, str(ROOT / "python/defer.py")]
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
