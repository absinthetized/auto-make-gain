# Codebase tour — for the JUCE-newcomer

> Goal: a musician-programmer who knows C++ but has never seen JUCE (or any
> plugin format) should be able to read this repo and know where things
> happen, *without* prior framework knowledge. Maintained per AGENTS.md
> rule 7 — kept current **at milestone merges** (cadence relaxed by the
> owner 2026-10-19; per-PR currency proved unsustainable). Last updated:
> 2026-10-19 (M1 PR-A + x64 lane merged).

## The one shock: **there is no `main()`**

A plugin is a *library the DAW loads*. Reaper owns the process, the event
loop, the audio devices. Our binary is a dynamic library exposing a tiny
format-specific ABI (VST3, CLAP or AU). JUCE's magic: it ships that ABI
glue for every format, and needs exactly **one function from our code**:

```
createPluginFilter()            ← in src/plugin/MakeGainProcessor.cpp
```

The generated wrapper(s) call this factory once per loaded instance, get
our `MakeGainProcessor` object back, and drive its virtual methods for the
rest of the plugin's life:

```
host (Reaper) ──loads──▶ VST3/CLAP/AU wrapper (JUCE-generated)
                              │ calls createPluginFilter()
                              ▼
                        MakeGainProcessor ──▶ (createEditor) ──▶ MakeGainEditor
                        (audio thread)                        (UI thread)
```

The build system (`juce_add_plugin`) produces one bundle per format from
the *same* compiled processor — that's the whole multi-format payoff.

## File map (M1, PR-A + x64 lane)

| Path | Role |
|---|---|
| `src/plugin/MakeGainProcessor.h/.cpp` | The audio-thread brain + **entry point** (`createPluginFilter`, bottom of the .cpp). Lifecycle contract documented at the top of the `.h`. Still passthrough — the dsp layer below exists and is tested but is **not wired** yet (wiring = M1 PR-B). |
| `src/ui/MakeGainEditor.h/.cpp` | The window. UI thread; disposable; talks to the processor via thread-safe channels (parameters come with PR-B). Bottom label doubles as a build-state check: it names what the loaded binary actually contains. |
| `src/dsp/` | The JUCE-free DSP layer (**landed in PR-A, not yet wired**): `k_weighting.{h,cpp}` — the ITU BS.1770 K-weighting cascade as per-sample-rate biquads (DeMan formulation; the +0.691 dB anchor at ~1 kHz is load-bearing for the LUFS constant); `loudness_meter.{h,cpp}` — the dual-metric rolling window (short-term LUFS + unweighted dB RMS off the same 100 ms sub-block ring). Pure C++17, no framework types, double-precision internals. |
| `tests/dsp/` | The lab: JUCE-free Catch2 v3 unit tests feeding ragged block sizes over these kernels — golden K-weighting anchor, loudness goldens, sliding-window robustness. Wired from `CMakeLists.txt` (`makegain_tests`). |
| `scripts/bootstrap.sh` | Fresh-machine toolchain/dependency setup — the **single dependency source** (package lists per distro; sanitizer runtimes explicit, see `d8c740b`). |
| `scripts/check-all.sh` | CI-stand-in: builds + tests all presets for the current platform. |
| `scripts/docker-check.sh` | Tier-1 Linux verification lanes through containers only (AGENTS rule 8): default = arm64 native; `--x64` = Rosetta-emulated **amd64** (build.md §8.2). |
| `docker/fedora-dev.Dockerfile` | The pinned fedora:44 image both lanes build from — faithful stand-in for the Asahi Remix userland (kernel/mesa only differ). |
| `cmake/deps-pins.cmake` | The pinned third-party versions (JUCE, clap-juce-extensions, Catch2). |

## How the build folds together (CMakeLists.txt orientation)

`juce_add_plugin(...)` is a JUCE-CMake function that creates one "shared
code" target plus a bundle per format in `FORMATS`. The metadata keys
(`PLUGIN_MANUFACTURER_CODE`, `BUNDLE_ID`, …) are what the host eventually
reads in the bundle's manifest — they must stay unique/stable across
builds. `clap_juce_extensions_plugin(...)` from free-audio's extension
layers the CLAP wrapper over the same code (JUCE has no native CLAP).
`COPY_PLUGIN_AFTER_BUILD` drops fresh bundles into your user's plug-in
folders so Reaper sees new builds without manual copying.

## Where the DSP plugs in next (status after PR-A)

The `src/dsp/` layer now exists and carries the FR contract's
measurement half (K-weighting, LUFS/RMS metering — TS §3), unit-tested
green on macOS arm64, Linux arm64 (native) and Linux amd64 (Rosetta —
incl. sanitizers). The *wiring* is the remaining M1 half (PR-B): the
processor will instantiate the meter, tap the blocks, run the gain
computer, and publish meters to the editor. Until that lands the signal
path is honest passthrough — which the editor's status label states.

## Going deeper (official docs)

- JUCE tutorial series: <https://docs.juce.com/master/tutorial_build_gui_application_basic.html>
- AudioProcessor class reference: <https://docs.juce.com/master/classAudioProcessor.html>
- JUCE example plugins shipped in-tree: <https://github.com/juce-framework/JUCE/tree/master/examples>
- CLAP format: <https://cleveraudio.org/>
