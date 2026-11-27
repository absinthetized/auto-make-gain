# M4 plan — CI/CD + release process (2026-11-27)

> Audience: AI/owner process material (ledger). Successor of `m3-plan.md`.
> **Status: re-planned as a three-layer split (§Layers); S1's shape is
> ruled in §Rulings (all four calls closed, 2026-11-27). Nothing
> implemented, nothing committed.** M4's CI half (Actions matrix,
> packaging, audit, doc-lint, tag gate) is still only in `backlog.md`
> item 1.

## Rulings in force (from `backlog.md`)

- **Compiler half — landed** as `M4 compiler warnings (#22)`, `8d3a558`:
  `-Wall -Wextra -Werror` per source file (not per target, so bundled
  JUCE/opus code cannot fail the build), unused test helper removed, two
  vector builders reserve final capacity.
- **Agentic context half** — owner's reading: too much build/test output
  is injected into agent sessions, driving token counts and the bill up.
  Scope before starting.
- Separate activity from M3; build/tooling hygiene, reviewed on its own.
- Owner direction (2026-11-27): the problem is **not repo-specific** —
  agents interact with raw output everywhere. Decide per artifact
  whether it belongs to general config, a toolchain profile inside
  general config, or the repo.

## Three artifact kinds

- **Instruction** — always-on text in an `AGENTS.md`. Costs context in
  every session, so it must be short and normative ("do X, not Y").
- **Skill** — a procedure loaded on demand when its description matches
  the task. Costs nothing until relevant; right home for multi-step
  recipes and toolchain detail.
- **Tool (script)** — deterministic, no model judgement. When a
  procedure can be a script, make it a script and let the instruction
  be "run it". This is the layer that actually removes noise.

## Layers

| Layer | Location | Holds | Test for membership |
| --- | --- | --- | --- |
| **General** | `~/.config/opencode/` (`AGENTS.md`, `skills/`, `scripts/`) | True of any task in any language: how to read command output, session budget, briefs | Would it be wrong for a C++ repo *and* a Python repo? |
| **Toolchain** | `~/.config/opencode/toolchains/<name>/` (new) | True of any project using that toolchain: CMake/Ninja semantics, ctest patterns, generator facts | Would it be correct for another CMake project on this machine? |
| **Repo** | the repository | Facts CI, other humans and AGENTS rule 9 depend on: presets, log paths, lanes, release rules | Must it run from a fresh clone with no home directory? |

### Placement rules

1. **CI or another human must run it without your home dir → repo.**
   That is the hard boundary; everything else can move up.
2. **True for every CMake+Ninja project here → toolchain profile.**
3. **True for any coding task → general.**
4. **Encodes machine paths, versions, budgets → general, never repo.**
5. **Touches containers → repo.** Rule 9 allows `docker` only through
   committed repo scripts; a general or toolchain wrapper must never
   call `docker`.
6. **One implementation.** Repo scripts stay self-contained; profiles
   never reimplement them — a profile points at `<repo>/scripts/...`
   when it exists.
7. **Activation is explicit and reviewable.** A repo declares its
   profile with one line in its `AGENTS.md`
   (`Toolchain profile: cmake-ninja`); the general instructions say to
   read `~/.config/opencode/toolchains/<name>/AGENTS.md` when a repo
   declares one. No hidden per-directory config.

## Layer contents (the plan)

### General layer — machine-wide, lands first

- **Instruction** in `~/.config/opencode/AGENTS.md` (~10 lines): output
  read protocol — read the summary; open the log only for the failing
  step; quote failing lines, never whole logs; prefer a tool that
  answers a question over a stream; never `cat` a build log. When the
  owner wants detail, point at the command or log path so the owner runs
  it — do not pull the stream into the session.
- **Tool** `scripts/run-capture.sh <logfile> -- <command...>`: runs the
  command, tees to the log, prints one line plus the log path on
  success, and on failure prints the exit code, the **last 60 lines**
  and the log path. `-v` streams live and still logs. Deliberately dumb:
  no pattern matching, works for make, cargo, pytest, `docker build`
  alike.
- **No skill**: the protocol is short and always-on; a skill would add a
  load step for no gain.

#### Measurement visibility (owner, 2026-11-27 — part of this problem)

The cost tool exists but cannot be reached on demand, which makes it part
of the same defect rather than a finished feature:

- `skills/session-budget/SKILL.md` and `scripts/token-report.sh` exist;
  global AGENTS.md fires `--budget` only "at a commit or gate boundary".
  A planning session on a merged tree never crosses a gate, so the
  measurement never runs — measured this session: 26 turns,
  23.9k context/turn, 93% cache hit, restart ~9.8k, break-even 0.7 turns,
  and the owner had no way to ask for those numbers.
- `window=unknown` in the output: the tool reads the model's context
  limit from `opencode.json`, and only `ovh-deepseek` declares one; the
  active `opencode` provider has no entry, so the window (and with it the
  threshold's context check) is blind.

Fix, all general layer, machine-local, no repo commit:

- **Tool, user-triggered**: `command/budget.md` → `/budget` runs
  `token-report.sh --budget` and reports its four lines. Owner asks, tool
  answers; no agent judgement in the path.
- **Cadence**: global AGENTS.md also runs `--budget` at session start and
  when asked, in addition to gates. Session start costs the prefix only
  (~10k cached), which is the cheapest useful datapoint.
- **Config**: declare `limit.context` for the active provider's model in
  `opencode.json`, so `window` and the verdict stop saying `unknown`.
- **Shape**: `--budget` stays read-only and four lines — same rule as
  `run-capture.sh`: the summary is the interface, the database is the
  detail.

Not here: a new measurement (the query exists), a dashboard, or per-turn
accounting pasted into the chat.

### Toolchain profile `cmake-ninja` — machine-wide

- **Instruction** `AGENTS.md` (~20 lines): configure and build are two
  steps; generator is Ninja, so the real build file is
  `build/<preset>/build.ninja`; `ninja -C build/<preset> -t commands |
  wc -l` sizes a clean build without running it; `cmake --build` is
  incremental and near-instant when clean; compiler depfiles mean header
  edits rebuild properly; with `-Werror` a warning stops the build at
  its first occurrence.
- **Skill** `skills/cmake-ninja/SKILL.md`: reading a red lane — which
  step failed (configure / build / link / ctest), the failure classes
  (CMake configure error, compile error, undefined reference, Catch2
  assertion block), **ASan/UBSan reports keep their whole block** and
  their `SUMMARY:` line (tail-60 normally keeps it; widen to the full
  block if not), and the re-run pattern
  `ctest --test-dir build/<preset> --rerun-failed --output-on-failure`
  for a single red test (ctest has no "failures only" flag — this is
  the closest correct one).
- **Tool** `tools/cm-check.sh <preset>`: configure + build + ctest for
  one preset through `run-capture.sh`, exactly the shape the repo script
  uses. This makes profile and repo behavior identical by construction.
- **Not here**: repo presets, log paths, `-Werror` as policy (that is
  repo config; the profile only says what `-Werror` implies).

### Repo layer — the contract CI and humans consume

- **Scripts** `check-all.sh` and `docker-check.sh`: capture each step to
  `build/<preset>/logs/<step>.log` (git-ignored already; stable name,
  overwritten per run), print one line per step on success and the
  failing step's tail plus log path on failure, keep exit codes, add
  `-v` for anyone who wants the stream. Counts (warnings, tests) replace
  narration. No pattern matcher: tail is the one mechanism, shipped
  inline (~15 lines) so a fresh clone needs nothing from `$HOME`.
- **Instruction** in repo `AGENTS.md`: the `Toolchain profile:` line,
  and one rule — build/test output is read from the summary, the full
  log is opened only for the failing step (this extends rule 14, which
  already forbids restating logs).
- **Doc** `doc/human/build.md`: `-v`, log location. `code-tour.md` table
  cell updated only if the script set changes shape.
- **Not here**: token budgets, agent reading habits, generic Ninja
  facts, anything that only makes sense with `~/.config/opencode`.

## Why the split pays

- The noisy part (agent habits + generic capture) is fixed once for
  every repo; only 15 lines of the mechanism ship per repo, and only
  because CI and rule 9 need them.
- Two repos with CMake get consistent agent behavior with zero
  duplication of knowledge; a second repo needs one line in its
  `AGENTS.md`.
- Nothing in CI depends on the machine, and no profile can bypass the
  container rule.

## Baseline and acceptance (was S1.5)

Measured `main` @ `8d3a558`, macOS arm64; the harness caps one tool
result at 600 lines / 24 KB, so "fits one call" is the budget:

| Run | Today | Target |
| --- | --- | --- |
| full `cmake --build build/macos` | 135 lines (200 ninja steps listed) | 1 line + log path |
| `ctest` pass | 3 lines | 1 line |
| `check-all.sh` green | 2 × configure + 2 × build + 2 × ctest, live | ≤ 8 lines |
| `check-all.sh` red | noise with the failure buried | failing-step tail + log path |
| `docker-check.sh` green | image build + 2 lanes, live | ≤ 10 lines |
| `docker-check.sh` red | noise with the failure buried | failing-step tail + log path |

Acceptance: green runs fit one tool call with room to spare; red runs
print the failing lines verbatim, enough to quote in a RED commit
(AGENTS rule 8) with the full log named for the rest.

Measurement acceptance: `/budget` answers in one line of output plus its
four detail lines, at any moment, with a real window number; a session
start shows the cost of the prefix before any work is done.

## Commit / landing breakdown

**Global side** (no repo commit; machine-local, shown in chat as a diff
before it is written): general instruction, `run-capture.sh`, the
`cmake-ninja` profile (instruction, skill, `cm-check.sh`), and the
measurement-visibility fix (`/budget` command, cadence line, provider
limit in `opencode.json`).

**Repo side** (the two script commits take the diff gate; the docs commit
is docs-exempt, rule 4):

**Branch:** `m4-quiet-output`, from `main@8d3a558` (owner, 2026-11-27).
All three repo-layer commits ride it; created before the first source edit.

1. `scripts: capture build/test output, print a step summary` —
   `check-all.sh` first; no RED commit (ruling above): the checkpoint is
   a green run of ≤ 8 lines and a deliberately broken assertion whose
   block and log path are quoted in the message.
2. `docker-check: the same summary contract inside the container` —
   both lanes green, one broken assertion exercised.
3. `docs: toolchain profile line and how to read build output` —
   `AGENTS.md` line + rule, `build.md` paragraph, `code-tour.md` cell.

Order: global layer first (free, no gate), then repo scripts, then docs.

## Rulings (owner, 2026-11-27)

- **Rule 8 covers application code only.** Scripts, utilities and
  build/test tooling are not test-covered units; they are verified by a
  documented checkpoint — the command run and its output quoted in the
  commit message — not by a RED/GREEN pair. No shell harness is built.
  Recorded in `decisions.md` and AGENTS rule 8.
- **`bootstrap.sh` stays as it is** (one-time local provisioning); it is
  not part of this change.
- **Quiet is the default for everyone.** The scripts print one summary
  line per step; `-v` streams. An agent reads the summary, and opens the
  failing step's log only when it must debug. **When the owner wants
  detail, the agent points at the command or the log path and the owner
  runs it** — verbose output is not pulled into the agent's context. A
  flag an instruction must remember is exactly the failure being fixed,
  so the quiet path cannot depend on one.
- **Logs are overwritten each run** at `build/<preset>/logs/<step>.log`.
  No history is kept for now; run-to-run trends can be added later as a
  one-line TSV if a question needs them. Evidence lives in the quoted
  commit text, not in past logs.
- **A plan names its branch, and the branch check acts.** Every
  commit-breakdown plan states the branch and its base
  (`m4-quiet-output` from `main@8d3a558`). When the unit of work has no
  branch, the agent creates it before the first source edit — not before
  the first commit. Origin: this plan named no branch, so M4 work sat
  uncommitted on `main` until the per-commit gate caught it. Recorded in
  `decisions.md` and AGENTS rules 4 and 6.

## What this plan does not do

No log-filtering framework, no pattern-matching matcher in shipped
scripts, no global docker wrapper, no duplicated build scripts, no
change to tests, flags, presets or build content. `stage-linux-artifacts.sh`
is out of scope (backlog item 3 rules it).
