# MakeGain User Panel — Visual Grammar guideline

Distilled from the PR-D UI-polish sessions (owner + agent, 2026-10-25),
after four owner-guided design iterations on the PR-C panel. This is the
learned-lessons document for all future MakeGain UI work: follow it by
default; deviating from it is a design decision the owner judges first
(same posture as FR changes — it is the panel's contract in miniature).

## 1. Color vocabulary

Each color means exactly one thing, everywhere on the panel:

| color | meaning | used as |
|---|---|---|
| dark amber `#a8762a` | *automatable role* | a thin ring around the one automatable control — never a fill, never the "on" color |
| soft green `#5e9e6f` | *running / on* | pill track's active side, checkbox tick fill, status "rolling" |
| dark slate `#2c333c` | *off / inactive / rest* | default track of a two-position control, checkbox at rest |
| ice blue `#274a5a` | *frozen hold* | a deliberate hold of value (status light; Freeze-flavored zones) |
| **red** | **fault** | a problem the user must resolve (dead sidechain tap). Red is a diagnosis, never a mode: no user choice — however deliberate (Freeze) — may borrow it |

The status light formula stands as the worked example of the vocabulary:
gray = nothing happening / green = compensating / red = sidechain tap
dead / ice = frozen; precedence frozen → red → green → gray; the text
line says it, the band color confirms it at a glance.

No double-coding: the same hue may appear in two widget layers only when
the meaning is the same (green = "this is active" in a pill and in the
status light are the same statement).

## 2. Control taxonomy — skin follows the control's NATURE

Pick the skin by what the control *is*, not by which row it happens to
sit in:

- **On/off boolean** (no third state) → **checkbox**: centered tick box,
  caption below, caption dims when off (an off IS a meaningful state).
- **Modality A/B** (no off; exactly one of two is always chosen) →
  **sliding pill**: track highlighted on the active side, caption shows
  the live selection and *never dims* (a modality has no off state, so
  dimming would lie).
- **Continuous value** → **rotary** with a name+unit caption under it.

Plumbing (param attachments, glue) is never rewritten for a skin: skins
are JUCE `LookAndFeel` overrides applied per component via
`setLookAndFeel` — scoped, never global, never touching the host's other
windows.

## 3. Column grid — hero-central pattern

The panel lays out as a **column grid**. The number of columns is a
layout fact of the current panel (today: 3 × 144px at x = 14 / 162 /
310), not a doctrine. What IS doctrine:

- The grid's **center column(s) host the hero element** — the one
  control or readout the panel exists to show (today: the applied-gain
  number); satellites (pills, checkboxes) flank it in the side columns.
- If a future hero needs more estate — e.g. a spectrum analyser — the
  hero **spans multiple center columns** and the satellites collapse
  toward the flanks; adding an element means giving it a column (or
  spanning), and usually shrinking the neighbors' share, never breaking
  the grid.
- A column is *owned*, top to bottom: pills aligned to the
  MaxComp+Active column and the Mtr-HP-freq+its-checkbox column on the
  flanks, checkboxes over the rotary columns — elements that belong
  together stack true.

## 4. The hero is protected

Whatever flanks the hero must **clamp its draw rect** to its own column
space: a text draw (the big number) never slides under a neighboring
component. The worst case is clipping (it reads as "the string is
long"), never overlap (it reads as "the panel is broken"). Same for a
future analyser: it spans its columns, and the number yields font/or
spans fewer — re-evaluate the estate, keep the rectangles disjoint.

## 5. Affordances are honest — dependent controls grey

A control whose feature is currently inert **greys out (alpha ≈ 0.35)
and stops accepting clicks** the moment its parent state makes it
inoperative — and lights back up when the state reverses (cheap no-op
writes at the poll timer make this free at steady state).

Locks are directional and honest about it: a dominating state (Freeze)
locks its dependents **both ways** while active (toggling the metering
definition mid-hold shifts the readouts under the hold), but a locked
value that is *actually in use* (HP freq with HP engaged) stays lit —
only the control that would *change* the situation dims.

## 6. Text discipline

- Captions are title case (`Short`, `Fast`, `Active`) —
  SCREAMING CASE implies automatic emphasis; controls don't shout.
- Technical units keep their native casing (`LUFS`, `dB RMS`, `Hz`,
  `s`).
- Drawn strings stay plain ASCII (the hosts' text stacks mis-decode
  UTF-8 punctuation — see the mojibake history in PR-B3).
- Inactive text: `#8a9099`; active text: `#e8e4dc`; the hero number has
  its reserved color (`#cfe6c8`).

## 7. Process discipline — iterations, iterations, iterations

Every visual change lands as a small **iteration commit** with:
build + plugin install + owner's eyeball on the host **before** the
commit (per the AGENTS.md cadence), then commit, then a short record in
`doc/ai/m1-plan.md`. One iteration = owner-reviewed, revertible, and the
m1-plan log keeps the rationale that survives refactors.

## Witness history (where each rule earned its place)

- Amber ring, not label — owner: "no explicit label, some visual clue"
  (rule 1; darkened in iteration 3: full amber "too much").
- Checkbox-caption-below vs beside — owner: mixed label placement made
  the row "weird" (rule 2, rule 3).
- Pill caption dimming reserved — owner: "when we slide we shouldn't
  change the label color" (rule 2, modality ≠ on/off).
- Gray/green/red/ice status light — owner ordering, with red promoted
  to "problem in general" (rule 1).
- Column alignment up to hero-central — owner: "pills aligned with the
  checkbox and the rotary", then generalized to the hero-span rule
  (rule 3, rule 4).
- Freeze locks Meter HP — owner ruling with the false-alarm rationale
  (rule 5).
