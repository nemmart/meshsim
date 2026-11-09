# WORKER14 report: M3's G5 (the Tracer, its memory system and its memory)

**So far: done up to the chip.** The Tracer is built, equal to `ref` every cycle, wired into W15's slot, and timed. What
remains waits for W16's chip assembly (T2's exactness may bring Tracer questions back through the owner).

Branch `sim-w14`, merged into `main` piece by piece (last a2009dc). Day-by-day detail is in `NOTES.md`; this is what
the integrator, W15, W16 and the rest of M3 need.

**State at hand-off.**
- **The Tracer equals `ref` on every call, every cycle**, observed through 61 fields of its state after each cycle and
  its fingerprint (AUDIT-M3 6.6) at intervals:
  - TracerTest's ten cases (2.28 M cycles; the 180 assertions pass on `ref`'s side as the fixture is made);
  - TracerConf's seeded guests (9 fixtures in `make check`; 178 more on the runner, 43 locally);
  - **T1 on real segments**: segments 0-3 of evm-zone-29 and cyclone-29, 172 M Tracer cycles and 542 M calls, every
    window also equal to WORKER9's delivery on `ref`'s side (Quest 163).
- **The fingerprint writer** (tr, tr.ic, tr.dc, tr.mem, tr.files) equals StateFpM3 at 1,740 points on the fixtures and
  at 1,720 on T1.
- **In W15's slot** (type 3, C&C 3's peripheral 1): the slot unit with the Tracer equals SlotCore driven directly, in
  plain, step-all and shuffled runs (`test_tracer_slot`).
- **Speed** (Quest 166, one P core): 33.9 / 33.5 ns per Tracer cycle, 1.23x / 1.11x `ref`'s Tracer. Not optimised
  (the owner decides after T2's first timing).
- **My binaries** (`test_tracer_parts`, `test_tracer`, `test_tracer_slot`) pass in all four builds (gcc and clang,
  debug and release), and W15's `test_periph_slot` and `test_slotconf` still pass with the Tracer case in the slot. I
  did not run the tsan builds; the owner reported clang release clean at a2009dc.

## 1. Files

| file | what |
|---|---|
| `src/units/tracer.h`, `.cpp` | the Tracer: `Tracer.java` method for method, its lazy `settle` kept (exact whatever order the slot calls in); `PeripheralBase<Tracer>`; the fingerprint writer (a template, so the state stays private); `own_memory` |
| `src/units/mem_system.h` | `MemorySystem.java`: the shared cache (512 × 8, LRU by stamp), the free-or-clean policy, the write buffer; synchronous, as `ref` |
| `src/units/file_store.h`, `.cpp` | `FileStore.java`: the trees, the statuses, cow_open, path copying, ownership; pages in one dense array (addresses are handed out in order); the region-11 sum kept at every write when asked |
| `src/units/lru_coder.h` | `LruCoder.java` and its packer (`Bits`); the packer's sink a callable |
| `src/units/lru_index.h` | `LinkedHashMap` in access order, fixed capacity: the two caches and the jump LRU, `ref`'s eviction order exactly |
| `src/units/rvc.h` | `Rvc.java` (uint32 arithmetic) |
| `src/units/replay_tables.h`, `.cpp` | `ReplayTables.java` (configuration phase); W15 uses it read-only |
| `src/units/stream_word.h` | the record's stream word (`Replayer.java`'s layout); W15's replayer latches through it |
| `src/units/tracer.mk` | the rules; included **before** `units.mk` (W15's `SLOT_LINK` includes `TRACER_LINK`) |
| `tests/test_tracer_parts.cpp`, `tests/ref/TracerParts.java`, `tracerparts.sh`, `tests/tracerparts/` | the pure pieces against `ref` (RVC all 2^16; LRU streams; FileStore and MemSystem streams; MemorySystemTest's cases; the tables' hashes) |
| `tests/test_tracer.cpp`, `tests/ref/TracerConf.java`, `tracerconf.sh`, `tests/tracerconf/` | TracerConf (section 4) and its replay; `TRACERCONF_STREAM` (a FIFO) and `TRACERCONF_LONG` (a directory) for the runner |
| `tests/test_tracer_slot.cpp` | the Tracer in the slot unit against SlotCore driven directly |
| `bench/w14/tracer_play.cpp`, `TracerPlay.java`, `w14.mk` | the speed measurement (section 6) |
| `planning/sessions/WORKER14/tasks/` | `mk_task.sh` (a kit: sim/ to build on the runner, `ref`'s and the drivers' classes precompiled), `t1-body.sh` (Quest 163), `speed-body.sh` (Quest 166) |

Fixtures: 1.0 MB (tracerparts) and 3.1 MB (tracerconf).

## 2. The units

- **The Tracer** keeps `ref`'s interface and timing, including its lazy settle: inputs at the end of their cycle, a
  region packet landing at t + 2, level reads at the end of t − 1. Fixed state throughout: the record, the tables, the
  input rings, the FIFOs (65,536 each), output queues 1-3, sets 1 and 2. Errors stop it as item 152 says, and are
  recorded as fixed-size facts with named causes (`error_log`). `ref`'s flags (`violate`) are facts too
  (`violations`, `violation_count()` for the slot). UB and `ref`'s IllegalStateExceptions are FATAL.
- **The memory system and the memory are the Tracer's own**, called synchronously as `ref` does (no ports: `ref` times
  every access at the row's start). In the chip the slot creates the Tracer bare and the harness hands it the run's
  memory with `own_memory(store)`, which also makes the memory cold at every state-word landing (T2's `onStateWrite`).
  The tables come as region packets from `tracer_ctl`, as in `ref`. **`install` takes nothing beyond the tile.**
- **The wake rule**: `idle()` is false as in `ref` (the slot steps it every cycle). `must_step()` says when skipping a
  step would be exact, for the framework if it wants it.
- **The slot's calls**: W15's method list, plus `skip_cycles`, `digest_words` (`DIGEST_N` = 8, words 44-51) and
  `consume_events`. A late accept is taken: the slot calls `accept(t)` at level 0 of t + 3, and the packet lands at
  t + 2 though the Tracer may be settled through t + 2. An accept whose landing was already settled is FATAL, as is a
  PULSE_RUN or window store at an already-settled cycle.

## 3. Shared files and the crossings

- **W15's** `row_core.h`, `replayer_alu.h`, `event_counts.h`, `peripheral.h`: used read-only. I merged `sim-w15`'s
  step 1 into my branch early (its own files only), so main took W15's headers first.
- **Mine used by W15**: `replay_tables.h`, `stream_word.h` (read-only).
- **In W15's `periph_slot.cpp`** (its marked places): the include, `case PT_TRACER` in `visit`, the delete in `~Ref`,
  the creation in `install`; beside them the accessor `SlotCore::tracer(n)` (in `periph_slot.h`) and a Tracer branch
  in `digest()` (the debug checks' progress).
- **`Makefile`**: includes `tracer.mk` before `units.mk`, and `bench/w14/w14.mk`.

## 4. How it is checked

- **TracerParts**: `ref`'s RVC, LruCoder, FileStore and MemorySystem on seeded streams, every result line for line.
  It also ports MemorySystemTest's own cases.
- **TracerConf**: `ref`'s Tracer behind a recording proxy (`TracerConf.Rec`).
  - It writes every call and its result, the Tracer's state after every cycle (61 fields: the private ones by
    reflection), the store's hash at each record load, and fingerprints every 250 or 2,000 cycles.
  - Calls are grouped by cycle and identical cycles run-length coded, so a 2.28 M-cycle log is 2.3 MB.
  - The C++ replay compares everything. A call `ref` refuses as UB or with its own ISE must be FATAL here too,
    checked in a forked child. Every Tracer call runs under the allocation hook.
  - **Cases**: TracerTest and TracerRig unchanged, through copies `tracerconf.sh` makes with sed.
  - **Seeded**: three families.
    - mix: loops, calls through the map, every width, RVC, MUL, DIV with its advice, the CSR and inline templates,
      random windows, compression and DMA policies, a second segment, five kinds of stopping error.
    - wb: the write buffer queued far past its depth of 4.
    - wbover: `ref`'s ring overflow, FATAL in both at the same call.
  - **T1**: TracerT1 through the same proxy, its three harness edits between segments recorded (the ownership set, the
    cache made cold, the stall counters cleared), the log streamed through a FIFO into the replay.
- **Teeth**: planted timing mutations fail at their first differing cycle (four in the Tracer, one in the fingerprint,
  RVC, LRU and memory-system ones in the parts). In the slot test, the unit's region packets a cycle late are caught.
- **Not reached by any stimulus**:
  - `reset()` (no `ref` harness calls it);
  - a separate map memory system (`map_mem`, B1's sweep only);
  - an instruction cache other than 128 lines;
  - the write buffer exactly at depth 4 with a drain wait in the parts streams (TracerConf's wb family reaches it
    through the Tracer).

## 5. Surprises worth knowing

- **`ref`'s write buffer queues past its depth of 4** (an early eviction frees its way at once) and throws at 64
  (`write-buffer ring overflow`): a guest storing to 136-176 pages a page apart gets there in three passes. The owner
  ruled it `ref`'s own, self-consistent simplification (D27); W13 recorded it as a finding. The port reproduces it.
- **StateFpM3's cached `files()`** is keyed by the memory system's writes and the state writes, so a harness's own store
  edits between calls (TracerTest's map-hole case) read a stale sum. TracerConf uses the scan 6.6 defines; T2 makes no
  such edits.
- **The framework steps a stand from cycle 1**: a SlotCore reference must start its controller there too.
- **Quest numbers**: I once took a number W15 had claimed and renumbered before the runner reached it. Now: pull,
  re-read `NUMBERS.md` and push the claim in one command, then write the task.

## 6. Speed (Quest 166; not optimised)

The same recorded calls of T1's segments 0-3 replayed on each side alone, one P core of the i5-14400F (D21's record in
the results), 5 repetitions, every pass checked to end on `ref`'s rows and record hash; each harness's own decode cost
measured apart.

| | Tracer cycles | C++ ns/cycle | `ref` ns/cycle (median) | `ref` / C++ |
|---|---|---|---|---|
| evm-zone-29 | 91.8 M | 33.92 | 41.89 | 1.23x |
| cyclone-29 | 80.1 M | 33.47 | 37.03 | 1.11x |

At 34 ns, segment 10's 23.7 M Tracer cycles are about 0.8 s of C&C 3's work.

perf (cycles, C++):
- `settle` 31 %:
  - one stall right after its snapshot copy (about 45 % of it);
  - the live service flags' linear scan of the credits (about 30 % of it).
- The harness with the small calls inlined into it 28 %.
- `row` 10 %, `step` 4 %, `fetch` 3 %, set 1's code coder 2 %, the drain about 4.5 %.
- The memory system and the LRUs under 0.5 % each.

Candidates, cheapest first, for the owner's decision after T2's first timing:
1. Settle's snapshot only when something changed.
2. `flag()` in O(1).
3. The hot fields together, the large arrays after them.

## 7. What's left

- **T2 on the chip** (W16 assembles it with the slot): the harness gives the Tracer its memory (`own_memory` with the
  segment-10 snapshot); the Tracer's five fingerprint units are ready for the M3 references (W13's `golden-m3`).
  Tracer questions from T2's exactness come back to me through the owner.
- **Speed work**, if the owner asks after T2's first timing (section 6).
- **T1 beyond segment 3**: not needed so far (T2 starts from W13's segment-10 snapshot); `t1-body.sh` takes `SEGS`.
