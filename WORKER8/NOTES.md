# WORKER8 notes: the sort to v1.2

Branch `v1.2-sort`, from `v1.2-meshsim` at `e7a2106` (phase 2's last commit, green; the owner's choice over the
phase-3 tip, whose replayer tests don't compile). Merge `v1.2-meshsim` again once WORKER4 is green.
`WORKER8/PROMPT.md` copied from `main` (`a2a4700`), which `e7a2106` predates.

## Agreed with the owner (2026-10-31)

- The plan below; go-ahead given.
- I port `test/SortTest.java`, `Inputs.java`, `SortSummary.java`, `VerifySummary.java` on this branch (noted in
  REPORT.md so WORKER4 leaves them alone).
- Counters per `wip/sw/CONVENTIONS.md`; the fence counter (one of 5-7) is named in REPORT.md.

## Plan

1. Read the spec, RECIPES and CONVENTIONS in full.
2. Build: `sort.ld` on the v1.2 map, overlays dropped if the image fits 32 KB, `loader.S` replaced by meshsim's interim
   loader; `split_regs.h` replaced by the generated `mesh.h`; patches regenerated.
3. The program, file by file (inventory in WORKER3-REVIEW-2 s.6, checked against the spec).
4. `run_tests.sh`'s sort steps back in order: layout, build, L1/L2, every set at 64 x <= 2,000, shuffled, BT forced;
   then full size on the Quest runner.
5. REPORT.md, QUESTIONS.md, REQUESTS.md.

## Environment

- `apt-get install gcc-riscv64-unknown-elf openjdk-21-jdk-headless` (as WORKER4's notes).
- `run_tests.sh` rewrites `results/*.log`; restore with `git checkout -- wip/meshsim/results` before committing.

## Known-failing at each push

| push | known-failing |
|---|---|
| branch made at `e7a2106` | none (the sort's steps PENDING (sort port), as on v1.2-meshsim) |
| step 1 (the program ported) | none in run_tests (the sort's steps still PENDING); the sort builds but has not run |
| step 3 | none known: layout, build, p1 and sort pass; sort-shuf and the full-mode BT runs not yet run on v1.2 |
| step 4 | none known: sort-shuf and sort-bt-kv32 pass; sort-bt-k128 running; run_tests.sh not yet run whole |

## Log

- **Step 0** (2026-10-31): branch, toolchain, build checked (src, tests minus PENDING, sw/).
- **Step 1** (2026-10-31): the sort program, the planner's machine section and the split ported to v1.2 (one image,
  no overlays, no loader, no staging; counters 1-3 by CONVENTIONS 5; waits by events; replies as mode-0 writes with
  no-increment). Found: the planner hard-coded mode 2 (v1.1's write by source, v1.2's bucket write) for its hop-1 and
  empty streams; now SK_MODE_SRC per build. Patches regenerated (split-barrier.patch is now a plain diff against the
  ported split.c). `sw/ext/sort/build.sh` builds: code 21,856 B, data 240 B, .bss end 0x2a94. Not run yet: SortTest
  still v1.1 (next).
- **Step 2** (2026-10-31): SortTest, Inputs, SortSummary, VerifySummary ported (owner: mine on this branch) and taken
  off test/PENDING: boot through meshsim's interim loader (as Mach.boot), no staging, exit through SIM_EXIT with 0,
  ERRORS = 0, the PTR[64] checks retired, counters 1-3 in the state dump; Inputs hashes the interim loader as
  image/loader.bin. First runs: P1 all six sets pass (42 checks; v1.1 54, the 12 PTR[64] checks gone), L1 -1,708 and
  L2 -1,200 cycles on every set; the whole sort, uniform kv32 n=2000, passes: 236,037 vs v1.1's 241,114.
- **Step 3** (2026-10-31): run_tests.sh's sort steps back (layout, build, p1, sort, sort-shuf; full mode: the BT pair
  with KeyGen's trace10-10 at 16,384); split-barrier.patch is now a unified diff (check_layout applies it). The quick
  sort suite passes: 104 checks (v1.1 124: the 20 PTR[64] checks retired), every set on v1.1's path, every total
  2,669-5,230 cycles below v1.1's. v1.1 built from `main` in a worktree (/tmp/v11) for per-function comparisons
  (-Dsort.watch). Explained so far: xchg +80 (ROUTER_HDR's dst moved to [31:25]: one shift per send, + the last
  decrement's K); plan_transfer +306 and config_direct_lb +83 (to confirm in the disassembly: d in STREAM_COUNT).
  Still to run: sort-shuf (in progress), the BT pair, the full suite; then the Quest runner.
- **Step 4** (2026-10-31): shuffled suite (seed 4) passes, 104, same paths. BT forced kv32 at 16,384 passes on BT:
  708,595 vs 709,658; DESC and the BT layers slower by a few hundred each, to explain. BT k128 running (/tmp/bt.sh,
  detached with setsid: plain nohup jobs die with the tool call). Next: the k128 result; itemise every step's
  difference (disassembly, -Dsort.watch against /tmp/v11); read the Quest repo's PIPELINE.md and send the full-size
  runs; the full run_tests.sh.
- **Step 5** (2026-10-31): the BT k128 run passes (929,425 vs 927,893); logs committed (results/sort*.log). Merged
  v1.2-meshsim (WORKER4 phase 3, its quick run passing; test/PENDING: both sides' deletions kept) and main
  (DECISIONS-v1.2 47; planning docs and WORKER9's notes taken as main has them); rebuilt: the sort cycle-identical.
  DECISIONS 47 noted in the code. Explained plan_transfer (+306) and the HIST load latency (REPORT). The kit for
  the runner: WORKER8/runner (run_p4.sh, kit/ with a Java 17 jar, the v1.2 image, the interim loader, v1.1's
  jobs.txt and key hashes, a new HASHES); SortTest reads the interim loader from the image directory when it has one.
  Smoke-tested locally (-n 2000, uniform-kv32: passes). **Quest task 070 pushed** (the 43 P4 runs; kit embedded, sha
  99b56125...). Whole run_tests.sh (all) running in /tmp/rt.log.
- **State at the end of this turn** (2026-10-31): run_tests.sh (all) running in /tmp/rt.log: every suite so far
  passes (Bus, Net ×3, Mem ×3, Engine ×2, Ctrl ×2, Riscv, ReadPortGather, Accounting, SearchBench, AgTest, RegDoc,
  Hooks, PeripheralSlot ×2, RequestBus ×2, Replayer, Stage B), sort-p1 passes (42); sort, sort-shuf and the full
  extras still to come. Only the sort's logs are committed from it (results/sort*.log); the others are WORKER4's and
  are restored with `git checkout -- wip/meshsim/results` before every commit. Quest task 070 queued, not yet picked
  up. Next: the rest of run_tests; the BT -Dsort.watch runs on both versions (16K, kv32 and k128) to confirm the BT
  steps' explanation; 070's results into REPORT.md.
- **Step 6** (2026-10-31): background jobs don't survive a turn's end (run_tests stopped mid-sort: 9 of 10 sets had
  passed); long runs now either finish within a turn or go to the runner. `pkill -f`/`pgrep -f` with a pattern that
  appears in the tool call's own command line kills the call: don't. plan_transfer and k_start step explicit
  pointers (XB back to v1.1's; REPORT). **Task 070: all 43 full-size runs pass, every run on v1.1's path**
  (WORKER8/results/070-p4-full, COMPARE-065.md). Quick sort suite after the change passes (104); shuffled running,
  then run_tests's full-mode extras (/tmp/extras.sh → /tmp/extras.txt). **Task 071 pushed:** per-function watches on
  both versions (BT 16K kv32/k128: BT's functions; uniform and trace100-30 at full size: the split's) to finish
  explaining SPLIT and the BT layers. The runner kit's image refreshed to HEAD (HASHES regenerated; Java unchanged).
- **Step 7** (2026-10-31): task 070 (43 full-size P4 runs) all pass, every run on v1.1's path; task 071 (per-function
  watches, both versions) 8 of 8 pass, the per-step differences explained (REPORT). plan_transfer/k_start step
  pointers (XB back to v1.1's). **run_tests.sh's full-mode extras on this branch (after the merges), run locally:**

  | step | result |
  |---|---|
  | rdport-full (ReadPortGatherTest -1 full) | 567 passed, 0 failed |
  | accounting-full (AccountingTest -1 full) | 11 passed, 0 failed |
  | agbench (AgBench) | exit 0 (measurement output; nothing to fail) |
  | sweep-quick (Sweep quick) | exit 0 (measurement output) |
  | ctrl-full (CtrlTest -1 full) | 6,793 passed, 0 failed |
  | ctrl-full, again on the runner (Quest task 072) | 6,793 passed, 0 failed |
  | engine-full (EngineTest -1 full), Quest task 072 | 398 passed, 0 failed |
  | skewtable (NetTest -1 skewtable 40), Quest task 072 | output identical to the branch's results/skewtable.log |
  | sort-bt-kv32, sort-bt-k128 | pass (results/sort-bt-*.log) |
- **Step 8** (2026-10-31): **every run_tests.sh step now has a result on this branch** (the table above; task 072's logs
  in WORKER8/results/072-extras; 072 ran the branch as of 375bc23, before the phase-4 merge below). REPORT.md's final
  pass done. Merged v1.2-meshsim again (WORKER4's phase 4: **the boot ROM replaced the interim loader, R2**) and main
  (DECISIONS-v1.2 48); test/PENDING keeps both sides' deletions (CondTest, the sort's four); DECISIONS-v1.2 as main.
  SortTest now boots as Mach.boot (code, data, CMD_JUMP, GO; the ROM jumps); Inputs hashes sw/bootrom.hex; sort.ld's
  program memory is the full 32 KB. Cycle-identical (uniform kv32 235,663). On the merged branch: P1 42 passed, the
  sort 104 passed, shuffled running. WORKER4's own suites after their phase 4 are theirs (their NOTES: green from
  f7c694f).
- **Step 9, ready to merge** (2026-10-31): on the branch after the phase-4 merge (the boot ROM), P1 (42), the sort (104)
  and the shuffled sort (104) pass, cycle-identical to the logs before the merge on every set (results/sort*.log
  replaced with these runs). The BT pair and the full-size runs were run before the ROM merge (tasks 070-072 and
  results/sort-bt-*.log); the ROM changes only boot, which is outside the sort's measured total.

  | push | known-failing |
  |---|---|
  | step 9 (ready to merge) | none in the sort's steps; WORKER4's suites as their NOTES say for the merged commit |
- **Step 10, final** (2026-10-31): merged `v1.2-meshsim` at `2569a40` (WORKER4's phase 5: meshsim-only checks for
  invariants 15 (any `ERRORS` bit set is a violation; `Config.checkErrors`, on in SortTest), 16, 20, 21 (reserved
  bits on register stores) and 25; phase 6 started) and `main` (decision-log entries, WORKER9's tables; nothing of the
  sort's). No conflicts. Re-run on the merged branch: P1 42 passed, the ten sets 104, shuffled (seed 4) 104, forced BT
  kv32 10 and k128 11; **no violation from the new checks, nothing to fix in the sort**, and every cycle count
  identical to before (BT 708,221 and 929,115, task 071's final-code figures). results/sort*.log are these runs, the
  BT pair now on the final code too. One finding, not the sort's: the committed mesh.h lags Registers.java by one
  comment (REQUESTS R4). **Ready: no further merges until the integrator merges this branch with v1.2-meshsim.**

  | push | known-failing |
  |---|---|
  | step 10 (final) | none in the sort's steps; a build leaves wip/meshsim/sw/mesh.h modified by one comment (R4) |
