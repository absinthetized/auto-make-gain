# M0 Implementation Plan — "walking skeleton"

> v1 — 2026-10-17. Commit policy per `AGENTS.md`: commit breakdown agreed
> *before* coding; each checkpoint is a moment the owner acts (manual test
> ✅ or review), then we proceed to the next commit.

## Goal (M0 exit, from FR-18/TS §6)

A `git clone` of this repo, after following `docs/build.md` *and only that*,
produces a building `MakeGain` in all three formats on macOS arm64 —
loadable in Reaper — and, on Fedora Asahi aarch64, completes bootstrap +
dependency-correct configure (build/test/host-load steps explicitly moved
out of M0 scope by owner ruling 2026-10-18; see "Scope note" below).
No DSP yet.

> **Scope note (owner, 2026-10-18):** cross-compilation and cross-execution
> validation (Linux build → DAW execution loop) is **not** an M0 deliverable
> and does not come with this PR's merge. It becomes the follow-up branch's
> mission (`m0-linux-inner-loop`: Docker-based build/test loop on the mac,
> then a native Asahi host session). Tag/release notes are to state
> macOS-validation-only until that branch lands.

## Dependency / toolchain story ("download stuff or tell what to download")

- **`scripts/bootstrap.sh`** — the only entrypoint a fresh machine needs:
  - **macOS**: checks for Xcode CLT (`xcode-select --install` if absent);
    if `cmake`/`ninja` are missing, downloads official binaries (cmake.org
    GitHub releases, ninja GitHub releases) into `~/.local/bin` and prints
    the PATH hint. Homebrew is **not** required.
  - **Fedora (dnf)**: prints — and installs with `--yes` — the concrete
    package list: `gcc-c++ cmake ninja-build alsa-lib-devel git`.
  - **Debian/Ubuntu (apt)** and generic Linux: prints exact equivalents
    (best-effort; Asahi/Fedora is the supported path per TS §6).
  - Ends by printing the next command: `cmake --preset <os>`.
- **JUCE, clap-juce-extensions, Catch2** are fetched by CMake at first
  configure (FetchContent, pinned versions below) — no submodules, no
  vendoring.

Pins (record, to be set precisely at commit 2):
| Dep | Pin | Note |
|---|---|---|
| JUCE | latest `8.0.x` tag (≈ 8.0.16) | JUCE 9 (9.0.4, Oct 2026) deliberately NOT adopted at M0 — upgrade as its own milestone later |
| clap-juce-extensions | latest commit compatible with JUCE 8 | pin by commit hash |
| Catch2 | latest v3 tag | |

## Commit breakdown

| # | Commit | Contents | Checkpoint (owner action) |
|---|---|---|---|
| C1 | `build: repo hygiene + developer bootstrap` | `.gitignore`; `scripts/bootstrap.sh`; one-line README pointer | ✅ mac: passed 2026-10-17 · **Fedora: PENDING (owner reboots to cold state)** |
| C2 | `build: CMake skeleton + fetch pins (JUCE/clap/Catch2)` | root `CMakeLists.txt`, `cmake/*`, `CMakePresets.json` (macos / linux-arm64 / linux-x64), `cmake/VerifyPins.cmake` | ✅ mac configure self-test passed (361 s, 2026-10-17) · Fedora pending cold-reboot checkpoint |
| C3 | `plugin: passthrough stub + editor shell (VST3)` | `src/plugin/makegain_processor.{h,cpp}` (silence-preserves passthrough, no params yet), `src/ui/editor.{h,cpp}` (empty window), VST3 target | ✓ mac build: `MakeGain.vst3` produced + auto-installed (2026-10-17) |
| C4 | `plugin: CLAP + AU format targets` | clap-juce-extensions target, AU target (mac only) | ✅ **owner PASSED 2026-10-17**: all three formats load in Reaper (flawless) |
| C5 | `tests: Catch2 wiring + sanitizer presets` | `tests/dsp/*` (one placeholder test that the empty framework passes), `scripts/check-all.sh` (builds all presets + runs ctest; the local stand-in for CI) | ✓ mac: ALL GREEN (default + ASan/UBSan lane) · Fedora pending cold-reboot checkpoint |
| C6 | `docs: developer build guide (macOS / Fedora Asahi)` | `docs/build.md`: exact OS-by-OS commands, first-run JUCE download notes, ad-hoc mac code-sign/Gatekeeper notes, "load in Reaper/Ardour" quick test, troubleshooting section (doc-only skeleton at first, grown from C1–C5 friction) | ✅ fresh `git clone` in a new dir → follow doc → Reaper accepts the plugin, with zero knowledge beyond the doc |
| C7 | `milestone: v0.1.0` (renamed 2026-10-17, see release-model.md) | CHANGELOG.md execution + release notes; **re-scoped per owner 2026-10-18**: tag gate moved from "Asahi host verification" to "owner's disposition post-merge (macOS-validated release notes mandatory); Linux validation = next branch (m0-linux-inner-loop) + native Asahi session" | ✅ owner approves the tag itself |

## Explicit non-goals of M0

- No CI on GitHub yet (backlog §10 "CI enablement" — `check-all.sh`
  simulates it locally until then).
- No parameters, no FR logic, no K-weighting — C3's processor is a
  transparency shell so C1–C4 never carry *both* infra risk and DSP risk.
- No installers (FR-18 v0: clone+build only).

## Friction log

Every manual checkpoint friction (missing package? doc gap? build flag?)
goes into C6's troubleshooting section as we hit it — the doc absorbs hard
learnings, not just theory.
