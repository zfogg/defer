"""Check incremental header discovery with excluded dependency trees."""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize("extension", ["m", "mm"])
def test_discovery_tracks_included_objc_files(tmp_path: Path, extension: str) -> None:
    cmake = shutil.which("cmake")
    if not cmake:
        pytest.skip("CMake is required")
    project = tmp_path / "project"
    project.mkdir()
    objc = project / f"included.{extension}"
    objc.write_text("#define OBJC_CLEANUP(action) defer(action)\n", encoding="utf-8")
    module = (ROOT / "Defer.cmake").as_posix()
    (project / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.25)\nproject(scan LANGUAGES NONE)\n'
        f'include("{module}")\nset(_batch_stage test)\n'
        'defer_scan_targets(selected)\nget_property(wrappers GLOBAL PROPERTY DEFER_WRAPPER_NAMES)\n'
        'file(WRITE "${CMAKE_BINARY_DIR}/result.txt" "${wrappers}")\n',
        encoding="utf-8",
    )
    build = tmp_path / "build"
    result = subprocess.run([cmake, "-S", str(project), "-B", str(build)], capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "OBJC_CLEANUP" in (build / "result.txt").read_text(encoding="utf-8")


def test_discovery_prunes_environments_and_keeps_explicit_roots(tmp_path: Path) -> None:
    cmake, ninja = shutil.which("cmake"), shutil.which("ninja")
    if not cmake or not ninja:
        pytest.skip("CMake and Ninja are required")
    project = tmp_path / "project"
    headers = project / "src" / "nested"
    headers.mkdir(parents=True)
    (headers / "wrapper.h").write_text("#define FIRST(action) defer(action)\n", encoding="utf-8")
    for name in (".venv", "venv", "node_modules", "__pycache__"):
        ignored = project / "src" / name / "package"
        ignored.mkdir(parents=True)
        (ignored / "ignored.h").write_text(
            "#define IGNORED(action) defer(action)\nstatic void ignored(void) { defer((void)0); }\n",
            encoding="utf-8",
        )
    external = tmp_path / ".venv"
    external.mkdir()
    (external / "external.h").write_text("#define EXTERNAL(action) defer(action)\n", encoding="utf-8")
    build = tmp_path / "build"
    generated = build / "generated" / "nested"
    generated.mkdir(parents=True)
    (generated / "wrapper.h").write_text("#define GENERATED(action) defer(action)\n", encoding="utf-8")
    if sys.platform != "win32":
        linked = tmp_path / "linked"
        linked.mkdir()
        (linked / "linked.h").write_text("#define SYMLINKED(action) defer(action)\n", encoding="utf-8")
        (project / "src" / "link").symlink_to(linked, target_is_directory=True)
    module = (ROOT / "Defer.cmake").as_posix()
    (project / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.25)\nproject(scan LANGUAGES NONE)\n"
        'set(DEFER_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")\nset(DEFER_SOURCE_DIRS src CACHE STRING "")\n'
        f'set(DEFER_WRAPPER_HEADER_DIRS "{external.as_posix()};{build.as_posix()}" CACHE STRING "")\n'
        f'include("{module}")\nset(_batch_stage test)\n'
        "defer_scan_targets(selected)\n"
        "get_property(wrappers GLOBAL PROPERTY DEFER_WRAPPER_NAMES)\n"
        "get_property(overlays GLOBAL PROPERTY DEFER_HEADER_ACTIVE)\n"
        'file(WRITE "${CMAKE_BINARY_DIR}/scan-result.txt" "${wrappers}|${overlays}")\n',
        encoding="utf-8",
    )
    def run(*args: str) -> str:
        result = subprocess.run(args, capture_output=True, text=True, check=False, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout

    run(cmake, "-S", str(project), "-B", str(build), "-G", "Ninja")
    result_file = build / "scan-result.txt"
    initial = result_file.read_text(encoding="utf-8")
    assert all(name in initial for name in ("FIRST", "EXTERNAL", "GENERATED")) and "IGNORED" not in initial
    if sys.platform != "win32":
        assert "SYMLINKED" in initial
    assert initial.endswith("|FALSE")
    new_dir = project / "src" / "new" / "nested"
    new_dir.mkdir(parents=True)
    new_header = new_dir / "new.h"
    new_header.write_text(
        "#define SECOND(action) FIRST(action)\nstatic void f(void) { SECOND((void)0); }\n",
        encoding="utf-8",
    )
    run(cmake, "--build", str(build))
    updated = result_file.read_text(encoding="utf-8")
    assert "SECOND" in updated and updated.endswith("|TRUE")
    new_header.write_text("#define SECOND(action) ((void)0)\n", encoding="utf-8")
    run(cmake, "--build", str(build))
    assert "SECOND" not in result_file.read_text(encoding="utf-8")
    (project / "src" / ".venv" / "package" / "another.h").write_text("ignored\n", encoding="utf-8")
    assert "Re-running CMake" not in run(cmake, "--build", str(build))
