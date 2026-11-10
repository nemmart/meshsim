# Worker 17 (sim/, v1.35): all 128 segments, back to back

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`), with v1.3's Java simulator
(`sim/ref/`) as the exactness oracle. Three milestones are closed: M1 (the framework and the RV32 controller), M2 (the
sort, exact, 20.9× `ref`), and **M3: v1.3's segment test T2 on the new framework, exact at every point on segment 10
of both reference workloads and 6.5× `ref`** (`sim/planning/PLAN-M3.md`, D36: the first step of v1.35). Only one
segment of 128 has been run. **You run all 128 segments of each workload, back to back**, the owner's next step.

READ FIRST: `sim/planning/PLAN-M3.md` (the close-out, "Carried to v1.35's next steps"), `sim/planning/DECISIONS.md`
(D21, D26, D30-D36), **`sim/planning/sessions/WORKER16-REPORT.md`** (the T2 harness, the `t2` tool, the cycle numbering,
the fingerprint labelling, Quests 170-171) and `WORKER16-NOTES.md`, `WORKER13-REPORT.md` and `WORKER13-NOTES.md` (the
reference runs, the runner's data paths and hashes), `WORKER14-REPORT.md` (T1 on real segments: the carry between
segments as `ref`'s TracerT1 does it), `WORKER15-REPORT.md` (Stage A on every window). **The oracle:**
`sim/ref/meshsim/test/TracerT1.java` (segments in sequence, the memory carried), `TracerT2.java`, and W13's
`sim/tests/ref/M3Seg.java`.

THE WORK
1. **The chain.** Segment 0 starts from the initial image in the runner's `w26-jumpmap` data; each later segment starts
   from the memory the previous one left, carried as `ref`'s T1 carries it (including its harness edits between
   segments: the ownership set, the cache made cold, the stall counters cleared, or whatever T2's form of them is).
   Each segment boots as T2 does now. Establish from `ref` exactly what carries between segments and what is fresh.
2. **The checks, every segment:** v1.3's own acceptance check (every tile's rows equal `rows.bin`, count and hash;
   every tile exits 1; C&C 3's results report status 1 and its windows; `lag_cycles` 0), plus each segment's C&C 3
   exit and end cycles, reported.
3. **The checks on the carry:** when the chain reaches segment 10, its starting memory must equal W13's snapshot
   (Quest 167's files dump) byte for byte, and segment 10 must then match `golden-m3` at every point as before. Also check
   each segment's starting memory against W26's record of the pages written per segment, where that applies.
4. **A driver** (extend W16's `t2` tool or add one) that runs a range of segments of one workload as a chain and reports
   each segment's checks, cycles and timing, plus the totals. One thread for the timing (D26); run the two workloads as
   two independent chains side by side on the runner.
5. **The full runs on the runner:** all 128 segments of evm-zone-29 and of cyclone-29, the machine recorded (D21), the
   time per segment and in total. For scale: about 7-8 s per segment on one thread (Quest 171), so about 16 minutes per
   workload.
RULES
- **Propose your plan first** in your `NOTES.md` and wait for the go-ahead before writing code.
- Work on your own branch; merge `main` in often (never rebase); the integrator merges into `main`. Keep notes, questions
  and report in your session folder.
- `sim/ref` is frozen (D5). Exactness is the requirement: a difference from a reference is a bug, never a tolerance; if
  you believe a reference is wrong, stop and report it with the evidence.
- The cycle-loop rules (D6, D24): no STL, no allocation, no virtual calls in any `step` or engine path, diagnostics
  included; untyped 64-bit words; three-word link words (D17). Sanitizers and deterministic tests from the first commit;
  `make check` clean on gcc and clang (run it build by build if the tool limit bites). Every new unit passes the debug
  checks: shuffle, both orders, step-all, the missed-wake detector.
- Long runs and all timing go to the runner. **Quest numbers:** pull Quest and re-read `NUMBERS.md` immediately before
  claiming, push the claim before writing the task. Tasks carry embedded kits. **No machine-specific values** (D21): CPU
  lists from `sim/tools/machine.sh`, every result with its machine. Background jobs don't survive a turn.
- Never write a token into a file. Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, keep the last tool calls for committing, end each turn with a clean
  `git status`.
- Keep replies to the owner short.
- Branch `sim-w17`, in `sim/tests/` (the driver, the harness) and `sim/src/units/` only if a unit needs a change for the
  carry; every unit's owner has closed, so note each change and its reason. The framework has no active owner: if the
  API needs a change, tell the integrator. Exactness first (D30 applies): a segment that fails its acceptance check is a
  bug until explained.

DELIVERABLES: the chain and its driver; segment 10's start equal to W13's snapshot; all 128 segments of both workloads
passing T2's acceptance check, with cycles and timing; `WORKER17/REPORT.md`.
