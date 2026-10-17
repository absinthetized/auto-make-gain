#!/usr/bin/env bash
# MakeGain local CI stand-in (C5 per m0-plan) — the same checks the future
# GitHub Actions matrix will run, locally:
#   1. configure + build the platform preset
#   2. configure + build the sanitized test preset
#   3. run ctest on both
# Usage: ./scripts/check-all.sh              (auto-detects platform)
#        ./scripts/check-all.sh macos ...    (explicit presets, space-separated)
set -u

platform="$(uname -s)-$(uname -m)"
case "$platform" in
  Darwin-arm64)  DEFAULT_PRESETS="macos macos-sanitize" ;;
  Linux-aarch64) DEFAULT_PRESETS="linux-arm64 linux-arm64-sanitize" ;;
  Linux-x86_64)  DEFAULT_PRESETS="linux-x64 linux-x64-sanitize" ;;
  *) echo "unknown platform '$platform'; pass presets explicitly:" >&2
     echo "  $0 <preset> [-sanitize-variant] [more...]" >&2; exit 2 ;;
esac

if [ $# -gt 0 ]; then
  PRESETS=("$@")
else
  # shellcheck disable=SC2206
  PRESETS=(${DEFAULT_PRESETS})
fi

fail=0
for preset in "${PRESETS[@]}"; do
  dir="build/${preset}"
  echo ""
  echo "=== [${preset}] configure ==="
  if ! cmake --preset "${preset}"; then
    echo "--- configure FAILED (${preset})"; fail=1; continue
  fi
  echo "=== [${preset}] build ==="
  if ! cmake --build "${dir}"; then
    echo "--- build FAILED (${preset})"; fail=1; continue
  fi
  echo "=== [${preset}] ctest ==="
  if ! ctest --test-dir "${dir}" --output-on-failure; then
    echo "--- tests FAILED (${preset})"; fail=1; continue
  fi
  echo "=== [${preset}] OK ==="
done

if [ "$fail" = "0" ]; then
  echo ""
  echo "check-all: ALL GREEN"
  exit 0
fi
echo ""
echo "check-all: FAILURES PRESENT"
exit 1
