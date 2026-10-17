# Codebase tour — for the JUCE-newcomer

> Goal: a musician-programmer who knows C++ but has never seen JUCE (or any
> plugin format) should be able to read this repo and know where things
> happen, *without* prior framework knowledge. Maintained per AGENTS.md
> rule 6 — it must stay current with the file map. Last updated: 2026-10-17 (M0).

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

## File map (M0)

| Path | Role |
|---|---|
| `src/plugin/MakeGainProcessor.h/.cpp` | The DSP brain + **entry point** (`createPluginFilter`, bottom of the .cpp). Lifecycle contract documented at the top of the `.h`. This is where the loudness/gain DSP will live (M1+). |
| `src/ui/MakeGainEditor.h/.cpp` | The window. UI thread; disposable; talks to the processor via thread-safe channels (none wired yet — parameters come with M1). |
| `tests/dsp/` | JUCE-free unit tests (originally nonexistent here, now the project's lab), run under ASan/UBSan via `scripts/check-all.sh`. |
| `scripts/bootstrap.sh` | Fresh-machine toolchain setup. |
| `scripts/check-all.sh` | CI-stand-in: builds + tests all presets for the current platform. |
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

## Where the M0 stub plugs the future in

The FR contract's DSP (K-weighting, LUFS/RMS, gain computer — TS §3) enters
as a *new* `src/dsp/` layer that is deliberately **JUCE-free**: pure C++
that the processor calls each block, which is what makes it unit-testable
without a DAW. The test-first workflow for M1 lives in the tech spec §7.

## Going deeper (official docs)

- JUCE tutorial series: <https://docs.juce.com/master/tutorial_build_gui_application_basic.html>
- AudioProcessor class reference: <https://docs.juce.com/master/classAudioProcessor.html>
- JUCE example plugins shipped in-tree: <https://github.com/juce-framework/JUCE/tree/master/examples>
- CLAP format: <https://cleveraudio.org/>
