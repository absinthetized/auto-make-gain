#!/usr/bin/env bash
# scripts/stage-linux-artifacts.sh — the ONLY sanctioned way Tier-2
# artifacts reach the pen / the Asahi box (owner ruling, doc/ai/backlog.md).
#
# WHAT: copies the lane-built Linux plugin bundles from build/<preset>/
# into a desktop folder named by the commit sha, refuses stale or dirty
# builds, prunes older staging folders, and writes a MANIFEST.txt +
# SHA256SUMS so the payload can be verified ON THE TARGET with no repo.
#
# WHY: hand-copying once shipped the wrong generation of binaries to a
# Tier-2 test (the folder said one thing, the .so embedded another).
# This script makes provenance part of the copy, not part of memory.
#
# USAGE:
#   scripts/stage-linux-artifacts.sh            # arm64 lane artifacts (Asahi)
#   scripts/stage-linux-artifacts.sh --x64      # amd64 lane artifacts
#   scripts/stage-linux-artifacts.sh --out DIR  # alternate destination
#
# REQUIREMENTS for a clean staging:
#   - HEAD is clean (no tracked modifications): a dirty tree cannot have
#     a trustworthy stamp, and the whole point is trustworthy artifacts;
#   - the lane artifacts exist and their embedded build stamp equals the
#     short sha of HEAD. Mismatch = a lane ran on different source: run
#     the lane again, don't hand-fix.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PRESET_DIR="build/linux-arm64"
ARCH_DIR="aarch64-linux"
ARCH_LABEL="aarch64-linux (Asahi / arm64 lane)"
OUT_DIR="${HOME}/Desktop"
while [ $# -gt 0 ]; do
    case "$1" in
        --x64)
            PRESET_DIR="build/linux-x64"
            ARCH_DIR="x86_64-linux"
            ARCH_LABEL="x86_64-linux (amd64 lane, Rosetta - NOT for Asahi)"
            ;;
        --out)
            shift
            OUT_DIR="${1:-}"
            ;;
        *)
            echo "usage: $0 [--x64] [--out DIR]" >&2
            exit 2
            ;;
    esac
    shift
done

# --- provenance preconditions ----------------------------------------------
sha=$(git rev-parse --short HEAD)
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "stage: REFUSED - tracked modifications present; commit first."
    echo "       (an artifact without a trustworthy stamp is the bug this"
    echo "        script exists to prevent)"
    exit 1
fi

vst3_so="$PRESET_DIR/MakeGain_artefacts/Release/VST3/MakeGain.vst3/Contents/$ARCH_DIR/MakeGain.so"
clap_so="$PRESET_DIR/MakeGain_artefacts/Release/CLAP/MakeGain.clap"

for f in "$vst3_so" "$clap_so"; do
    if [ ! -e "$f" ]; then
        echo "stage: REFUSED - lane artifact missing: $f"
        echo "       run scripts/docker-check.sh ${PRESET_DIR##*linux-} first"
        exit 1
    fi
done

# The stamp literal is compiled in as a standalone string (CMake builds
# -DMAKEGAIN_BUILD_SHA="<sha>"); an exact-line match proves THIS .so was
# configured from THIS commit.
for f in "$vst3_so" "$clap_so"; do
    if ! strings "$f" | grep -Fxq "$sha"; then
        echo "stage: REFUSED - $f does not embed stamp '$sha'."
        echo "       The lane ran on other source. Re-run the lane at HEAD."
        exit 1
    fi
done

# --- payload ----------------------------------------------------------------
version=$(grep -m1 -oE 'VERSION [0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | awk '{print $2}')
juce=$(strings "$vst3_so" | grep -m1 -oE 'JUCE v[0-9]+\.[0-9]+\.[0-9]+' || echo "JUCE unknown")

dest="$OUT_DIR/makegain-linux-arts-$sha"
rm -rf "$dest"
mkdir -p "$dest"
cp -R "$PRESET_DIR/MakeGain_artefacts/Release/VST3/MakeGain.vst3" "$dest"/
cp -R "$clap_so" "$dest"/
cp -R jsfx "$dest"/

# One staging generation at a time: a stale sibling folder is exactly how
# the wrong binaries got tested.
if [ -d "$OUT_DIR" ]; then
    for d in "$OUT_DIR"/makegain-linux-arts-*; do
        if [ -d "$d" ] && [ "$d" != "$dest" ]; then
            rm -rf "$d"
        fi
    done
fi

# --- verification data that travels with the folder --------------------------
cat > "$dest/MANIFEST.txt" <<EOF
MakeGain Linux artifacts
========================
version:  $version
commit:   $sha
build:    $juce, $ARCH_LABEL, lane-produced
staged:   $(date -u '+%Y-%m-%d %H:%M UTC')
source:   build/${PRESET_DIR#build/}

Verify ON THE TARGET (no repo needed):
  sha256sum -c SHA256SUMS
  strings MakeGain.vst3/Contents/$ARCH_DIR/MakeGain.so | grep -F "$sha"
  strings MakeGain.clap | grep -F "$sha"

Both greps must print the commit line above. A different line = wrong
folder or wrong copy: do not test it; restage from the lane.
EOF

(cd "$dest" && find . -type f ! -name SHA256SUMS -print0 | xargs -0 shasum -a 256 > SHA256SUMS)

echo "stage: OK - $dest"
echo "       version $version, commit $sha, $juce"
echo "       next: copy the folder to the pen; on the target run"
echo "       'sha256sum -c SHA256SUMS' and the two strings checks (MANIFEST)."
