# Build guide

Everything needed to go from a fresh machine to a working MakeGain in
your DAW. Nothing here assumes pre-installed tools.

**How this build is validated:** the DSP is framework-free and unit-
tested in its own lanes; the plugin builds for all three formats. Every
Linux compile/test cycle runs through the committed container lanes of
[§8](#8-linux-compiletest-inner-loop-on-the-mac-docker); DAW loading
behavior is verified on real hosts (see [§4](#4-quick-host-verification)).

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
| macOS | Xcode CLT (auto-triggers), cmake/ninja downloaded to `~/.local/bin` if absent | **Homebrew is NOT required.** If you *do* have brew and cmake, existing installs are used as-is |
| Fedora / Asahi (dnf) | `gcc-c++ cmake ninja-build alsa-lib-devel freetype-devel fontconfig-devel mesa-libGL-devel libX*n-devel family gtk3-devel` | note §6.8 (`juceaide` needs FreeType + X11 dev headers); official JUCE dep list linked below |
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

(`COPY_PLUGIN_AFTER_BUILD` is ON in-tree — the build steps copy into
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

The pipeline's unit for "it works in a host" is the ear + the panel:

- Load the freshly built formats in Reaper (macOS, or Asahi via the
  transfer lane of §8.1); check the editor opens, the status band reads
  standby (gray), and with `Active` engaged + a dry tap routed (see
  [`usage.md`](usage.md)) the applied-gain readout settles near 0 dB on
  a transparent gain stage.
- Re-check by ear after any substantial processor change; Host-specific
  quirks (discovery problems, exec bits) live at the end of
  [`usage.md`](usage.md).

## 5. Handling received binaries — quarantine, signing

If artifacts came from another machine rather than this build, see
[`install.md`](install.md) — including the macOS Gatekeeper/quarantine
step for unsigned binaries and the exec-bit restore on transfer
filesystems. (Signing/notarization is a later-milestone goal.)

## 6. Troubleshooting (the friction this setup actually produces)

Build-environment issues belong here; **code-level gotchas** live in
[`CONTRIBUTING.md`](../../CONTRIBUTING.md) ("coding gotchas"), and
**host discovery problems** in [`usage.md`](usage.md).

1. **"first configure takes forever"** — JUCE is a large shallow clone +
   juceaide build; >15 min on slow links is documented expectation. On
   re-configure it is a no-op.
2. **Metal deprecation warnings on macOS 27** — `MTLResourceStorageModeManaged is deprecated` from the
   SDK matrix vs JUCE 8.0.15. Harmless and cosmetic; zip them when filing build issues.
3. **clap-juce-extensions: the link target is `clap_juce_extensions`** — no
   `clap-juce-extensions::clap-juce-extensions` ALIAS exists; linking the
   alias name as documented in older forums fails at configure. Also: do
   **not** add `CLAP` to juce_add_plugin FORMATS — the extension lays the
   format over the base target itself.
4. **`dnf install` says nothing to do** on a partially-provisioned Asahi —
   bootstrap is idempotent; re-run to verify.
5. **"jmessage: ft2build.h / X11/Xlib.h not found while building juceaide"**:
   JUCE's configure-time helper `juceaide` is itself built as a headless
   JUCE app, so even plugin-only targets need the **FreeType + X11 dev
   headers** Linux launch expects — pkg-config probes for
   freetype2/fontconfig/gl/curl/webkit/gtk are normal *warnings* and
   non-fatal once the flags in CMakeLists disable the correlative
   modules (`JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, `JUCE_JACK=0`,
   `JUCE_PLUGINHOST_LADSPA=0`). Canonical reference:
   <https://github.com/juce-framework/JUCE/blob/develop/docs/Linux%20Dependencies.md> —
   consult it for every new platform-dependent surprise before
   improvising package lists.
6. **Host discovery problems after a working build** — e.g. "Ardour
   does not see the CLAP while Reaper does" — are documented in the
   host-quirks section at the end of [`usage.md`](usage.md).

## 7. Dependency pins (what fetches at configure)

| Pin | Value | Why this pin |
|---|---|---|
| JUCE | `8.0.15` | latest 8.0.x; JUCE 9 deliberately deferred to its own milestone (decision record) |
| clap-juce-extensions | commit `7adee3a` | newest main HEAD compatible with JUCE 8 |
| Catch2 | `v3.16.1` | latest release at pin time |

Pins are reviewed manually; commits to `cmake/deps-pins.cmake` are
deliberate, not incidental (AGENTS.md policy).

### How a pin moves

Pins live in exactly one file (`cmake/deps-pins.cmake`) and never move
as a side effect of ordinary work. A pin bump is its own commit, says
*why* it moved, and passes the same green gates as code changes — the
Docker lanes of §8 exercise the exact fetched versions on Linux, so a
bad pin shows up loudly, not silently.

### Where dependency knowledge lives

Two places, both kept executable so they cannot rot:

1. **Third-party pins** — the table above (what fetches at configure
   time, per `FetchContent`/pin file).
2. **Development packages** — `scripts/bootstrap.sh` is the single
   source for per-distro package lists (macOS Homebrew-Xcode path,
   Fedora/Debian/Arch). `docker/fedora-dev.Dockerfile` **executes
   that same script** inside the image build, so the container cannot
   drift from the script's list by construction. The sanitizer
   runtimes are explicit entries in it — the sanitize lane links
   against them at test time.

### The Linux header map

The **authority for which Linux dev headers JUCE actually requires**
is JUCE's own upstream document,
<https://github.com/juce-framework/JUCE/blob/develop/docs/Linux%20Dependencies.md>.
It is external to this repo on purpose: comments in `bootstrap.sh`,
`CMakeLists.txt` and this guide that cite "the Linux dependencies doc"
mean *that upstream file* — never a repo-local copy or summary, which
would drift from JUCE releases. Consult it first for every
new platform-dependent surprise, before improvising package lists.

### What deliberately does not exist

There is **no in-repo `dependencies.md`**. A third list (after this
section and `bootstrap.sh`) had two sources to keep in sync and no
compilation pressure to stay honest — the decision register
(`doc/ai/decisions.md`) records the ruling. If you add a dependency,
its pin goes in `deps-pins.cmake` and its package in `bootstrap.sh`;
docs follow those two, never the other way round.

## 8. Linux compile/test inner loop on the Mac (Docker)

Rebooting into Fedora Asahi for every compile/test cycle is too slow.
The project therefore uses a **two-tier loop**:

| Tier | What it covers | Frequency |
|---|---|---|
| **Tier 1 — Docker on the mac, arm64 lane** | Linux configure + build + ctest (incl. sanitizer lane) inside an arm64 Fedora container *natively* on Apple Silicon — no emulation | every commit |
| **Tier 1 — Docker on the mac, amd64 lane (`--x64`)** | same inside an amd64 container — runs under Rosetta emulation on Apple Silicon (§8.2; exercise of DSP kernels against different codegen/libm) | on-demand / pre-PR (slow: first provisioning ~30–90 min, later runs layer-cached) |
| **Tier 2 — bare-metal Asahi** | the oracle for what Tier 1 cannot see: bootstrap on real hardware, DAW load (Reaper/Ardour), PipeWire audio | per milestone |

**Prerequisite:** any arm64-capable Docker environment (OrbStack, Docker
Desktop, colima — any is fine; agents use only the committed
`scripts/docker-check.sh` wrapper, which runs plain `docker build`/`docker run`).

**Why a *stock* Fedora container is a faithful stand-in for the Remix**
(checked against Asahi's own docs): Fedora Asahi Remix differs from
upstream Fedora only in forked **kernel** (`kernel-16k`), **mesa**
(Apple GPU), `m1n1`/u-boot and audio-userland daemons — none of which a
compile/test lane exercises. Everything the lane touches (gcc, cmake,
ninja, freetype/fontconfig/X11/GL/ALSA *headers*) is plain Fedora
packaging. Version-skew guard: the image is pinned to the Remix's
release (`fedora:44`, not `:latest`), and once per machine provisioning
one compares `gcc --version` inside the container vs. on the Asahi box.

**Committed lane:**

```sh
scripts/docker-check.sh         # arm64 native lane
scripts/docker-check.sh --x64   # amd64 under Rosetta (§8.2)
```

`docker/fedora-dev.Dockerfile` provisions the image (same dnf package
list as `scripts/bootstrap.sh`). Sanitizer presets run through the same
wrapper (`§3`). What the container can never answer: *does the plugin
load and behave inside a real DAW* — that stays Tier 2, by design.

### 8.1 Artifact transfer lane (container → pen → bare metal)

Container-built plugin artifacts run directly on the Fedora Asahi box,
because the Remix's *userland* is upstream Fedora 44 — the same release
the container is pinned to (§8). So instead of re-provisioning +
rebuilding during a rare Tier-2 session, carry the binaries over. The
generic install steps (folders, `.so` exec bits, host scanning) are
exactly [`install.md`](install.md). Lane-specific discipline:

1. **Compatibility guard:** compare `gcc --version` container vs.
   Asahi before trusting a transfer. If the Asahi install is an *older*
   Fedora release, a newer-built binary fails loudly at load
   (`version 'GLIBC_x' not found`) — fix by re-pinning the image to
   that release tag, not by silent fudging.
2. **Diagnosis purity:** a misbehaving plugin on bare metal is first
   suspected to be the cross-built binary. Discipline: rebuild natively
   on Asahi *before* debugging code. The native rebuild is minutes; the
   bootstrap story is already correct.
3. **Staleness naming:** point-in-time artifacts must carry their
   truth — name the copied folder `makegain-linux-arts-<short-sha>` so
   "which build is on this pen?" is answerable in one glance.
4. **Scope:** owner-to-owner transfer between his own machines only.
   The repo's distribution promise stays FR-18's *clone and build* —
   binaries never leave the owner's ecosystem as project deliverables.

### 8.2 Cross-arch lane: amd64 under Rosetta (`--x64`)

The `linux-x64` / `linux-x64-sanitize` CMake presets exist, but no
native amd64 machine is part of the validation matrix (FR scope is
arm64-first). To still exercise the DSP tests against genuinely
different codegen + libm — the arch-invariance question —
`scripts/docker-check.sh` accepts `--x64` (rationale in the decision
register of `doc/ai/decisions.md`):

```sh
scripts/docker-check.sh --x64
```

- builds the same `docker/fedora-dev.Dockerfile` with `--platform
  linux/amd64`, tagged `makegain-fedora-dev:x64` (separate tag and
  layer cache from the arm64 image — switching lanes never thrashes
  cache);
- runs the `linux-x64` + `linux-x64-sanitize` presets and ctest inside;
- on the owner's mac this executes under Docker Desktop's Rosetta
  emulation — enable **"Use Rosetta for x86_64/amd64 emulation on
  Apple Silicon"** in Docker Desktop settings, or it silently falls
  back to QEMU (much slower, occasionally flaky).

What this lane claims, and what it doesn't: the same source compiles
clean and every assertion passes under amd64 gcc/glibc — evidence for
the arch invariance of the DSP kernels (margins absorb libm/codegen
variance). It is **not** evidence about real amd64 hardware or audio
behavior; Tier 2 on bare metal remains the oracle for that. Cadence:
on-demand / pre-PR, not per-commit — first provisioning is slow
(~30–90 min under emulation), later runs are layer-cached.
