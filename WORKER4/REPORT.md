# WORKER4 report: meshsim to v1.2

meshsim on branch `v1.2-meshsim` implements `wip/spec/machine-spec.md` v1.2 (with DECISIONS-v1.2 up to section 49).
Six phases, each closed with the quick suite and the full extras passing (the last on the Quest runner, task 076).
This report is for the merge with `v1.2-sort` (WORKER8) and for the v1.25 work (WORKER11's `v1.25-meshsim`).

## What was built, by phase

1. **The link word, the director, the engines** (phase 1): the 256-bit word with a 32-bit address and a 3-bit counter
   field; modes 0-5; bad packets dropped and counted; seven write counters decremented K cycles after acceptance
   (a delay line); the stray named at acceptance; STREAM's v1.2 fields; byte-addressed mode 0; the read port's and the
   write engine's v1.2 rules (natural alignment, no spanning: misaligned reads not performed, misaligned writes dropped
   and counted); C&C 1-3 on tiles 0-2's fifth ports; 32 KB controller memories.
2. **The register map** (phase 2): `Registers.java`'s v1.2 table; `MeshHeader.java` generates `sw/mesh.h` 1.2, which
   agrees with the draft everywhere they overlap; `RegDoc.java` generates `wip/sw/REGISTERS.md` (W4-04); the block, the
   windows, the table window and their timing; `SIM_EXIT`/`TRACE`; the request tree's receive queues (128, 256, 128);
   `sw/` and the riscv-tests on the v1.2 map.
3. **Peripherals** (phase 3): the framework (three per unit, the standard window, counted events, live bits, a DMA);
   the replayer on it (type 4, credits, its ERRORS and words); C&C 1's peripheral (ERROR/READY lines, `PULSE_GO`, the
   watchdog and toast pin, LED, the UART); the replayer port end to end (`sw/replay/replay_ctl.c`, StageB synthetic).
4. **Errors, reset, boot** (phase 4): traps and the trap CSRs, `mcycle`/`mtime`, the hard interrupt, meshsim's loud
   stops; the boot ROM (`sw/bootrom.S`, image `sw/bootrom.hex`); the reset (every bus emptied, every unit as at
   power-on except the memories); RUN/HALT, the interim loader and v1.1's REPORT flow removed; CondTest on the new path.
5. **meshsim-only checks** (phase 5): invariant 15 as a check; 16, 20, 21, 25 added; the coverage table below.
6. **The recipes as suites, the measurements** (phase 6): all 19 v1.2 recipes built unedited and run (RecipeTest);
   item S1 measured (S1Bench); K's formula per D49; RECIPES.md section 14 filled.

## Decisions of my own, and the questions

| # | question | answer |
|---|---|---|
| W4-01 | the sort's steps in run_tests.sh while the sort is unported | report "PENDING (sort port)" (agreed) |
| W4-02 | writes into data memory's first 16 bytes | flagged at any time (agreed) |
| W4-03 | uncounted writes into controller memory | only from C&C 1-3 (agreed) |
| W4-04 | who owns `wip/sw/REGISTERS.md` | WORKER4; RegDoc generates it |
| W4-05 | peripheral types | 0 none, 1 Ingress/Egress, 2 Updater/Binder/RW, 3 Tracer, 4 replayer (D46) |
| W4-06 | words 8-9 of a peripheral without a DMA | read 0, a load unflagged, a store flagged (D48) |
| W4-07 | `mtime`'s rate | implementation-defined; meshsim counts reference cycles through `time` (D48) |
| W4-08 | K's formula against meshsim's write path | meshsim's reference becomes max(2L + 4, L + 4, cm_latency, rp_latency) (D49) |

meshsim's own choices, not in the spec: the boot ROM's incidental load of slot 0 isn't held to the command-read check
(the next command may land first); the boot ROM's WFIs stay out of `wfiLog`; C&C 1's model waits for all 64 READY
(`READY_EXPECT` all ones, set again after a reset); `Config.checkErrors` turns invariant 15 off only where a test makes
errors on purpose; S1 flags a write later than K + 4 (the controller's least reaction).

## The header against the draft

`sw/mesh.h` (generated) and `wip/sw/mesh-v1.2-draft.h` agree on every macro and struct offset they share (170 macros,
81 members; RegDocTest runs `tools/header_diff.py`). The generated header adds `OFF_*` for every register,
`TABLE_BASE` and `sim_trace()`. `wip/sw/mesh.h` is the generated header.

## Measurements

- **The recipes:** RECIPES.md section 14, from RecipeTest (`results/recipes.log`).
- **Item S1** (`results/s1bench.log`): big SRAM's write path 2L + 4, PTR and HIST L + 3; K = 8 covers L <= 2 (margin
  2 at L = 1, 0 at L = 2); at L = 3 and 4, K = 10 and 12 (margin 0). The table-window case: HIST loads come 24-25
  cycles after the last update in the radix sort.
- **A read-port search step** costs about 6 cycles more than in v1.1 (35.9 + 37.42 per active search): K = 8 after
  acceptance against v1.1's landing at `cm_latency` (one K for every destination is deliberate, 4.7).
- **The sweep's bands** equal phase 2's (`results/sweep-quick.log`).

## What the sort port needs (for the merge with v1.2-sort)

- **R1:** the sort's test files (SortTest, Inputs, SortSummary, VerifySummary) and run_tests.sh's sort steps are
  WORKER8's: v1.2-sort's versions stand at the merge.
- **R2 (boot):** no interim loader and no `Registers.LOADER_BASE`: `Mach.boot` broadcasts the image, sends `CMD_JUMP`,
  then GO when every tile in `READY_EXPECT` is READY; the ROM jumps. `cc1p.expect` selects the tiles; `CnC.reset(hard)`
  is the reset.
- **Phase indices:** `cc.phaseLog` and the chip's `phases` start with the boot's GO (entry 0).
- **Invariant 15:** an ERRORS bit is now a violation unless `Config.checkErrors` is off.
- **K:** `Config.kMin()` is 2L + 4 at most L; `withTsLatency(L)` raises K to it.
- **R4:** the regenerated `mesh.h` is committed in both places check_layout.sh compares.
- **R3:** SplitRun and splitdrv.c stay unported (below).

## What the replayer port needs (v1.25)

- The replayer is type 4 on the peripheral framework; Stage A's driver (`ReplayDriver`) and `replay_ctl.c` consume
  credits as 4.19.8 says (the expansion's first chunk on an explicitly taken credit); invariant 16's checks (no refill
  past the credits taken, only the tile itself refills during a pass) apply to any new controller loop.
- Deliveries for real guest windows come with v1.25 (`DELIVERY=<dir>` runs StageB on them); StageB synthetic is v1.2's
  check.

## Known gaps at v1.2's freeze

- **The boot ROM's controller branch is untested.** The ROM (sw/bootrom.S, 6.2) branches on PARAMS' endpoint id; its
  tile branch (0-63) runs in every suite, but its branch for C&C 1-3 (64-66: wait for "command landed", restore the
  mask, jump) never runs in meshsim, because C&C 1-3 are behavioural Java models without cores (6.2's boot sequence,
  step 3: "In meshsim v1.2, C&C 1 stays a behavioural Java model").
- **C&C 2/3's "started" requests (6.2, step 4) aren't modelled**: their images and firmware don't exist yet, so C&C 1's
  model never waits for them. The request tree carries a controller's requests (RequestBus.controllerSend, tested in
  RequestBusTest), so the path is there when the firmware is.

## Stale, not ported

- `test/SplitRun.java` and `sw/ext/split-bisect/splitdrv.c` (WORKER8's R3, owner: leave both unported).

## Section 9: the invariants in meshsim (phase 5)

Every invariant is a meshsim check unless noted. "Where" is the class that flags it; "tested" is a suite that provokes it.

| # | invariant | where | tested |
|---|---|---|---|
| 1 | no FIFO overflow (routers, fifth ports, write-engine input, PTR/HIST requests, GATHER queue) | Chip (every hardware FIFO), ERRORS [0] [4] [5] [6] | NetTest fan-in, CondTest 0 |
| 2 | one access per area per cycle; no buffer read and written in one run | BigSRAM, Chip | MemTest |
| 3 | the timetable holds (drift) | AddressGenerator, ERRORS [3] | EngineTest drift, CondTest 3 |
| 4 | every expected arrival lands before done | TileControl (done early, ERRORS [8]) | CtrlTest invariants, CondTest 8 |
| 5 | no bad packets, no size 11 | TrafficDirector, InjectionPath, Endpoint (ERRORS [1]) | NetTest director, CondTest 1 |
| 6 | no HIST count past 17 bits | TableStore | EngineTest |
| 7 | withdrawn (no RUN/HALT) | - | - |
| 8 | overwritten-unread results counted (strict mode a violation) | ControllerMemories, ReadPort | CtrlTest invariants |
| 9 | no unexpected arrival; the stray named at acceptance | TrafficDirector (ERRORS [2] at the decrement) | NetTest director, CondTest 2 |
| 10 | gathers are local | InjectionPath, AddressGenerator | NetTest director, AgTest |
| 11 | controller-memory writes keep to their places | TrafficDirector.checkCmem | NetTest director |
| 14 | read-port results and incoming traffic don't overlap | TrafficDirector | ReadPortGatherTest |
| 15 | no errors: every ERRORS stays 0 (Config.checkErrors) | Chip | CondTest |
| 16 | the peripherals' rules: no PULSE_RUN while running, no PULSE_DMA while busy or to a missing queue, no DMA_HDR/ADDR change mid-transfer, no credit to a full counter; the replayer's: its ERRORS, only the tile refills during a pass, no refill past the credits taken | PeripheralSlot, EventCounts, Replayer | PeripheralSlotTest, ReplayerTest |
| 17 | the request contract | partly: d = 67-125/127 and d = 126 on a mesh tile (AddressGenerator), packets to 67-125 (InjectionPath), targets without a receiver (RequestBus), REQUEST_RECEIVE on an empty queue (ERRORS [14]) | AgTest, NetTest, RequestBusTest, CondTest 14 |
| 18 | one source per order-placed destination | partly: the replayer's queue ports (Replayer) | ReplayerTest |
| 19 | everything stays in bounds | TrafficDirector (packets), Controller (core accesses alias, flagged), SRAMReadQueue, WriteEngine | NetTest director, MemTest |
| 20 | pointers and runs stay aligned | AddressGenerator, SRAMReadQueue, WriteEngine, ReadPort, TileControl (DMA_RECORD_BASE_ADDRESS) | MemTest, EngineTest, AgTest, CtrlTest registers |
| 21 | no reserved encodings: modes 6-7, count-only on counter 0, DMA_CONFIG digits 5-7, reserved words and bits | TrafficDirector, InjectionPath, AddressGenerator, Tile, TileControl, PeripheralSlot | NetTest director, HooksTest, CtrlTest registers |
| 22 | uncounted traffic is commands and overlays only; nothing in data memory's first 16 bytes | TrafficDirector (W4-02, W4-03), Controller | NetTest director |
| 23 | no silent traps (ecall, ebreak, fetch, misaligned jump, a trap in the handler) | Controller: meshsim stops loudly | CtrlTest faults |
| 24 | K covers the write path | TrafficDirector (the formula at construction); the margin measured in phase 6 | NetTest director |
| 25 | boot keeps its order: one kind; no overlay after the command | TrafficDirector | CtrlTest invariants |
| 26 | no ignored start (PULSE_DMA while running) | AddressGenerator | AgTest, HooksTest |

**Not checked, and why:**
- **17, the per-tile request limits** (1, 3, 1 outstanding to C&C 1-3): "outstanding" lasts until the reply or the data has
  landed, which only the memory system's protocol defines (6.7, item 119); meshsim has no replies to match. Also
  17's tag-window rules and "no read outstanding during a group run" (the memory system, deferred).
- **18, two clients of one unit feeding one PTR entry at once** (the address generator, the GATHER engine and the send
  port): the receiver can't tell the clients apart in time without a model of "at once"; left for a later version.
- **16, "every refill but a stream's last is exactly half a queue"**: a software balancing rule the loop keeps; the
  credit check catches the harmful case (a refill past its credits).

## The recipes as suites (phase 6)

`sw/build_recipes.sh` builds all 19 of `wip/sw/recipes/`, unedited, against the generated `mesh.h` (with the recipes'
own `crt0.S` and `handler.S`, `sw/recipes.ld`, and `sw/recipes_support.c` for what a recipe leaves to its program:
`template_lookup`). RecipeTest runs one driver per program and checks the book's code blocks against their sources.

- **Not run: `rcp_cc1.c`** (recipe 27, C&C 1's side of a phase): C&C 1 is a Java model without a core in v1.2, so the
  program only builds. The model's own GO, READY and request service stand in for it.
- **`rcp_replay.c`'s `state[]` is never written** (a placeholder for the program's checkpoint): gcc folds it to zeros,
  so the pass it starts has an empty code stream and the replayer raises invalid expansion at its first pop. The
  driver checks the loop's shape (the state load, PULSE_RUN, credits within their limits, the masks restored) and that
  error alone; the replayer's real check is `StageB synthetic` (`sw/replay/replay_ctl.c`). For the recipes' owner: a
  state worth loading would make recipe 26 testable end to end.
- **`rcp_tables.c`'s `expected` is never set** either: command 1 is a run that writes nothing here, so `a2a_finish`
  waits for DMA run done alone; the driver sets up an 8-stream empty run from Java.
- Drivers changed from v1.1's where the v1.2 programs differ: sizes the same on every tile (bucket 200, aug 64, copy
  100, rdport 300), the selftest's signatures as requests to C&C 1 (answered by its model), the rules' right forms only,
  `blk` and `vecs` filled from Java after start-up (the programs leave them zero).
