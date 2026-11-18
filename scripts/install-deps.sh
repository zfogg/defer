#!/usr/bin/env bash
set -euo pipefail

case "${1:-}" in
  linux)
    sudo apt-get update
    sudo apt-get install -y clang lld llvm ninja-build cmake python3 binutils zip
    ;;
  macos)
    brew install ninja cmake python
    ;;
  *)
    echo "usage: $0 linux|macos" >&2
    exit 2
    ;;
esac
