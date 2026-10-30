#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Runs the libFuzzer campaigns side by side, at most one per physical core, so
# each keeps a core of its own for its whole time budget (CI-12). Fails if any
# campaign fails or leaves no exit status. The fuzz workflow runs it, and
# tests/fuzz/scheduler_tests.py tests it with stand-in fuzzers (CI-15).
# See docs/develop/testing.md#fuzz-smoke-tests-and-longer-campaigns.
set -u

usage() {
  cat <<'USAGE'
Usage: scripts/fuzz_campaigns.sh [--build DIR] [--output DIR] [--seconds N]
                                 [--jobs N] [--source DIR] [TARGET...]

  --build DIR    folder holding the fuzz_TARGET programs (default build-fuzz)
  --output DIR   corpora, logs, status files and findings (default fuzz-out)
  --seconds N    time budget of each campaign (default 60)
  --jobs N       campaigns at once (default: the number of physical cores)
  --source DIR   repository with tests/fuzz/seeds and tests/fuzz/regressions
                 (default: the one holding this script)
  TARGET...      default: config_text config_list host_state ir_file
USAGE
}

needs_value() {
  if [ $# -lt 2 ] || [ -z "$2" ]; then
    echo "error: $1 needs a value" >&2
    usage >&2
    exit 2
  fi
}

# The default limit and where it came from: physical cores from lscpu (Linux;
# hardware threads of one core count once) or sysctl (macOS), else the CPU
# count, else 1. Prints "COUNT DESCRIPTION".
count_cores() {
  local count
  count=$(lscpu --parse=CORE,SOCKET 2>/dev/null | grep -v '^#' | sort -u | wc -l)
  count=$((count))
  if [ "$count" -ge 1 ]; then
    echo "$count physical cores (lscpu)"
    return
  fi
  count=$(sysctl -n hw.physicalcpu 2>/dev/null)
  if [[ $count =~ ^[1-9][0-9]*$ ]]; then
    echo "$count physical cores (sysctl)"
    return
  fi
  count=$(getconf _NPROCESSORS_ONLN 2>/dev/null)
  if [[ $count =~ ^[1-9][0-9]*$ ]]; then
    echo "$count CPUs (getconf; physical cores unknown, so hardware threads count as cores)"
    return
  fi
  echo "1 (no CPU count found; give --jobs)"
}

# `wait -n` needs bash 4.3 (macOS ships 3.2; use a newer bash there).
if [ "${BASH_VERSINFO[0]}" -lt 4 ] || { [ "${BASH_VERSINFO[0]}" -eq 4 ] && [ "${BASH_VERSINFO[1]}" -lt 3 ]; }; then
  echo "error: $0 needs bash 4.3 or later (for wait -n); this is bash $BASH_VERSION" >&2
  exit 2
fi

source_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
build='build-fuzz'
output='fuzz-out'
seconds=60
limit=
while [ $# -gt 0 ]; do
  case $1 in
    --build) needs_value "$@"; build=$2; shift 2 ;;
    --output) needs_value "$@"; output=$2; shift 2 ;;
    --seconds) needs_value "$@"; seconds=$2; shift 2 ;;
    --jobs) needs_value "$@"; limit=$2; shift 2 ;;
    --source) needs_value "$@"; source_dir=$2; shift 2 ;;
    -h | --help) usage; exit 0 ;;
    --) shift; break ;;
    -*) echo "error: unknown option $1" >&2; usage >&2; exit 2 ;;
    *) break ;;
  esac
done
targets=("$@")
[ ${#targets[@]} -gt 0 ] || targets=(config_text config_list host_state ir_file)

if ! [[ $seconds =~ ^[1-9][0-9]*$ ]]; then
  echo "error: --seconds must be a whole number of seconds, 1 or more, not '$seconds'" >&2
  exit 2
fi
if [ -z "$limit" ]; then
  read -r limit counted <<< "$(count_cores)"
  echo "Campaign limit: $limit $counted"
  lscpu 2>/dev/null | grep -E '^(CPU\(s\)|Thread\(s\) per core|Core\(s\) per socket|Socket\(s\)):' || true
elif ! [[ $limit =~ ^[1-9][0-9]*$ ]]; then
  echo "error: --jobs must be a whole number, 1 or more, not '$limit'" >&2
  exit 2
fi

# Prepare every corpus before starting a campaign, so a missing seed folder
# stops the step at once instead of leaving campaigns running.
mkdir -p "$output" || exit 1
for target in "${targets[@]}"; do
  if [ ! -d "$source_dir/tests/fuzz/seeds/$target" ]; then
    echo "error: no seed folder $source_dir/tests/fuzz/seeds/$target" >&2
    exit 1
  fi
  mkdir -p "$output/corpus/$target" &&
    cp "$source_dir/tests/fuzz/seeds/$target/"* "$output/corpus/$target/" || exit 1
  # A status file left by an earlier run must not stand in for this one's.
  rm -f "$output/$target.status"
done

echo "Running $limit campaigns at a time, $seconds seconds each."
for target in "${targets[@]}"; do
  while [ "$(jobs -pr | wc -l)" -ge "$limit" ]; do
    wait -n || true
  done
  (
    status=0
    "$build/fuzz_$target" "$output/corpus/$target" "$source_dir/tests/fuzz/regressions/$target" \
      -max_total_time="$seconds" -max_len=65536 -timeout=2 -rss_limit_mb=512 \
      -artifact_prefix="$output/$target-" > "$output/$target.log" 2>&1 || status=$?
    echo "$status" > "$output/$target.status"
  ) &
done
wait

# Every campaign's result, then the step's: any failure, or a campaign killed
# before it could record its status, fails it.
status=0
for target in "${targets[@]}"; do
  result=$(cat "$output/$target.status" 2>/dev/null || echo 'no status')
  if [ "$result" = 'no status' ]; then
    echo "== fuzz_$target left no exit status (killed before it could record one?)"
  else
    echo "== fuzz_$target exited with $result"
  fi
  tail -5 "$output/$target.log" 2>/dev/null || true
  [ "$result" = 0 ] || status=1
done
exit $status
