#!/usr/bin/env bash
# scripts/docker-check.sh — Tier-1 Linux build/test lane (doc/human/build.md §8).
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
# Usage:
#   scripts/docker-check.sh          # native arm64 lane (default)
#   scripts/docker-check.sh --x64    # amd64 lane, Rosetta-emulated on the
#                                    # Apple Silicon mac (doc/human/build.md §8.2)
#   scripts/docker-check.sh -v       # streams output live while still logging
#   scripts/docker-check.sh -v --x64 # verbose amd64 lane
#
# Idempotent: `docker build` below is layer-cached — after the first
# provisioning run it costs ~1 second unless scripts/bootstrap.sh changed.
# The --x64 image has its own tag and layer cache; its FIRST provisioning
# compiles the whole toolchain workload under emulation (~30-90 min).
#
# What it validates:
#   0. version gate - scripts/check-version.sh, ON THE HOST before any
#      container work (CMake version vs CHANGELOG vs HEAD tag; fail-fast,
#      tagged HEAD is strict)
# Inside the fedora-dev image:
#   1. configure  - cmake --preset <preset> (first run fetches pinned JUCE)
#   2. build      - all plugin formats (VST3 + CLAP)
#   3. ctest      - Catch2 DSP unit tests
#   4. the same three steps under the sanitizer preset
#      (ASan + UBSan + LeakSanitizer, test lane only)
#
# What it can never validate: DAW load & audio behaviour on the real
# machine — that is Tier 2, by design (build.md §8). The --x64 lane adds
# cross-architecture evidence (different codegen + libm against the same
# assertions); it is still an emulated environment, not a claim about
# real amd64 hosts.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"   # repo root, script-location independent

# Single parse loop: -v, --x64, both (any order), empty, or bad flag.
# No arrays so bash 3.2 is still happy.
LANE=""
stream=0
for arg in "$@"; do
  case "$arg" in
    -v)     stream=1 ;;
    --x64)  LANE="--x64" ;;
    *)      echo "usage: $0 [-v] [--x64]" >&2; exit 2 ;;
  esac
done

# Lane selection from parsed flags.
PLATFORM=""
IMAGE=makegain-fedora-dev:latest
ARCH_LABEL="arm64 (native in Docker Desktop's Linux VM)"
case "$LANE" in
  "")     ;;
  --x64)  PLATFORM="linux/amd64"; IMAGE=makegain-fedora-dev:x64
          ARCH_LABEL="amd64 (Rosetta-emulated)" ;;
esac

cd "$ROOT"

# [0/2] Version gate (rule: tier 1 = this script; the gate runs on the
# HOST, before any container work). Rationale: the version is a source-
# tree fact, not an arch fact — checking it inside the container would
# need git ownership workarounds and would waste the whole build on a
# mismatch. A failing release-state check aborts the lane here.
echo "==> [0/2] version gate (scripts/check-version.sh)"
bash scripts/check-version.sh

BUILD_FLAGS=""
RUN_FLAGS=""
if [[ -n "$PLATFORM" ]]; then
  BUILD_FLAGS="--platform $PLATFORM"
  RUN_FLAGS="--platform $PLATFORM"
fi

echo "==> [1/2] building image (layer-cached; slow only on first provisioning)"
docker build $BUILD_FLAGS --tag "$IMAGE" -f docker/fedora-dev.Dockerfile .

# Run check-all.sh inside the container. It auto-detects the correct presets
# from uname, so we don't pass individual preset names — the lane label
# identifies the architecture. VERBOSE is empty or -v; safe to expand directly.
if [[ "$stream" -eq 1 ]]; then
  echo "==> [2/2] lane: ${ARCH_LABEL} (verbose)"
else
  echo "==> [2/2] lane: ${ARCH_LABEL}"
fi
VERBOSE=""
[[ "$stream" -eq 1 ]] && VERBOSE="-v"
docker run --rm --tty $RUN_FLAGS \
  --volume "$ROOT":/work --workdir /work \
  "$IMAGE" bash -euc "scripts/check-all.sh ${VERBOSE}"

echo
echo "ALL GREEN — Linux ${ARCH_LABEL} configure+build+ctest passed in-container."
echo "(Host-load validation stays Tier 2: bare-metal Asahi, build.md §8.)"
