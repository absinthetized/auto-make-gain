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
# Usage:
#   scripts/docker-check.sh          # native arm64 lane (default)
#   scripts/docker-check.sh --x64    # amd64 lane, Rosetta-emulated on the
#                                    # Apple Silicon mac (docs/build.md §8.2)
#
# Idempotent: `docker build` below is layer-cached — after the first
# provisioning run it costs ~1 second unless scripts/bootstrap.sh changed.
# The --x64 image has its own tag and layer cache; its FIRST provisioning
# compiles the whole toolchain workload under emulation (~30-90 min).
#
# What it validates inside the fedora-dev image:
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

# Lane selection. Plain string flags (no arrays) so the script also runs
# under macOS's stock bash 3.2, where empty-array expansion trips `set -u`.
case "${1:-}" in
  "")
    PLATFORM=""
    IMAGE=makegain-fedora-dev:latest
    PRESETS="linux-arm64 linux-arm64-sanitize"
    ARCH_LABEL="arm64 (native in Docker Desktop's Linux VM)"
    ;;
  --x64)
    PLATFORM="linux/amd64"
    IMAGE=makegain-fedora-dev:x64
    PRESETS="linux-x64 linux-x64-sanitize"
    ARCH_LABEL="amd64 (Rosetta-emulated)"
    ;;
  *)
    echo "usage: $0 [--x64]" >&2
    exit 2
    ;;
esac

cd "$ROOT"

BUILD_FLAGS=""
RUN_FLAGS=""
if [[ -n "$PLATFORM" ]]; then
  BUILD_FLAGS="--platform $PLATFORM"
  RUN_FLAGS="--platform $PLATFORM"
fi

echo "==> [1/2] building image (layer-cached; slow only on first provisioning)"
docker build $BUILD_FLAGS --tag "$IMAGE" -f docker/fedora-dev.Dockerfile .

for preset in $PRESETS; do
  echo "==> [2/2] lane: ${preset}"
  # The preset name is interpolated by the OUTER shell (local variable),
  # then the whole block runs inside the container with bash -u -e.
  docker run --rm --tty $RUN_FLAGS \
    --volume "$ROOT":/work --workdir /work \
    "$IMAGE" bash -uec "
      echo \"--- configure: ${preset}\"
      cmake --preset ${preset}
      echo \"--- build: ${preset}\"
      cmake --build build/${preset}
      echo \"--- ctest: ${preset}\"
      ctest --test-dir build/${preset} --output-on-failure
    "
done

echo
echo "ALL GREEN — Linux ${ARCH_LABEL} configure+build+ctest passed in-container."
echo "(Host-load validation stays Tier 2: bare-metal Asahi, build.md §8.)"
