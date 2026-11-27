#!/usr/bin/env bash
# MakeGain local CI stand-in (C5 per m0-plan) — the same checks the future
# GitHub Actions matrix will run, locally:
#   1. configure + build the platform preset
#   2. configure + build the sanitized test preset
#   3. run ctest on both
# Usage: ./scripts/check-all.sh              (auto-detects platform)
#        ./scripts/check-all.sh macos ...    (explicit presets, space-separated)
#        ./scripts/check-all.sh -v macos ...  (streams live output)
set -u

platform="$(uname -s)-$(uname -m)"
case "$platform" in
  Darwin-arm64)  DEFAULT_PRESETS="macos macos-sanitize" ;;
  Linux-aarch64) DEFAULT_PRESETS="linux-arm64 linux-arm64-sanitize" ;;
  Linux-x86_64)  DEFAULT_PRESETS="linux-x64 linux-x64-sanitize" ;;
  *) echo "unknown platform '$platform'; pass presets explicitly:" >&2
     echo "  $0 <preset> [-sanitize-variant] [more...]" >&2; exit 2 ;;
esac

# Capture mode: -v before first preset means stream live.
stream=0
if [ $# -gt 0 ]; then
  # Shift off a leading -v so PRESETS still works below.
  if [[ "${1:-}" = "-v" ]]; then
    stream=1
    shift
  fi
  [ $# -eq 0 ] && {
    echo "usage: $0 [-v] <preset> [more...]" >&2
    exit 2
  }
  PRESETS=("$@")
else
  # shellcheck disable=SC2206
  PRESETS=(${DEFAULT_PRESETS})
fi

# run_with_capture <preset> <step> -- <cmd...>
# Captures output to build/<preset>/logs/<step>.log; prints a one-line
# summary on success, the failing step's tail + log path on failure.
# The log directory is created automatically.
# -v flag (set above, checked inside) streams live output.
run_with_capture() {
  local preset="$1" step="$2"; shift 2
  # Drop the -- separator.
  [[ "${1:-}" == "--" ]] && shift
  local log="build/${preset}/logs/${step}.log"
  mkdir -p "build/${preset}/logs"
  : > "$log"   # truncate — run-to-run; matches run-capture.sh
  if [[ "$stream" -eq 1 ]]; then
    "$@" 2>&1 | tee "$log"
    rc=${PIPESTATUS[0]}
  else
    "$@" >>"$log" 2>&1
    rc=$?
  fi
  if [[ $rc -eq 0 ]]; then
    echo "OK [$preset] $step"
  else
    echo "FAILED [$preset] $step" >&2
    echo "--- last 60 lines of ${log} ---"
    tail -n 60 "$log"
    echo "--- end ---"
    echo "log at ${log}" >&2
  fi
  return $rc
}

fail=0
for preset in "${PRESETS[@]}"; do
  dir="build/${preset}"
  run_with_capture "$preset" configure -- cmake --preset "${preset}" || {
    echo "--- configure FAILED (${preset})"; fail=1; continue
  }
  run_with_capture "$preset" build -- cmake --build "${dir}" || {
    echo "--- build FAILED (${preset})"; fail=1; continue
  }
  run_with_capture "$preset" ctest -- ctest --test-dir "${dir}" --output-on-failure || {
    echo "--- tests FAILED (${preset})"; fail=1; continue
  }
done

if [ "$fail" = "0" ]; then
  echo ""
  echo "check-all: ALL GREEN"
  exit 0
fi
echo ""
echo "check-all: FAILURES PRESENT"
exit 1
