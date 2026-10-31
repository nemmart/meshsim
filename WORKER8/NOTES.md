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
