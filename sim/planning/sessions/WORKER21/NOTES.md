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

## Log

- **2026-11-11, go-ahead** (owner): the plan as written. Q1: build on W20's `T` harness (`suite.h`), published first;
  step 1 meanwhile; anything that doesn't fit goes to W20 through the owner. Q2: `test_tracer_parts.cpp` left as is;
  duplicates go in P8. Q3: `Tracer::reset()` noted as unreached, as v1.3. Java-side scripts in `tests/ref/` (W19 moves
  them to `sim/oracle/`).
- **Step 1 done.** `tests/ref/tracersuites.sh` (`--fixtures`, `--check`) and `GoldRvc.java`; fixtures in
  `tests/tracersuites/`: `memsys.log` (56 passed), `rvc.log` (49,157 passed; 49,152 compressed encodings, 2,472 with no
  valid form), `tracer.log` (180 passed, the loop's and the output path's lines), `rvc-gold.txt.gz` (49,152 lines),
  `PROVENANCE`. `--check` regenerates them identically. ~11 s. The container had a JRE only: the JDK (21.0.13) came
  from apt; nothing about the version shows in the logs.
- **The rig** (`tests/tracer_rig.h`, `.cpp`), ahead of W20's `suite.h` (it doesn't need it). TracerRig.java member for
  member; the window loop, `step` and `drain_port` under the allocation hook (fixed rings, preallocated DMA buffers);
  `java_hash_order` reproduces HashMap/HashSet iteration for Long keys (the image's pages and the owned set are put in
  that order, which decides FileStore's addresses and so the caches' sets); `mem_summary` reproduces
  `MemorySystem.summary()` (Java's `%.2f` rounds HALF_UP on the decimal digits; done that way). A scratch driver of
  TracerTest's loop case printed `ref`'s `loop:` line exactly (34 rows, 550 cycles, the stalls, the summary), clang
  release and debug (ASan/UBSan), gcc compiles it. Not yet in the build: the suites that link it wait for `suite.h`.
  Local tools: the container had neither a JDK nor clang; both from apt (JDK 21.0.13, clang 18.1.3, gcc 13.3).
- **2026-11-11, merged main** (W20's `suite.h`, f615486). Owner: build the three suites on it; record the HashMap order.
- **Java's page order sets the page addresses (recorded at the owner's request; also in `tracer_rig.h`'s header and
  at `build_image`).** `ref`'s rig keeps the guest's pages in a `HashMap<Long, long[]>` (filled by `poke`'s
  `computeIfAbsent` and by `put`) and the pages to pre-copy in a `HashSet<Long>`; `buildImage` hands them to FileStore
  in iteration order, FileStore allocates in the order it is handed pages, so the order fixes every page's address, the
  addresses fix the shared cache's sets, and the sets fix conflicts, evictions and timing. Another order builds a
  different, equally valid machine whose timings need not be `ref`'s.
  - **First version was wrong, and its check caught it.** `java_hash_order` modelled `put` only. A Java-side fixture
    (`tests/ref/JavaHashOrder.java` → `tests/tracersuites/hashorder.txt`, 49 cases) showed `computeIfAbsent`
    **prepends** a new key to its bin where `put` appends, grows the table before inserting where `put` grows after,
    and treeifies one key earlier. In TracerTest's `rig()` that puts the **data page before the code page** (both poked
    into bin 0); in the unaligned loop, page 0x1000 before page 0. Now `JavaLongHashMap` models both calls operation by
    operation (Long.hashCode, the spread, resize's order-keeping split, treeifyBin's resize under 64 bins);
    `test_tracer_rig` checks it on all 49 cases.
  - **A limit, not a guess**: a bin Java treeifies (9 keys in one bin of a 64+ table, e.g. 11+ consecutive pages of one
    64 KB block) iterates in an order that follows the red-black tree's root. Not modelled: the map records it and
    `order()` is FATAL. The fixture has 3 such cases (Java reports them by reflection); the model flags exactly those.
    TracerTest's guests have 6 pages at most.
  - **TracerTest's own figures don't see the order**: the suite's output equalled `ref`'s log with the wrong order and
    with the right one (its few lines never conflict in a set). The calls check (step 7) will: TracerConf's
    `cases.log.gz` carries the store's pages by address.
- **Steps 2-4 done, on `suite.h`.** `tests/suite_memsys.cpp` (56 passed), `suite_rvc.cpp` (49,157 passed; gold's
  fixture), `suite_tracer.cpp` (180 passed, both printed lines): **each output equals `ref`'s log byte for byte on the
  first run**. `tests/tracersuites.mk` (included by the Makefile) builds `suite_memsys`, `suite_rvc`, `suite_tracer`;
  `test_tracer_suites` (make check) runs each as `run_tests.sh` runs a step (stdout and stderr together) and compares
  with `tests/tracersuites/<step>.log`, exit 0 required. TracerTest's two UB stops run through `suite::ub` (a forked
  child, FATAL naming "credits cover", "past word 67", "65,536"). Rigs are made where Java makes them, probes included,
  so a recorder can number Tracers as TracerConf does. The Java-double helper `suite.h` mentions is in the rig
  (`mem_summary`'s HALF_UP `%.2f`); W20 may lift it.
  - All four builds (gcc, clang; debug with ASan/UBSan, release): `test_tracer_suites` 3/3, `test_tracer_rig` 2/2,
    about a second each. `make run` on clang release: all 62 binaries pass. TracerTest alone: 0.3 s release.
  - `tracersuites.sh --check`: the fixtures (now with `hashorder.txt`) equal `ref`'s regeneration.
