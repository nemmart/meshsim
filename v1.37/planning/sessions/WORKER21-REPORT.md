# WORKER21 report: P7a, the Tracer's rig and directed cases (inventory C21, C22, C23)

**Done.** v1.3's three Tracer suites run in C++ without Java or `ref`, each one's output byte-identical to v1.3's
log of the same `run_tests.sh` step. TracerRig is a real port, and its calls equal `ref`'s rig's, call for call. All of
it is in `make check`, clean on gcc and clang, debug and release. Branch `sim-w21`; merged into main to 731008c, the
rest after it (step 7, the planted bugs, this report).

| suite | v1.3 step | result |
|---|---|---|
| MemorySystemTest | `memsys` | 56 passed; output equal to `ref`'s log |
| RvcTest | `rvc` | 49,157 passed (all 49,152 compressed encodings against gold's); output equal |
| TracerTest on TracerRig | `tracer` | 180 passed, both printed lines (the loop's figures, the output path's); output equal |
| the rig's calls | (TracerConf's `cases.log.gz`) | 37 Tracers, every store dump, 6,862,252 calls equal to `ref`'s rig's |

## 1. Files

| file | what |
|---|---|
| `tests/tracer_rig.h`, `.cpp` | **TracerRig in C++**: the assembler, `Prog`, `bases`, the image and the map, the ideal controller (`step`, `window`, the DMA, advice and expansion feeding, the service policies, `on_step`), `collect`, `stream_values`; `mem_summary` (MemorySystem.summary's line, Java's HALF_UP `%.2f`); **`JavaLongHashMap`** (the page order, section 3); **`RecTracer`** (the call recorder, section 4) |
| `tests/suite_memsys.cpp`, `suite_rvc.cpp`, `suite_tracer.cpp` | the three suites on W20's `suite.h`, Java's order and wording; binaries `suite_memsys`, `suite_rvc`, `suite_tracer` (`--calls FILE` records the calls) |
| `tests/test_tracer_suites.cpp` | make check: each suite run as `run_tests.sh` runs a step (stdout and stderr), equal to `tests/tracersuites/<step>.log`, exit 0 |
| `tests/test_tracer_rig.cpp` | make check: `JavaLongHashMap` against Java (49 cases); the rig's calls against `ref`'s rig's |
| `tests/tracersuites.mk` | the rules (one `include` line in the Makefile) |
| `tests/tracersuites/` | fixtures: `memsys.log`, `rvc.log`, `tracer.log` (v1.3's step logs), `rvc-gold.txt.gz` (gold's 49,152 expansions), `hashorder.txt`, `PROVENANCE` |
| `tests/ref/tracersuites.sh`, `GoldRvc.java`, `JavaHashOrder.java` | the Java side: makes the fixtures from `sim/ref` and gold (`--fixtures`), `--check` regenerates and compares (equal) |
| `tests/tracer_teeth.sh` | the planted bugs (section 5); not in make check |

**Where the fixtures sit.** The generators are in `tests/ref/` (oracle side, for W19's move). The fixtures they write
are read by `make check` (the stand-alone suites' references), so they are in `tests/tracersuites/`, committed fixtures
in v1.35 as inventory I6 says. One exception: `rig_calls_equal_refs_rig` reads W14's `tests/tracerconf/cases.log.gz`;
if W19 moves `tests/tracerconf/` into the oracle, that test goes with it (prompt item 4: the recorded streams are the
oracle's deeper check).

## 2. How it is checked

- **Log-equal**, the inventory's check for C21-C23: the C++ suite's stdout and stderr equal v1.3's `results/<step>.log`
  byte for byte (no timing in these three logs to mask). v1.3 commits no log for these steps, so `tracersuites.sh`
  makes them from `sim/ref` (JDK 21) and commits them; `--check` regenerates them identically. Each of the three was
  equal on its first run.
- **The assertions**: every `T` call of the Java, in order, with Java's text (W20's `suite.h`). TracerTest's three
  UB stops run in `suite::ub`'s forked child and must FATAL naming "credits cover", "past word 67", "65,536".
- **C22 against gold**, not `ref`'s Rvc: gold's `Decoder.uncompress` on every compressed encoding, committed once
  (gold stays Java in `pipeline/`, D40). Without the file the comparison is PENDING, as v1.3's RvcTest without gold.
- **The rig's calls** (section 4): stronger than the log, since TracerTest's assertions and printed lines don't see
  everything the rig does (two planted rig bugs pass the log and fail here, section 5).
- **The allocation hook**: the rig's per-cycle path (`step`, `drain_port`, the window loop) runs under it, recording
  included.
- **Builds**: `test_tracer_suites` and `test_tracer_rig` on gcc and clang, debug (ASan, UBSan) and release, about a
  second each; `make run` on clang release, all binaries (62 before step 7). No engine units were added, so the
  debug checks (shuffle, both orders, step-all, missed-wake) have nothing new to run on; the Tracer in the slot stays
  W14's `test_tracer_slot`.

## 3. The page order (owner's request: recorded here, in NOTES and in the rig)

`ref`'s rig keeps the guest's pages in a `HashMap<Long, long[]>` (filled by `poke`'s `computeIfAbsent` and by `put`)
and the pages to pre-copy in a `HashSet<Long>`; `buildImage` hands them to FileStore in iteration order. FileStore
allocates in the order it is handed pages, so **the order fixes every page's address**, the addresses fix the shared
cache's sets, and the sets fix conflicts, evictions and timing. Another order builds an equally valid machine whose
timings need not be `ref`'s.

- `JavaLongHashMap` models JDK 21's HashMap call by call: Long.hashCode and the spread; **`put` appends** a new key to
  its bin and grows the table after inserting; **`computeIfAbsent` prepends** it and grows before; their treeify
  thresholds (9 and 8 in a bin), treeifyBin's resize under 64 bins; resize's order-keeping split.
- My first version modelled `put` only. The Java-side fixture caught it: in TracerTest's `rig()` Java hands FileStore
  **the data page before the code page**. TracerTest's log was equal either way (its few lines never meet in a set);
  the store dumps of step 7 now confirm the order against `ref`, every page by address.
- **A limit, not a guess**: a bin Java treeifies (9 keys in one bin of 64 or more, e.g. 11 or more consecutive pages of
  one 64 KB block) iterates by its red-black tree's root, which isn't modelled; the map records it and `order()` is
  FATAL. The fixture has 3 such cases (Java reports them by reflection) and the model flags exactly those. TracerTest's
  guests have 6 pages at most; a later rig user with bigger guests would need the tree (or a FATAL tells them).

## 4. The rig's calls against `ref`'s rig's (step 7)

`RecTracer` is the rig's Tracer. It hides (doesn't override: the Tracer's code is untouched) every call TracerConf.Rec
records, and with a log open writes each as Rec does: ids at construction, probes included; "N"; the store dumps at the
first call and at each word-0 packet with new pages (else the store's hash); each cycle's calls with Rec's "@d"
offsets and their results. Fixed buffers: nothing allocates while recording. `test_tracer_rig` runs `suite_tracer
--calls` and compares with W14's `cases.log.gz` Tracer by Tracer:

- **37 Tracers with `ref`'s ids** (so probes are made where Java makes them); **every store dump identical** (410
  lines); **6,862,252 calls equal**, token by token, cycle by cycle. Both logs are expanded to one (cycle, call)
  stream per Tracer, so how each side runs equal cycles together doesn't matter.
- **The UB checks**: `suite::ub`'s child makes calls that are in `ref`'s log and not in ours. The suite marks each
  (`tr.mark_ub()`, a "U" line) and the comparison skips `ref`'s calls from there to the one that stopped ('!'):
  required to be exactly 5 (`os,1,257!`; `os,0,69!`; `u`, `st`, `st!` for the 65,536 rows).
- With W14's replay of the same log (`test_tracer`: the Tracer's state after every call equals `ref`'s), **the C++
  rig is `ref`'s rig call for call**, not only in outcome.

## 5. Planted bugs (`tests/tracer_teeth.sh`, 77 s)

The tree copied, one bug planted by sed, rebuilt (clang release), both tests run. Every bug caught:

| planted bug | caught by |
|---|---|
| memory system: an LPDDR miss 81 cycles, not 80 | memsys, tracer logs; rig calls |
| FileStore: a path not owned reads as a hole | memsys log |
| RVC: C.LI/C.ADDI's immediate loses bit 4 | rvc log |
| Tracer: 5 cycles a row, not 4 | tracer log; rig calls |
| a stall: the map probe's compare 3 cycles, not 2 | tracer log; rig calls |
| an ERRORS bit: LD aligned on 4 bytes | tracer log; rig calls |
| the output path: a transfer one word past its credits | tracer log (the UB check runs on); rig calls |
| the rig: service always from queue 1 | **rig calls only** |
| the rig: `computeIfAbsent` appends (the page order) | **HashMap check and rig calls only** |

The last two are why step 7 matters: TracerTest's assertions compare outputs that are the same in any service order
(its own policy test says so) and on any page placement its guests reach.

## 6. Notes for others

- **Tracer::reset()** is reached by no case, as in v1.3 (inventory B16): noted as unreached, no case added (owner).
- **No bug found in W14's Tracer**: nothing for the integrator there.
- **For W20**: `suite.h` fitted without changes. Its header says a helper for Java's double formatting comes with the
  first suite that prints one: `mem_summary`'s `java_2f` in `tracer_rig.cpp` (shortest round-trip digits, then
  HALF_UP), local for now; lift it into `suite.h` if another suite needs it. The suites are programs (`suite_*`), so
  W20's runner can call them as steps `memsys`, `rvc`, `tracer` with v1.3's log names; `test_tracer_suites` shows the
  comparison.
- **For W19**: generators in `tests/ref/` (`tracersuites.sh`, `GoldRvc.java`, `JavaHashOrder.java`) move to the oracle;
  `tracersuites.sh` reads `sim/ref/meshsim` and `sim/ref/gold`. The tables come from `ReplayTables::default_dir()`, so
  your repoint carries over. `rig_calls_equal_refs_rig` reads `tests/tracerconf/cases.log.gz` (section 1).
- **For P8's cleanup**: MemorySystemTest's hand-ported CHECKs in `test_tracer_parts.cpp` duplicate `suite_memsys`
  (left alone, owner's Q2).
- **Local tools**: the container had a JRE and gcc only; the JDK (21.0.13) and clang (18.1.3) came from apt.
