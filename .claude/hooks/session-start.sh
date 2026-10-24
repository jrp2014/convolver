#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Cloud sessions: install what the CMake build and tests need (CMake, FFTW,
# libsndfile, Clang sanitizer runtimes, latest stable Clang tools, and the LV2
# headers, lilv and serdi; CI pins the LV2 dependencies), and install
# the pre-commit Git hook so every commit runs the lint checks, formatting
# included. Local machines are left alone (CONTRIBUTING.md, "Lint checks").
set -euo pipefail
[ "${CLAUDE_CODE_REMOTE:-}" = "true" ] || exit 0
[ "$(uname -s)" = "Linux" ] || exit 0

install_build_tools() {
  command -v apt-get >/dev/null || return 0
  if command -v cmake >/dev/null && [ -f /usr/include/fftw3.h ] && [ -f /usr/include/sndfile.h ] &&
     [ -f /usr/include/lilv-0/lilv/lilv.h ] && command -v lv2info >/dev/null && command -v serdi >/dev/null &&
     ls /usr/lib/llvm-*/lib/clang/*/lib/linux/libclang_rt.asan-x86_64.a >/dev/null 2>&1; then
    return 0
  fi
  ver=$(clang++ --version 2>/dev/null | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -1 || true)
  ver=${ver:-18}
  SUDO=""; [ "$(id -u)" = 0 ] || SUDO="sudo"
  pkgs=(cmake libfftw3-dev libsndfile1-dev "libclang-rt-${ver}-dev")
  # serdi (a Turtle parser) also lets plugins.presets check LV2 preset syntax.
  lv2_pkgs=(lv2-dev liblilv-dev lilv-utils serdi)
  $SUDO apt-get install -y "${pkgs[@]}" >/dev/null 2>&1 || {
    $SUDO apt-get update -qq >/dev/null 2>&1
    $SUDO apt-get install -y "${pkgs[@]}" >/dev/null 2>&1
  } || echo "Could not install: ${pkgs[*]}; the CMake build or sanitizer tests may fail." >&2
  # Separate, so a missing LV2 package cannot stop the core tools installing.
  $SUDO apt-get install -y "${lv2_pkgs[@]}" >/dev/null 2>&1 ||
    echo "Could not install: ${lv2_pkgs[*]}; LV2 work needs them." >&2
}

# The Git hook runs `pre-commit run` on the staged files before each commit,
# the same checks as the lint workflow. Hook environments are installed in the
# background so the first commit does not wait for the downloads.
install_lint_hook() {
  local venv="${HOME}/.cache/convolver-lint"
  if [ ! -x "${venv}/bin/pre-commit" ]; then
    if ! { python3 -m venv "$venv" && "${venv}/bin/python" -m pip install -q pre-commit==4.5.1; } >/dev/null 2>&1; then
      echo "Could not install pre-commit; run the lint checks by hand (CONTRIBUTING.md)." >&2
      return 0
    fi
  fi
  # Upgrade tools without letting a download failure disable the commit hook.
  if "${venv}/bin/python" -m pip install -q --upgrade clang-format clang-tidy; then
    "${venv}/bin/clang-format" --version
    "${venv}/bin/clang-tidy" --version
    # Refresh cached hook environments only when the formatter release changes.
    local formatter_version
    formatter_version=$("${venv}/bin/clang-format" --version)
    if [ ! -f "${venv}/formatter-version" ] || [ "$(cat "${venv}/formatter-version")" != "$formatter_version" ]; then
      if "${venv}/bin/pre-commit" clean; then
        printf '%s\n' "$formatter_version" > "${venv}/formatter-version"
      fi
    fi
  else
    echo "Could not update the Clang tools; retaining cached hooks, not latest-tool validation." >&2
  fi
  cd "$(dirname "$0")/../.." || return 0
  if ! "${venv}/bin/pre-commit" install >/dev/null; then
    echo "Could not install the pre-commit Git hook; run 'pre-commit run --all-files' before each push." >&2
    return 0
  fi
  nohup "${venv}/bin/pre-commit" install-hooks >/dev/null 2>&1 </dev/null &
}

# Neither step may stop the other or fail the session.
install_build_tools || true
install_lint_hook || true
