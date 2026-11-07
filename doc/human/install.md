# Installing MakeGain from a bundle

 Received a set of built MakeGain files (a folder or archive copied from
another machine) and want it working in your host? This page does that.
If you would rather compile from source, go straight to
[`build.md`](build.md) — the sources are the distribution of record
(the project's own promise is *clone and build*; hand-carried binaries
are an extra convenience, nothing more).

Because these are unsigned, self-built or privately shared artifacts,
"installation" is a manual file copy into the folders your host scans.

## What is in a bundle

| Extension | Path inside the bundle | Format |
|---|---|---|
| `MakeGain.vst3` | a *bundle directory* (macOS/Linux contain `Contents/…`) | VST3 |
| `MakeGain.clap` | a bundle directory | CLAP |
| `MakeGain.component` | a bundle directory | AU (macOS only) |

A bundle may carry a build tag in its folder name (e.g. a short commit
id) — treat that as "which build is this", not decoration.

## Where each file goes

**macOS (arm64)** — `~/Library/Audio/Plug-Ins/`:

```
~/Library/Audio/Plug-Ins/VST3/MakeGain.vst3
~/Library/Audio/Plug-Ins/CLAP/MakeGain.clap
~/Library/Audio/Plug-Ins/Components/MakeGain.component   (AU)
```

**Linux (aarch64 and x86_64, e.g. Fedora Asahi)** — hosts auto-scan the
user folders:

```
~/.vst3/MakeGain.vst3
~/.clap/MakeGain.clap
```

**Ardour's preferred CLAP location** — if Ardour does not find the CLAP
in the standard spot, also try `~/.local/share/clap` and its discovery
checklist (see the quirks section at the end of
[`usage.md`](usage.md)).

After copying, let the host rescan plugins and, on first doubt, restart
the host — several hosts keep newly discovered plugins unusable until
then.

## Unsigned binaries: the quarantine step (macOS)

Locally built files normally load fine. Artifacts that arrived via
AirDrop, a download or a shared drive can carry macOS' quarantine flag,
and Gatekeeper refuses them once. Remove it:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/MakeGain.vst3
```

(repeat per bundle you installed). Proper signing/notarization is on the
project's later-milestone list; until then this one command is the
whole ceremony.

## Filesystem came from elsewhere? Executable bits

If the bundle traveled over a filesystem that drops Unix permission bits
(FAT/exFAT USB sticks, some network shares), plugin `.so` files on Linux
may need their exec bit restored before a host will load them:

```sh
find <bundle folder> -name '*.so' -exec chmod +x {} +
```

## Verify

Load the plugin in your DAW (see [`usage.md`](usage.md)). If it does
not appear, check the host's plugin rescan/error log and the
host-quirk checklist; if it loads, you are done.
