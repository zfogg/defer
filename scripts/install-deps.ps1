$ErrorActionPreference = 'Stop'

foreach ($tool in @('cmake', 'ninja', 'clang-cl', 'llvm-lib', 'llvm-readobj')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Required LLVM release tool not found: $tool"
    }
}

Write-Host 'CMake, Ninja, and the required LLVM tools are available.'
