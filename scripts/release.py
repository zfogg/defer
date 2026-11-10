"""Build and validate one self-contained defer release archive."""
from __future__ import annotations

import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LLVM_VERSION = "23.1.3"
LLVM_SHA256 = "c44186a7762ed28954be72e5ff6df9808e0779d4f1bf014ecc4e7e211d31ee34"


def run(*args: object, cwd: Path = ROOT) -> None:
    command = [str(arg) for arg in args]
    print("+", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def download_source(work: Path) -> Path:
    source = work / "llvm-source"
    marker = source / f".complete-{LLVM_VERSION}"
    if marker.is_file():
        return source
    archive = work / f"llvm-project-{LLVM_VERSION}.src.tar.xz"
    url = f"https://github.com/llvm/llvm-project/releases/download/llvmorg-{LLVM_VERSION}/{archive.name}"
    if not archive.exists():
        print(f"Downloading {url}", flush=True)
        urllib.request.urlretrieve(url, archive)
    digest = hashlib.sha256()
    with archive.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != LLVM_SHA256:
        raise RuntimeError("LLVM source archive SHA-256 mismatch")
    source.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as tar:
        for member in tar:
            parts = Path(member.name).parts
            if len(parts) < 2 or parts[1] not in {"llvm", "clang", "cmake", "libc", "third-party"}:
                continue
            if any(part in {"test", "tests", "unittests", "examples", "docs", "benchmarks"} for part in parts[2:]):
                continue
            # Symlinks in the upstream archive are test fixtures; they are not needed for release builds.
            if member.issym() or member.islnk():
                continue
            member.name = str(Path(*parts[1:]))
            tar.extract(member, source, filter="data")
    marker.write_text(LLVM_SHA256 + "\n", encoding="utf-8")
    return source


def configure_llvm(work: Path, source: Path, jobs: int) -> Path:
    build = work / "llvm-build"
    windows = sys.platform == "win32"
    compiler = "clang-cl" if windows else "clang"
    cxx = "clang-cl" if windows else "clang++"
    args = ["cmake", "-S", source / "llvm", "-B", build, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_C_COMPILER={compiler}", f"-DCMAKE_CXX_COMPILER={cxx}",
            "-DLLVM_ENABLE_PROJECTS=clang", "-DLLVM_TARGETS_TO_BUILD=all", "-DLIBCLANG_BUILD_STATIC=ON",
            "-DLLVM_INCLUDE_TESTS=OFF", "-DCLANG_INCLUDE_TESTS=OFF", "-DLLVM_INCLUDE_EXAMPLES=OFF",
            "-DLLVM_INCLUDE_BENCHMARKS=OFF", "-DLLVM_INCLUDE_DOCS=OFF", "-DCLANG_INCLUDE_DOCS=OFF",
            "-DLLVM_ENABLE_TERMINFO=OFF", "-DLLVM_ENABLE_LIBEDIT=OFF",
            "-DLLVM_ENABLE_ZLIB=OFF", "-DLLVM_ENABLE_ZSTD=OFF", "-DLLVM_ENABLE_LIBXML2=OFF",
            "-DLLVM_ENABLE_CURL=OFF"]
    if windows:
        args += ["-DLLVM_ENABLE_PIC=OFF", "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded", "-DLLVM_USE_CRT_RELEASE=MT"]
        if os.environ.get("DEFER_COMPILER_TARGET"):
            args += [f"-DCMAKE_C_COMPILER_TARGET={os.environ['DEFER_COMPILER_TARGET']}",
                     f"-DCMAKE_CXX_COMPILER_TARGET={os.environ['DEFER_COMPILER_TARGET']}"]
    run(*args)
    run("cmake", "--build", build, "--target", "libclang" if windows else "libclang_static", "--parallel", jobs)
    return build


def combine_archives(stage: Path, llvm: Path) -> None:
    windows = sys.platform == "win32"
    sdk = stage / "lib" / ("defer-static.lib" if windows else "libdefer-static.a")
    archives = [sdk, *sorted((llvm / "lib").glob("*.lib" if windows else "*.a"))]
    if len(archives) < 2:
        raise RuntimeError("static LLVM archives are missing")
    combined = sdk.with_name("combined" + sdk.suffix)
    if windows:
        response = sdk.with_name("archives.rsp")
        response.write_text("\n".join(f'"{path}"' for path in archives), encoding="utf-8")
        run("llvm-lib", f"/OUT:{combined}", f"@{response}")
        response.unlink()
    elif sys.platform == "darwin":
        run("libtool", "-static", "-o", combined, *archives)
    else:
        script = "CREATE " + str(combined) + "\n" + "".join(f"ADDLIB {path}\n" for path in archives) + "SAVE\nEND\n"
        subprocess.run(["llvm-ar", "-M"], input=script, text=True, check=True)
    os.replace(combined, sdk)


def verify_binary(binary: Path, musl: bool) -> None:
    run(binary, "--help")
    if sys.platform == "win32":
        output = subprocess.check_output(["llvm-readobj", "--coff-imports", str(binary)], text=True)
    elif sys.platform == "darwin":
        output = subprocess.check_output(["otool", "-L", str(binary)], text=True)
    else:
        output = subprocess.check_output(["readelf", "-d", str(binary)], text=True, stderr=subprocess.STDOUT)
        if musl:
            program_headers = subprocess.check_output(["readelf", "-l", str(binary)], text=True)
            if "INTERP" in program_headers or "(NEEDED)" in output:
                raise RuntimeError("musl executable is not fully static")
    if "libclang" in output.lower() or "libllvm" in output.lower():
        raise RuntimeError("executable depends on a shared LLVM or libclang library")


def verify_sdk(work: Path, stage: Path, cxx: str) -> None:
    source = work / "sdk-smoke"
    source.mkdir(exist_ok=True)
    (source / "main.cpp").write_text(
        '#include "driver.h"\n#include <iostream>\n'
        'int main(int argc, char** argv) { if (argc != 2) return 1; '
        'std::cout << defer_tool::read_file(argv[1]); return 0; }\n', encoding="utf-8"
    )
    (source / "CMakeLists.txt").write_text(
        '''cmake_minimum_required(VERSION 3.25)
project(sdk_smoke LANGUAGES CXX)
find_package(Threads REQUIRED)
add_library(sdk_static STATIC IMPORTED)
add_library(sdk_shared SHARED IMPORTED)
if(WIN32)
  set_target_properties(sdk_static PROPERTIES IMPORTED_LOCATION "${SDK_ROOT}/lib/defer-static.lib")
  set_target_properties(sdk_shared PROPERTIES IMPORTED_IMPLIB "${SDK_ROOT}/lib/defer-shared.lib" IMPORTED_LOCATION "${SDK_ROOT}/bin/defer-shared.dll")
elseif(APPLE)
  set_target_properties(sdk_static PROPERTIES IMPORTED_LOCATION "${SDK_ROOT}/lib/libdefer-static.a")
  set_target_properties(sdk_shared PROPERTIES IMPORTED_LOCATION "${SDK_ROOT}/lib/libdefer-shared.dylib")
else()
  set_target_properties(sdk_static PROPERTIES IMPORTED_LOCATION "${SDK_ROOT}/lib/libdefer-static.a")
  set_target_properties(sdk_shared PROPERTIES IMPORTED_LOCATION "${SDK_ROOT}/lib/libdefer-shared.so")
endif()
foreach(kind static shared)
  add_executable(smoke_${kind} main.cpp)
  target_compile_features(smoke_${kind} PRIVATE cxx_std_17)
  target_include_directories(smoke_${kind} PRIVATE "${SDK_ROOT}/include")
  target_link_libraries(smoke_${kind} PRIVATE sdk_${kind} Threads::Threads ${CMAKE_DL_LIBS})
  if(WIN32)
    target_link_libraries(smoke_${kind} PRIVATE advapi32 shell32 ole32 version ws2_32 dbghelp bcrypt ntdll)
  endif()
endforeach()
''', encoding="utf-8"
    )
    build = work / "sdk-smoke-build"
    configure = ["cmake", "-S", source, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
                 f"-DCMAKE_CXX_COMPILER={cxx}", f"-DSDK_ROOT={stage}"]
    if sys.platform == "win32" and os.environ.get("DEFER_COMPILER_TARGET"):
        configure.append(f"-DCMAKE_CXX_COMPILER_TARGET={os.environ['DEFER_COMPILER_TARGET']}")
    run(*configure)
    run("cmake", "--build", build)
    if sys.platform == "win32":
        shutil.copyfile(stage / "bin/defer-shared.dll", build / "defer-shared.dll")
    fixture = work / "sdk-input.txt"
    fixture.write_text("SDK smoke test\n", encoding="utf-8")
    for kind in ("static", "shared"):
        executable = build / (f"smoke_{kind}.exe" if sys.platform == "win32" else f"smoke_{kind}")
        if subprocess.check_output([executable, fixture], text=True) != "SDK smoke test\n":
            raise RuntimeError(f"{kind} SDK smoke test failed")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--musl", action="store_true")
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = download_source(work)
    llvm = configure_llvm(work, source, args.jobs)
    build = work / "defer-build"
    stage = work / "stage"
    compiler = "clang-cl" if sys.platform == "win32" else "clang"
    cxx = "clang-cl" if sys.platform == "win32" else "clang++"
    configure = ["cmake", "-S", ROOT, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_C_COMPILER={compiler}", f"-DCMAKE_CXX_COMPILER={cxx}", f"-DDEFER_LLVM_DEV_ROOT={llvm}",
        "-DDEFER_STATIC_LIBCLANG=ON", f"-DDEFER_FULLY_STATIC_EXECUTABLE={'ON' if args.musl else 'OFF'}"]
    if sys.platform == "win32" and os.environ.get("DEFER_COMPILER_TARGET"):
        configure += [f"-DCMAKE_C_COMPILER_TARGET={os.environ['DEFER_COMPILER_TARGET']}",
                      f"-DCMAKE_CXX_COMPILER_TARGET={os.environ['DEFER_COMPILER_TARGET']}"]
    run(*configure)
    run("cmake", "--build", build, "--parallel", args.jobs)
    run("ctest", "--test-dir", build, "--output-on-failure")
    run("cmake", "--install", build, "--prefix", stage)
    shutil.copyfile(ROOT / "LICENSE", stage / "LICENSE")
    shutil.copyfile(source / "llvm/LICENSE.TXT", stage / "LICENSE-LLVM.txt")
    shutil.copyfile(ROOT / "README.md", stage / "README.md")
    shutil.copytree(source / "clang/include/clang-c", stage / "include/clang-c", dirs_exist_ok=True)
    combine_archives(stage, llvm)
    verify_sdk(work, stage, cxx)
    binary = stage / "bin" / ("defer-tool.exe" if sys.platform == "win32" else "defer-tool")
    verify_binary(binary, args.musl)
    generated = work / "Defer.cmake"
    run(binary, "cmake-config", "--write-file", generated)
    if generated.read_bytes() != (ROOT / "Defer.cmake").read_bytes():
        raise RuntimeError("embedded CMake configuration differs from source")
    output = work / "hello.c"
    run(binary, ROOT / "examples/hello_world/main.c", output, "--", f"-I{stage / 'include'}")
    executable = work / ("hello.exe" if sys.platform == "win32" else "hello")
    target_flag = [f"--target={os.environ['DEFER_COMPILER_TARGET']}"] if os.environ.get("DEFER_COMPILER_TARGET") else []
    run("clang", *target_flag, output, f"-I{stage / 'include'}", "-o", executable)
    if subprocess.check_output([executable], text=True).splitlines() != ["hello", "goodbye"]:
        raise RuntimeError("hello-world output was incorrect")
    example_build = work / "example-build"
    example_configure = ["cmake", "-S", ROOT / "examples/hello_world_cmake", "-B", example_build, "-G", "Ninja",
        f"-DCMAKE_C_COMPILER={compiler}", f"-DDEFER_MODULE={generated}", f"-DDEFER_TOOL={binary}",
        f"-DDEFER_INCLUDE_DIR={stage / 'include'}"]
    if os.environ.get("DEFER_COMPILER_TARGET"):
        example_configure.append(f"-DCMAKE_C_COMPILER_TARGET={os.environ['DEFER_COMPILER_TARGET']}")
    run(*example_configure)
    run("cmake", "--build", example_build)
    run(example_build / ("hello.exe" if sys.platform == "win32" else "hello"))
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / f"defer-{args.name}.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zip_file:
        for path in stage.rglob("*"):
            if path.is_file():
                zip_file.write(path, path.relative_to(stage))
    print(f"Published archive candidate: {archive} ({platform.machine()})", flush=True)


if __name__ == "__main__":
    main()
