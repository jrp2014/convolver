#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Check (default) or apply (--fix) clang-format on the portable C++ sources.
# Files are found by location, so a new adapter under plugins/ or a new test
# folder is covered without editing a list. Historical code, vendored code and
# the regression harnesses that slice historical source are excluded.
set -euo pipefail
cd "$(dirname "$0")/.."
format=${CLANG_FORMAT:-clang-format-18}
mapfile -d '' files < <(git ls-files -z --cached --others --exclude-standard -- \
  'include/*.hpp' 'include/*.h' \
  'portable/*.hpp' 'portable/*.cpp' 'portable/*.h' \
  'plugins/*.hpp' 'plugins/*.cpp' 'plugins/*.h' 'plugins/*.mm' \
  'tests/*.hpp' 'tests/*.cpp' 'tests/*.h' \
  ':!:portable/third_party/' ':!:tests/regression/')
if [ "${#files[@]}" -eq 0 ]; then
  echo "No C++ sources found" >&2
  exit 1
fi
if [ "${1:-}" = "--fix" ]; then
  "$format" -i "${files[@]}"
else
  "$format" --dry-run --Werror "${files[@]}"
  echo "clang-format: ${#files[@]} files formatted correctly"
fi
