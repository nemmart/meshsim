# WORKER14 (sim/, M3): notes

The Tracer. Branch `sim-w14`. Prompt: `PROMPT.md`.

## Status

**Plan agreed (owner, 2026-11-09): go-ahead.** Step 1 (the pure pieces) done and equal to `ref` (section 5); next, step
2 (the Tracer as a class).

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

## Log

- 2026-11-09: plan agreed; branch `sim-w14` from main 1a898aa.
- 2026-11-09: step 1 done (section 5); `test_tracer_parts` passes in all four builds, `tracerparts.sh --check` equal.
