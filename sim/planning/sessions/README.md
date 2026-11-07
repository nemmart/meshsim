# sim/ sessions: the new series, numbered from WORKER1

The workers of the next-simulator project (from 2026-11-06). The old series (WORKER1-30, v1.0 to v1.3) is in
`wip/planning/sessions/` and `wip/planning/reviews/`; numbers here never refer to it.

**While a worker runs**, its folder is `WORKERn/` here: `PROMPT.md` (the integrator's), and the worker's own `NOTES.md`,
`QUESTIONS.md`, `REPORT.md`. A message for a busy worker is parked in `WORKERn/NEXT.md` and removed once sent. Code goes on
the worker's branch (`sim-wn`); the integrator merges into `main`. **Quest numbers** (two collisions so far): pull Quest and re-read `NUMBERS.md` immediately before claiming, and push the
claim before writing the task; on a rejected push, pull and take the next free number. **When it closes**, its report, questions and data move
here as `WORKERn-REPORT.md` etc., with a line in the table below, and its folder goes.

| session | what it did | kept here |
|---|---|---|
| WORKER1 | the build (plain make, gcc and clang, debug with ASan/UBSan and release), `FATAL`/`DCHECK`, the allocation hook, the cycle loop's structures (Ring, Bitmap, ValuePool, Bucket, the arena) with unit, differential and death tests; the runner probe (Quest 121: hybrid 6P+4E, gcc only, no riscv toolchain) and D4's answer | `WORKER1-REPORT.md` (the interfaces, the runner, notes for W3 and W6), `WORKER1-NOTES.md` (the plan and its details) |
| WORKER2 | the reference side: the one observer in `sim/ref` (D5, 7 call sites, proven by Quest 123: 39/39 quick-suite steps identical off and on); `sim/tests/TRACE-FORMAT.md`; the coupling audit (now `sim/tests/AUDIT-M1.md`); the M1 programs in `sim/fw/` (csr ×3, timers, irq, wfi_cmd, wfi_go, reqfull); the trace runner and 67 golden traces with `regen.sh --check`; the baseline (Quest 123: 36.8 µs per chip-cycle for one JVM, ~100-110 K chip-cycles/s for the box) | `WORKER2-REPORT.md` (the proof per step, the baseline table, reqfull), `WORKER2-NOTES.md` |
| WORKER6 | the first benchmarks (`sim/bench/`): a synthetic 8×8 mesh, link word (a) two words against (b) three inline, runtime against fixed width, the destination id's place, ready marks against polling, the credit wheel's share, throughput against N and footprint per mesh (D15); Fp128 multiply (Crandall fold, D14) in three forms, inline against pool-held operands, gcc against clang (Quest 124, 125, 126) | `WORKER6-REPORT.md` (the tables and the recommendations for W3), `WORKER6-NOTES.md`, `WORKER6-QUESTIONS.md` |
