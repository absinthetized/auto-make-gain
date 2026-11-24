# Token budget — plan (owner-ordered 2026-11-24)

> Ledger: why this exists, what is measured, which levers are adopted, how
> the effect gets verified. Operative rules land in `AGENTS.md`; config in
> the global `opencode.json` (outside this repo).

## The measurement that started it

Owner's provider figures, two days of paid models:

- 9.0 M input tokens, 6.0 M cache reads (reported as a **separate line
  item**), 70 k output. Total prompt ≈ 15 M per two days.
- Ratio prompt:output ≈ 214:1. At any paid rate this workload is ~99 % input.
- Cache hit rate 40 %. At a typical 1/10 cache discount the uncached 9 M is
  ~94 % of the bill.

Owner's first decision (already in force): shorter sessions, bootstrapped by
`doc/ai/session-brief.md` + the global `session-freeze` / `session-resume`
skills.

## Model of the cost

Per turn the harness re-sends the whole conversation (system prompt +
instructions + tool schemas + transcript). So

    prompt_tokens ≈ Σ_turns (fixed_prefix + transcript_at_turn_t)

Two consequences:

1. **Turns are the multiplier.** Halving turns nearly halves spend; nothing
   else in the list comes close.
2. **Accumulated transcript is the coefficient.** Tool results stay in
   context forever and are re-sent every later turn. A session that reads
   3 MB of tool output pays for it on every subsequent turn.

Cache reads are cheap but not free. The audited local session below runs at
~94 % cache reads; the owner's paid sessions run at 40 %, which means many
cold starts (short sessions, idle gaps, or mid-session edits to the prefix).
That gap is worth some money — but the larger factor stays **turns ×
transcript**.

## Local evidence (opencode.db, read-only audit)

Largest local session (`proud-star`, 1667 turns): 180 M prompt tokens,
$6.61 equivalent, context bounded by compaction to early 28 k / late 47 k,
peak 219 k. Tool-result volume in that session:

| tool | calls | total | max single |
|---|---|---|---|
| edit (diff echo) | 523 | 1356 KB | 15 KB |
| shell | 848 | 1319 KB | 51 KB |
| write (full file) | 95 | 452 KB | 21 KB |
| read | 111 | 349 KB | 13 KB |
| websearch / webfetch | 26 | 331 KB | 36 KB |

Edit diffs and shell output are the bulk — both are transcript that never
leaves.

## Levers, in order of expected effect

| # | Lever | Mechanism | Est. effect | Cost |
|---|---|---|---|---|
| **L0** | **Compressed state** | append-only ledger + `INDEX.md` + archive + session brief: a fresh small session starts informed instead of re-deriving the project | precondition for L8 | docs work |
| **L1** | **Fewer turns per task** | batch approvals, mechanical self-verification, parallel tool calls, no re-confirming settled facts | −30…50 % prompt | free |
| **L1=** | **Cap tool output** | global `tool_output.max_lines/max_bytes`; `--stat`, `\| tail`, `grep` instead of full dumps; offset/limit reads | −20…30 % ctx, **permanently** | config |
| L3 | Read-once + targeted reads | never re-read a file already in context or just edited (the edit result carries the diff); grep before reading | −10…15 % | free |
| L4 | Delegate exploration | subagents absorb search/build noise, return a summary; their context dies with them | −10…20 % on investigations | paid tokens too |
| L5 | Compaction budget | lower `compaction.keep.tokens` (default 15000) so the retained tail is lean | −5…10 % | forgetting cost |
| L6 | Model routing | cheap/free model for subagents and grunt work, paid model for design only | price, not tokens | config |
| L7 | Cache hygiene | don't edit `AGENTS.md`/skills/config mid-session; don't switch model mid-session; keep prefix stable | protects warmth | free |
| L8 | Right-sized sessions | one task per session; length at the measured optimum, not a guessed 50 | bounded ctx growth | free |

**L1 and L1= are co-first.** Fewer turns shrinks the multiplier; capping
tool output shrinks the multiplicand *permanently* (the cap applies to every
future session), which is why it is not a second-tier item. Evidence above:
~3 MB of tool text resident in one session.

**L0 is not a parallel track — it is the precondition.** A short session is
only cheap if the incoming context is compressed; without the ledger, a fresh
session re-derives the project at full price, which is the left arm of the
curve below.

**L4 caveat:** subagent tokens are billed on a paid tier. The current
free-model window makes delegation look cheaper than it will be on Scaleway;
adopt the pattern, but do not bake subagent-heavy workflows in and then be
surprised by the invoice.

**L5 caveat:** a smaller retained tail makes each compaction cheaper but
makes the agent forget more, which re-enters the transcript as re-reads and
re-derivation. Tune deliberately against the report; never just minimise.

**L1 trust caveat (owner's project):** self-verification covers *mechanical*
checks — build, tests, lint, file state. Design and judgment calls stay in
the owner's loop. Saving turns must not erode the trust-gym.

## Session length is a U-curve, not "shorter is better"

Cost per task falls as sessions get longer (fixed prefix amortised over more
turns) and rises again once context growth and compaction dominate:

- too short → cold prefix every start, and re-derivation of state the brief
  did not carry;
- too long → context grows, compaction fires, and the cold turns land late
  when context is largest (the 1667-turn session's 6 % miss rate is
  expensive precisely because of *when* it missed).

**"≤50 turns" is a provisional ceiling, not the target.** The optimum is
measured, not guessed: `~/.config/opencode/scripts/token-report.sh` reports per-session turns
against prompt tokens per turn; the minimum of that curve is the owner's
number. Do not adopt a number from this document.

### What the local sample already shows

| turns | sessions | avg prompt | avg context/turn |
|---|---|---|---|
| 1–10 | 4 | 0.0 M | 2.8 k |
| 11–25 | 1 | 0.15 M | 9.6 k |
| 26–50 | 2 | 1.05 M | 24.8 k |
| 51–100 | 1 | 1.65 M | 32.4 k |
| 100+ | 1 | 9.55 M | 65.4 k |

Two readings, one of them a correction to the guess:

- Per-turn context rises monotonically with length, so for a *fixed task*
  shorter always wins. The 100+ bucket is 65 k/turn against 25 k at 26–50.
- The **left arm is shallow**: a 1-turn session still carries only ~5–6 k
  tokens of fixed prefix, so the cold-start penalty is small compared with
  per-turn growth. Fragmentation hurts mainly below ~10 turns, not below 50.

Provisional guidance from the data, not from the document: end at commit
boundaries (tasks are ≥10 turns in practice) and treat 50–100 turns as the
point where compaction starts rewriting context. The exact floor needs task
labels, which the database does not carry.

**Data gap:** these are the sessions in this machine's database (mostly
free-model). The owner's paid 15 M/2-day figure is not in it — the audit must
be run where those sessions actually ran before any before/after claim is
made.



## Decisions taken

- **2026-11-24 — gates stay as written.** Rules 2, 4 (per-commit eyeball
  on source), 6 (branch check) and 13 (artifact provenance) are kept
  unchanged: a gate costs one replay (~30–45 k prompt tokens), so ~20
  commits per two days ≈ 3–4 % of the bill, while a single resident
  50 KB build log costs ~6 %. Cutting transcript beats cutting checks.
- **Bundle-review-by-default was rejected** — it would rewrite rule 4,
  which exists because of the 2026-10-20 incident; the owner may still
  ask for a bundle ad hoc.
- **Instruction files combine, they do not override** (OpenCode docs):
  a contradictory global line would not overrule the repo gates — it
  would force per-commit adjudication, which is itself turn cost. Hence
  additivity only: new rule 14 (check economy) states the split
  explicitly and pins the conflict rule ("the gate wins").
- Rule 14 costs ~180 tokens/turn (~1 % at 500 turns over two days) and
  must be edited at a session boundary — a mid-session instruction
  change mutates the prompt prefix and cold-starts the cache.

## Verification

A read-only report over `~/.local/share/opencode/opencode.db`
(`~/.config/opencode/scripts/token-report.sh` — a machine-local tool, kept
outside this repo): per day and per session — prompt
tokens, uncached vs cache-read split, output, turns, mean/max context, top
tool-output consumers. Run before/after a working day; compare uncached
input and turns, not the headline figure.
