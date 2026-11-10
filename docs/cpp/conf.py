import os
import shutil
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "python" / "vendor"))
from clang import cindex

cindex.Config.set_compatibility_check(False)
if os.environ.get("LIBCLANG_LIBRARY_FILE"):
    cindex.Config.set_library_file(os.environ["LIBCLANG_LIBRARY_FILE"])

project = "defer C++ SDK"
extensions = ["hawkmoth", "myst_parser"]
source_suffix = {".md": "markdown", ".rst": "restructuredtext"}
hawkmoth_root = str(root / "cpp" / "include")
hawkmoth_autoconf = []
hawkmoth_clang = [f"-I{hawkmoth_root}"]
compiler = shutil.which("clang")
if compiler:
    resource_dir = subprocess.check_output([compiler, "-print-resource-dir"], text=True).strip()
    hawkmoth_clang.append(f"-resource-dir={resource_dir}")
if sys.platform == "darwin":
    sdk = os.environ.get("SDKROOT") or subprocess.check_output(
        ["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True
    ).strip()
    hawkmoth_clang.extend(["-isysroot", sdk])
if os.environ.get("LLVM_INCLUDE_DIR"):
    hawkmoth_clang.append(f"-I{os.environ['LLVM_INCLUDE_DIR']}")
hawkmoth_clang_cpp = ["-std=c++17"]
if sys.platform == "darwin":
    libcxx = Path(compiler).resolve().parent.parent / "include/c++/v1" if compiler else Path()
    if not compiler or not libcxx.is_dir():
        libcxx = Path(sdk) / "usr/include/c++/v1"
    hawkmoth_clang_cpp.append(f"-I{libcxx}")
html_theme = "alabaster"
