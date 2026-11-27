# AGENTS.md — collaboration rules for AI assistants on this repo

Binding for any AI/agent working in this repository:

1. **No commits without an explicit order from the owner.** Editing files is
   fine; `git commit` only when the owner says so (in their message, not
   implied).
2. **Never push.** `git push` is prohibited for agents. Pulling / fetching /
   diffing against remote is explicitly encouraged at the start of a session.
   **Tags are the owner's ceremony** (owner, 2026-10-19): agents never
   create, move or push tags — not even locally; they prepare the
   release-notes material (`CHANGELOG.md`) for the owner's cut.
3. Working language: keep user-facing replies in English; doc files follow
   repo convention (English).
4. **Commit policy** (owner, 2026-10-17):
   - commits are **atomic when possible** and **human-sized**: a human must
     be able to read and review a commit's full diff in one sitting;
   - never mix unrelated changes in one commit;
   - when starting an implementation phase, the assistant first proposes a
     **commit-breakdown plan** (per-commit contents + checkpoints) as part
     of the implementation plan, and the owner agrees on it *before* coding;
     the plan names the **branch** the unit lands on and its base commit
     (e.g. `m4-quiet-output` from `main@8d3a558`), per the naming in
     `doc/ai/release-model.md` — a plan without a branch line is
     incomplete;
   - commit messages set out *what and why* in the subject, details in the
     body when needed.
   - **Commit cadence (owner, 2026-10-18; amended twice — read BOTH):**
     For every commit, the agent shows the owner the exact content (diff)
     and commits only on a per-commit explicit "ok" — batch approvals
     cover the *plan*, never the commits.
     **Exception (owner, 2026-10-25, SUPERCEDING the same-day "AI-only
     commits skip eyeball" ruling):** *docs-only* commits (pure markdown/
     txt: FR/DTS amendments, plan/ledger records, rule edits) may land
     directly — they are never code and the owner reads the PR. The
     exemption is EXACTLY that: any commit touching a source file
     (anything in a programming language, including build scripts) keeps
     the full per-commit eyeball gate, red/green notwithstanding.
     Owner judgment gates (FR rulings/ceremony/scope) stay above that,
     docs or not. Extra hygiene (owner, 2026-10-25): the assistant
     maintains this distinction as a hard procedure gate *before every
     commit*: ask "does this diff touch a source file?"; if yes → show
     diff → wait for explicit ok → only then commit.
   - **Reference metrics** (owner, 2026-10-18) — warnings, not strict rules;
     exceeding either needs explicit justification in the PR description:
     - one PR (and its branch) stays **within 10 commits**;
     - one PR touches **≤ 5–7 source files** — source = anything written
       in a programming language *including shell/build scripts*; docs
       (md, txt) do not count;
     these metrics presume the commit size/atomicity above unchanged (they
     are the base unit; the PR sizing builds on top of it).
5. **Session resume:** first read `README.md`, then
   `doc/human/MakeGain_FR.md` (the contract), then
   `doc/ai/MakeGain_TS-v0.1.md` + its decision log (§9) and session
   backlog (§10). FRs change only by an owner ruling, never silently;
   the contract is rewritten in place (rule 12).
6. **Release & branching model (owner, 2026-10-17):** from the commit that
   introduces `doc/ai/release-model.md` onward, agents never work on `main`
   directly — a unit of work lands on a branch, the owner pushes the
   branch, opens and merges the PR. Agents never push or merge (rule 2
   above stays absolute). Details: `doc/ai/release-model.md`.
   - **Branch verification (owner, 2026-10-20):** the agent verifies the
     checked-out branch (`git branch --show-current`) at session start and
     before every commit — file contents and `git log` prove nothing about
     *where* a commit lands. Origin of the rule: five PR-B commits landed
     on an unverified `main` during the 2026-10-20 tag ceremony; the owner
     accepted them as-is (no rewrite, no retroactive PR) and ordered this
     check formalized. **Verification acts (owner, 2026-11-27):** when the
     unit of work has no branch yet, the answer "we are on `main`" means
     *create it* — the agent runs `git switch -c <planned-name>` from the
     plan's base **before editing any source file**, not merely before
     committing. Origin: M4 work sat uncommitted on `main` (the plan named
     no branch) until the per-commit gate caught it at the commit.
7. **Comment standard (owner, 2026-10-17):** code must carry enough
   comments for a reader with **limited framework knowledge** (decent C++
   but *not* JUCE) to follow the process. Concretely, in addition to
   "why" comments:
   - every non-obvious framework call indirection gets a short
     "what this means in framework terms" note at the call site;
   - entry-point chain (no `main()` in plugins!) is documented in-place
     *and* in `doc/human/code-tour.md`;
   - build-side magic (juce_add_plugin metadata, format targets) is
     annotated in `CMakeLists.txt`;
   - lifecycle contracts (which thread, what a callback may not do) are
     stated where the override appears — not assumed known;
   - no comment narrating trivia; no comment that will rot with the next
     refactor without maintenance.
   - `doc/human/code-tour.md` is kept current **at milestone merges**
     (cadence relaxed per owner, 2026-10-19 — per-PR currency proved
     unsustainable): at a milestone's merge the file map and the
     entry-point chain must not be stale.
8. **Red/green testing discipline (owner, 2026-10-18):** no
   implementation may exist before its tests. **Scope (owner,
   2026-11-27):** application code only — the discipline exists to raise
   the quality of the plugin. Shell scripts, utilities and build/test
   tooling are not test-covered units; they are verified by a documented
   checkpoint (the command run and its output quoted in the commit
   message). Every test-covered unit
   lands in two commits: a **RED** commit (tests + an honest stub that
   compiles but obviously cannot satisfy them; the failing checkpoint
   output is quoted in the message) and a **GREEN** commit (the real
   implementation; the same tests now pass, output quoted). The owner
   reviews the assertions before the implementation exists. A red commit
   failing the build gate is intentional and acceptable while no CI
   exists; bisectability is preserved across green commits.
9. **Docker containment lane (owner, 2026-10-18):** an agent may run Linux
   build/test containers, but **only through committed repo scripts**
   (e.g. `scripts/docker-check.sh`) — never by calling `docker` (or any other
   system tooling) directly. The agent's output is **code, not system
   operation**: no direct interaction with Docker, package managers of the
   host, or system services. If a container script fails, the agent reports
   the error verbatim and stops; system-level remediation (Docker daemon,
   driver/host configuration) belongs to the owner/sysadmin.
10. **Answer style (owner, 2026-10-25):** when the owner asks a question,
   answer with the simple/short answer first. In-depth breakdowns
   (itemized by-commit or by-scope detail) are an option — offered in a
   single line or provided on request — never delivered unbidden as the
   base reply.
11. **Visual grammar (owner, 2026-10-25, PR-D lessons):** all MakeGain
   user-panel work follows `doc/ai/visual-grammar.md` (color vocabulary,
   control taxonomy, column grid / hero-central pattern, protected
   hero, honest affordances, text discipline). Deviating from it is a
   design decision — the owner judges it first.
12. **Docs split — ledgers vs snapshots (owner, 2026-11-07):** `doc/ai/`
   is *ledger* material — append-only records of decisions, plans,
   progress and rationale (history lives there, and in git +
   `CHANGELOG.md`). `doc/human/` is *snapshot-only* — always a current-
   state picture, organized by reader depth (README → use → install →
   build → contribute → code tour); it never carries dated rulings,
   progress ticks or "what changed" narration. Redundancy rule: every
   fact lives in exactly ONE doc — higher levels link down, ledgers
   reference human docs by path; human-doc restructures must keep
   ledger references resolvable (stable section numbers or a map).
   **The FR contract is a human doc too (owner, 2026-11-24):** it states
   the requirements as they stand — no dated amendment sections, no
   fate/supersession tables, no "retires/replaced/no longer" narration.
   A contract change rewrites the affected requirement in place; the
   ruling that caused it is recorded in `doc/ai/decisions.md` and in the
   milestone plan, and git carries the old text.
   **Snapshot hygiene scan (owner, 2026-11-12; hard procedure gate):**
   before every commit touching human docs (`doc/human/`, README,
   CONTRIBUTING.md, CHANGELOG.md), the assistant mechanically scans
   the diff for history markers — "since", "as of", "historical",
   "previously", "no longer", milestone/PR references, ✅/⏳ ticks,
   narrative dates — and either removes them (state *now*) or justifies
   each surviving hit in the commit message. Current-fact provenance
   is exempt (e.g. a pin's "main HEAD <date>" is a fact, not
   narration); the scan targets storytelling about change. The scan
   automates as a doc-lint in the CI/CD milestone (queued M4).
13. **Tier-2 artifact provenance (owner, 2026-11-23):** artifacts for
   bare-metal (Asahi) testing come only from a green lane at the current
   HEAD and are staged only via `scripts/stage-linux-artifacts.sh`
   (stamped, sha-named, MANIFEST + SHA256SUMS). Before testing, the GUI
   build stamp (`version / sha`) must match the PR head. Hand-copied or
   unstamped artifacts are never tested — origin: an unverified copy
   shipped the previous generation of binaries to a Tier-2 session.
14. **Check economy (owner, 2026-11-24):** gates protect *decisions*; no
   gate requires re-confirming a fact already established in the
   session, or re-checking state nothing has touched since it was
   checked. One approval covers the plan and the batch it names; the
   per-commit diff gate on source files stands, reviewed per commit
   unless the owner asks for a bundle. Self-verification covers
   *mechanical* checks only (build, tests, lint, file state) — design and
   judgment calls stay with the owner. Never restate a diff, plan or
   log already shown — quote failing lines, not whole build logs.
   Session unit is one task, ended at a commit boundary. Conflict rule:
   if an efficiency default ever conflicts with a gate above, the gate
   wins — say so in one line and continue.
