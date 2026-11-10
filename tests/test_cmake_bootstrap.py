"""Exercise configure-time installation from a release-shaped ZIP."""
from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize("compiler_name", ["clang", "clang-cl"])
def test_downloaded_tool_generates_cmake_and_detects_new_defer(tmp_path: Path, compiler_name: str) -> None:
    binary = os.environ.get("DEFER_TEST_BINARY")
    if compiler_name == "clang-cl" and (sys.platform != "win32" or not os.environ.get("INCLUDE")):
        pytest.skip("clang-cl requires a Windows developer environment")
    cmake, ninja, clang = (shutil.which(name) for name in ("cmake", "ninja", compiler_name))
    if not all((binary, cmake, ninja, clang)):
        pytest.skip("native defer binary, CMake, Ninja and Clang are required")
    assert binary is not None and cmake is not None and clang is not None
    tool = Path(binary).resolve()
    archive = tmp_path / "defer.zip"
    with zipfile.ZipFile(archive, "w") as bundle:
        bundle.write(tool, f"bin/{tool.name}")
        bundle.write(ROOT / "cpp/include/defer.h", "include/defer.h")
        if sys.platform == "win32":
            bundle.write(tool.parent / "libclang.dll", "bin/libclang.dll")
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    project = tmp_path / "project"
    project.mkdir()
    for excluded in (".git", ".venv"):
        directory = project / excluded
        directory.mkdir()
        (directory / "unrelated.h").write_text("static void f(void) { defer((void)0); }\n", encoding="utf-8")
    (project / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.25)\nproject(consumer C)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
        f'set(url "{archive.as_uri()}")\nset(expected "{checksum}")\n'
        'set(root "${CMAKE_BINARY_DIR}/defer-release/${expected}")\nfile(MAKE_DIRECTORY "${root}")\n'
        'if(NOT EXISTS "${root}/defer.zip")\n'
        '  file(DOWNLOAD "${url}" "${root}/defer.zip" EXPECTED_HASH "SHA256=${expected}" TLS_VERIFY ON)\n'
        'endif()\n'
        'if(CMAKE_HOST_WIN32)\n set(tool "${root}/bin/defer-tool.exe")\nelse()\n set(tool "${root}/bin/defer-tool")\nendif()\n'
        'if(NOT EXISTS "${tool}")\n file(ARCHIVE_EXTRACT INPUT "${root}/defer.zip" DESTINATION "${root}")\nendif()\n'
        'execute_process(COMMAND "${tool}" cmake-config --write-file "${root}/Defer.cmake" COMMAND_ERROR_IS_FATAL ANY)\n'
        'set(DEFER_NATIVE_BINARY "${tool}")\ninclude("${root}/Defer.cmake")\n'
        'add_executable(consumer main.c)\ntarget_include_directories(consumer PRIVATE "${root}/include")\n'
        'defer_targets(consumer)\n', encoding="utf-8"
    )
    source = project / "main.c"
    source.write_text('int main(void) { return 0; }\n', encoding="utf-8")
    build = tmp_path / "build"
    configured = subprocess.run([cmake, "-S", str(project), "-B", str(build), "-G", "Ninja", f"-DCMAKE_C_COMPILER={clang}"],
                                capture_output=True, text=True, check=True)
    assert "C header defer detected" not in configured.stdout
    first = subprocess.run([cmake, "--build", str(build)], capture_output=True, text=True, check=True)
    assert "Transforming defer source batch" not in first.stdout
    source.write_text('#include "defer.h"\nint main(void) { int x=0; { defer(x=1); } return x!=1; }\n', encoding="utf-8")
    second = subprocess.run([cmake, "--build", str(build)], capture_output=True, text=True, check=True)
    assert "Transforming defer source batch" in second.stdout
    executable = build / ("consumer.exe" if sys.platform == "win32" else "consumer")
    subprocess.run([executable], check=True)
    third = subprocess.run([cmake, "--build", str(build)], capture_output=True, text=True, check=True)
    assert "Transforming defer source batch" not in third.stdout
    (project / "cleanup.h").write_text(
        '#include "defer.h"\nstatic int cleanup(void) { int x = 0; { defer(x = 1); } return x; }\n',
        encoding="utf-8",
    )
    source.write_text('#include "cleanup.h"\nint main(void) { return cleanup() != 1; }\n', encoding="utf-8")
    subprocess.run([cmake, "--build", str(build)], capture_output=True, text=True, check=True)
    subprocess.run([executable], check=True)
