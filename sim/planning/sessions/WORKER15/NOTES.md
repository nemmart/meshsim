# WORKER15 (sim/, M3): notes

The replayer and the peripheral slot. Branch `sim-w15`. Prompt: `PROMPT.md`.

## Status

**Go-ahead 2026-11-09** (owner): the plan as written; answers to Q1-Q4 below. Step 1 done (shared headers, the ALU, both equal to `ref`); working on step 2 (the replayer).

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
