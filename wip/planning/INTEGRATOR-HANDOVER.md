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
| WORKER16 | T2 done (`WORKER16/REPORT.md`: answered, DECISIONS-v1.3 section 12); now **W2**: the JIT's advice streams, and how jolt-cpp handles registers 40-47 at a chunk's start | starting W2 | plan first | its W2 prompt |
| WORKER17 | Jolt's memory, from the tracing side (layout, accesses per row, what the workloads touch) and the checking side (the RAM, register and bytecode sumchecks), connected | starting | plan first | start with "Clone meshsim and follow WORKER17/PROMPT.md" |
| WORKER18 | a self-contained Java emulator of Jolt's tracer (`wip/gold/`), to become our gold (DECISIONS-v1.3 section 17, W4) | starting | plan first | start with "Clone meshsim and follow WORKER18/PROMPT.md" |
| WORKER19 | the v1.26 spec: v1.25 plus the replayer's v1.3 changes (`DECISIONS-v1.26.md`) | starting | plan first | "Clone meshsim and follow WORKER19/PROMPT.md" |
| WORKER20 | meshsim and the software at v1.26, the deliveries' conversion and the acceptance runs | starting | plan first | "Clone meshsim and follow WORKER20/PROMPT.md" |

## What's left, by milestone

- **v1.2.1: frozen** (2026-11-01). Branch `v1.2.1` merged into `main` and deleted; WORKER15 closed (`sessions/`). Pure `v1.2/`
  checked: `spec/`, `sw/`, `planning/` byte-identical to `v1.2-tony`'s `v1.2_wip/`; only `ERRATA.md` and `README.md` differ (by design).
  Tony's diff: `git diff --no-index v1.2/spec/ v1.2.1/spec/` (also at the top of `v1.2.1/CHANGES-v1.2.1.md`).
- **v1.25: frozen** (2026-11-01) as `v1.25/`: `wip/` copied without `tracer/`, `planning/DECISIONS-v1.3.md` and this handover. Acceptance
  Quest 083/084 (every window at 2²⁹); DECISIONS-v1.25 sections 8-9 merged (`s8`, `s9`) and `cc-cores` (C&C 1-3 as real cores,
  opt-in); freeze check Quest 093 (p4 1,229,008 cycles; 25 selected real windows in stages A, B, F) and 094 (quick 36/36, full
  11/11). WORKER10, 11, 13, 14 closed (`sessions/`). After the freeze: recipe 27's `command()` parity loop (about 380 of a phase's
  435 cycles; DECISIONS-v1.25, after the freeze); WORKER11's builder change (section 9) is merged into PR #2's branch
  `tracer-standalone-main` (8d546e3).
- **v1.3, the Tracer:** **decided in `DECISIONS-v1.3.md` sections 1-25** (2026-11-01; read them, they are the record). In
  short: the Tracer is its own core (25), C&C 3's peripheral, sharing with the replayer the region map (12), the 68-word state
  record (23), the universal tables and templates (13) and the controller-fed expansion path; advice comes from the host as the
  Tracer's read queue (13, 23, 24), no untraced rows, 128 registers; the host sends per proof a stream of frames (15, 16: page
  counts per segment and per ASIC, the jump-target supplement, then per segment processor state, advice, updates; one byte
  stream, identical for every ASIC); memory is files (19, 20, 22: 4 KB pages, trees growing by depth, sparse and dense,
  copy-on-write with owned bits and freeze, per-proof arenas sized by frame 1, the guest's memory as the `.pages` file); the
  memory system translates (root, depth, line) requests through a shared 512 KB, 8-way, 128 B-line cache near the memory (12,
  21); the Tracer has 16 KB instruction and data caches, a 64-entry jump LRU over a host-built jump-target hash table (18), and a
  control loop of start, RUN_CYCLES (typically 65,536), go, done, with six wake flags (24); errors it checks (25); padding rows
  after the program's end (25). Gold, the Java emulator (17), matches jolt-cpp and upstream row for row. **Next: a spec chapter
  for the Tracer** (proposed outline in the session: overview, region and record, control, execution, memory, output, boundaries,
  errors, the replayer's v1.3 changes), with the 512 KB cache a black box. **v1.26** (`DECISIONS-v1.26.md`): v1.25 plus the
  replayer's v1.3 changes, WORKER19 (spec) and WORKER20 (meshsim, software, acceptance), started 2026-11-01.
  Open in v1.3: register offsets beyond words 32-35; sizes to confirm by gold (caches, TLB); UPSTREAM-DIFFS D1, D3, D5 follow
  upstream (the verifier is upstream's, jolt-cpp's submodule at 7189216; not yet recorded as ruled), D2 and D12 deferred; demo
  guests where upstream works and jolt-cpp doesn't (D1 with `.rodata` in the executable segment; C.SLLI x0), proposed for WORKER18.
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
