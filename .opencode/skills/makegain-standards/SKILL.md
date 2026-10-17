---
name: MakeGain standards
description: Collaboration standards and workflow requirements for any coding, committing, or review work in the MakeGain (auto-make-gain) repository
---

MakeGain-specific agent standards. This skill must be consulted for any
session working in / on the auto-make-gain repo (plugin code, CMake,
scripts, docs).

**AGENTS.md at the repo root is the authoritative, binding contract.**
Read it first at session start; this skill only summarizes what is
non-negotiable there. On any conflict, AGENTS.md wins and this skill
should be patched in the next related PR.

## Session start

1. Read `README.md`, `functional_reqs/MakeGain_FR-v1.md` (frozen FR
   contract), then `tech_specs/MakeGain_TS-v0.1.md` including its decision
   log (§9) and backlog (§10).
2. `git fetch` / pull and diff against `origin` before doing anything else.

## Absolute rules

- Never `git push` (any branch, any remote). Pushing, PR opening and
  merging belong to the human owner — see `docs/release-model.md`.
- Never `git commit` without an explicit owner order.
- Never work directly on `main`: units of work go on a branch
  (`mN[-descriptor]`, `fix-*`) and land via owner's PR.
- Requirements (FRs) are frozen contract; changes go through the owner,
  never silently.

## Commit & PR sizing (commit policy, AGENTS.md §4)

- Atomic, human-readable commits; commit-breakdown plan agreed *before*
  coding an implementation phase.
- Reference metrics (warnings, not strict rules — justify in the PR
  description when exceeded):
  - ≤ 10 commits per PR/branch;
  - ≤ 5–7 source files per PR (source = any programming-language file
    including scripts; md/txt do not count).

## Comment standard (AGENTS.md §6)

Code must be followable by a C++ reader who does not know JUCE:
framework indirections and lifecycle/thread contracts explained at the
call site; the no-main() entry-point chain documented in-place and in
`docs/code-tour.md`; build-side CMake magic annotated in
CMakeLists.txt; keep `docs/code-tour.md` current with structural changes.
