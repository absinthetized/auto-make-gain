# Build guide

Everything needed to go from a fresh machine to a working MakeGain in your
DAW. Nothing here assumes pre-installed tools.

**Validation status (2026-10-18, end of the M0 PR):**

| Check | macOS 27/arm64 | Fedora Asahi aarch64 |
|---|---|---|
| bootstrap | ✅ passed | ✅ ran (deps list was incomplete → fix landed) |
| cmake configure (juceaide, deps fetch) | ✅ | ❌→ 🔁 pending re-run with updated deps list |
| build all formats | ✅ VST3/CLAP/AU | 🔁 pending |
| ctest (incl. sanitizers) | ✅ | 🔁 pending |
| load in DAW | ✅ Reaper (flawless) | ❌ NOT validated — **deliberately out of this PR's scope** (owner ruling: cross-compile & cross-exec validation postponed to the `m0-linux-inner-loop` branch) |

> Therefore: anything tagged on this PR's merge describes **macOS-side
> validation only**. Linux compile/run validation is the explicit job of
> the next branch (Docker inner loop) plus a final native Asahi session.

---

## 1. Clone + bootstrap (the only manual entrypoint)

```sh
git clone <your-fork-url> auto-make-gain
cd auto-make-gain
./scripts/bootstrap.sh            # dry run: checks and *prints* what's missing
./scripts/bootstrap.sh --yes      # let it fix things
```

Platform behavior:

| OS | Toolchain source | Notes |
|---|---|---|
| macOS | Xcode CLT (auto-triggers), cmake/ninja downloaded to `~/.local/bin` if absent | **Homebrew is NOT required.** If you *do* have brew and cmake, existing installs are used as-is (verified on cmake 4.4.4) |
| Fedora / Asahi (dnf) | `gcc-c++ cmake ninja-build alsa-lib-devel freetype-devel fontconfig-devel mesa-libGL-devel libX*n-devel family gtk3-devel` | ⏳ retry after 2026-10-18 lesson (see §6.8); official JUCE dep list linked below |
| Debian/Ubuntu (apt) | `g++ cmake ninja-build libasound2-dev libfreetype-dev libfontconfig1-dev libX11-dev libXcomposite-dev libXcursor-dev libXext-dev libXinerama-dev libXrandr-dev libXrender-dev libglu1-mesa-dev mesa-common-dev` | best-effort per TS §6, official doc scope applies |
| Arch (pacman) | `git gcc cmake ninja alsa-lib freetype2 fontconfig libgl libx11 libxcomposite libxcursor libxext libxinerama libxrandr libxrender` | best-effort |

If Homebrew is detected but cmake/ninja are missing, the script points you
at the `brew install cmake ninja` command rather than silently installing.

## 2. Configure + build

```sh
cmake --preset macos          # or linux-arm64 / linux-x64
cmake --build build/<preset>
```

Artifact locations (Release default):

| Format | Artefact | Auto-installed to |
|---|---|---|
| VST3 | `build/<preset>/MakeGain_artefacts/Release/VST3/MakeGain.vst3` | `~/Library/Audio/Plug-Ins/VST3/` (macOS) |
| CLAP | `.../Release/CLAP/MakeGain.clap` | `~/Library/Audio/Plug-Ins/CLAP/` (macOS) |
| AU | `.../Release/AU/MakeGain.component` | `~/Library/Audio/Plug-Ins/Components/` (macOS) |

(`COPY_PLUGIN_AFTER_BUILD` is ON in-tree at M0 — the build steps copy into
your user's plug-in folders automatically on macOS.)

First configure downloads the pinned dependencies (JUCE 8.0.15,
clap-juce-extensions, Catch2 v3.16.1 — pins live in
`cmake/deps-pins.cmake`).

## 3. Tests

```sh
./scripts/check-all.sh        # default + ASan/UBSan lane, ctest both
```

Sanitizers exist **only** in the test lane (a deliberate decision, TS §7) —
plugin artifacts are always sanitizer-free.

## 4. Quick host verification

- **Reaper (macOS)** — ✅ verified at C4 checkpoint (all three formats,
  transparent passthrough). Re-check after substantial processor changes.
- **Ardour (Asahi)** 🔁 + **Reaper (Asahi)** 🔁 — pending, *not part of
  the M0 PR scope*; validation lane = `m0-linux-inner-loop` branch (Docker
  build/test) followed by a native Asahi session.
- The M0 stub must pass audio unchanged and show the "M0 stub — passthrough"
  editor window.

## 5. Loading on macOS 27 — Gatekeeper and signing

M0 artifacts are **ad-hoc signed** by default. On first load, Gatekeeper may
warn on downloaded (quarantined) binaries — locally built artifacts carry no
quarantine flag, so normally nothing happens. If you got the artifact from
someone else's release, remove quarantine manually:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/MakeGain.vst3
```

Release packaging with proper signing/notarization is a post-M0 milestone
(FR-18).

## 6. Troubleshooting (real friction encountered in this repo's history)

1. **"first configure takes forever"** — JUCE is ~ a large shallow clone +
   juceaide build. Documented expectation: >15 min on slow links (it was
   6 min on the reference Mac). Nothing is wrong; on re-configure it is a
   no-op.
2. **Metal deprecation warnings on macOS 27** — Apple's own SDK matrices vs
   JUCE 8.0.15 emit `MTLResourceStorageModeManaged is deprecated` warnings.
   Harmless and cosmetic at M0; zip them when filing build issues with us.
3. **`-I` is not recursive** — cross-directory C++ includes must spell the
   sub-path (`#include "ui/MakeGainEditor.h"`), never a bare basename, unless
   the file sits at the include-root (`src/`). This bit the very first
   plugin build here — now the convention everywhere.
4. **JUCE abstract-class error names the first unimplemented pure virtual**
   (`getNumPrograms()` etc.): the AudioProcessor needs a full interface
   stub even for a no-parameter plugin. Done once in C3; if you subclass
   again later, copy the stub block.
5. **clap-juce-extensions: the link target is `clap_juce_extensions`** — no
   `clap-juce-extensions::clap-juce-extensions` ALIAS exists; linking the
   alias name as documented in older forums fails at configure. Also: do
   **not** add `CLAP` to juce_add_plugin FORMATS — the extension lays the
   format over the base target itself.
6. **Exact-equality on floats in tests** — the reference toolchain produced
   a value *equal* to the float asserted "not-to-equal": tolerance-based
   CHECKs only; framework-level mocks arrive with real DSP tests.
7. **`dnf install` says nothing to do** on a partially-provisioned Asahi —
   bootstrap is idempotent; re-run to verify.
8. **"jmessage: ft2build.h / X11/Xlib.h not found while building juceaide"**
   (hit on Fedora 2026-10-18): JUCE's configure-time helper `juceaide` is
   itself built as a headless JUCE app, so even plugin-only targets need
   the **FreeType + X11 dev headers** Linux launch expects — pkg-config
   probes for freetype2/fontconfig/gl/curl/webkit/gtk are normal
   *warnings* and non-fatal once the flags in CMakeLists disable the
   correlative modules (`JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`,
   `JUCE_JACK=0`, `JUCE_PLUGINHOST_LADSPA=0`). Canonical reference:
   <https://github.com/juce-framework/JUCE/blob/develop/docs/Linux%20Dependencies.md> —
   the doc we should have consulted first; consult it for every new
   platform-dependent surprise before improvising package lists.
9. **A `.txt` floor of raw logs/fixes at the repo root** — absorbed into
   this guide and removed from the tree (owner-ordered 2026-10-18);
   future incidents follow the same path: incident file → absorb here →
   delete.

## 7. Dependency pins (what fetched at configure)

| Pin | Value | Why this pin |
|---|---|---|
| JUCE | `8.0.15` | latest 8.0.x; JUCE 9 (9.0.4) deliberately deferred to its own milestone (decision: m0-plan) |
| clap-juce-extensions | commit `7adee3a` | newest main HEAD compatible with JUCE 8 |
| Catch2 | `v3.16.1` | latest release at pin time |

Pins are reviewed manually; commits to `cmake/deps-pins.cmake` are
deliberate, not incidental (AGENTS.md policy).
