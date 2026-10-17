# AGENTS.md — collaboration rules for AI assistants on this repo

Binding for any AI/agent working in this repository:

1. **No commits without an explicit order from the owner.** Editing files is
   fine; `git commit` only when the owner says so (in their message, not
   implied).
2. **Never push.** `git push` is prohibited for agents. Pulling / fetching /
   diffing against remote is explicitly encouraged at the start of a session.
3. Working language: keep user-facing replies in English; doc files follow
   repo convention (English).
4. **Commit policy** (owner, 2026-10-17):
   - commits are **atomic when possible** and **human-sized**: a human must
     be able to read and review a commit's full diff in one sitting;
   - never mix unrelated changes in one commit;
   - when starting an implementation phase, the assistant first proposes a
     **commit-breakdown plan** (per-commit contents + checkpoints) as part
     of the implementation plan, and the owner agrees on it *before* coding;
   - commit messages set out *what and why* in the subject, details in the
     body when needed.
5. When resuming a session, first read `README.md`, then
   `functional_reqs/MakeGain_FR-v1.md` (frozen contract), then
   `tech_specs/MakeGain_TS-v0.1.md` + its decision log (§9) and session
   backlog (§10). FRs are frozen: changes go through the owner, not silently.
5. **Release & branching model (owner, 2026-10-17):** from the commit that
   introduces `docs/release-model.md` onward, agents never work on `main`
   directly — a unit of work lands on a branch, the owner pushes the
   branch, opens and merges the PR. Agents never push or merge (rule 2
   above stays absolute). Details: `docs/release-model.md`.
