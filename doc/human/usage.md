# Using MakeGain in your DAW

This page is the everyday handbook: put MakeGain on your track, route the
dry reference it needs, and read the panel. Installation of the plugin
files is covered separately — see [`install.md`](install.md) (or
[`build.md`](build.md) if you compile it yourself).

## How it works for you, in one paragraph

Insert **MakeGain last in the FX chain** you want level-steady. It
continuously compares two recordings of the same audio: the *wet* signal
(as it arrives after your chain) against a *dry* pre-chain tap, and
applies one gain that nulls the difference between them. Loudness stays
constant while your plugins expand, duck, saturate or are
bypassed/edited; the plugin's applied-gain readout shows exactly what it
is doing, and **Active** disengages it whenever you want the raw chain.

The dry tap comes in on the plugin's **aux input** — the plugin does no
routing magic, your host carries the signal (next section).

## Routing the dry tap

Choose one:

### Reaper — the shipped TAP (recommended)

Reaper's serial FX chain makes this trivial with the included TAP
scripts, [`jsfx/MakeGainTap.jsfx`](../../jsfx/MakeGainTap.jsfx) (stereo)
and [`jsfx/MakeGainTapMono.jsfx`](../../jsfx/MakeGainTapMono.jsfx)
(mono):

| Order | FX |
|---|---|
| 1 | **MakeGain Tap** (JSFX) — copies channels 1/2 onto 3/4 bit-exact |
| … | your chain FX — stereo FX leave lanes 3/4 untouched |
| last | **MakeGain** |

Insert the Tap first and MakeGain last; Reaper raises the track channel
count automatically. The dry tap rides along inside the chain, so any
edit you make to items or FX is always reflected — there is no way for
it to go stale.

To load them: copy both `.jsfx` files into your Reaper JSFX folder
(`Options → Show extension folder path`, or click *New/Edit* in the FX
browser), hit *Rescan*.

### Plain routing fallback (all hosts)

Any sidechain route your host offers works:

- **Reaper without the Tap**: a pre-FX send from a clean (no-FX) copy of
  the track into MakeGain's aux input, channels 3/4 (Target: the
  MakeGain track). Receiving tracks auto-add channels only for
  priorities/destinations you set — match the aux pins.
- **Ardour**: use the plugin's native sidechain input hookup.

Caveat of any routed tap: the dry copy is a snapshot of a route, not of
the source — later edits to the track (gains, more FX up top) can
diverge from it (a *stale tap*). If your results drift, this is the
first suspect; the in-chain TAP cannot suffer it.

### Mono tracks

The aux bus mirrors the main bus's channel count: a mono track gets a
**mono aux (one pin, pin 2)** — use the mono TAP variant on mono
sources. The engine is channel-count agnostic internally.

## The panel

Read the panel top-down: title, the applied-gain readout (the big
number, flanked by the metric/response switches), three rotary
parameters with on/off switches below, and the status band at the
bottom (its color *is* the state).

| Control | What it does |
|---|---|
| **Active** | Engages compensation. The one parameter the host may automate. |
| **MaxComp** | Compensation ceiling in dB (clamps how far the correction may reach). |
| **Ramp tc** | Time constant of the per-sample ramp: larger = smoother/slower moves. |
| **Response** | Measurement window: **Short (3 s)** or **Fast (0.4 s)**. |
| **Metric** | What loudness means here: **LUFS (K-weighted)** or **dB RMS** (unweighted). |
| **Freeze** | Holds the applied gain at its current value (deliberate hold). While frozen the gain controls are disabled and Meter HP is locked (flipping the metering standard mid-hold would shift numbers under your feet). |
| **Meter HP** + **freq** | Enables a high-pass on the *metering* only (20–400 Hz, default 150 Hz) — judge loudness without rumble coloring it. |
| **Zoom** | Panel zoom, status-band corner: 75–200 %. Manual and persistent; platforms and hosts scale differently, you are the judge. |

Color grammar of the status band:
**gray** = standby (nothing happening), **green** = compensating,
**red** = fault — the aux/dry input carries no signal (check routing),
**ice blue = frozen**.

## Automation

Only **Active** is host-automatable; everything else is deliberately
UI-only ("the user is the judge" — see the frozen FR, §4.2/§10). The
panel marks the automatable control (amber ring). Zoom and switches are
persisted in the session but not automatable.

## Host-specific quirks

- **Ardour does not see the CLAP while Reaper does**: Reaper loading the
  `.clap` proves the file is valid, so this is host-side. Checklist,
  cheapest first: (1) also copy the bundle to `~/.local/share/clap`
  (the XDG data dir is Ardour's preferred CLAP path; nothing creates
  it); (2) Window → Plugin Manager → *Discover New/Updated*, then
  **restart Ardour** — newly discovered plugins can stay unusable
  until restart; (3) check Edit → Preferences → Plugins for a
  per-format CLAP switch and read any *Error* row in Plugin Manager —
  a concrete error string is the most valuable finding; (4) check your
  Ardour provenance: ardour.org prebuilt = full support; distro
  package / Flathub = maybe compiled without CLAP or sandboxed away
  from `~/.clap`. (Fedora note: Ardour may want the plugin `.so`
  executable on a transferred filesystem — see §8.1 of
  [`build.md`](build.md).)
