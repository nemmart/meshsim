# WORKER21 notes (P7a: C21, C22, C23)

Branch `sim-w21` from main e90facf.

## The plan (proposed, waiting for the go-ahead)

**What exists.** W14 checked the C++ Tracer by replaying `ref`'s recorded calls (TracerConf, `cases.log.gz` from
`ref`'s TracerTest through sed copies of the rig); MemorySystemTest's cases are already hand-ported as CHECKs in
`test_tracer_parts.cpp`; RVC is checked against `ref`'s Rvc, not gold's. No C++ rig, no suite output to compare.

**The check: log-equal plus the assertions** (inventory's C21-C23). Each suite prints what v1.3's prints (Java `T`'s
`FAIL [...]` lines, its own `System.out` lines, `<Suite>: N passed, 0 failed`), and its stdout must equal `ref`'s log of
the same step (`run_tests.sh`'s `memsys`, `rvc`, `tracer`). v1.3 commits no log for these three steps, so the
references are made once on the oracle side and committed.

1. **Oracle side** (`tests/ref/tracersuites.sh`, into `sim/oracle/` when W19's move lands): compiles `ref` and gold into
   a scratch directory, runs MemorySystemTest, RvcTest (with gold) and TracerTest, and writes
   `tests/tracersuites/{memsys,rvc,tracer}.log` with their provenance; plus `GoldRvc.java`, gold's `uncompress` on all
   2^16 encodings → `tests/tracersuites/rvc-gold.txt.gz` (C22's fixture; gold stays Java in `pipeline/`, D40).
   `--fixtures` and `--check`, as `tracerconf.sh`.
2. **`tests/suite_t.h`**: Java `T` in C++ (group, check, eq, throws, ub, pending, done; the same messages and counts).
   `T.ub` and `throwsISE` become a forked child that must abort with FATAL naming the same words (the parent goes on
   without the call, as W14's replay does). Small; handed to W20 for C1's `T` (Q1).
3. **MemorySystemTest** (`tests/suite_memsys.cpp`): the three groups, all 56 assertions, as the Java.
4. **RvcTest** (`tests/suite_rvc.cpp`): `rvc::uncompress` against gold's fixture on every compressed encoding, the
   code-word rules, the four named cases, its count line.
5. **TracerRig** (`tests/tracer_rig.h`, `.cpp`): a real port of the ideal controller: the assembler, `Prog`, `bases`,
   the image and map builders, the ideal DMA, `loadTables`, `loadRecord`, advice and expansion feeding, `window`,
   `collect`, `streamValues`; the service policies (`order`, `stateLast`, `q0One`, `hold`) and `onStep`. Configuration
   uses the STL; the per-cycle path (`step`, `window`'s loop) uses fixed rings and preallocated output buffers and runs
   under the allocation hook. Tables from `ReplayTables::default_dir()`, so W19's repoint carries over. A test-side
   `mem_summary()` reproduces `MemorySystem.summary()`'s line (W14's MemSystem has no summary; not touching it).
6. **TracerTest** (`tests/suite_tracer.cpp`): the ten cases, every assertion and printed line (the loop's and the
   output path's figures), `TracerTest: N passed, 0 failed` equal to `ref`'s log.
7. **The rig's calls equal `ref`'s rig's**: the rig can write its Tracer calls in TracerConf's token format (an
   optional recorder pointer, null by default; no virtual calls); an oracle-side test compares them with
   `cases.log.gz`, cycle by cycle. Equal calls with W14's replay (state after every cycle) close the loop: the port of
   the rig, not just its outcome, is `ref`'s. It sits with TracerConf in the oracle (prompt item 4).
8. **Teeth** (`tests/tracersuites/teeth.sh`, not in `make check`): copies the tree, plants one bug per area (the
   memory system's timing, FileStore's status, an RVC field, the Tracer's rows, a stall count, the output path's
   credits, an ERRORS bit, the rig's service order), rebuilds the suite and expects a failure; each result recorded.
9. **`make check`**: the three suites run and their stdout is compared with the committed logs (gcc and clang, debug
   with ASan/UBSan, and release); into W20's runner as steps `memsys`, `rvc`, `tracer` when it lands. Each suite also
   run twice in one process where cheap, to catch hidden state.

No engine units are added (the rig drives the Tracer directly, as `ref`'s does), so the debug checks (shuffle, step-all,
missed-wake) have nothing new to run on; the Tracer in the slot keeps W14's `test_tracer_slot`. No Quest task expected:
TracerTest is ~2.3 M Tracer cycles, local.

Commits: (1) oracle script and fixtures, (2) `suite_t.h` + MemorySystemTest, (3) RvcTest, (4) the rig + TracerTest,
(5) the calls check, (6) teeth, make check, report.

## Questions

- **Q1 (W20/integrator).** The `T` harness: I write `tests/suite_t.h` now; W20 adopts or replaces it for C1. OK?
- **Q2 (W19/integrator).** MemorySystemTest's hand-ported CHECKs in `test_tracer_parts.cpp` duplicate the new suite.
  Remove them there (the suite is the stand-alone one), or leave W14's file alone and let W19's oracle move decide?
- **Q3.** `Tracer::reset()` is reached by no v1.3 case (W14; inventory B16). I note it as unreached, not add a case.
