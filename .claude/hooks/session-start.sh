#!/bin/bash
# Cloud sessions: make sure Clang's sanitizer runtimes exist so
# tests/regression/run_all.py can link. Local machines are left alone.
set -euo pipefail
[ "${CLAUDE_CODE_REMOTE:-}" = "true" ] || exit 0
[ "$(uname -s)" = "Linux" ] || exit 0
command -v apt-get >/dev/null || exit 0
if ls /usr/lib/llvm-*/lib/clang/*/lib/linux/libclang_rt.asan-x86_64.a >/dev/null 2>&1; then
  exit 0
fi
ver=$(clang++ --version 2>/dev/null | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -1)
ver=${ver:-18}
SUDO=""; [ "$(id -u)" = 0 ] || SUDO="sudo"
$SUDO apt-get install -y "libclang-rt-${ver}-dev" >/dev/null 2>&1 || {
  $SUDO apt-get update -qq >/dev/null 2>&1
  $SUDO apt-get install -y "libclang-rt-${ver}-dev" >/dev/null 2>&1
} || echo "Could not install libclang-rt-${ver}-dev; sanitizer suites will not link." >&2
