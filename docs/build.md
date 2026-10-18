# Build guide

Everything needed to go from a fresh machine to a working MakeGain in your
DAW. Nothing here assumes pre-installed tools.

**Validation status (2026-10-18, end of the M0 PR):**

| Check | macOS 27/arm64 | Fedora Asahi aarch64 |
|---|---|---|
| bootstrap | ✅ passed | ✅ ran (deps list was incomplete → fix landed, merged in PR #1) |
| cmake configure (juceaide, deps fetch) | ✅ | ✅ **in container** (`fedora:44`, gcc 16.2.1) — pins resolved (JUCE 8.0.15 / clap-ext `7adee3a` / Catch2 v3.16.1), juceaide built+tested |
| build all formats | ✅ VST3/CLAP/AU | ✅ — VST3+CLAP for aarch64-linux, both lanes, in container on the pinned fedora:44 toolchain |
| ctest (incl. sanitizers) | ✅ | ✅ both lanes pass in container (sanitizer: ASan/UBSan/LSan) |
| load in DAW | ✅ Reaper (flawless) | ✅ **bare-metal, 2026-10-18**: Reaper VST3+CLAP, Ardour 9.8 VST3 — artifacts pen-transferred per §8.1; compatibility guard passed (Asahi gcc = container gcc `16.2.1-2` to the patch digit, so transfer = build equality). ⏳ Ardour + CLAP: not detected in 9.8 — parked to next milestone, diagnosis checklist in §6 (below) |

> Therefore: anything tagged on this PR's merge describes **macOS-side
> validation only**. Linux compile/run validation is the explicit job of
> the `m0-linux-inner-loop` branch (Docker inner loop, §8) plus a final
> native Asahi session.

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
10. **Ardour does not see the CLAP while Reaper does** (observed 2026-10-18,
    deferred to the next milestone by owner): Reaper loading the `.clap`
    proves the file itself is valid, so this is host-side. Diagnosis
    checklist, cheapest first: (1) also copy to `~/.local/share/clap`
    (Xdg data dir is Ardour's preferred CLAP path; nothing creates it);
    (2) Window → Plugin Manager → *Discover New/Updated*, then **restart
    Ardour** (new plugins can stay unusable until restart); (3) check
    Edit → Preferences → Plugins for a per-type CLAP switch, and read any
    *Error* row in Plugin Manager — a concrete error text is the most
    valuable finding; (4) name the build source (ardour.org prebuilt =
    full support; Fedora package / Flathub = possibly compiled without
    CLAP or sandboxed away from `~/.clap`).

## 7. Dependency pins (what fetched at configure)

| Pin | Value | Why this pin |
|---|---|---|
| JUCE | `8.0.15` | latest 8.0.x; JUCE 9 (9.0.4) deliberately deferred to its own milestone (decision: m0-plan) |
| clap-juce-extensions | commit `7adee3a` | newest main HEAD compatible with JUCE 8 |
| Catch2 | `v3.16.1` | latest release at pin time |

Pins are reviewed manually; commits to `cmake/deps-pins.cmake` are
deliberate, not incidental (AGENTS.md policy).

## 8. Linux compile/test inner loop on the Mac (Docker)

Rebooting into Fedora Asahi for every compile/test cycle is too slow. This
branch therefore establishes a **two-tier loop**:

| Tier | What it covers | Frequency |
|---|---|---|
| **Tier 1 — Docker on the mac** | Linux configure + build + ctest (incl. sanitizer lane) inside an arm64 Fedora container *natively* on Apple Silicon — no emulation | every commit |
| **Tier 2 — bare-metal Asahi** | the oracle for what Tier 1 cannot see: bootstrap on real hardware, DAW load (Reaper/Ardour), PipeWire audio | per milestone |

**Prerequisite:** any arm64-capable Docker environment (OrbStack, Docker
Desktop, colima — any is fine; this lane uses plain `docker build`/`docker
run`, nothing vendor-specific).

**Why a *stock* Fedora container is a faithful stand-in for the Remix**
(checked 2026-10-18 against Asahi's own docs): Fedora Asahi Remix
differs from upstream Fedora only in forked **kernel** (`kernel-16k`),
**mesa** (Apple GPU), `m1n1`/u-boot and audio-userland daemons — none of
which a compile/test lane exercises. Everything we touch (gcc, cmake,
ninja, freetype/fontconfig/X11/GL/ALSA *headers*) is plain Fedora
packaging. Version-skew guard: pin the container to the Remix's release
(`fedora:44`, not `:latest`), and once per machine provisioning compare
`gcc --version` inside the container vs. on the Asahi box.

**Manual lane (works today; the `scripts/docker-check.sh` wrapper +
`docker/fedora-dev.Dockerfile` land in this branch's next commits):**

```sh
# once: provisioned image (same dnf package list as scripts/bootstrap.sh)
docker build -t makegain-fedora-dev - < docker/fedora-dev.Dockerfile

# every cycle:
docker run --rm -v "$PWD":/work -w /work makegain-fedora-dev \
  bash -c 'cmake --preset linux-arm64 && cmake --build build/linux-arm64 && ctest --test-dir build/linux-arm64'
```

Sanitizer lane runs the same way with the `MAKEGAIN_SANITIZERS` preset
flag (§3). What the container can never answer: *does the plugin load and
behave inside a real DAW* — that stays Tier 2, by design.

### 8.1 Artifact transfer lane (container → pen → bare metal)

The plugin artifacts from the container lane are directly runable on the
Fedora Asahi box, because the Remix's *userland* is upstream Fedora 44 —
the same release the container is pinned to (§8). So instead of
re-provisioning + rebuilding during a rare Tier-2 session, carry the
binaries over:

```sh
cp -R build/linux-arm64/MakeGain_artefacts/Release/VST3 \
      build/linux-arm64/MakeGain_artefacts/Release/CLAP <pen>
# on Asahi: hosts auto-scan ~/.vst3 and ~/.clap
```

Caveats that keep this honest (ruling 2026-10-18):

1. **One-time compatibility guard:** compare `gcc --version` container vs.
   Asahi before trusting a transfer. If the Asahi install is an *older*
   Fedora release, a newer-built binary fails loudly at load
   (`version 'GLIBC_x' not found`) — fix by re-pinning the image to that
   release tag, not by silent fudging.
2. **Diagnosis purity:** a misbehaving plugin on bare metal is first
   suspected to be the cross-built binary. Discipline: rebuild natively on
   Asahi *before* debugging code. The native rebuild is minutes; the
   bootstrap story is already correct.
3. **Staleness naming:** point-in-time artifacts must carry their truth —
   name the copied folder `makegain-linux-arts-<short-sha>` so a later
   ambiguity ("which build is on this pen?") is answerable in one glance.
4. **Scope:** owner-to-owner transfer between his own machines only. The
   repo's distribution promise stays FR-18's *clone and build* — binaries
   never leave the owner's ecosystem as project deliverables.

### 8.2 Cross-arch lane: amd64 under Rosetta (`--x64`)

The `linux-x64` / `linux-x64-sanitize` CMake presets exist, but no native
amd64 machine is part of the validation matrix (FR scope is arm64-first).
To still exercise the DSP tests against genuinely different codegen +
libm — the arch-invariance question — `scripts/docker-check.sh` accepts
`--x64` (decision recorded in `docs/m1-plan.md`, 2026-10-18):

```sh
scripts/docker-check.sh --x64
```

- builds the same `docker/fedora-dev.Dockerfile` with `--platform
  linux/amd64`, tagged `makegain-fedora-dev:x64` (separate tag and layer
  cache from the arm64 image — switching lanes never thrashes cache);
- runs the `linux-x64` + `linux-x64-sanitize` presets and ctest inside;
- on the owner's mac this executes under Docker Desktop's Rosetta
  emulation — enable **"Use Rosetta for x86_64/amd64 emulation on Apple
  Silicon"** in Docker Desktop settings, or it silently falls back to
  QEMU (much slower, occasionally flaky).

What this lane claims, and what it doesn't: the same source compiles
clean and every assertion passes under amd64 gcc/glibc — evidence for
the arch invariance of the DSP kernels (margins absorb libm/codegen
variance). It is **not** evidence about real amd64 hardware or audio
behavior; Tier 2 on bare metal remains the oracle for that. Cadence:
on-demand / pre-PR, not per-commit — first provisioning is slow
(~30–90 min under emulation), later runs are layer-cached.
