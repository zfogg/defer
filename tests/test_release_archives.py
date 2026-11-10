"""Exercise release archive assembly with the platform archiver."""

from __future__ import annotations

import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.skipif(sys.platform != "darwin", reason="requires Apple's libtool")
def test_macos_combined_sdk_ignores_llvm_auxiliary_file(tmp_path: Path) -> None:
    if not all(shutil.which(tool) for tool in ("clang", "ar", "xcrun")):
        pytest.skip("Apple's build tools are required")
    stage, llvm = tmp_path / "stage", tmp_path / "llvm"
    (stage / "lib").mkdir(parents=True)
    (llvm / "lib").mkdir(parents=True)
    for name, archive in (("sdk", stage / "lib/libdefer-static.a"), ("clang", llvm / "lib/libclang.a")):
        source, obj = tmp_path / f"{name}.c", tmp_path / f"{name}.o"
        source.write_text(f"int {name}_symbol(void) {{ return 1; }}\n", encoding="utf-8")
        subprocess.run(["clang", "-c", str(source), "-o", str(obj)], check=True)
        subprocess.run(["ar", "rcs", str(archive), str(obj)], check=True)
    (llvm / "lib/liblibclang_static.a").write_text("auxiliary file", encoding="utf-8")
    spec = importlib.util.spec_from_file_location("defer_release", ROOT / "scripts/release.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.combine_archives(stage, llvm)
    members = subprocess.check_output(["ar", "-t", str(stage / "lib/libdefer-static.a")], text=True)
    assert "sdk.o" in members and "clang.o" in members
