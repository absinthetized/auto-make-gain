# Release & branching model

> Owner-ratified 2026-10-17 (in force from the commit that introduces this
> file — see AGENTS.md §5). Supersedes the m0-plan's placeholder "tag m0".

## Branches

- **Permanent:** `main` only. Always green.
- **Unit of work = branch = PR.** A milestone is the largest PR we open;
  patches and docs are tiny PRs. No `develop`, no long-lived branches.
- Naming: `mN[-descriptor]` for milestone phases (e.g. `m1-mvp`,
  `m1-loudness-kernel`), plain descriptive names otherwise
  (`m0-release-model` for this file), `fix-*` for patches.
- `main` is protected on GitHub: everything beyond the pre-2026-10-18
  pushes goes through branch + PR (owner-set rule). The owner personally
  remains exempt from history rules: force-push allowed for the owner.
- AI agents never push and never merge; they commit on a work branch and
  hand the push/PR/merge to the owner (AGENTS.md §5, tooling note below).

## Versions & tags

- Semver with pre-1.0 semantics. One tag scheme — no separate
  "milestone tags".
- Milestones map onto minor versions unless the milestone is the one that
  completes the contract:

| Milestone | Tag | Content |
|---|---|---|
| M0 | `v0.1.0` | walking skeleton — **macOS-validated scope** (owner re-scope 2026-10-18: Linux compile/exec validation moved to the `m0-linux-inner-loop` branch + native Asahi session; release notes must state the boundary honestly) |
| M1 | `v0.2.0` | MVP compensation |
| M2 | `v0.3.0` | robust analysis + UI |
| M3 | `v1.0.0-rcN` | rc series, workflow/stereo |
| M4 | `v1.0.0` | GA |

- **`v1.0.0` triggers on substance, not schedule:** the first milestone
  that completes the entire frozen FR contract is the major. Breaking
  changes may promote any future milestone to a major, but nothing
  before the full-FR point gets a number ≥ 1.
- Patch tags (`v0.x.z`) for bugfix strands between milestones.
- Tags are created by the owner post-merge (annotated), after checkpoint
  rows in the milestone plan go green.

## GitHub Releases

**No GitHub Releases object until `v1.0.0`.** Distribution before v1 is
"clone and build" (FR-18). From `v1.0.0-rcN`, Releases objects carry
per-OS zipped artifacts, produced by CI (backlog item) — this is also the
point where proper code signing is decided.

## CHANGELOG

`CHANGELOG.md` exists from the `v0.1.0` tag commit; one section per tag,
fed from the commit history (the commit-breakdown discipline makes this
trivial). Release notes for future Releasesobjects are derived from it.

## Protection tightening schedule

| Phase | Rules on `main` |
|---|---|
| now (no CI) | PRs required; no other gates enforced (force-push owner-exempt) |
| after CI enablement | CI green required on PRs; agents land via PRs only; owner direct-push grace retained |
