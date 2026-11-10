# WORKER15 (sim/, M3): notes

The replayer and the peripheral slot. Branch `sim-w15`. Prompt: `PROMPT.md`.

## Status

**Go-ahead 2026-11-09** (owner): the plan as written; answers to Q1-Q4 below. Steps 1-2 done: the ALU and the replayer equal to `ref` (ReplayConf every cycle; ReplayerUnitTests ported, 1,456
checks). The slot's header published; the owner settled the round (W14: violation_count and the late accept; W16: its two
classes and `visit` cases, the ports/begin/end hooks). Stage A passed (Quest 164: every window of both programs). The slot's SlotCore equal to ref call for call (SlotConf); the unit working under the engine. The tile's paths tested at the tile level (test_tile_slot) and against ref's exact cycles (TileSlotConf: PeripheralSlotTest's
groups). Merged to main (85a8140). Replayer::fingerprint (6.8) for W16's boot-end comparison, equal to ref every cycle.

## The owner's answers (2026-11-09)

- **Q1: yes.** Build to `ref` as it runs in v1.3: type 4, the window words, EVENTS [15:13], ERRORS [13:11], mode 0 to
  region 0x04. The prompt's names were REPLAYER-NOTES' v1.1 ones; `ref`'s code wins.
- **Q2: yes.** One generic slot for the tiles and C&C 1-3, dispatching on a type tag. W14 proposed the same: the
  peripheral a member object of the slot unit, a fixed method interface, a switch on its type, called synchronously as
  `ref` does. W14 implements only the Tracer's side.
- **Q3: yes.** A running hash of the rows always; an optional record buffer, preallocated at configuration from the
  declared window size, for comparing with `rows.bin` in tests. Nothing allocated in the run (D6).
- **Q4:** W13's Quest 159 lists the runner's persistent data; the delivery paths and hashes come to me through the owner.
- **Shared code:** W15 owns `row_core.h`, `replayer_alu.h`, `event_counts.h` and the slot; W14 owns the tables loader
  (decode table, templates), RVC, LruCoder, FileStore, MemorySystem and the Tracer. Each uses the other's read-only;
  exact file names agreed with W14 in one round (below).

## For W14: file names and the peripheral interface (one round)

Mine, in `sim/src/units/`, namespace `units`:

| file | contents |
|---|---|
| `row_core.h` | `rowcore::`: `decode_index`, `source_imm`, the expansion word's field getters (`op`, `c_rd`, ... `offset_imm`), `invalid_word`, `needs_source_imm`, `reg`, `imm_val`, `decode` (`K_PAD/ROM/NOP/CTRL/BAD`, `Decoded`) |
| `replayer_alu.h`, `replayer_alu.cpp` | `alu::`: the opcode numbers, `name`, `cls` ('S'/'M'/'L'/0), `executes`, `cycles`, `code(name)`, `Out {rd, new_pc, undefined}`, `exec(...)`, `mem_addr`, `is_load/is_store/is_shift_data`, `pext`, `mulhu` |
| `event_counts.h` | `EventCounts`: depths and full-flags per bit (fixed 32), `post`, `set`, `clear`, `clear_all`, `live`, `value(t)`, `consume(bits)` reporting the bits it took from (no callback: the caller gets a mask) |
| `peripheral.h` | the type tags and the fixed method list the slot calls (below) |
| `periph_slot.h`, `periph_slot.cpp` | `PeriphSlot`: up to three peripherals and their DMAs, the window, the mask, the consuming loads, the live bits |
| `replayer.h`, `replayer.cpp` | the replayer (type 4) |

Suggested for yours (your choice; tell me and I'll use them): `replay_tables.h` (the loader), `rvc.h`, `lru_coder.h`,
`file_store.h`, `mem_system.h`, `tracer.h/.cpp`.

**The interface** (`Peripheral.java`, method for method, non-virtual): each peripheral type is a class with these
members; the slot holds `{type, ptr}` and `switch`es on the type to call them (the case for type 3, the Tracer, includes
`units/tracer.h`):
`params()`, `running(t)`, `status_bits(t)`, `errors(t)`, `sync(t)`, `events() -> EventCounts&`, `word(t, w, uint32_t& v) -> bool`
(false: undefined), `write_word(t, w, v) -> int` (1, 0, -1), `store_stalls(t, w)`, `accept(t, src, offset, size, lo, hi)`,
`run(t)`, `step(t)` (`runCycle`), `out_queues()`, `out_ready(t, q)`, `out_take(t, q, uint64_t w[2])`,
`out_start(t, q, words)`, `reset(t)`, `idle(t)`, `next_wake_at(t)`. Inputs take effect at the end of t, level reads see
the end of t − 1, as in `ref`. Violations go to the slot's `violate(t, code)` (fixed-size facts, D24).

## What I read, and what the oracle actually is

`ref` is v1.3, frozen; it differs from the prompt's wording in places, and I follow `ref` (D27):
- **The replayer is peripheral type 4** in `ref` (`Replayer.type()`, 4.17.1, D46), not type 1 (v1.1's number). It is still
  peripheral **1** of every mesh tile (region 0x04, window 0xFFFFFA00).
- **The prompt's item 3 names are v1.1's** (REPLAYER-NOTES' "Wiring it into the tile" is the v1.1 section). In v1.3 `ref`
  there is no `periph_running`, `WAKE_*` service lines, `DMA_PERIPH_REMAINING` or HALT/`setHalted`. What the tile does
  reach, and what I will build:
  - **mode 0 to region 0x04-0x06** (the director → `PeripheralSlot.accept` → `Peripheral.accept`, rp_latency 2, one per
    cycle); the director's FATAL "not modelled in M2" becomes this path;
  - **the window** (`PeripheralSlot.read/write`): PARAMS, STATUS ([0] running, [1] DMA busy), ERRORS, PULSE_RUN (word 3),
    PULSE_DMA (4), EVENTS consuming (5), EVENTS_PEEK (6), EVENTS_MASK (7), DMA_HDR/ADDR (8, 9), reserved 10-31, the
    peripheral's 32-127 (the replayer's RUN_CONFIG 32, ROWS_RUN 33, EXPANSION_OFFSET 34, EXPANSION_OPCODE 35);
  - **the live bits**: EVENTS [15:13] (events & mask ≠ 0) and ERRORS [13:11] (peripheral ERRORS ≠ 0), which also wake WFI;
  - `EventCounts` (depths, sticky bits, consume at end of cycle), `endCycle`'s mask store and consume, `reset`.
  - "Conditions [11]-[13]" and "mode 9" I read as the above (ERRORS [13:11]; the replayer's write port is still called
    "mode-9" in comments, but the packets are mode 0 to region 0x04). **Q1.**

## The plan

### 1. Shared code with W14 (one round, proposal)

| file (`sim/src/units/`) | from `ref` | owner | what |
|---|---|---|---|
| `row_core.h` | `RowCore.java` | **W15** | the expansion word's fields, the decode index, source immediates, `decode` (K_PAD/ROM/NOP/CTRL/BAD), operand/register mapping |
| `replayer_alu.h` (+ `.cpp` if needed) | `ReplayerAlu.java` | **W15** | the opcode table (88 row opcodes, classes), what a row computes: pure functions, no state |
| `event_counts.h` | `EventCounts.java` | **W15** | a peripheral's counted events (depths, sticky, consume), plain arrays |
| `periph_slot.h/.cpp` | `PeripheralSlot.java` (incl. its DMA) | **W15** | the slot, generic over the peripheral (section 4) |
| Tracer, `Rvc`, `LruCoder` (the encoder), the memory system | | W14 | |

W15 owns these because they come first in my order (the ALU against `vec.tsv` is step 1); W14 uses them read-only and
asks for changes in my NOTES (I turn them round quickly). The record layout and region map constants live in
`row_core.h`. If W14 prefers to own `row_core.h`, fine by me: whoever starts first. **For W14.**

### 2. The replayer on its own (`units/replayer.h`, `.cpp`)

- **State laid out for the cycle loop** (D6): one flat struct per replayer; part 0 (66 × 2 words), the decode table
  (2048 × u16), the ROM (512 × 2 words); the four input queues as fixed rings (256 entries, `LOG_QUEUE_ENTRIES` a
  build-time constant), with occupancy, pending and the three decompressors (`Stream`, `ExpQueue`); the front end's two
  `Instr`s and the row in flight as fixed fields; the 32-row output queue as a ring of 10-word records. No STL, no
  allocation, no virtual calls; violations recorded as fixed-size facts (D24) and formatted after the run.
- **Timing as `ref` has it**: inputs (accept, run, RUN_CONFIG store, the consuming EVENTS load) take effect at the end
  of t; a packet accepted at t lands at the end of t + 2; level reads at t see the end of t − 1. `ref` does this lazily
  (`settle`); I do it eagerly in the replayer's step, which is equal because nothing observes it in between (PORTS.md 1).
- **Model choices M1-M6 and N1-N12** as `Replayer.java` implements them (v1.26.1: RUN_CONFIG, the stream word, the
  flush at the state word's write, part 0 as working state, underflow and leftover errors, UB stops as FATAL).
- **Two entry points over the same code**: a plain `step(t)` + input calls (for the unit tests, ReplayConf and the
  Stage A driver, no engine), and the framework unit wrapping it (ports from W13's audit). Waking: the replayer asks
  `next()` only while running, a packet is landing or the output queue holds rows; otherwise it sleeps until an input.

### 3. Checked against `ref` before any wiring

1. **The ALU** (`tests/test_replayer_alu.cpp`): `ReplayerTest`'s opcode table against `sim/ref/tables/opcode_table.txt`
   and the 252-line `sim/ref/meshsim/data/vec-821e1bd.tsv` (read in place, not copied), with its flagged and n/a rows;
   the same counts as `ref` (225 match, 4 flagged, 23 n/a).
2. **The unit tests** (`tests/test_replayer.cpp`): a port of `ReplayerUnitTests` (1,177 lines) with `ReplayWindow`'s
   builders and stream encoders as a test header (`tests/replay_window.h`). Same checks, same expected values.
3. **ReplayConf** (`tests/ref/ReplayConf.java`, `replayconf.sh`, as RouterConf): a seeded script (packets to every part,
   RUN_CONFIG, PULSE_RUN, window loads, consuming EVENTS loads, resets) drives `ref`'s `Replayer` through `Peripheral`
   only; it writes per-cycle observations (running, ERRORS, EVENTS, words 32-35, `landed`, each record leaving the
   sink, violations). The C++ side replays the script and must be equal line for line. Windows from
   `ReplayWindow`/`SyntheticDelivery` (small, committed as `.gz` fixtures in `tests/replayconf/`, run by `make check`),
   plus bad cases (underflow, overrun, invalid expansion, leftover).
4. **Stage A** (`tests/replay_delivery.cpp`, a port of `ReplayDriver` + `ReplayDelivery`): every delivered window, three
   record variants, rows byte for byte against `rows.bin`, streams consumed exactly, end state against `end.bin`, no
   violations. **On the runner** (the deliveries are its data; a Quest task with an embedded kit), `ref`'s
   `ReplayDelivery` beside it for the same windows.

### 4. The peripheral slot and the tile's paths

- **`periph_slot.h`**: `PeripheralSlot` with up to three peripherals and their DMAs. **No virtual calls**: the slot holds
  a type tag per peripheral and dispatches with a `switch` over a closed set (replayer, Tracer, C&C 1's peripheral, a test
  peripheral); adding a peripheral type adds a case. The DMA (only C&C peripherals have output queues) is built here too,
  so W14's Tracer only provides `out_ready`/`out_take`/`out_start`. **Q2.**
- A port of `PeripheralSlotTest` (272 lines, with `TestPeripheral`).
- **The register block, director and tile** (W8's `regblock.h`, `tile_ctl.*`; W11's/W12's director paths): window
  loads and stores to 0xFFFFFA00-0xFFFFFFFF through the slot, EVENTS [15:13] and ERRORS [13:11] live, the WFI wake on
  them, mode 0 to regions 0x04-0x06, PARAMS [2:0]. Each change noted here with its reason (those workers are closed).
- **Wiring into the tile** once W13's `AUDIT-M3.md` gives the ports; the debug checks (shuffle, both orders, step-all,
  the missed-wake detector) on every new unit.
- **Stage B** (`StageB.java`'s runs: `replay_ctl` on a tile, its timing windows): rows byte for byte, and the cycle counts
  equal `ref`'s; on the runner.

### Order and commits

1 → shared headers + ALU (commit) → 2 + unit tests (commit) → ReplayConf (commit) → Stage A on the runner (commit) →
slot + its test (commit) → tile paths (commit) → wiring (after the audit) → Stage B → `REPORT.md`. `make check` on gcc
and clang at each commit; `main` merged in often.

## Questions

- **Q1 (owner/integrator).** I build to v1.3 `ref` (type 4; window words, EVENTS/ERRORS live bits; no HALT, no service
  lines, no `DMA_PERIPH_REMAINING`), reading the prompt's item 3 as those paths. Right?
- **Q2 (integrator, W14).** The slot including the DMA is mine (one generic slot for the tiles and C&C 1-3), dispatching
  by type tag instead of virtual calls; W14 implements the Tracer's side of it. Agreed?
- **Q3 (integrator).** Where do the replayer's rows go on the chip? `ref` drains one row per cycle into a listener. I
  propose a per-replayer sink that keeps a running hash and, optionally, writes the records to a preallocated buffer for
  the comparison with `rows.bin`.
- **Q4 (integrator/W13).** The runner's data paths for deliveries 1-3 (and their hashes), for Stage A.

## Log

### Step 1: the shared headers and the ALU (2026-11-09)

- `units/row_core.h`, `units/replayer_alu.h` (header-only: the opcode table is an `inline constexpr` table, so `cls()`
  and `executes()` cost no call per row), `units/event_counts.h` (ref's `onConsume` callback becomes `consume`'s
  returned mask).
- `tests/test_replayer_alu.cpp`, three tests, all four builds:
  - **ReplayerTest's ALU report, line for line**: the opcode table (171 opcodes, 88 row opcodes) and the 252 vectors
    (225 match, 4 flagged, 23 n/a), every flagged and n/a line equal to `ref`'s (`tests/replayer/alu-report.txt`).
  - **A differential sweep** against `ref` (`tests/ref/ReplayConf.java alu`): every executed opcode on 2,000 seeded
    inputs biased to the edges (small, negative, powers of two, masks, sign bits, words), a digest of rd, the new pc and
    the undefined flag per opcode; and RowCore (decode index, the six source immediates, `reg` for every code and both
    offsets, `imm_val`, `invalid_word`'s cause, `needs_source_imm`) on 32,000 words. Inputs are regenerated on both
    sides (splitmix64), so only the digests are committed (`tests/replayer/alu-sweep.txt`). Equal, under UBSan too.
  - `EventCounts`' timing (a post or set at t hides at t, the clamp, full posts, live bits, consume's mask).
- `tests/ref/replayconf.sh --fixtures | --check` regenerates both fixtures from `ref` (needs a JDK).
- Note on `ref`: ReplayerTest's "every row opcode has a matching vector" check can't fail: the `missing.append` sits
  after a `//` comment on the same line (ReplayerTest.java line 105). The C++ test makes the check as meant, and it
  passes: every executed opcode but NOP has a matching vector, as REPLAYER-NOTES says.

### Step 2: the replayer, and ReplayConf (2026-11-09)

- `units/replayer.h`, `replayer.cpp`: `Replayer.java` method for method (WIP commit 984025f, then tested). `ref`'s lazy
  `settle` is kept as is (fixed rings for the landings and inputs): it is exactly AUDIT-M3 3.1's level-0 order once the
  slot calls `consume_events` (S3) and the mask store before the peripheral's first touch of the cycle, which settles
  (landings before inputs at one time). The stream no longer points at the replayer's events (`release_to(pos, ev)`):
  ASan caught a copied replayer's streams posting to the old object; `Replayer` is now non-copyable.
- **ReplayConf** (`tests/ref/ReplayConf.java trace | run`): `ReplayDriver`'s ideal controller, line for line, with
  every call to the replayer recorded as a script (accept, run, RUN_CONFIG, the read points, the consumes, runCycle)
  and `ref`'s observations after each cycle (running, ERRORS, EVENTS, words 32-35, the output queue, rows done,
  `landed`, the seven stall counts, each violation class, the error lists, pc, `instr_idx`, and a hash of every row
  the sink takes). `run` replays a script on `ref` and must equal the trace (it does: the script holds every input).
  `tests/replayconf.h` is the C++ side; `build/<cc>-<mode>/replayconf` the tool.
- **Fixtures** (`tests/replayer/`, 956 KB, `make check`'s `test_replayconf`): 13 traces on `ref`'s `SyntheticDelivery`
  windows (1,024-8,192 rows; raw, LRU and picked streams; a long controller-fed expansion; two passes), with mutations
  for the error paths: underflows of each stream, leftovers of each, an invalid expansion, RUN_CONFIG rows 0, a code
  refill past its credits (invariant 16 and overruns), a flood past 256 entries. **All equal, every cycle, in all four
  builds.** Planted bugs (a row reaching the sink a cycle late; E[15] written to E[14]) make it fail.
- `replayconf.sh --long OUT`: every window of the four synthetic deliveries in all three variants: **36 traces, all
  equal** (clang release, locally).
- **A finding in `ref`'s test tools** (not the model): `SyntheticDelivery` isn't reproducible run to run: with the same
  seed, its last window's logs, rows and `end.bin` differ (and so the manifest). So `replayconf.sh --check` doesn't
  regenerate the scripts; it replays the committed ones on `ref` and compares the observations.

### AUDIT-M3 3.1/3.2 (W13, on main 2026-11-09): read

My design fits it: the slot one unit after the tile's children, the peripheral a member; S1 a Link(3), S2/S3 a Link(1),
applied at level 0 in the order consume and mask, then the landings, then the core's inputs (the replayer's `settle`
does the last two in that order already); the window, the live bits and C&C 1's stall a RegFile written at level 0.

### The deliveries (Q4; owner 2026-11-09)

`/home/questrunner/data/w9-deliveries/{evm-zone-29,cyclone-29}` (v1.25 records), the selected windows in
`w9-deliveries-selected/` and `w9-selected-windows.tgz`; ConvertDelivery first, in scratch, as Quests 105-107 did. By
"deliveries 1-3" I meant REPLAYER-NOTES' (the tracer session's replay-inputs 1-3, jolt-cpp rows of v1.1's era). W9's
deliveries at 821e1bd are the ones `ref`'s v1.3 replayer was last checked on (Quests 105-107), so Stage A uses these.

### ReplayerUnitTests, ported (2026-11-09)

`tests/test_replayer.cpp`: every group of `ref`'s `ReplayerUnitTests`, in its order, one TEST per group, with
`tests/replay_window.h` (ReplayWindow: the builders, the stream encoders, `java.util.Random` so the seeded inputs are
`ref`'s), `tests/replay_driver.h` (ReplayDriver line for line, on the C++ replayer: also Stage A's driver) and
`tests/replay_t.h` (ref's `T`; `T.ub` and `T.throwsISE` become a forked child that must abort naming the same texts).
**1,456 checks, all passing, in all four builds**; `ref` runs 1,473. The 17 not ported, exactly: ConvertDelivery's two
groups (10 + 5 checks: they test `ref`'s Java converter, which Stage A uses as is) and `queueSizes`' 8 KB control
(2 checks: `-Dreplayer.logq=512`, v1.25's log queues for a stall comparison, isn't modelled; LOG_QUEUE_ENTRIES is 256).
The timing group prints `ref`'s line exactly ("program pass: 85 cycles ..., stalls [0, 4, 0, 0, 2, 0, 0]").

Also: the replayer now latches the stream word through W14's `stream_word.h` (one definition of the layout); the
tests build stream words with its `build`. (A slip of mine on the way, worth recording: a `//` comment swallowed a
statement in the C++ `Bits::put` exactly as in ref's ReplayerTest line 105; the compiler caught it.)

### make check, all of it (2026-11-09)

clang debug (47 binaries), gcc debug, gcc release, clang release: every test passes; TSan, gcc and clang (the seven
thread tests): clean. (A tool call here is cut at 300 s and its background jobs with it, so the builds ran in pieces.)

## The slot's interface: for W14 and W16 (one round; the owner arbitrates)

Published, declarations only: `src/units/peripheral.h` (the method set, the type tags, `PeripheralBase<Self>`'s
defaults) and `src/units/periph_slot.h` (the unit: ports, the window RegFile's layout, the core's word, the dispatch).
The step's order and every port are AUDIT-M3 3.1's.

**The method set** (`peripheral.h`'s header lists it with ref's names): W14's `tracer.h` signatures taken as the
standard where they existed (int `type`, `params`, `errors`; `word(t, int w, uint32_t&)`; `write_word` 1/0/-1;
`consume_events` returning the bits taken; `out_take(t, q, uint64_t w[2])`; `out_start(t, q, int64_t)`), plus:
- `violation_count()`: ref's `violate()` becomes a count the slot reads after each step and forwards to the tile's
  violation counter (no host callback);
- `static constexpr int DIGEST_N` and `digest_words(int64_t out[DIGEST_N])`;
- three framework hooks, no-ops by default: `ports(sim::Ports&)` (a peripheral's own ports), `begin(sim::Ctx&, t)` (level
  0, first: its own inputs due) and `end(sim::Ctx&, t)` (after `step(t)`: its own outputs). Only C&C 1 needs them (READY,
  the error lines, GO: AUDIT 3.4).
- **The one timing point to agree:** the slot calls `accept(t, ...)` at level 0 of t + 3 (the director's Link(3)), so a
  peripheral must take an accept whose landing (t + 2) is not yet settled though it may have settled through t + 2. The
  replayer does now (its landings are checked against t + 2, not t). **W14: the Tracer's `accept` needs the same.**
- `run`, `write_word` and `consume_events` for a core access at t are called at level 0 of t + 1, consume first.

**For W14 (the Tracer, type 3):** derive `Tracer` from `PeripheralBase<Tracer>` (or keep defining every method), add
`violation_count()` (your `n_violations`) and `DIGEST_N = STAT_N`, and the late accept above. The slot drives the DMA
(`out_ready`/`out_take`/`out_start`) into the CreditOut S6.

**For W16 (types 1 and 2):** your classes `units::Cnc1Peripheral` and `units::CcStub` (names yours to change), deriving
`PeripheralBase<Self>`; C&C 1's own ports through `ports`/`begin`/`end`. Adding a type is one `case` in
`PeriphSlot::visit` (periph_slot.cpp, a marked block) plus your header's include there: add your two cases yourself, or
tell me the header and I will. AUDIT 3.4's open item (C&C 1's stores applied at level 0 of t + 1, one cycle later than
ref for its own work in the store's cycle) is yours to settle: the interface supports the D30 route as is; a `Wire` from
the core would need a second core input on the slot, which I'd add.

**What the core side does** (my work with the slot's body, in W8's/W11's files): a window load reads the RegFile
(`Win`'s layout; a reserved or undefined word reads 0 and the core flags it, from the `R_DEFINED` bits); a store or a
consuming EVENTS load puts one `CoreOp` on S2/S3; a store to a word whose `R_STALL` bit is set retries (S8).

### Stage A on the runner: Quest 164 (2026-11-09)

Claimed and pushed (Quest c8503c3): `tasks/164-w15-stage-a.sh`, kit from sim-w15 fd11957. W9's deliveries converted to
v1.26.1 in scratch by ref's ConvertDelivery (as 105-107); (1) the selected windows, all three variants, ref's
ReplayDelivery and `replay_delivery` compared with cmp (each also passing on its own); (2) every window of both programs
on the C++ driver alone (state all 8,192 each; state-lru and state-pick every 8th), against rows.bin, the streams,
end.bin, clean.

**Result (Quest 164, DONE, ALL PASS; runner godspeed, 16 CPUs, 2 minutes):**
- selected windows: evm-zone-29's 12 and cyclone-29's 13, each in state, state-lru and state-pick: ref's ReplayDelivery and
  `replay_delivery` **byte-identical** (every window's rows, the pass's cycles, the streams, new expansions, the
  stalls), each passing (786,432 and 851,968 rows compared per variant);
- every window: evm-zone-29 and cyclone-29, state all 8,192 each, state-lru and state-pick 1,024 each (every 8th):
  **20,480 windows, all OK**: every row of rows.bin byte for byte, the streams consumed exactly, end.bin, clean.

### The slot's body (WIP, 2026-11-09)

- `periph_slot.h` restructured: **`SlotCore`** is ref's `PeripheralSlot` method for method (immediate calls, ref's
  timing: `accept`, `read`, `write`, `store_stalls`, `live_events`, `live_errors`, `run_cycle`, `end_cycle`, `reset`,
  the DMA, the reserved-word flags as facts), testable without the engine; **`PeriphSlot`** is the unit: AUDIT 3.1's
  level-0 order around it (begin; the consume or EVENTS_MASK store of t - 1; the landings, `accept(t - 3)`; the other
  store of t - 1 at t - 1; the window published, changed registers only), then `run_cycle(t)` with the DMA's credits,
  `end(t)`. Wake: the next cycle while an input arrived or a peripheral wasn't idle, and once more after (to publish
  what it left); else `next_wake_at`; else S1/S2.
- `units/test_peripheral.h`: ref's TestPeripheral (type 15) for the slot's tests.
- `visit`: the replayer and the test peripheral; marked places for W14's Tracer case and W16's two.
- **A UB-only difference, recorded (owner: accepted, not a D30 case; also in AUDIT-M3 3.1):** a DMA_HDR/DMA_ADDR store
  while a transfer runs (flagged as undefined in ref) takes effect a cycle later than ref's (at level 0 of t + 1, after
  ref's runCycle(t) would have used the new value). Only in the flagged case; a store before PULSE_DMA (the defined use)
  is unaffected. Coverage 0 in both M3 runs (ref flags each such store; AUDIT section 0: no chip violations).
- Next: **SlotConf** (as ReplayConf): ref's `PeripheralSlot` taken from a `Net` built as PeripheralSlotTest builds it
  (TestPeripheral on a tile; the replayer on another), driven directly by a seeded script (posts, credits, live bits,
  faults, accepts, window loads and stores, DMA transfers with and without burst, runCycle, endCycle), every result
  and observation recorded; SlotCore must equal it call for call. Then the unit under the engine, then the tile's paths
  and PeripheralSlotTest's port.

### SlotConf: SlotCore against ref's PeripheralSlot (2026-11-09)

- `tests/ref/SlotConf.java gen | run`: ref's `PeripheralSlot` taken from a `Chip` built as PeripheralSlotTest builds its
  machine (`Config.defaults()`, a TestPeripheral as tile 5's peripheral 1; tile 6 keeps its replayer), the Chip never
  stepped, the two slots driven directly by a seeded script: the test peripheral's posts, credits, live bit, faults and
  burst; region accepts (one per slot per cycle, as the director); window loads (standard, reserved, undefined, the
  peripheral's words, consuming EVENTS loads) and stores (PULSE_RUN, EVENTS_MASK, DMA_HDR/DMA_ADDR/PULSE_DMA, the
  replayer's RUN_CONFIG, scratch and read-only words, reserved words); storeStalls; runCycle and endCycle. ref's UB is
  avoided by the generator (PULSE_DMA while busy or naming a missing queue, the replayer's PULSE_RUN or RUN_CONFIG while
  running, part-0 writes while running). Observed after each cycle: both slots' live bits, counters, the DMA's state and
  every packet (time, dst, mode, counter, address, data), the test peripheral's state, the replayer's.
- `tests/slotconf.h` replays it on two `SlotCore`s; `test_slotconf` (`make check`): **5 seeds of 3,000 cycles (about
  1,000 DMA packets), equal to ref, all four builds**; locally also **20 seeds of 10,000 cycles, all equal**
  (`slotconf.sh --long`). A planted bug (EVENTS_MASK stores never applied) fails all five.
- Two fixes on the way: the test peripheral's records became rings (a script has thousands of accepts); the
  peripherals' violation counts are summed when read (`violation_total`, ref's single count) instead of forwarded at
  `run_cycle`.
- **My own slip, twice now:** an inline edit put a `//` comment in the middle of a line and swallowed the statements
  after it (ref's ReplayerTest line 105 has the same bug). Caught both times by a failing comparison; I now write
  comments in such edits as `/* */` or on their own line.

### The slot unit under the engine: first working commit (2026-11-09)

- `PeriphSlot` is a framework unit now: move-only (each `SlotCore::Ref` owns its peripheral and deletes it by type),
  non-polymorphic, with a `digest()` for the debug checks.
- `tests/test_periph_slot.cpp`: a stand-in core and director (S2/S3 Link(1), S1 Link(3), the window RegFile, the DMA's
  credit Fifo of 4) and the slot, run plain, with step-all and shuffled, the missed-wake detector on (debug builds):
  - the test peripheral on a controller (64): PARAMS and the defined-word bits; PULSE_RUN at 3 seen as STATUS [0] at 4;
    the scratch word; DMA_HDR/DMA_ADDR/PULSE_DMA at 10: STATUS [1] at 11, three packets put at 11, 12, 13 (ref's: the
    cycle after PULSE_DMA), at 0x100 + 16k, src 72 (item 118), their data; a region write at 12 landing at the end of
    14; EVENTS_MASK, run done and DMA done in EVENTS and the live bit, a consuming load taking both;
  - the replayer on a mesh tile: PULSE_RUN with rows 0 (STATUS [0] for one cycle, run done), the consume, a landing in
    the code queue, ROWS_RUN; the slot asleep for most of 200 cycles.
  All four builds; clang release `make run`: 50 binaries, all pass.
- **For W14 and W16:** wire into `units/periph_slot.cpp`: your include, your `case` in `SlotCore::visit` (marked), your
  `delete` in `SlotCore::Ref::~Ref` (marked), and your creation in `SlotCore::install` (W14: the Tracer needs its tables
  and memory system: tell me what `install` should take, or add a `PeriphSlotConfig` field for it).

### make check on main a880d08, and W16's requests (2026-11-09)

- **The full make check on main (a880d08, the merge of dab8ec9): clean.** gcc debug, gcc release, clang debug, clang
  release: 49 test binaries each (`make run`'s list), all pass; TSan gcc and clang: the seven thread tests, all pass, no
  warnings. (Run in pieces: a tool call here stops at 300 s.)
- **W16's requests:** `SlotCore::cnc1(n)` (and `cc_stub(n)`, `tracer(n)` for W14) beside `replayer(n)`: a static_cast
  from the slot's pointer, no include needed; `ControlIO::dma` (a CreditOut, width 3: the slot's S6 into the controller
  router's CC_DMA_IN credit Fifo) and `ControlIO::has_dma` in `chip.h`.
- **`install`'s place marked** for W16's types, as `visit` and `~Ref` are; `install` takes the slot's `PeriphSlotConfig`.
- **Merged main a2009dc (W14's Tracer wiring):** W14's design stands: the Tracer created bare (`new tracer::Tracer(tile)`),
  its tables by region packets, its memory from the harness after the build (`tracer(n).own_memory(store)`), as ref
  does. I dropped my `PeriphSlotConfig::tracer` (no need beyond `own_memory`) and my duplicate `tracer(n)`; kept W14's
  case lines in `visit`, `~Ref` and `install`, and its accessor.

### The tile's paths (WIP, 2026-11-09): in the closed workers' files, each change and its reason

Gated by `G1Config::periph_slot` (default off: M2's machines, goldens and digests unchanged; W16's M3 chip turns it on).
- **`rv32.h` (W5/W8, the core):** a present window's load, store and store stall go through three optional environment
  hooks (`window_load`, `window_stalls`, `window_store`; the header lists them) instead of FATAL "not modelled yet". A
  reserved or undefined word's load reads 0 and is counted by the core (`reserved_accesses`, `violations`): ref counts
  it in the slot; the counts are diagnostics (T2 requires no violations). W5's death test
  `rv32_peripheral_window_not_modelled` now expects the new message ("this bench has no peripheral slot").
- **`tile_ctl.h` (W8):** `TileCore` declares `win` (the slot's RegFile) and `periph` (S2/S3, Link(1)) when the slot is
  present, and implements the hooks from the `Win` layout: a consuming EVENTS load puts its `CoreOp::consume(value &
  mask)`; a store to a standard read-only word (0, 1, 2, 5, 6) or to a defined read-only word of the peripheral's
  (`Win::R_WRITABLE`, new: the static map from the peripheral's `writable(w)`) is a bus error at once, as ref's
  `write()` returns false; any other store goes to the slot as `CoreOp::store`. `TileCtl` reads the slot's `R_LIVE`
  (`slot_win`) before its end of cycle.
- **`regblock.h` (W5/W8):** `slot_live`: EVENTS [15:13] and ERRORS [13:11] in `reg()` and `publish()`, the WFI wake,
  `irq()` and the error line (ref's `eventsAt`, `errorsAt`, `wakeTerm`, `errorLine`).
- **`director.h` (W8):** mode 0 to regions 0x04-0x06 goes to `periph` (S1, Link(3)) when `periph_slot`, instead of
  FATAL.
- **`tile_ctl.cpp` (W8, the builder):** with `periph_slot`, adds `t<k>.slot` (the replayer as peripheral 1) and wires
  S1, S2/S3 and the RegFile to the core and the block.
- **make:** `UNITS_LINK` and the chip's TSan link list carry the slot, the replayer and the Tracer's objects.
- `peripheral.h`: the optional `writable(w)`; the replayer's (word 32) and the test peripheral's (word 33). **W14, W16:**
  add `writable(w)` to the Tracer and C&C 1's peripheral (without it every defined word is taken as writable and a
  refused store is FATAL at the slot).

### The slot holds its peripherals' own ports (2026-11-09; owner's priority, for W16's boot)

The framework rewrites a port handle in place at the build and requires it inside the unit object, so C&C 1's own
ports (READY rows, the error lines, GO) couldn't be declared from its heap-allocated body. Now, in general:
- **`OwnPorts`** (`peripheral.h`): a member of `PeriphSlot` (`slot.own`), fixed capacity per kind (Link inputs 16,
  Link outputs 80; another kind is one more array and one more declare method), filled at configuration.
- **`OwnPortsDecl`**: a peripheral's `ports(OwnPortsDecl& d)` declares each port with `d.link_in(name, width)` /
  `d.link_out(name, width)`, which returns its index; the peripheral keeps the indices (integers survive the build's
  move). Its hooks get the storage: `begin(Ctx&, t, OwnPorts&)`, `end(Ctx&, t, OwnPorts&)`.
- **Wiring** (a chip builder, a test): `slot.own.link_in[i]` / `slot.own.link_out[i]` with the peripheral's indices.

**For W16 (please confirm or adjust; the owner arbitrates): I changed your files to the interface:**
- `cnc1_periph.h`: `rows[8]`, `cc_line[3]`, `go[64]` became the indices `row0`, `line0`, `go0` (declared in your order);
  `ports`, `begin`, `end` take the new types and reach the handles through `OwnPorts`; their bodies otherwise
  unchanged. Added `writable(w)` (37-45 but 43; 32-36 and 43 read-only), from your `write_word`.
- `test_cnc1_periph.cpp`: your stand holds an `OwnPorts` for the peripheral and wires through it; **your through-the-slot
  test is on** (`#define W16_SLOT_OWN_PORTS 1`; remove the guards when you like). In `run_slot`'s core side: a store to
  a defined read-only word isn't sent (the core judges it a bus error, as TileCore does); and the check "the slot
  flagged a violation" became "exactly the script's stores to undefined words": the slot flags those as ref's
  `PeripheralSlot.reserved` does (your Cnc1Conf's ref side calls the peripheral without a slot, so it has none).
- **Acceptance:** `cnc1_periph_through_the_slot_conformance_with_ref` (your s1-s3 fixtures, plain, shuffled and
  step-all) passes in all four builds; with `test_cnc1_periph`'s other tests, `test_cc_units`, `test_periph_slot`,
  `test_slotconf`, `test_tracer_slot`.

### The DMA record on mesh tiles, and the tile-level test (2026-11-09)

- **DMA record:** a mesh tile's slot has no DMA port; its peripherals' DMA packets are recorded (`PeriphSlot::
  dma_record`, a ring of 16, every packet counted in `dma_recorded`), as ref's slot keeps them in `dmaOut` (its
  `dmaPort` is null there), instead of FATAL.
- `G1Config::periph1` (default the replayer): peripheral 1's type in a tile's slot (tests: the test peripheral).
- **`tests/test_tile_slot.cpp`**: a tile from `add_g1` with `periph_slot` on, the test peripheral as peripheral 1, a
  program in the boot ROM through the core's real paths: PARAMS; a store and load of a read/write word; PULSE_RUN and
  STATUS right after it (running); the slot's EVENTS_MASK and the block's (EVENTS [13]) and a **WFI woken by the run's
  end through the live bit** (11 cycles after PULSE_RUN, ref's run_length + 1, to 4 more for the wake's path); the
  peek and **consuming** EVENTS loads (consumed by the next peek); a reserved word (reads 0, flagged by the core); a DMA
  transfer of two words (recorded); the block's EVENTS [13] down (DMA done not under the slot's mask); a store to a
  read-only word: **a store access fault at once** (mcause 7, mtval its address), the scratch word unchanged. And a
  region-0x04 write put by the router's stand-in, accepted by the test peripheral. Plain, step-all and shuffled; the
  debug builds' missed-wake detector on; all four builds.
- Exact cycles against ref come with PeripheralSlotTest's port (next).

### PeripheralSlotTest as conformance: TileSlotConf (2026-11-09; owner: approved as planned)

- `tests/ref/tileslotconf_progs.py` writes `tests/tileslotconf/<group>.prog`: the window, events, region writes
  ("landing"), the DMA and the replayer's window, each a boot-ROM program doing the group's loads and stores from the
  core (a prelude sends every other tile to a WFI loop), storing results and mcycle stamps to data memory; pokes on the
  test peripheral between cycles (post, credit, the live level, fault); region writes into the tile.
- `tests/ref/TileSlotConf.java` runs a .prog on ref: the program as `Controller.bootRom`, `Config.machine()`, the test
  peripheral as the tile's peripheral 1 (or its replayer), the pokes before their cycles, the injects with `Net.at`;
  writes the data-memory image, the director's accepted region writes ("dir": their cycles), bad packets, the test
  peripheral's accepts, the slot's DMA packets (`<group>.obs`, committed).
- `tests/test_tileslotconf.cpp` runs it on a tile from `add_g1` with the slot: the pokes at the same cycles (between runs,
  the slot woken with `Sim::wake`), each region write put in the eject one cycle before ref's director accepted it.
  **All five groups equal ref exactly** (the data-memory images with every mcycle stamp, the accepts and their landing
  cycles, the bad packet, the seven DMA packets with their cycles), plain, step-all and shuffled, in all four builds.
  Two planted timing bugs fail it (S1 as Link(4): the landings a cycle late; the core's stores applied a cycle late:
  seven words of the images).
- `tests/ref/tileslotconf.sh --fixtures | --check` regenerates the programs and reruns ref (`--check`: equal).
- PeripheralSlotTest's two UB stops (PULSE_DMA while a transfer runs; a transfer naming a missing queue): death tests
  in `test_periph_slot` (the FATALs name them, as `T.ub` expects). Not ported here: the C&C 1 group (W16's acceptance
  test covers it through the slot).

### TSan on main (2026-11-09)

clang `tsan-chip` and `tsan-units` (`test_controllers`) on gcc and clang: all pass, no warnings.

### Replayer::fingerprint (AUDIT-M3 6.8), for W16's boot-end comparison (2026-11-09)

- **`Replayer::fingerprint(Out& o, int64_t c, const FpPending* pending = nullptr)`** (`replayer.h`, a template like
  W14's `Tracer::fingerprint`): unit `rp`, 6.8's stream in its order with ref's values (Java's ints sign-extended,
  booleans 0/1): part 0, the decode-table and ROM sums (regions 12, 13), RUN_CONFIG; the three streams and the expansion
  queue (pops at c); the core, `cur`/`nxt`, the row in flight (its instruction by identity: the replayer now numbers its
  instructions, and keeps a row's `hold`); the events' counts, ERRORS; the output queue; the inputs not yet in effect.
- **Checked against ref:** ReplayConf's observation now carries the fingerprint at the end of every cycle (ref's side
  `StateFpM3.replayer`, compiled with ReplayConf; before the observation's reads settle t + 1). **All 13 traces equal
  every cycle** (up to 25,857 fingerprint changes in a trace), in all four builds; the fixtures' observations were
  regenerated by ref from the committed scripts (`replayconf.sh --check`: equal). Planted bugs (the pops at c
  unconditioned; the input count off by one) fail it. (`inflight.instr` is always 0 in practice: a row in flight
  always belongs to `cur`, since the next instruction only becomes `cur` after the row finishes.)
- **In a chip, the inputs are in the slot's links.** At the end of c, ref's replayer still holds in its own lists the
  region writes its director accepted at c - 2 .. c and a PULSE_RUN or RUN_CONFIG store of c; ours has applied
  everything older and those are in S1 (Link(3)) and S2 (Link(1)). **`PeriphSlot::fingerprint_replayer(sim, n, o, c)`**
  reads them with the port observer (`Sim::observe`) and writes them as ref's (landing e = arrival - 1, word address,
  size, lo, hi; inputs e = arrival - 1, kind 2 or 3, value). `test_periph_slot`: a slot's replayer at the end of 5 (a
  PULSE_RUN of 5 in S2) and of 12-14 (a region write in S1) equals a twin replayer driven directly, as ref's is.
- **For W16:** call `slot.fingerprint_replayer(sim, 1, out, c)` for unit `rp` of tiles 0-63 from the M3 chip line (not
  `replayer(1).fingerprint` directly: that would miss the inputs in flight).

### D35: the slot publishes only what changed (2026-11-10)

- `PeriphSlot::publish`: the first publish writes every register (from `full_values`, the plain per-word way) and records
  each peripheral's defined own words; every later one makes one dispatch per peripheral and computes only the
  standard words, the defined own words, the store stalls if the type has its own `store_stalls`, and the live bits from
  those values; only changed registers are written. Debug builds check every register against `full_values` after each
  publish (fatal, naming the register; a planted missed register fires it).
- Unchanged behaviour: the slot's tests (`test_periph_slot`, `test_slotconf`, `test_tileslotconf`, `test_tile_slot`,
  `test_tracer_slot`, `test_cnc1_periph`); **T2 locally on one domain with every point** (inputs from Quests 165, 167,
  168, sha256 as Quest 170's): evm-zone-29 241 points and cyclone-29 285, all 1,214 units equal, exits and ends as ref,
  64 of 64 rows equal, lag_cycles 0; make check build by build (57 binaries each).
- Local timing (this container, clang release, the checked evm run): **53 s before, 17 s after**.
