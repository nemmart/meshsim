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

## Log

- **Step 0** (2026-10-31): branch, toolchain, build checked (src, tests minus PENDING, sw/).
