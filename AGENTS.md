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
   - **Reference metrics** (owner, 2026-10-18) — warnings, not strict rules;
     exceeding either needs explicit justification in the PR description:
     - one PR (and its branch) stays **within 10 commits**;
     - one PR touches **≤ 5–7 source files** — source = anything written
       in a programming language *including shell/build scripts*; docs
       (md, txt) do not count;
     these metrics presume the commit size/atomicity above unchanged (they
     are the base unit; the PR sizing builds on top of it).
5. **Session resume:** first read `README.md`, then
   `functional_reqs/MakeGain_FR-v1.md` (frozen contract), then
   `tech_specs/MakeGain_TS-v0.1.md` + its decision log (§9) and session
   backlog (§10). FRs are frozen: changes go through the owner, not silently.
6. **Release & branching model (owner, 2026-10-17):** from the commit that
   introduces `docs/release-model.md` onward, agents never work on `main`
   directly — a unit of work lands on a branch, the owner pushes the
   branch, opens and merges the PR. Agents never push or merge (rule 2
   above stays absolute). Details: `docs/release-model.md`.
7. **Comment standard (owner, 2026-10-17):** code must carry enough
   comments for a reader with **limited framework knowledge** (decent C++
   but *not* JUCE) to follow the process. Concretely, in addition to
   "why" comments:
   - every non-obvious framework call indirection gets a short
     "what this means in framework terms" note at the call site;
   - entry-point chain (no `main()` in plugins!) is documented in-place
     *and* in `docs/code-tour.md`;
   - build-side magic (juce_add_plugin metadata, format targets) is
     annotated in `CMakeLists.txt`;
   - lifecycle contracts (which thread, what a callback may not do) are
     stated where the override appears — not assumed known;
   - no comment narrating trivia; no comment that will rot with the next
     refactor without maintenance.
   - `docs/code-tour.md` is kept current whenever the file map or the
     entry-point chain changes.
8. **Docker containment lane (owner, 2026-10-18):** an agent may run Linux
   build/test containers, but **only through committed repo scripts**
   (e.g. `scripts/docker-check.sh`) — never by calling `docker` (or any other
   system tooling) directly. The agent's output is **code, not system
   operation**: no direct interaction with Docker, package managers of the
   host, or system services. If a container script fails, the agent reports
   the error verbatim and stops; system-level remediation (Docker daemon,
   driver/host configuration) belongs to the owner/sysadmin.
