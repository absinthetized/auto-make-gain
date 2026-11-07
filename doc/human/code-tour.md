# Codebase tour — for the JUCE-newcomer

> Goal: a musician-programmer who knows C++ but has never seen JUCE (or any
> plugin format) should be able to read this repo and know where things
> happen, *without* prior framework knowledge. Maintained per AGENTS.md
> rule 7 — kept current **at milestone merges** (see `doc/ai/decisions.md`).
> Last updated: 2026-11-07 (M1 complete through PR-D + PR-E).

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
                        (audio thread)             │                       (UI thread)
                                                   ▼
                                              MainPanel (the visible child;
                                              its transform = panel zoom)
```

The build system (`juce_add_plugin`) produces one bundle per format from
the *same* compiled processor — that's the whole multi-format payoff.

## File map (M1 complete)

| Path | Role |
|---|---|
| `src/plugin/MakeGainProcessor.h/.cpp` | The audio-thread brain + **entry point** (`createPluginFilter`, bottom of the .cpp). Owns the APVTS parameters (see `createParameterLayout`: Active is the only automatable one; the rest are UI-only by design, including zoom) and the thread-safe status atomics the UI polls. The internal-sidechain AGC (FR-10-A2): wet vs dry aux, per 100 ms slot, ramped by `gain_computer`. |
| `src/ui/MakeGainEditor.h/.cpp` | The window, split by PR-E: `MakeGainEditor` = shell (owns `Timer`@10 Hz + FR-27 zoom: `applyZoom` transforms the **child**, never the editor — JUCE reserves the editor's transform for host scaling); `MakeGainEditor::MainPanel` = child component holding **everything visual** in logical 468×400 space. PR-D's visual grammar lives here: two control skins by control *nature* (modality pill vs on/off checkbox, both drawn in hand-rolled LookAndFeel overrides), the automatable-ring amber mark, dependent greying, the status band (gray/green/red/ice). |
| `src/dsp/` | The JUCE-free DSP layer (wired in PR-B/C): `k_weighting.{h,cpp}` — ITU BS.1770 K-weighting as per-sample-rate biquads (DeMan formulation; the +0.691 dB anchor at ~1 kHz is load-bearing for the LUFS constant); `loudness_meter.{h,cpp}` — dual-metric rolling window (short-term LUFS + unweighted dB RMS off the same 100 ms sub-block ring); `gain_computer.{h,cpp}` — the FSM + clamp + one-pole ramp + silence-hold that turns the delta into the applied gain. Pure C++17, no framework types, double-precision internals. |
| `tests/dsp/` | The lab: JUCE-free Catch2 v3 unit tests feeding ragged block sizes over these kernels — golden K-weighting anchor, loudness goldens, sliding-window robustness, gain-computer behavior (ramp, clamp, freeze/park). Wired from `CMakeLists.txt` (`makegain_tests`). |
| `scripts/bootstrap.sh` | Fresh-machine toolchain/dependency setup — the **single dependency source** (package lists per distro; sanitizer runtimes explicit — see `doc/human/build.md` §7). |
| `scripts/check-all.sh` | CI-stand-in: builds + tests all presets for the current platform. |
| `scripts/docker-check.sh` | Tier-1 Linux verification lanes through containers only (AGENTS rule 9): default = arm64 native; `--x64` = Rosetta-emulated **amd64** (build.md §8.2). |
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

## Where the interesting JUCE lessons live (guaranteed non-obvious)

- **The editor's `setTransform` is host-reserved**: JUCE jasserts the
  editor keeps `hostScaleTransform`; applying your own scale there
  breaks window sizing in hosts. Panel zoom therefore transforms the
  `MainPanel` **child** and sizes the shell around it — see
  `applyZoom` in `MakeGainEditor.cpp` and the doc header above it.
- **Attachment-free parameter wiring**: choice parameters have no
  ToggleButton attachment type — the two modality switches are manual
  glue (`setValueNotifyingHost` on click, `setToggleState(…,
  dontSendNotification)` in the poll). The lesson is at the wiring
  sites, commented.
- **Anchor constants**: the K-weighting +0.691 dB ~1 kHz anchor and the
  LUFS −0.691 offset pair are load-bearing for the metric's absolute
  level — the goldens in `tests/dsp/` pin them.

## Going deeper (official docs)

- JUCE tutorial series: <https://docs.juce.com/master/tutorial_build_gui_application_basic.html>
- AudioProcessor class reference: <https://docs.juce.com/master/classAudioProcessor.html>
- JUCE example plugins shipped in-tree: <https://github.com/juce-framework/JUCE/tree/master/examples>
- CLAP format: <https://cleveraudio.org/>
