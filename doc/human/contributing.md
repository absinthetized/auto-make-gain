# Contributing to MakeGain

How the repository works if you want to propose changes — in one page.
For the *plugin's design* go to the frozen FR
([`MakeGain_FR-v1.md`](MakeGain_FR-v1.md)); for a line-by-line code walk,
[`code-tour.md`](code-tour.md).

## Assumptions the codebase stands on

1. **Frozen requirements.** `doc/human/MakeGain_FR-v1.md` is the
   behavioral contract; behavioral changes are amendments by ruling, not
   drive-by edits.
2. **The DSP is framework-free.** All loudness/GGM logic lives in
   `src/dsp/`, plain C++ without JUCE types, unit-tested with Catch2 in
   its own targets. JUCE is the adapter layer (`src/plugin/`,
   `src/ui/`), never the domain.
3. **Red/green discipline.** Tests exist before the implementation that
   satisfies them: a failing-then-passing commit pair (`dsp(...)` RED /
   GREEN subjects), so every behavior lands with its proof.
4. **Docker containment.** Linux builds/tests run only through the
   committed lane scripts (`scripts/docker-check.sh`), never raw system
   tooling — see [`build.md`](build.md) §8.
5. **License: AGPLv3.** Everything contributed here inherits it.

## Architecture in four sentences

`MakeGainProcessor` (JUCE `AudioProcessor`) owns the state: thread-safe
status atomics plus `APVTS` parameters. `MakeGainEditor` (JUCE
`AudioProcessorEditor`) is a disposable UI shell: a `Timer` at 10 Hz
polls the atomics and parameters; all visuals live on its child
`MainPanel` component (the editor's own transform is host-reserved —
zoom is a child-panel transform). The DSP engine runs on the audio
thread, controlled via lock-free channels; the UI never reaches into
audio internals and vice versa. Requirements→parameters are wired
declaratively where possible (`ButtonAttachment`/`SliderAttachment`/
`ComboBoxAttachment`) with manual glue only for unmapped special cases.

## The working rhythm

- **Work lands on branches** (`pr-<unit>`-named), unified through PRs by
  the maintainer; nobody commits to `main` directly.
- **Human-sized commits**: atomic when possible, reviewable in one
  sitting, unrelated changes never mixed. Commits on source files are
  reviewed individually against their diff.
- **Every change is green**: the lane/build plus the DSP test suite
  (`scripts/check-all.sh`) must pass; Linux coverage is both arm64
  (native) and amd64 (Rosetta emulation) lanes.
- **Comments carry framework context**: code is written so a reader
  with decent C++ but *no JUCE knowledge* still follows it — each
  non-obvious framework indirection gets a "what this means in
  framework terms" note at its call site; lifecycle contracts are
  stated where the override appears, not assumed.

## What the maintainer gates

Design decisions (FR amendments, UI changes against the visual
grammar of `doc/ai/visual-grammar.md`), release ceremony (tags,
CHANGELOG), and merges. If your change touches user-visible behavior,
expect it to go through the owner's judgment first — "propose, then
code", not "code, then justify".

## Where the process rules live

The full collaboration contract (including the AI agent's rules:
commit cadence, eyeball gates, docs split) is
[`AGENTS.md`](../../AGENTS.md); the running decision register is
[`doc/ai/decisions.md`](../ai/decisions.md). Human-facing docs under
`doc/human/` are deliberately snapshot-only — history lives in the
ledgers and git, not in them.
