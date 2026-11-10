# WORKER14 (sim/, M3): notes

The Tracer. Branch `sim-w14`. Prompt: `PROMPT.md`.

## Status

**Plan agreed (owner, 2026-11-09): go-ahead.** Step 1 (the pure pieces) done (section 5). Step 2 (the Tracer as a class)
done (section 6). Step 3 (TracerConf's seeded guests, section 7) and the fingerprint writer (section 8) done, all equal to
`ref`. T1 on the runner and the long sweep equal (Quest 163, section 10). W15's interface taken (section 11). Next:
wiring through W15's slot when its body lands.

## 1. What the oracle is

`ref`'s `Tracer.java` (833 lines) is C&C 3's peripheral 1, type 3. It is driven by `PeripheralSlot` through
`Peripheral`'s synchronous calls:
- inputs taking effect at the end of t: `accept` (a region packet, landing at t + 2), `run`, `writeWord`, EVENTS'
  consume;
- level reads returning the end of t − 1: `running`, `errors`, `word`, `events`;
- the DMA's side: `outStart`, `outReady`, `outTake`.

`ref` settles all of this lazily (`settle(T)`). Its fill bus is a **synchronous, analytic** `MemorySystem`: `read` and
`write` return the cycle the data is ready, with one request outstanding, over `FileStore`'s trees of 4 KB pages. Row
timing is computed at a row's start (`busyUntil`, `waitUntil`, `statUntil`). The caches and the jump LRU are
`LinkedHashMap`s in access order (128, 128 and 64 entries).

## 2. Decisions (owner, 2026-11-09)

1. **Coupling.** The peripheral is a **member object of W15's slot unit**, with a fixed method interface and a `switch`
   on its type, no virtual calls (W15 proposed the same: one generic slot for the tiles and C&C 1-3). I implement the
   Tracer's side; the slot, its DMA and `EventCounts` are W15's.
2. **Shared code.** W15 owns `row_core.h`, `replayer_alu.h`, `event_counts.h` and the slot. I own the tables loader (the
   decode table, ROM, controller-fed templates), RVC, `LruCoder`, `FileStore`, `MemorySystem` and the Tracer. Each uses
   the other's read-only and asks for changes through the owner.
3. **The fingerprint.** W13 defines the Tracer's in `AUDIT-M3.md` section 6, wider than `ref`'s `digestWords`
   (registers, caches, FIFOs, output queues), so a mismatch can be localised. My writer follows that definition when it
   lands.
4. **`MemorySystem` and `FileStore` live inside the Tracer**, not as units: `ref` calls them synchronously, so ports
   would add latency `ref` doesn't have. Pages are allocated at configuration only.
5. **FIFOs** sized at configuration, fatal when full; **UB** fatal (`fprintf` + `abort`); **violations** as fixed-size
   facts (D24), formatted after the run.

## 3. The plan

### Step 1: the pure pieces, each against `ref`

| file (`sim/src/units/`) | from `ref` | check |
|---|---|---|
| `rvc.h` | `Rvc.java` | `tests/test_rvc.cpp`: all 2^16 encodings against a digest and spot values from `ref`'s `Rvc` (`RvcTest` checks `ref` against gold) |
| `lru_coder.h` | `LruCoder.java` (the encoder, set 1 counting, set 2 with the packer) | `tests/test_lru_coder.cpp`: seeded value streams, bits and packed words against `ref` |
| `file_store.h`, `.cpp` | `FileStore.java` (trees, statuses, `cow_open`, path copying, ownership) | with the memory system's test |
| `mem_system.h` | `MemorySystem.java` (the shared cache's timing, the write buffer) | `tests/test_mem_system.cpp`: a port of `MemorySystemTest`, plus a seeded request stream against `ref` |
| `replay_tables.h`, `.cpp` | `test/ReplayTables.java` (the four files, the index, `key`, `lookup`) | loaded from `sim/ref/tables/replayer/` in place, sizes and the manifest's hashes checked |

The `ref` side of these checks is a small driver in `sim/tests/ref/` (default package, as RouterConf), writing committed
fixtures; `make check` needs no JDK.

### Step 2: the Tracer as a class (`tracer.h`, `.cpp`)

- `Peripheral`'s method surface: `accept`, `run`, `write_word`, the level reads, `out_start`/`out_ready`/`out_take`,
  `reset`, and `cycle(t)`. `ref`'s lazy settle becomes a once-per-cycle step: inputs of t applied at the end of t,
  landings at t + 2, the snapshot for reads at t + 1.
- It steps every cycle while running (STATUS [0]), and is idle otherwise. Making it sleep through a row's cycles is
  timing work, for later and on the runner.
- State as fixed arrays: the record, the decode table, the ROM, the two input queues, the code FIFO (65,536 × 32 bits) and
  the log FIFO (65,536 × 64 bits plus tags), the output queues, set 1 and set 2's coders and packers. The three LRUs
  become array LRUs with `ref`'s exact eviction order.
- Uses W15's `row_core.h` and `replayer_alu.h` read-only.

### Step 3: checked against `ref` on its own, before any wiring

1. **`TracerTest`'s cases** (`loop`, `cut`, `writeBack`, `compressed`, `division`, `controllerFed`, `outputPath`,
   `errors`, `words`, `counters`) in `tests/test_tracer.cpp`, with a C++ version of `TracerRig`'s ideal controller and
   assembler (`tests/tracer_rig.h`).
2. **TracerConf** (`tests/ref/TracerConf.java`, `tracerconf.sh`, fixtures in `tests/tracerconf/`, as RouterConf):
   - `ref`'s `Tracer` driven through `Peripheral` only, by a seeded stimulus: generated guests (loops, loads and stores,
     mul/div, compressed code, jumps through the map, controller-fed expansions, advice), with randomised controller,
     credit and DMA timing, and error scripts.
   - A fixture records the setup (pages, tables, record), the per-cycle stimulus and the per-cycle observations: the
     level reads, words 32-51, EVENTS, `outReady` per queue, the words taken, the code and log FIFO pushes, the
     counters; then W13's fingerprint streams once section 6 lands.
   - The C++ side replays the stimulus and must be equal line for line. Short fixtures in `make check`, long runs on
     the runner.
3. **T1** (the Tracer with the ideal controller over a real segment), if W13 confirms it's a useful reference: on the
   runner, against the delivery.

### Step 4: wiring into the chip, from `AUDIT-M3.md`

The Tracer's side of the slot (W15's dispatch), the memory system's inputs (the initial image, the map), the DMA. Every
new unit through the debug checks: shuffle, both orders, step-all, the missed-wake detector.

## 4. File names, for W15 (one round)

Mine, in `sim/src/units/`: `rvc.h`, `lru_coder.h`, `file_store.h`/`.cpp`, `mem_system.h`, `replay_tables.h`/`.cpp`,
`tracer.h`/`.cpp`, and `tracer.mk`. Tests: `tests/test_rvc.cpp`, `test_lru_coder.cpp`, `test_mem_system.cpp`,
`test_tracer.cpp`, `tests/tracer_rig.h`; `tests/ref/TracerConf.java`, `tracerconf.sh`, `tests/tracerconf/`.

Yours, as your NOTES name them: `row_core.h`, `replayer_alu.h`, `event_counts.h`, `periph_slot.h`/`.cpp`. I'll use the
record layout and region-map constants from `row_core.h`.

**For W15:** `replay_tables.h` is a configuration-phase loader (STL allowed) returning the decode table, the ROM and the
templates as plain arrays; your tests can use it read-only. Tell me if you want a different name or shape.

**W15's round (sim-w15 3ae62c2): agreed.** The names match on both sides: W15's `row_core.h` (`rowcore::`),
`replayer_alu.h`/`.cpp` (`alu::`), `event_counts.h`, `peripheral.h`, `periph_slot.h`/`.cpp`; mine `replay_tables.h`/`.cpp`,
`rvc.h` (`rvc::`), `lru_coder.h`, `file_store.h`/`.cpp`, `mem_system.h`, `tracer.h`/`.cpp`, all in namespace
`units::tracer` except `rvc::`. The Tracer will be `units::tracer::Tracer` with W15's method list as given (`params`,
`running`, `status_bits`, `errors`, `sync`, `events`, `word`, `write_word`, `store_stalls`, `accept`, `run`, `step`,
`out_queues`, `out_ready`, `out_take`, `out_start`, `reset`, `idle`, `next_wake_at`, violations to the slot's
`violate(t, code)`). Two additions from `Peripheral.java` the Tracer uses: `skip_cycles(n)` (FATAL if the counters would
tick while asleep, as `ref`) and `digest_words` (the counters, words 44-51; W13's wider fingerprint comes on top),
approved (owner, 2026-11-09). W15's `consume_events(bits)` replaces `ref`'s `onConsume` callback; the Tracer implements
it.

## 5. Step 1: the pure pieces (done)

| file | what | against `ref` |
|---|---|---|
| `src/units/rvc.h` | `Rvc.java`, uint32 arithmetic throughout (the Java uses only `<<` and `>>>`) | all 65,536 encodings, `uncompress` and `code_word` |
| `src/units/lru_coder.h` | `LruCoder` and its `Bits` packer; the packer's sink is a callable (no virtual, no `std::function` in the loop) | two seeded streams of 20,000 events: every value's peek and coded bits (set 1's count-only coder too), every packed word, the statistics at each window reset |
| `src/units/file_store.h`, `.cpp` | `FileStore`: pages in one dense array by (address − first) / 32, since `ref` hands addresses out in order; building allocates, the walk and line reads/writes don't | the image built by the same operations (create, put_page, cow_open, make_private, set_ownership: the same roots) |
| `src/units/mem_system.h` | `MemorySystem`: the shared cache, the free-or-clean policy, the write buffer, synchronous as in `ref` | two seeded streams of 8,000 events: every read's status, ready cycle and data hash, every write's acknowledgement, every statistic; the policy reached (up to 77 early evictions and 38 read waits between colds) |
| `src/units/replay_tables.h`, `.cpp` | `test/ReplayTables.java` (configuration phase) | the four files' sha256 against `ref/tables/manifest.json`; every template found by its key |

- **`tests/ref/TracerParts.java`** (`rvc`, `lru SEED N`, `mem SEED N`) writes one event per line, inputs and results; the
  C++ side replays the inputs and must reproduce every result. **`tests/ref/tracerparts.sh`** `--fixtures` / `--check` /
  `N [OUT]` for long runs, as `routerconf.sh`. Fixtures in `tests/tracerparts/` (1.0 MB).
- **`tests/test_tracer_parts.cpp`** (11 tests, in `make check`): the fixtures, `MemorySystemTest`'s cases ported with
  their expected values (statuses, timing, policy; the two refusals as death tests), the tables.
- **Teeth:** planted mutations fail at their first differing line (an RVC field, the write log's E[15] rule, the read
  miss's wait for the write buffer).
- **Not reached by the stimulus:** the write buffer full at its depth of 4 (it never holds more than 2 lines between
  drains here); a stream with longer dirty bursts would reach it. Noted for TracerConf.

## 6. Step 2: the Tracer (done)

- **`src/units/tracer.h`, `.cpp`**: `Tracer.java` method for method, in its order, including its lazy `settle` (so it is
  exact whatever order W15's slot calls in). W15's peripheral method list, plus `skip_cycles`, `digest_words` and
  `consume_events` (owner, 2026-11-09). Fixed state: the record, the tables, the two input rings, the FIFOs (65,536),
  output queues 1-3, sets 1 and 2; the caches and the jump LRU on **`src/units/lru_index.h`** (access-ordered keys,
  open addressing plus an intrusive list: `LinkedHashMap`'s eviction order exactly). ERRORS causes and `ref`'s flags are
  fixed-size facts (`error_log`, `violations`, codes with names); UB and `ref`'s IllegalStateExceptions are FATAL.
- **`src/units/stream_word.h`**: the record's stream word (`Replayer.java`'s layout). **For W15:** the replayer reads the
  same word; use it read-only, or tell me if it should move into `row_core.h`.
- **Built on W15's step 1** (`row_core.h`, `replayer_alu.h`, `event_counts.h`): `sim-w15` merged into `sim-w14`
  (061d54c; W15's files only, no conflict). **For the integrator:** merge `sim-w15` before `sim-w14`.
- **TracerConf** (`tests/ref/TracerConf.java`, `tracerconf.sh`): `TracerConf.Rec`, a recording proxy for `ref`'s
  Tracer. TracerTest's ten cases run unchanged through copies of TracerTest and TracerRig that the script makes with
  sed (the rig's Tracer replaced by the proxy, field reads redirected to the real one); TracerTest's 180 assertions pass
  as the fixture is made. The log groups calls by cycle (identical cycles run-length coded) with the Tracer's state
  observed after every cycle (61 fields: the core's private state by reflection, the statistics, the counters, the
  queues, the fact counts, the record's hash, the event counts), the store dumped at each rig's first call and hashed at
  each word-0 packet. Fixture `tests/tracerconf/cases.log.gz` (2.3 MB; 2.28 M cycles, 6.9 M calls).
- **`tests/test_tracer.cpp`** replays it: every result, store check and observation equal; the three calls `ref`
  refuses as UB are FATAL here (checked in a forked child); every Tracer call under the allocation hook. Passes in all
  four builds; `tracerconf.sh --check` equal. Teeth: four planted timing mutations (a stall counter's last cycle, the
  scan's room rule, an expansion credit a cycle late, the scans' start) each fail at their first differing cycle.
- **What TracerTest's cases leave to step 3:** the write buffer at its depth, RVC and branch-heavy code at scale,
  controller-fed templates under credit pressure, advice refills, resets and mid-run reads, error stops at random points.

## 7. Step 3: TracerConf's seeded stimulus

- **`TracerConf gen LOG SEED FAMILY`** (`TracerConf.Gen`, on TracerConfRig): generated guests, structured so the advice
  is known in advance. **mix:** blocks of ALU ops (20 kinds, MUL family included), compressed instructions (fetches
  straddling lines), loads and stores of every width in two owned pages, DIV with constant operands (its quotient
  supplied as advice, walked in execution order through loops and calls), counted loops (two deep), calls and returns
  (every instruction start in the map), a CSR and the 3,121-row inline now and then, and one in five seeds ending on an
  error that stops (a store to a hole [8], misaligned [12], out of range [10], c.unimp [2], a wrong quotient [11]);
  random window lengths, compression enables, DMA service order, `hold`, `stateLast`, `q0One`, window reads mid-run,
  one or two segments. **wb:** stores to 136-192 pages a page apart in two or three passes: the write buffer queued
  far past its depth of 4 (17-27 lines) within `ref`'s 64-line ring. **wbover:** a third pass that overflows the ring.
- **The write buffer** (owner, 2026-11-09): `ref` queues lines beyond the depth of 4 (its own simplification,
  self-consistent: D27) and throws at 64 (`write-buffer ring overflow`); the port reproduces both, FATAL at the same call
  (`%` tokens in the log: one of `ref`'s IllegalStateExceptions, FATAL here). W13 records it as a finding.
- **In `make check`** (`tracerconf.sh` GENS): mix seeds 1, 3, 4, 5 (c.unimp), 9 (store to a hole), 16, 21 (out of
  range); wb seed 1 (25 lines queued); wbover seed 1 (the overflow). All equal to `ref`, every cycle. Fixtures
  3.0 MB with the cases. **Long runs:** `tracerconf.sh SEEDS OUT`, then `TRACERCONF_LONG=OUT test_tracer long`.

## 8. The fingerprint (AUDIT-M3 6.6)

- **The writer** is a template on each unit, so their state stays private: `Tracer::fingerprint(unit, Out&, c)` for
  `tr` (all eleven parts of 6.6, in order, with 6's rules: max(v, c), "none", the inputs not yet in effect from the
  Tracer's own rings), `tr.ic` and `tr.dc` (eldest to newest), `tr.mem` (`MemSystem::fingerprint`: the write buffer's
  live completions, each valid set's ways with their LRU rank), `tr.files` (`nextFree` and the region-11 sum).
  `Out` is W8's `fp::FpOut` (or any sink with `w`/`wi`).
- **`tr.files` incrementally:** `FileStore::track_sum(true)` keeps the sum at every `write_line` (16 words: old terms
  out, new in); off by default (only when fingerprints are wanted). The replay checks it against a full scan at every
  fingerprint.
- **Checked against `ref`'s StateFpM3** through TracerConf: `fp` lines every 2,000 cycles (cases) or 250 (seeded), and
  `fe` at each rig's end, `ref`'s hashes taken right after `runCycle(c)`. **1,740 points over the ten fixtures, all
  equal** (the incremental files sum included); a swapped pair of words fails at the first point.
- **For W13:** TracerConf takes `tr.files` as the scan 6.6 defines, not StateFpM3's cached `files()`: the cache is keyed
  by the memory system's writes and the state writes, so a harness's own store edits between calls (TracerTest's
  map-hole case allocates a page) would read a stale sum. T2 makes no such edits, so it doesn't matter there.
- **Long runs** (local, before the fingerprint lines): mix seeds 1-40 and wb 1-3, 43 logs, all equal.

## 9. The Tracer's side of W15's slot (proposal to W15, 2026-11-09; owner: agree in writing before the slot lands)

The slot (W15) holds `{type, ptr}` and switches on the type; for type 3 it includes `units/tracer.h` and calls
`units::tracer::Tracer` directly. Everything below exists today and is what TracerConf checks against `ref`.

| W15's list | the Tracer | notes |
|---|---|---|
| `params()` | `int params() const` (3) | also `type()` |
| `running(t)`, `errors(t)`, `sync(t)` | `bool running(int64_t)`, `int errors(int64_t)`, `void sync(int64_t)` | settle(t) first: the end of t − 1 |
| `status_bits(t)` | `uint32_t status_bits(int64_t) const` (0) | |
| `events()` | `EventCounts& events()` (W15's `event_counts.h`) | the live bits [7]-[10] set by settle |
| (consume) | `uint32_t consume_events(uint32_t bits)` | `ev.consume`; the Tracer has no `onConsume` |
| `word(t, w, v)` | `bool word(int64_t, int, uint32_t&)` | false: undefined (`ref`'s null) |
| `write_word(t, w, v)` | `int write_word(int64_t, int, uint32_t)` | 1, 0 (a bus error), −1 (undefined) |
| `store_stalls(t, w)` | `bool store_stalls(int64_t, int) const` (false) | |
| `accept(t, src, off, size, lo, hi)` | `void accept(int64_t, int, uint32_t, int, uint64_t, uint64_t)` | size is LinkWord's code (2 = 128 bits); lands at t + 2 |
| `run(t)` | `void run(int64_t)` | |
| `step(t)` | `void step(int64_t)` (`runCycle`) | |
| `out_queues()`, `out_ready`, `out_take`, `out_start` | as listed; `out_ready` is const and never settles (as `ref`) | queue 0 the record read in place |
| `reset(t)` | `void reset(int64_t)` | |
| `idle(t)`, `next_wake_at(t)` | false, INT64_MAX (as `ref`) | |
| (added) | `bool must_step() const` | the wake rule: false only when not running, no dropped expansion word waits and nothing pending; then skipping `step` is exact |
| (added) | `void skip_cycles(int64_t n) const` | FATAL if the counters would tick (`ref`'s check) |
| (added) | `void digest_words(int64_t out[8]) const` | words 44-51 as `ref`'s `digestWords` |
| (added) | `template <class Out> void fingerprint(int unit, Out&, int64_t c) const` | units `FP_NAMES`: tr, tr.ic, tr.dc, tr.mem, tr.files (AUDIT-M3 6.6); the slot's own state is W15's |

**Violations.** `ref`'s Tracer flags through `host.violate(t, message)`. Proposal: the Tracer keeps its own fixed-size
facts (`violations[64]`, `n_violations`, `violation_name(code)`), and the slot's report includes them (it reads them
after the run, or polls `n_violations` in its step if it wants the cycle at once): no callback pointer into the slot.
Errors likewise (`error_log`, `error_name`). **For W15: agree, or say you want a `violate` hook.**

**Configuration (the chip builder, W15 or the integrator):** `Tracer(me, icache_lines = 128)`; then `mem` (a
`MemSystem` over the run's `FileStore`, both owned beside the Tracer), `map_mem` (null: the one memory system),
`cold_mem_on_state_write = true` (T2's `onStateWrite`); `FileStore::track_sum(true)` only when fingerprints are wanted.

**Order within the slot's step** is AUDIT-M3 3.1's (W15's): the Tracer's methods are `ref`'s with `ref`'s timing and
its lazy settle, so whatever order 3.1 fixes for `ref` is reproduced exactly.

## 10. Quest 163 (T1 and the sweep): ALL PASS

- **T1, segments 0-3 of both programs** (ref's TracerT1 through TracerConf, replayed from a FIFO): `ref` against the
  delivery 256 windows each, 0 failed (evm-zone-29 320 s, cyclone-29 286 s); **the C++ Tracer equal to `ref` on every
  call and every cycle:** evm-zone-29 91,765,907 cycles (289 M calls, 918 fingerprints), cyclone-29 80,108,285 cycles
  (253 M calls, 802 fingerprints), the store's hash equal at every segment's record load.
- **The sweep:** mix seeds 41-200, wb 4-16, wbover 2-6: 178 of 178 logs equal.
- Results in Quest's `results/163-w14-t1-sweep/`.

## 11. W15's slot interface (owner, 2026-11-09)

- **Violations:** `violation_count()` (W15's mechanism, as proposed: the Tracer keeps its fixed-size facts, the slot
  forwards new ones; no callback).
- **Done:** `Tracer` derives from `PeripheralBase<Tracer>` (`peripheral.h`); `DIGEST_N = STAT_N`; `violation_count()`;
  `type()` asserted equal to `PT_TRACER`.
- **The late accept:** the slot calls `accept(t, ...)` at level 0 of t + 3; the landing at t + 2 is taken while the
  Tracer is settled through t + 2 at most (`settled_ <= t + 2`), FATAL beyond; `run`/`write_word` at t FATAL if settled
  past t. Test: `late_accept_lands_at_t_plus_2` (an accept made before and one made after `sync(12)` land alike, at the
  end of 12), and the death test for an accept after its landing was settled. All fixtures and the replay still equal.
- **Next:** wiring through W15's slot when its body lands (`PeriphSlot::tracer(n)` is declared in `periph_slot.h`).

## 12. The Tracer's speed (owner, 2026-11-09: measure and report; no optimisation yet)

- **`bench/w14/tracer_play`** (`w14.mk`): `convert` turns a TracerConf log (a FIFO from `ref`'s run) into a compact
  call trace (the store, then the calls grouped by cycle, run-length coded; results and observations left out); `run`
  replays it on the C++ Tracer, timed, per segment and in all, REPS in-process repetitions each on a fresh store, memory
  system and Tracer (not timed), then the harness alone (decoding and dispatch, no Tracer call). Every pass must end on
  `ref`'s last row count and record hash (so a trace that lost a call can't be timed). **`TracerPlay.java`**: the same
  on `ref`'s Tracer. No proxy, no comparison in either: Quest 163 showed these calls equal, cycle by cycle.
- **Locally** (one CPU, not a timing machine; small seeded traces, JIT not warm): C++ about 50 ns per Tracer cycle.
- **Quest 166** (`166-w14-tracer-speed`; I claimed 164 first, which W15 had taken: renumbered): the traces of T1's
  segments 0-3 of both programs, then each side alone on one P core (the JVM on the core and its hyperthread),
  5 repetitions, the machine recorded (D21); perf (cycles) on the C++ replay with the hot lines of the Tracer's step
  (W10's annotate parse).

**Quest 166: ALL PASS** (the i5-14400F, governor powersave, P core 0; clang 18.1.3 release; Java 21.0.12; every pass
ended on `ref`'s rows and record hash):

| | Tracer cycles | C++ ns/cycle (5 reps) | C++ harness | ref ns/cycle (median; reps 0-4) | ref harness |
|---|---|---|---|---|---|
| evm-zone-29 0-3 | 91,765,907 (289 M calls) | **33.92** (33.88-33.98) | 0.50 | **41.89** (48.4, 42.0, 41.9, 40.2, 37.8) | 7.55 |
| cyclone-29 0-3 | 80,108,285 (253 M calls) | **33.47** (33.36-33.56) | 0.56 | **37.03** (45.5, 37.0, 41.7, 35.8, 35.9) | 8.13 |

- Per segment, C++: 32.3-34.7 ns (evm), 33.0-33.8 (cyclone), steady across segments and repetitions (±0.2 %); the
  part before segment 0's ownership (the tables and record loading) 15-17 ns. `ref`'s first repetition carries the JIT's
  warm-up (its first segment 1.8-2.0 µs/cycle), then 35-42 ns.
- **C++ is 1.23x (evm) and 1.11x (cyclone) faster than `ref`'s Tracer**, medians; less against `ref`'s best
  repetitions (1.12x, 1.07x). Net of each harness's own cost (decode and dispatch, measured alone), the two run at
  about the same speed. At 34 ns, segment 10's 23.7 M Tracer cycles (T2) are about 0.8 s of C&C 3's work.
- **perf** (cycles, the C++ replay, one repetition, evm-zone-29): `Tracer::settle` **31.1 %**, `main` 28.4 % (the
  harness with the small Tracer calls inlined into it: `events().value`, `running`, `word`, `out_ready`... not
  annotated: `main` was not in the symbol list), `Tracer::row` 9.7 %, `Tracer::step` 3.7 %, `fetch` 3.3 %, `push_code`
  (set 1's code coder) 2.4 %, the trace's loading 2.4 %, `drain_cycle` 2.2 % plus the packers' `Bits::put` 2.3 %,
  `fetch16` 1.9 %, `push_log` 1.0 %, `rvc::uncompress` 0.8 %, `data_line` 0.7 %, `alu::exec` 0.7 %; the memory system
  and the LRUs under 0.5 % each.
- **Inside settle** (called by every step and every level read, so several times a cycle): one load right after the
  snapshot's stores (`s_mul_ = hash_mul_`) takes 45 % of its samples, which looks like a store-forwarding or cache stall
  over the Tracer's large object (about 90 KB of inline arrays between the hot fields); the live service flags'
  `OutQueue::flag()` (`taken()` scans the credits' bounds linearly, three queues every cycle) about 30 %.
- In `step`, the samples sit on its entry and the `running_`/`stopped_` tests (a call per cycle, its frame); in `row`, 13 %
  on one store into `cur_`.
- **Not optimised** (owner: after the first T2 timing, D31's spirit). Candidates for that decision, cheapest first:
  settle's snapshot taken only when something changed since the last (a dirty flag); `flag()` in O(1) (the count of
  credits taken kept as reads advance); the hot fields together, the large arrays after them.

## 13. Wired into W15's slot (owner, 2026-11-09)

**The edits in `periph_slot.cpp`** (W15's file, the marked places): the include of `units/tracer.h`; `case PT_TRACER` in
`SlotCore::visit`; its delete in `SlotCore::Ref::~Ref` (the Tracer deletes its memory with it); its creation in
`SlotCore::install` (`new tracer::Tracer(int(tile))`). Also, beside them: the accessor `SlotCore::tracer(n)` (declared
in `periph_slot.h` next to `replayer(n)`), and a Tracer branch in `SlotCore::digest()` (the debug checks' progress:
pc, the stream word, rows, the FIFOs, the cycle counter, the fact counts). Build: `SLOT_LINK` includes `TRACER_LINK`
(`tracer.mk` is now included before `units.mk`).

**What `install` takes for the Tracer: nothing beyond the tile. For W15 (through the owner): no config field.**
- **The tables** (the decode table, the ROM) are not configuration: they arrive as region packets from `tracer_ctl`
  (part 0 at 0x1000 and 0x2000), as in `ref` (`Config.cc3Tracer` creates `new Tracer(me)` bare).
- **The memory system and the memory** are given after the build by the harness, which owns the run's memory snapshot:
  `slot.s.tracer(1).own_memory(std::unique_ptr<FileStore>)`. The Tracer then owns the store and a `MemSystem` over it,
  made cold at every state-word landing (T2's `onStateWrite`). A PULSE_RUN without a memory system is FATAL (named).
- `icache_lines` stays the default (128); only B1's sweep changed it.

**Tested in the slot** (`tests/test_tracer_slot.cpp`, test_periph_slot's harness pattern): a stand (the core's window
stores and consuming loads on S2/S3, the director's region writes on S1, the window RegFile read, the DMA's credit Fifo)
runs a small controller: the tables and the record (838 region packets), the hash words, EVENTS_MASK, RUN_CONFIG and
PULSE_RUN at the earliest legal cycle (the landing's own), then the drain (queue 0 in one transfer, the streams by
128-word credits) to run done. A straight-line guest (ALU, MUL, loads and stores over two owned pages, END, then
padding to 400 rows). The reference is the same controller driving `SlotCore` directly at `ref`'s timing (W15's
SlotConf holds SlotCore to `ref`'s slot). **Plain, step-all and shuffled: the same decisions at the same cycles, the
same 181 DMA packets at the same cycles (a cycle later through the Fifo), the same five fingerprint units at the end;
no violations.** The death test: a run before the memory is given. All four builds; `test_periph_slot` and
`test_slotconf` unchanged. **Teeth:** the unit's landing a cycle late (`accept(t - 2)`) makes the PULSE_RUN land first and
the record write is then UB while running: FATAL. (A Tracer-internal timing change moves both runs alike: that is
test_tracer's to catch, and it does.)

## 14. The request bus (owner, 2026-11-09: go-ahead, option 3 = D34)

**Taken over from W16** (its plan's step 3; its log shows steps 1, 2 and the boot-end point, nothing on the bus).
**For W16:** I'm building the request bus, both senders (the tiles' REQUEST_SEND and the controllers'), and the
controllers' receive side; the ports you wire into the chip are below, one round.

**D34** (in DECISIONS.md): one implementation, its credit latency an explicit configuration key (`reqbus_credit_latency`,
never derived from the partition): **0** exact as `ref` (every sender in the bus's domain; a split is fatal at build),
**3** the spec under D29 (each port's leaf in its tile, beats and credits over Link(3)), the default. Mode 3 counts the
cycles where it would differ from `ref` and every run reports the count; ReqConf checks mode 0 against `ref` every
cycle on all traffic, and mode 3 against `ref` on the traffic without backpressure.

**The design** (`src/units/reqbus.h`; approved changes, owner 2026-11-09: `ref`'s own structure, forced by the
framework's 64 inputs a unit; the root its own type, for levels):
- `ReqLeaf` (a sender's port: `RequestPort`'s FIFO, the leaf link), one per sender in its tile's domain: `push`
  (Link(1), from its core), `freed` (Wire to its core: the head freed at t, `canPush`), `freed_ev` (Link(1) to its
  register block: the FIFO's count, STATUS [6], EVENTS [11]); beats to its node over Link(3).
- `ReqNode` × 8 (the row nodes, `reqbus.row<r>`) and `ReqRoot` (`reqbus.root`): `ref`'s Buf per input, round-robin,
  wormhole, the requestor stamped, the root's credits and drops. Event-driven: stepped while a beat or request is in
  it, asleep otherwise.
- `ReqRecv` × 3 (`cc<c+1>.reqrecv`), in its controller's domain: the delivery over Link(3), the head published in a
  RegFile {pending, w0-w3} as a load at t sees it, the pop over Link(1).
- **Room**: mode 0, each node publishes its forwarded beats per input in a RegFile written first in its step (levels:
  root < rows < leaves < cores; same domain, so a split is refused at build); mode 3, credits over Link(3), with the
  D29 count (`lag_cycles`, retroactive and exact: a blocked cycle counts when the credits then arriving show `ref`
  would have had room).
- **The queue credit**: the controller core's pop also puts the root's credit (Link(1) in mode 0, `ref`'s next cycle;
  Link(3) in mode 3).

**Checked** (`tests/test_reqbus.cpp`, `tests/ref/ReqConf.java`, `reqconf.sh`, `tests/reqconf/`: mix, calm, full,
fanin, drop; 1 MB): mode 0 equal to `ref` every cycle (every call's result; the `reqbus` and the 67 `req` streams) on
all five, plain, step-all, shuffled, all four builds; mode 3 equal on calm (one request in the bus at a time),
`lag_cycles` 0, and 993-2,098 on the others; mode 3 split over five domains equal to one domain; mode 0 split refused
at build. Teeth: the root's credit check and the leaf's room check each fail when planted.

**The ports W16 wires, per controller C&C c (c = 0-2)** (one round; the tiles' are the same send side, wired by the
chip builder): `ControlIO` gains a request side, filled by `add_g1` (W8's builder, which W16's controllers use):
| `ControlIO` field | unit port | to / from | kind |
|---|---|---|---|
| `req.push` | the core's `req_push` | → `t<k>.reqleaf` / `cc<c+1>.reqleaf` `push` | Link(1), width 2 |
| `req.freed` | the core's `req_freed` | ← the leaf's `freed` | Wire |
| `req.freed_ev` | the block's `req_freed_ev` | ← the leaf's `freed_ev` | Link(1), width 0 |
| `req.recv` (controllers) | the core's `recv` | ← `cc<c+1>.reqrecv` `head` | RegFile (5) |
| `req.pop` (controllers) | the core's `recv_pop` | → the queue's `pop` | Link(1), width 0 |
| `req.qcredit` (controllers) | the core's `recv_credit` | → `reqbus.root` `qcredit<c>` | Link(`io.queue_credit_latency()`), width 1 |
`units::reqbus::add_reqbus(cfg, rc, tiles, ccs, placement)` adds the bus; `wire_reqbus(cfg, io, k, ControlIO&)` wires
sender k (a tile 0-63, or 64 + c) and, for a controller, its receive side. Placement (`ReqBusPlacement`): each leaf in
its sender's domain, each queue in its controller's, the nodes anywhere (mode 3); everything in one domain (mode 0).
The mode is `reqbus_credit_latency` in the chip's configuration, set explicitly (D34), and every run reports
`lag_cycles`.

**W16's round (owner, 2026-11-09): agreed.** W16 takes the ports above. **T2 (confirmed, D34):** T2 runs mode 3 (the
default, split as it needs) and **requires `lag_cycles` 0**; mode 0 on one domain is its cross-check (the same run must
give the same results). The end-to-end core test (a tile's REQUEST_SEND into a controller's REQUEST_RECEIVE) is built
on W16's units 64-66 once they're on main, not on a rig of mine.

**Changes to closed workers' files** (D34's wiring; each marked `W14` in the file):
- **W5's `rv32.h`**: the receive loads (REQUEST_GET_W0-W2, REQUEST_RECEIVE) and EVENTS / EVENTS_PEEK's live bit [12],
  each only where the Env offers them (`if constexpr (requires ...)`), so M1's `Core` (controller.h) is unchanged.
- **`regmap.h`**: `COND_RECV_UNDERFLOW = 14`.
- **W8's `tile_ctl.h`**: `TileCore` gains `req_push` (Link out), `req_freed` (Wire in) and, for a controller (`receiver`),
  `recv` (RegFile in), `recv_pop`, `recv_credit`; its Env's `can_push` (STATUS [6] at t - 1, or freed at t), `post`
  (REQUEST_SET_W0-W2 kept, REQUEST_SEND pushes), `req_get` / `req_receive` / `live_events`, and `wake` (the block's
  wake, or TileControl.wakeTerm with the live bit, never through a closed gate). `TileCtl` gains `req_freed_ev` (the
  FIFO's count, through RegBlockState's existing `freed`).
- **W8's `tile_ctl.cpp`** (`add_g1`): the core built with `receiver = k >= 64` and the done gate; `ControlIO::req` filled.
- **W9's `chip.h` / `chip.cpp`**: `ReqIO` in `ControlIO` and `TileIO`; `ChipSpec::reqbus` (the configuration, credit
  latency explicit); `build_chip` adds the bus for the tiles with a request port (leaves in their tiles' domains, the
  nodes with tile 0's) and wires each.
- **Tests**: `test_chip.cpp` (W9: its lone-tile builders tie the request side off with `tie_off`; the chip's
  fingerprint takes the real streams), `test_g1_tile.cpp` and `test_g1_replay.cpp` (W8: `tie_off`), `test_sort.cpp`
  (W12: the real streams), `statefp_g1.*` (W8: `req` from the leaf, `attach_reqbus`), `statefp_g2.*` (W9: `reqbus`
  from the bus).

**Status (2026-11-09).** `make run` on clang release: 406 tests pass. wfi, regs, a2a (test_chip, one and split
domains), the 16K sort (`sort_runs_equal_to_refs`) and M1's goldens (`golden_all_match_*`, the block's and core's
fingerprints on them) are unchanged, now with the real `reqbus` and `req` streams. gcc debug: test_chip, test_g1_tile,
test_g1_replay, test_reqbus pass. **Not yet**: an end-to-end test of the M2 core's request path (a tile's REQUEST_SEND
through its leaf and the bus into a controller core's REQUEST_RECEIVE): it needs a receiving controller, so it comes
with W16's units 64-66 (or a small two-core rig of mine if W16's are late); the full `make check` (both compilers,
debug, tsan).

## Log

- 2026-11-09: plan agreed; branch `sim-w14` from main 1a898aa.
- 2026-11-09: step 1 done (section 5); `test_tracer_parts` passes in all four builds, `tracerparts.sh --check` equal.
- 2026-11-09: step 2 done (section 6): the Tracer equal to `ref` every cycle on TracerTest's cases. Merged into main.
- 2026-11-09: step 3: the seeded fixtures (section 7) equal to `ref`; the write buffer past its depth and at the overflow.
- 2026-11-09: the fingerprint writer (section 8), equal to StateFpM3 at 1,740 points.
- 2026-11-09: T1 through TracerConf: TracerT1's copy with its three harness edits recorded (ownership, cold cache,
  stall counters), the replay streaming from a FIFO. **Quest 163** (`163-w14-t1-sweep`): T1 on segments 0-3 of both
  programs and the long sweep (mix 41-200, wb 4-16, wbover 2-6); smoke-run locally (build from the kit, the sweep's
  20 logs equal; the runner's data absent here). The slot's interface proposed to W15 (section 9).
- 2026-11-09: Quest 163 ALL PASS (section 10). W15's interface taken (section 11): PeripheralBase, violation_count,
  DIGEST_N, the late accept.
- 2026-11-09: the speed measurement (section 12): tracer_play and TracerPlay; Quest 166 queued.
- 2026-11-09: Quest 166 ALL PASS: C++ 33.9 / 33.5 ns per Tracer cycle, ref 41.9 / 37.0 (1.23x / 1.11x); perf: settle
  31 %, the harness with inlined calls 28 %, row 10 % (section 12).
- 2026-11-09: wired into W15's slot (section 13): install takes nothing; the memory given after the build
  (`own_memory`); `test_tracer_slot` equal to SlotCore driven directly in three variants.
- 2026-11-09: REPORT.md so far (the owner: nothing more until W16's chip assembly).
- 2026-11-09: the request bus taken over from W16 (section 14); D34 recorded by the owner; the design and W16's ports.
- 2026-11-09: the bus equal to ref (ReqConf, mode 0 every cycle; mode 3 on calm; split tests); wired into the tiles;
  the fingerprint writers registered; regressions unchanged (section 14, Status).
- 2026-11-09: merged main (W15's tile paths, W16's harness; 3b65e61): chip.h/.cpp and tile_ctl.h/.cpp resolved once
  (W15's slot ports behind `periph_slot`; the request side in ControlIO, TileIO, CcIO and the tile builder;
  `add_request_bus` serves M2's chip and W16's `cc_cores` chip, the controllers' leaves and queues in their domains).
  `Tracer::writable(w)` (W15's ask). **make check, build by build on the merged tree (16b8822): gcc and clang, debug and
  release, all 54 binaries pass; TSan, gcc and clang, all 8 thread tests (83 cases, `test_reqbus`'s split run
  included), no races.**
- 2026-11-09: the end-to-end request test, `ref`'s side done: `tests/m3-images/cc_req.c` (C&C 1: CMD_JUMP to tile 5,
  GO from its peripheral, waits 6,000 cycles undrained, then receives 300 requests through EVENTS_PEEK [12],
  REQUEST_GET_W0-W2 and REQUEST_RECEIVE, logs the words and mcycle, one receive on the empty queue, SIM_EXIT) and
  `tile_req.c` (tile 5: 300 REQUEST_SENDs back to back, stalling while its FIFO is full; mcycle after each), built by
  `build_req.sh`; `tests/ref/CcReq.java` and `ccreq.sh` write `cc_req.ref` (`ref`: exit at 24,702; the words stamped
  with requestor 5; the empty receive reads 0 with ERRORS [14]; 2,400 beats at the root). **The C++ side waits for W16's
  C&C 1 slot in the chip** (the tiles' boot ROM jumps only on GO, which comes from C&C 1's peripheral; on main C&C 1's
  peripheral is still the stand-in). Then `test_cc_req`: mode 0 one domain equal to `cc_req.ref` in every figure; mode 3
  (one domain and row bands) the same words, its `lag_cycles` reported (positive here: the queue and FIFO fill on
  purpose), the cycles equal where it is 0.
- 2026-11-09: **`test_cc_req`** on T2's machine with C&C 1's slot (main 8e3611b; `t2::build_machine` takes the bus's
  configuration, W16's harness, noted): **mode 0 on one domain equal to `cc_req.ref` in every figure** (C&C 1's exit
  at 24,702; all 300 send and 300 receive cycles; every word, stamped with requestor 5; the empty receive's 0 and
  ERRORS [14]; 2,400 beats at the root), plain, step-all and shuffled; the cycles compared as the programs' mcycle and
  TileCtl's `sim_exit_at` (AUDIT-M3 7's mapping inside both). Mode 3, one domain and T2's split (4 domains): the same
  words; `lag_cycles` 1,026, 42 of the 601 cycle figures differ from `ref`'s (exit still at 24,702); the split equal to
  one domain in every figure. Clang release and gcc debug.
- 2026-11-09: REPORT.md with the request bus (section 8). Paused (the owner): W16 builds the M3 fingerprint writers
  (calling `reqbus` / `req`); T2's questions come back through the owner.
- 2026-11-09: **W16's request** (evm's point 6,300,000: a PULSE_DMA store of the point's own cycle; ref's slot starts
  the transfer at once, ours a cycle later through the slot's core link). `Tracer::fingerprint` takes a
  `Tracer::FpPending` (as W15's `Replayer::FpPending`): the landings of c - 2 .. c, the core's PULSE_RUN or window store
  of c, a PULSE_DMA of c (xferQueue / xferLeft as `out_start` with DMA_HDR's queue and length) and a consuming EVENTS
  load of c (the event counts after ref's end-of-cycle consume). **`PeriphSlot::fingerprint_tracer(m, n, unit, o, c)`**
  (W15's `periph_slot.h`, beside `fingerprint_replayer`, noted) builds it from the port observer; W16 calls it in the
  M3 chip line for C&C 3's Tracer. Test (`test_tracer_slot`): the slot unit's Tracer against its twin driven directly
  at ref's timing, all five units after **every** cycle of a 6,000-cycle run (30,000 fingerprints equal); the overlay
  mattered at 845 cycles; at each of the 4 PULSE_DMA cycles the plain fingerprint differs (checked). Clang release and
  gcc debug; test_tracer, test_periph_slot and test_slotconf unchanged.
