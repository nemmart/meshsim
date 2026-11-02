# Integrator handover (2026-11-01, after the v1.25 freeze)

For a new integration session taking over from the previous one. **Read this first**, then the top-level `README.md`, then the
decision logs as needed (`wip/planning/DECISIONS-v1.2.md`, `-v1.25.md`, `-v1.3.md`). Everything below was checked against the
repository at the time of writing; trust the repository over this file if they disagree.

## The project in one paragraph

meshsim is the cycle-level simulator, spec and software of a 64-tile mesh in an ASIC for proving the Jolt zkVM (owner: Niall;
RTL: Tony, working in his own sessions). Milestones are folders: `v1.0/`, `v1.1/` (frozen), **`v1.2/` (pure: the spec exactly as
Tony took it, tag `v1.2-tony`, plus `ERRATA.md`)**, **`v1.2.1/` (v1.2 with every erratum and cleanup applied, simulator in line;
frozen 2026-11-01)**, and **`wip/` (v1.25: v1.2 plus the replayer and its tables at jolt-cpp `821e1bd`)**. Then **v1.3: the Tracer**
(design in progress, `DECISIONS-v1.3.md`). Tony's RTL goes from v1.1 straight to v1.25; his sessions are reviewing the v1.2 spec
now and diff it against v1.2.1 (`git diff --no-index v1.2/spec/ v1.2.1/spec/` on `main`). **Versions are folders on `main`,
not tags** (owner, 2026-11-01): no new tags; the old ones stay as history. "Frozen" is loose: v1.2 is a waypoint, corrected by point releases.

## How we work

- **The owner approves plans before work.** Every worker proposes a plan first; the integrator reviews it and drafts the reply.
  Keep replies short; the owner dislikes long agreeing paragraphs.
- **Workers** each get `WORKERn/PROMPT.md` on `main`; they keep `NOTES.md`, `QUESTIONS.md`, `REPORT.md`. The owner relays messages
  by hand. **A message for a busy worker is parked in `WORKERn/NEXT.md`** and removed once sent. When a worker finishes, its
  report, questions and data move to `wip/planning/sessions/` (with a line in that README) and its folder goes.
- **Branches:** code changes go on a branch the worker owns; **the integrator merges** into `main` (merge, never rebase, never
  rewrite others' history; one exception was rebuilding `v1.2.1` onto the new folders, with notice to WORKER15).
- **Recurring rules for every prompt:** keep the last few tool calls of every turn for committing and pushing, end with a clean
  `git status`; background jobs don't survive a turn (long runs go to the Quest runner); never `pkill -f` a pattern in your own
  command line; never write a token into a file.
- **Quest runner** (tokens and `PIPELINE.md` in the owner's preferences): claim task numbers in the Quest repository's `NUMBERS.md`
  before writing a task; smoke-test kits locally; an explicit `exit` before any embedded kit; private repositories can't be fetched
  by the runner, so tarballs are copied to `/tmp` on the box **by the owner** (check they're world-readable). Last numbers used:
  086 (WORKER11), 087 (WORKER11), 088 (WORKER15, passed).
- **Review finds go to `wip/planning/CLEANUP.md`** (works but could be cleaner) and, where pure v1.2 has them, to `v1.2/ERRATA.md`
  (with an RTL-impact column). Each is fixed in both `v1.2.1/` and `wip/`.
- **Big data** lives on the runner box, not in git: the v1.25 deliveries in `/tmp/w9-deliveries/` (46 and 44 GB; `/tmp` is not
  permanent; rebuild steps in `sessions/WORKER9-REPORT.md`).

## The workers

| worker | doing | state | what's left | waiting for it |
|---|---|---|---|---|
| WORKER16 | T2: is pc, `instr_idx` and registers 0-127 the Tracer's whole starting state (jolt-cpp); branch `worker16-state` of jolt-asic-planning | working: tool and small-input findings done (T2 holds at instruction boundaries; jolt-cpp's secp advice buffer is the one state outside, mid-sequence, which DECISIONS-v1.3 section 13 now handles); 2²⁹ runs next | the 2²⁹ runs, REPORT; then W2 (the JIT's advice streams) suggested | its last message |

## What's left, by milestone

- **v1.2.1: frozen** (2026-11-01). Branch `v1.2.1` merged into `main` and deleted; WORKER15 closed (`sessions/`). Pure `v1.2/`
  checked: `spec/`, `sw/`, `planning/` byte-identical to `v1.2-tony`'s `v1.2_wip/`; only `ERRATA.md` and `README.md` differ (by design).
  Tony's diff: `git diff --no-index v1.2/spec/ v1.2.1/spec/` (also at the top of `v1.2.1/CHANGES-v1.2.1.md`).
- **v1.25: frozen** (2026-11-01) as `v1.25/`: `wip/` copied without `tracer/`, `planning/DECISIONS-v1.3.md` and this handover. Acceptance
  Quest 083/084 (every window at 2²⁹); DECISIONS-v1.25 sections 8-9 merged (`s8`, `s9`) and `cc-cores` (C&C 1-3 as real cores,
  opt-in); freeze check Quest 093 (p4 1,229,008 cycles; 25 selected real windows in stages A, B, F) and 094 (quick 36/36, full
  11/11). WORKER10, 11, 13, 14 closed (`sessions/`). After the freeze: recipe 27's `command()` parity loop (about 380 of a phase's
  435 cycles; DECISIONS-v1.25, after the freeze); WORKER11's builder change on jolt-asic-planning `w11-section9` (be4cdac),
  unmerged, for the owner to merge into PR #2.
- **v1.3, the Tracer:** decided so far in `DECISIONS-v1.3.md` sections 1-13 (**13: advice comes from the host in the read log,
  128 streams per 2²⁹ run built by jolt-cpp's JIT; no untraced rows; supersedes 7's untraced rows, 8, 10**; components, FIFOs, two-pass compression, LRU tables,
  boundaries, output, expansion tables, divider for advice, jolt-cpp move (became v1.25), C&C 3 generating long expansions, the
  100 ms budget, the region's parts 0 and 1). **Next research area (owner): the Tracer's memory**
  (section 12): the page table as a two-level, sparse structure over 4 GiB, a base register at Jolt's `lowest_address`, what an
  unmapped page means, the entry format, how C&C 3 builds it per proof; upstream Jolt's layout findings are recorded there
  (contiguous range; I/O and advice below 0x8000_0000, program, stack and heap above; both ends used, middle empty). Open: T2
  (WORKER16), T3; work item W2 (the JIT's advice streams); section 13's open (b), whether bank 1 has any use left. Candidates C2
  (signed counters), C3 (trim the 256-bit word) in the v1.3 log (C4 moot); ST1 (compare
  jolt-cpp and Rust Jolt traces) as a study.

## Open items for the owner and Tony

- Tony: his full CSR list with reset values (v1.2 Q1, partly answered); what the soft reset keeps; WORKER13's Q10 (v1.1 gave
  the gather ordinal as [63:32] and [51:32]: which did the RTL build; guide L9); the simulation-only assertions (section 8 a);
  whether the region-decode bad packet passes section 8 a's test (item 142); section 8 e's two: whether the replayer's dropped
  overrun push is counted for free, and Q12 (reserved words read 0 and ignore stores, kept only if free).
- Whether the 100 ms Tracer budget is per proof (v1.3 section 11 assumes so).
- The Tracer's memory research (above).

## Pointers

- Repositories: meshsim (this), jolt-asic-planning (`tracer-standalone`, PR #1; `tracer-standalone-main`, PR #2; `worker12-counts`),
  jolt-cpp (snarkify, our tracer), a16z/jolt (upstream, public), Quest (the runner). Tokens are in the owner's preferences.
- Closed workers' reports: `wip/planning/sessions/` (WORKER4-15) and `wip/planning/reviews/` (WORKER1-3).
