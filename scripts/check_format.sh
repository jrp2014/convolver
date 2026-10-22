#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Check (default) or apply (--fix) clang-format on the portable C++ sources.
# Files are found by location, so a new adapter under plugins/ or a new test
# folder is covered without editing a list. Historical code, vendored code and
# the regression harnesses that slice historical source are excluded.
# pre-commit runs this with --fix and a pinned clang-format 18 (see
# .pre-commit-config.yaml), so the lint workflow and `pre-commit run` own the
# formatting check. Written for Bash 3.2 too, because macOS still ships it.
set -euo pipefail
cd "$(dirname "$0")/.."

mode=check
case "${1:-}" in
  "") ;;
  --fix) mode=fix ;;
  *)
    echo "usage: $0 [--fix]" >&2
    exit 2
    ;;
esac

# Formatting changes between clang-format major versions, so CI and every local
# path use version 18. Prefer an explicit CLANG_FORMAT, then clang-format-18
# (Ubuntu), then a plain clang-format (pre-commit's pinned copy, Homebrew).
format=${CLANG_FORMAT:-}
if [ -z "$format" ]; then
  for candidate in clang-format-18 clang-format; do
    if command -v "$candidate" >/dev/null 2>&1; then
      format=$candidate
      break
    fi
  done
fi
install_hint="Install it with 'python3 -m pip install clang-format==18.1.8' (any OS),
'apt-get install clang-format-18' (Ubuntu) or 'brew install llvm@18' (macOS),
or run 'pre-commit run clang-format --all-files', which installs it for you.
Set CLANG_FORMAT to the binary if it is not on PATH as clang-format-18 or clang-format."
if [ -z "$format" ]; then
  printf 'clang-format 18 was not found.\n%s\n' "$install_hint" >&2
  exit 1
fi
version=$("$format" --version 2>/dev/null | sed -n 's/.*clang-format version \([0-9][0-9]*\)\..*/\1/p' | head -n 1 || true)
if [ -z "$version" ]; then
  printf '%s did not report a clang-format version; is it installed?\n%s\n' \
    "$format" "$install_hint" >&2
  exit 1
fi
if [ "$version" != "18" ]; then
  printf '%s is clang-format %s, but this project is formatted with clang-format 18;\n' \
    "$format" "$version" >&2
  printf 'other major versions lay out the same code differently, so CI would disagree.\n%s\n' \
    "$install_hint" >&2
  exit 1
fi

files=()
while IFS= read -r -d '' file; do
  files+=("$file")
done < <(git ls-files -z --cached --others --exclude-standard -- \
  'include/*.hpp' 'include/*.h' \
  'portable/*.hpp' 'portable/*.cpp' 'portable/*.h' \
  'plugins/*.hpp' 'plugins/*.cpp' 'plugins/*.h' 'plugins/*.mm' \
  'tests/*.hpp' 'tests/*.cpp' 'tests/*.h' \
  ':!:portable/third_party/' ':!:tests/regression/')
if [ "${#files[@]}" -eq 0 ]; then
  echo "No C++ sources found" >&2
  exit 1
fi

if [ "$mode" = fix ]; then
  changed=()
  for file in "${files[@]}"; do
    if ! "$format" --dry-run --Werror "$file" >/dev/null 2>&1; then
      changed+=("$file")
    fi
  done
  if [ "${#changed[@]}" -eq 0 ]; then
    echo "clang-format: ${#files[@]} files already formatted"
    exit 0
  fi
  "$format" -i "${changed[@]}"
  echo "clang-format: reformatted ${#changed[@]} of ${#files[@]} files:"
  printf '  %s\n' "${changed[@]}"
  echo "Review the changes, then stage and commit them."
else
  if ! "$format" --dry-run --Werror "${files[@]}"; then
    echo "clang-format: the files above are not formatted. Run 'scripts/check_format.sh --fix'" >&2
    echo "or 'pre-commit run --all-files' to reformat them, then commit the result." >&2
    exit 1
  fi
  echo "clang-format: ${#files[@]} files formatted correctly"
fi
