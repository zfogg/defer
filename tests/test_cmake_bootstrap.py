"""Exercise configure-time installation from a release-shaped ZIP."""
from __future__ import annotations

import hashlib
import os
import platform
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize("backend", ["NATIVE", "PYTHON"])
def test_emscripten_uses_driver_sysroot_with_empty_cache(tmp_path: Path, backend: str) -> None:
    binary = os.environ.get("DEFER_TEST_BINARY")
    toolchain = os.environ.get("EMSCRIPTEN_TOOLCHAIN_FILE")
    libclang = os.environ.get("LIBCLANG_LIBRARY_FILE")
    cmake, ninja = (shutil.which(name) for name in ("cmake", "ninja"))
    if not all((binary, toolchain, cmake, ninja)) or (backend == "PYTHON" and not libclang):
        pytest.skip("defer binary, Emscripten toolchain, CMake, Ninja and Python libclang are required")
    assert binary is not None and toolchain is not None and cmake is not None
    project = tmp_path / "project"
    project.mkdir()
    (project / "probe.c").write_text(
        '#include <emscripten/emscripten.h>\n#include "defer.h"\n'
        '#if !defined(__EMSCRIPTEN__) || __STDC_VERSION__ != 201710L\n#error incorrect flags\n#endif\n'
        'int probe(void) { int n=0; defer(n++); return n; }\n', encoding="utf-8"
    )
    (project / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.25)\nproject(probe C)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
        f'include("{(ROOT / "Defer.cmake").as_posix()}")\nadd_library(probe STATIC probe.c)\n'
        f'target_include_directories(probe PRIVATE "{(ROOT / "cpp/include").as_posix()}")\n'
        'target_compile_options(probe PRIVATE -sASSERTIONS=1)\nset_property(TARGET probe PROPERTY C_STANDARD 17)\n'
        'defer_target(probe)\n', encoding="utf-8"
    )
    build = tmp_path / "build"
    env = {**os.environ, "EM_CACHE": str(tmp_path / "emsdk cache")}
    for args in ([cmake, "-S", str(project), "-B", str(build), "-G", "Ninja", f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
                  f"-DDEFER_NATIVE_BINARY={binary}", f"-DDEFER_BACKEND={backend}", f"-DDEFER_TOOL_ROOT={ROOT}",
                  f"-DDEFER_PYTHON_COMMAND={sys.executable}", f"-DDEFER_LIBCLANG_PATH={libclang or ''}"],
                 [cmake, "--build", str(build)]):
        result = subprocess.run(args, capture_output=True, text=True, env=env, check=False, timeout=180)
        assert result.returncode == 0, result.stdout + result.stderr
    if backend == "NATIVE":
        manifest = next((build / "defer_transformed").glob("batch-*.txt"))
        command = [binary, "--batch", str(manifest), "--compilation-database", str(build),
                   "--incremental", "--dependency-stamps"]
        changed_env = {**env, "EM_CACHE": str(tmp_path / "second sdk cache")}
        result = subprocess.run(command, capture_output=True, text=True, env=changed_env, check=False, timeout=180)
        assert result.returncode == 0, result.stderr
        assert "transformed 1 of 1" in result.stderr
        result = subprocess.run(command, capture_output=True, text=True, env=changed_env, check=False, timeout=180)
        assert result.returncode == 0, result.stderr
        assert "transformed 0 of 1" in result.stderr


def test_cmake_uses_musl_compile_database_without_extra_flags(tmp_path: Path) -> None:
    binary = os.environ.get("DEFER_TEST_BINARY")
    cmake, ninja, clang = (shutil.which(name) for name in ("cmake", "ninja", "clang"))
    include = Path(os.environ.get("DEFER_TEST_MUSL_INCLUDE", "/usr/lib/musl/include"))
    if (not all((binary, cmake, ninja, clang)) or not (include / "stdio.h").is_file()
            or platform.machine().lower() not in ("x86_64", "amd64")):
        pytest.skip("native defer binary, Clang, CMake, Ninja, and x86-64 musl headers are required")
    assert binary is not None and cmake is not None and clang is not None
    project = tmp_path / "project"
    project.mkdir()
    (project / "probe.c").write_text(
        '#include <stdio.h>\n#include "defer.h"\n#ifdef __GLIBC__\n#error incorrect libc\n#endif\n'
        'void probe(void) { defer(puts("cleanup")); }\n', encoding="utf-8"
    )
    (project / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.25)\nproject(probe C)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
        f'include("{(ROOT / "Defer.cmake").as_posix()}")\nadd_library(probe STATIC probe.c)\n'
        f'target_include_directories(probe PRIVATE "{(ROOT / "cpp/include").as_posix()}")\ndefer_target(probe)\n',
        encoding="utf-8",
    )
    build = tmp_path / "build"
    for args in ([cmake, "-S", str(project), "-B", str(build), "-G", "Ninja", f"-DCMAKE_C_COMPILER={clang}",
                  f"-DCMAKE_C_FLAGS=--target=x86_64-unknown-linux-musl -I{include}",
                  "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY", f"-DDEFER_NATIVE_BINARY={binary}"],
                 [cmake, "--build", str(build)]):
        result = subprocess.run(args, capture_output=True, text=True, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([cmake, "--build", str(build)], capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "Transforming defer source batch" not in result.stdout


@pytest.mark.parametrize("backend", ["NATIVE", "PYTHON"])
def test_source_hook_preprocesses_deferred_and_plain_sources(tmp_path: Path, backend: str) -> None:
    binary = os.environ.get("DEFER_TEST_BINARY")
    cmake, ninja, clang = (shutil.which(name) for name in ("cmake", "ninja", "clang"))
    if not all((binary, cmake, ninja, clang)):
        pytest.skip("native defer binary, CMake, Ninja and Clang are required")
    assert binary is not None and cmake is not None and clang is not None
    libclang = os.environ.get("LIBCLANG_LIBRARY_FILE")
    if backend == "PYTHON" and not libclang:
        pytest.skip("LIBCLANG_LIBRARY_FILE is required for the Python backend")
    project = tmp_path / "project"
    project.mkdir()
    (project / "main.c").write_text(
        '#include "defer.h"\nint other(void);\nint main(void) { int n=0; { defer(n=other()); } return n!=3; }\n',
        encoding="utf-8",
    )
    (project / "other.c").write_text("int other(void) { return 3; }\n", encoding="utf-8")
    (project / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.25)\nproject(hook C)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
        f'include("{(ROOT / "Defer.cmake").as_posix()}")\n'
        'function(preprocess out target source relative compiled)\n'
        '  set(generated "${CMAKE_BINARY_DIR}/preprocessed/${relative}")\n'
        '  get_filename_component(dir "${generated}" DIRECTORY)\n'
        '  add_custom_command(OUTPUT "${generated}" COMMAND "${CMAKE_COMMAND}" -E make_directory "${dir}"\n'
        '    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${source}" "${generated}" DEPENDS "${source}")\n'
        '  set(${out} "${generated}" PARENT_SCOPE)\nendfunction()\n'
        f'add_executable(probe main.c other.c)\ntarget_include_directories(probe PRIVATE "{(ROOT / "cpp/include").as_posix()}")\n'
        'set(DEFER_SOURCE_HOOK preprocess)\ndefer_target(probe)\n', encoding="utf-8"
    )
    build = tmp_path / "build"
    for args in ([cmake, "-S", str(project), "-B", str(build), "-G", "Ninja",
                  f"-DCMAKE_C_COMPILER={clang}", f"-DDEFER_NATIVE_BINARY={binary}", f"-DDEFER_BACKEND={backend}",
                  f"-DDEFER_TOOL_ROOT={ROOT}", f"-DDEFER_PYTHON_COMMAND={sys.executable}",
                  f"-DDEFER_LIBCLANG_PATH={libclang or ''}"],
                 [cmake, "--build", str(build)]):
        result = subprocess.run(args, capture_output=True, text=True, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
    assert (build / "preprocessed" / "main.c").is_file()
    assert (build / "preprocessed" / "other.c").is_file()
    executable = build / ("probe.exe" if sys.platform == "win32" else "probe")
    subprocess.run([executable], check=True)
    (project / "other.c").write_text("int other(void) { return 4; }\n", encoding="utf-8")
    rebuilt = subprocess.run([cmake, "--build", str(build)], capture_output=True, text=True, check=False)
    assert rebuilt.returncode == 0, rebuilt.stdout + rebuilt.stderr
    assert subprocess.run([executable], check=False).returncode == 1


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
    for excluded in (".git", ".venv", "venv", "node_modules", "__pycache__"):
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
