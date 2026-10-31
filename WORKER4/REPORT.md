# WORKER4 report: meshsim to v1.2

*In progress (phases 1-4 done; 5 and 6 to come). The final version lists what was built, every decision of my own,
the differences from the draft header, the measurements and what the sort and replayer ports need.*

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
