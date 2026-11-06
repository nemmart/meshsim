# WORKER5 questions (spec v1.2)

Where the decisions log was silent, ambiguous or self-contradictory. Each has the spec section, my proposed
answer (written into the spec, marked *(Proposed)* unless ruled), and its status. Spec section numbers are
`wip/spec/machine-spec.md` v1.2's; open items are its section 13.

## Ruled by the owner (2026-10-29)

| # | question | where | ruling |
|---|---|---|---|
| W5-01 | Where do the command and reply blocks sit in the 32 KB data memory? Proposed: at the top, as in v1.1's 8 KB one: command block 0x00007FC0–0x00007FFF, reply block 0x00007BC0–0x00007FBF, stack below | 4.11, 4.12, 10 | **as proposed** |
| W5-06 | WORKER2's round-2 checklist says write-engine input overflow becomes a hardware-fault detector (H); no decision says so, and a placement error can still overflow it. Proposed: it stays a program error (P) | 4.9, 4.13 | **as proposed** |
| W5-13 | Section 21's reset hold (at least `error_latency`) was for a C&C 1 outside the reset. Is it still needed with an outside, whole-chip reset? | 6.1 | **dropped:** the error lines' pipeline flops reset with the whole chip; the spec says why |
| W5-15 | Section 17 lists "boot and staging images for big SRAM" as uncounted; 31 and W2-27 make big-SRAM data counted. Proposed: always counted | 3.1, 4.14, 6.2 | **decided in the log** (dd6c651): 17's line superseded; spec updated |
| W5-17 | Section 25 left open whether requests carry counters. Proposed: a request names its data's and its reply's counters | 6.7, 9 (17) | **decided in the log** (dd6c651); *(Proposed)* dropped |

## Ruled in the decisions log, sections 39 and 42 (2026-10-29)

| # | question (short) | ruling |
|---|---|---|
| W5-02 | a write through a misaligned `PTR`: does the pointer advance? | **as proposed:** dropped but counted, the pointer advances by the size (D42) |
| W5-03 | `ROUTER_ADDR` advances for which modes? | **as proposed:** mode 0 only (D42) |
| W5-04 | one-cycle loads from program memory and the boot ROM | **with Tony** (Q16); spec item 134 |
| W5-05 | banking of the 32 KB memories | **port B takes one 128-bit write per cycle; how it is built is the implementer's choice** (D42) |
| W5-07 | read-port queue entries carry their counter | **as proposed** (D42) |
| W5-08 | D26's proposed bad-packet rows | **as proposed** (D42) |
| W5-09 | the replayer's window words | **D39:** 32 `ROWS_RUN`, 33 `EXPANSION_OFFSET`, 34 `EXPANSION_OPCODE`, one field per word. The event and error bits stay proposed (item 123) |
| W5-10 | the replayer's part bits at byte offset [22:20] | **as proposed** (D42) |
| W5-11 | a controller takes fifth-port packets one a cycle | **as proposed**; the router port deferred with W2-31 (D42) |
| W5-12 | `WATCHDOG` counts core cycles | **as proposed** (D42) |
| W5-14 | `LED` resets to 0 | **as proposed** (D42) |
| W5-16 | the memory system's unservable requests | **as proposed:** a peripheral error of C&C 2, raising its error line and the toast pin (D42) |
| W5-18 | C&C 1's peripheral words, event bits, `WATCHDOG`'s load | **as proposed** (D42) |
| W5-19 | the exit word at 0xFFFFF9F8 | **as proposed** (D42) |
| W5-20 | the controller block's addresses | **as given by D11, D30 and D40** (seven counters at 0x860–0x878, the read port at 0x87C and 0x880) (D42) |
| W5-21 | one-cycle access of the 32 KB memories at 1 GHz; their area | **area fine** (about 0.15 mm²); **timing with Tony** (Q16); spec item 134 |

## Round 2 questions (all answered in the decisions log, sections 43 and 44)

| # | question | where | proposed answer | item |
|---|---|---|---|---|
| W5-23 | D38 says a tile issues its next request to a target "only after the previous one's reply has come back", but a read has no reply: its answer is its data (6.7). When is a read no longer outstanding? | 3.3, 6.7 | when its data has landed (its counter done); the per-tile limits count it until then | 135 |
| W5-24 | D41 (F16) has the spec define the report set. I defined 24 reports (x1–x31, `mepc`, `mcause`, `mtval`, `PARAMS`, `STATUS`, `ERRORS`, `EVENTS_PEEK`, the seven counters, then three per peripheral slot, `DMA_ADDR` padded with two zeros). The header's field positions and the op's value are still open | 4.13 | as written; the header with C&C 1's request format | 122 |

| W5-25 | Does 3.2's "in the order sent" cover a unit's two injection sources? A send-port packet stored after a stream run's DMA run done can overtake the run's read returns still in big SRAM's pipeline (WORKER6's F17). Recipe 22 avoids it by putting the boot command in the overlays' run | 3.2, 4.5, 6.2 | say in 3.2 that the two sources are unordered, so a sender that needs the order waits for the run's packets to land or puts the packet in the run; hardware ordering would cost an interlock between the send port and the read returns | 136 |
| W5-26 | Should 6.2 state C&C 1's rule for one-at-a-time starts: land a tile's first command only when that tile should start at the next GO? It follows from the ROM's test (D37.1), and recipe 22 states it | 6.2 | yes, one sentence | — |

Recipe mismatches found in round 2 are in `WORKER5/RECIPE-CHECK.md`; questions that need a ruling are added here.

## Rulings on round 2's questions (decisions 43 and 44, 2026-10-29)

| # | ruling |
|---|---|
| W5-23 | **answered (D44):** "outstanding" is a software convention: a request until its reply lands, a read until its data lands. In hardware a request holds its receive-queue slot only until the target pops it; the convention is conservative and implies the bound. Item 135 closed |
| W5-24 | **answered (D44):** the 24-report set as defined; the header's op and sequence-number fields come with C&C 1's request format (item 122, deferred) |
| W5-25 | **answered (D43, D44):** ordering holds per sending unit; between units there is none. A program orders them with a fence (mode 5, count only, to the tile itself at the end of the run) or by putting the packet in the run. Item 136 closed |
| W5-26 | **answered (D44):** 6.2 states that C&C 1 lands a tile's first command only when that tile should start at the next GO |

No WORKER5 question is open.
