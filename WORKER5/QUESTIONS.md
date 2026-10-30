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

## Open (each written into the spec as *(Proposed)* and listed in section 13)

| # | question | where | proposed answer | item |
|---|---|---|---|---|
| W5-02 | A write through a misaligned `PTR` is dropped but counted (D32). Does the pointer still advance? | 4.9 | yes, by the size, as for a good write: the hardware needs no gating, and every later write through it fails the same way | 133 |
| W5-03 | `ROUTER_ADDR` "auto-advances at each send as today" (D7). For which modes? | 4.11, 8.4 | mode 0 only, as v1.1 advanced only modes that use the address; modes 1–4 ignore it | 133 |
| W5-04 | Do loads from program memory and the boot ROM take one cycle, like data memory? | 4.11 | yes | 133 |
| W5-05 | How are the 32 KB memories banked? Port B needs four word-interleaved banks for a 128-bit write | 4.11, 10 | four 2K × 32 dual-port banks per memory; the macros are the implementer's choice | 127 |
| W5-07 | `SRAM_RD_DEST_ADDR` now carries a counter (D13). Since a program may change it between queued reads, does each request-queue entry carry its counter? | 4.3 | yes: entries are 43 bits (address, destination, size, counter) | 133 |
| W5-08 | D26's table marks four rows proposed: mode 0 to region 0x02, 0x07–0xFE, 0xFF or an absent peripheral is a bad packet; mode 3 at 128 bits is a bad packet; modes 1–4 ignore the address; size 11 undefined and unchecked | 3.1 | as D26 proposes | 132 |
| W5-09 | The replayer's window: D10/W3's bit and word assignments, and names for words 32 and 33. **Note:** WORKER6's draft header calls them `RUN_STATUS` and `PRESENTED`; the spec uses v1.1's original names, `REPLAYER_STATUS` and `REPLAYER_OPCODE`. One should win | 4.19.9, 8.11 | W3-REVIEW-2 §5's bits (`ERRORS` [0] bad write, [1] overrun, [2] invalid expansion; `EVENTS` [0] run done, [2]–[5] credits, [6] new expansion), v1.1's names | 123 |
| W5-10 | The replayer's map in bytes: are the part-select bits offset [22:20] (v1.1's word-address [20:18] × 4)? | 4.19.10 | yes, mechanically; WORKER3 proposed the same | 123 |
| W5-11 | How does a controller take packets from its tile's fifth port, now that it is a tile instance with a router of its own? | 4.16 | one packet a cycle, no back-pressure, as v1.1; which port of the controller's router the link enters is deferred with W2-31 | 118 |
| W5-12 | What does `WATCHDOG` count? | 6.1, 8.14 | core cycles | 133 |
| W5-14 | `LED`'s reset value | 6.1, 8.14 | 0 | 131 |
| W5-16 | How does the memory system report a request it can't serve, now that C&C's error flag is gone? | 6.7 | it sets one of its peripheral's errors, which raises C&C 2's error line and the toast pin; whether a request dropped at the root is recorded at all is item 126 | 126 |
| W5-18 | C&C 1's peripheral: word numbers (D28 gives only the order), its `EVENTS` bits, and what a load of `WATCHDOG` returns | 8.14 | words 32–45 in D28's order (the UART as TX, RX, divisor, status); `EVENTS` [2] all tiles READY, [3] watchdog, [4] RX not empty, [5] TX not full; a load returns the current count. WORKER6's draft header agrees | 131 |
| W5-19 | The exit word "beside the trace word" (D36): where? | 8.15 | 0xFFFFF9F8. WORKER6's draft agrees | 131 |
| W5-20 | The controller block's addresses are marked proposed in D11 (revised by D28 and D30) | 8.1 | as D11/D30 give them. WORKER6's draft agrees | 131 |
| W5-21 | The budget: the 32 KB + 32 KB memories aren't costed anywhere | 11, 12 | ≈ 0.15 mm² for the eight banks [estimate], about doubling the control island; per-tile logic ≈ 0.52–0.61 mm², still inside 1.5 mm²; their one-cycle access at 1 GHz to confirm | 127 |
