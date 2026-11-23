#!/usr/bin/env bash
# scripts/check-version.sh — version-truth gate (tier 1, runs first).
#
# WHAT: compares the three places the version number appears —
#   1. CMakeLists.txt  project(VERSION x.y.z)   ← the single source of
#      truth (JUCE derives JucePlugin_VersionString + bundle metadata
#      from it, so the GUI/binary follow automatically);
#   2. CHANGELOG.md    top "## vX.Y.Z" heading  ← the release section;
#   3. the git tag on HEAD, when there is one   ← the owner's cut.
#
# WHEN: invoked by scripts/docker-check.sh before any container starts
# (fail fast: no 20-minute lane on a version mismatch), and by hand
# before a tag.
#
# SEVERITY: a tagged HEAD is a release — any mismatch is an ERROR
# (exit 1). An untagged HEAD is normal development (including the
# release PR that bumps the version before the tag exists) — mismatches
# are WARNINGS (exit 0) so drift is visible without blocking work.
#
# This script is not a bumper: bumps stay deliberate one-line edits the
# owner reviews. See doc/ai/backlog.md for the design ruling.

set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"

# 1. CMake version. First three-component "VERSION x.y.z" after the
#    project() call — cmake_minimum_required(VERSION 3.24) is two-
#    component and does not match this pattern.
cmake_ver=$(grep -m1 -oE 'VERSION [0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt \
            | awk '{print $2}' || true)
if [ -z "${cmake_ver:-}" ]; then
    echo "VERSION GATE: FAIL — cannot read project(VERSION) from CMakeLists.txt"
    exit 1
fi

# 2. Top CHANGELOG section heading ("## v0.2.1").
changelog_ver=$(grep -m1 -oE '^## v[0-9]+\.[0-9]+\.[0-9]+' CHANGELOG.md \
                | sed 's/^## v//' || true)

# 3. Tag state. --exact-match succeeds only when HEAD itself is tagged.
head_tag=$(git describe --tags --exact-match 2>/dev/null || true)
last_tag=$(git describe --tags --abbrev=0 2>/dev/null || true)
head_tag=${head_tag#v}
last_tag=${last_tag#v}

fail=0

if [ -n "$head_tag" ]; then
    # Release state: everything must agree.
    if [ "$cmake_ver" != "$head_tag" ]; then
        echo "VERSION GATE: FAIL — tag v$head_tag but CMakeLists says $cmake_ver"
        fail=1
    fi
    if [ -n "$changelog_ver" ] && [ "$changelog_ver" != "$head_tag" ]; then
        echo "VERSION GATE: FAIL — tag v$head_tag but CHANGELOG top section is v$changelog_ver"
        fail=1
    fi
    [ "$fail" -eq 0 ] && echo "VERSION GATE: OK — cmake $cmake_ver = changelog v${changelog_ver:-?} = tag v$head_tag"
else
    # Development state: report, never block.
    if [ -n "$changelog_ver" ] && [ "$changelog_ver" != "$cmake_ver" ]; then
        echo "VERSION GATE: WARN — CMake $cmake_ver but CHANGELOG top section is v$changelog_ver (bump pending?)"
    fi
    echo "VERSION GATE: OK — cmake $cmake_ver, changelog v${changelog_ver:-?}, untagged${last_tag:+ (last tag v$last_tag)}"
fi

exit "$fail"
