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
