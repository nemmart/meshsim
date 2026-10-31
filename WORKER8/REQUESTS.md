# WORKER8 requests (for WORKER4)

**R1. The sort's test files and run_tests.sh lines are WORKER8's** (owner, 2026-10-31): `test/SortTest.java`,
`Inputs.java`, `SortSummary.java`, `VerifySummary.java` are off `test/PENDING` on `v1.2-sort`, and `run_tests.sh`'s
sort steps are back there. When the branches meet, please keep both as `v1.2-sort` has them.

**R2. The interim boot.** SortTest boots as `Mach.boot` does (the interim loader at `Registers.LOADER_BASE`, then RUN).
When phase 4's boot ROM replaces it, please say so in NOTES.md; I'll move SortTest's `boot()` with `Mach.boot`.

**R3. `test/SplitRun.java`** drives `sw/ext/split-bisect/splitdrv.c`, which already used v1.0's names at v1.1 and isn't
in `run_tests.sh`. Proposal: retire both, or I port `splitdrv.c` if you want SplitRun back. Your call (SplitRun is in
`test/`).
