#!/usr/bin/env bash
# scripts/docker-check.sh — Tier-1 Linux build/test lane (docs/build.md §8).
#
# Constraint (AGENTS.md rule 8): container runs happen ONLY through this
# script — never by calling `docker` directly. The role split is:
#   - the agent generates/edits code (Dockerfile, scripts, sources);
#   - the owner/operator runs this script on their machine;
#   - on failure the error is reported verbatim and passed through here —
#     nothing is paraphrased or swallowed;
#   - system-level fixes (Docker daemon, drivers, host config) belong to
#     the owner/sysadmin.
#
# Idempotent: `docker build` below is layer-cached — after the first
# provisioning run it costs ~1 second unless scripts/bootstrap.sh changed.
#
# What it validates inside the fedora-dev image:
#   1. configure  - cmake --preset linux-arm64 (first run fetches pinned JUCE)
#   2. build      - all plugin formats (VST3 + CLAP) for Linux aarch64
#   3. ctest      - Catch2 DSP unit tests
#   4. the same three steps under the sanitizer preset
#      (linux-arm64-sanitize: ASan + UBSan + LeakSanitizer, test lane only)
#
# What it can never validate: DAW load & audio behaviour on the real
# machine — that is Tier 2, by design (build.md §8).
set -euo pipefail

IMAGE=makegain-fedora-dev:latest
ROOT="$(cd "$(dirname "$0")/.." && pwd)"   # repo root, script-location independent

cd "$ROOT"

echo "==> [1/2] building image (layer-cached; slow only on first provisioning)"
docker build --tag "$IMAGE" -f docker/fedora-dev.Dockerfile .

run_lane() {
  preset="$1"
  echo "==> [2/2] lane: ${preset}"
  # The preset name is interpolated by the OUTER shell (local variable),
  # then the whole block runs inside the container with bash -u -e.
  docker run --rm --tty \
    --volume "$ROOT":/work --workdir /work \
    "$IMAGE" bash -uec "
      echo \"--- configure: ${preset}\"
      cmake --preset ${preset}
      echo \"--- build: ${preset}\"
      cmake --build build/${preset}
      echo \"--- ctest: ${preset}\"
      ctest --test-dir build/${preset} --output-on-failure
    "
}

run_lane linux-arm64
run_lane linux-arm64-sanitize

echo
echo "ALL GREEN — Linux arm64 configure+build+ctest passed in-container."
echo "(Host-load validation stays Tier 2: bare-metal Asahi, build.md §8.)"
