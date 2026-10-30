# Worker 2, round 2: check the resolutions of your review

Your first review (`WORKER2/REVIEW.md`, findings W2-01 to W2-23) was gone through with the owner and Tony.
The answers are in `wip/planning/DECISIONS-v1.2.md`. Several changed the design beyond what a finding asked
for: **natural alignment for every access** (spanning goes), **out of bounds unchecked in hardware**,
**byte offsets**, the **engines as their controllers' peripherals** with LPDDR written through **tag
windows**, and a stated split between **what the hardware checks and what meshsim checks**. Meanwhile
WORKER1's round removed the HALT state: a **global reset** is the only stop, and every unit **boots from a
ROM** (sections 21-23). Check that the answers hold together.

REPOSITORY AND RULES: as in `WORKER2/PROMPT.md`. Pull first. This prompt is the go-ahead to write and push
`WORKER2/REVIEW-2.md` and to keep using `WORKER2/NOTES.md`; change nothing else.

READ
- The log's "Where the design stands", then every section your findings touched: 2, 3, 5, 6, 7, 10, 12,
  13, 17, 25, 26, 27, S1 and the pending questions; and 19-24 for the reset and boot changes. Superseded
  points are marked in place. **The regions were renumbered:** 0x02 is the boot ROM, 0x03 big SRAM,
  0x04-0x06 the peripherals.
- Your own `REVIEW.md` and `NOTES.md`.

HOW YOUR FINDINGS WERE ANSWERED

| finding | answered by |
|---|---|
| W2-01 K for big SRAM | S1 restated: what matters is that a read issued in response to done arrives after the write; the margin is K plus the controller's reaction plus the read's own path; K = 8 kept; meshsim measures it |
| W2-02 gather values | 17 C corrected: they pass the director as loopback packets; 6: a gather request is never counted, its counter field names its values' counter |
| W2-03 counting by mode | 6: the counter field counts for modes 0-3 only; reserved modes are discarded with no decrement (a hang, accepted) |
| W2-04 endpoints 64-74, LPDDR stream | 25: controllers are tile instances; engines are their peripherals; 72-74 go; LPDDR by tag windows |
| W2-05 memory-system requests | 25: full 32-bit destination, counters if needed; whether they carry counters is open |
| W2-06 bad packets | 26: natural alignment everywhere, checked; out of bounds undefined, meshsim errors; region checks proposed |
| W2-07 bytes or words | 5: bytes; chunk registers stay in chunks; 26: the units rule |
| W2-08 advancing past the end | 26: undefined, a software bug, meshsim errors |
| W2-09 one-store reply | 7: a no-increment bit in `ROUTER_HDR` [0] |
| W2-10 stale DMA run done | 17: `PULSE_DMA` clears it; helpers may poll `STATUS.rd_running` |
| W2-11 uncounted arrivals and tables | 17: uncounted traffic is mode 0 only; images go to big SRAM by address, not through `PTR[64]` |
| W2-12 command landed | 6: set when the slot-0 write lands in port B, only for writes through the director |
| W2-13 invariants | 27: meshsim checks them; invariant 11's checkable form; the rest rewritten with the spec |
| W2-14 the 256-bit cost | 2: cost revised; all 256 bits stored for now; trim later, mainly for power |
| W2-15 Q5 | closed: strict arrival order |
| W2-16 meshsim's timing | 3: meshsim models the hardware exactly, with a diagnostic at acceptance |
| W2-17 uncounted completion | 17: no uncounted writes but commands; `mem_idle`/`cm_idle` meshsim only (18) |
| W2-18 delay line at HALT | moot (no HALT); the reset clears it (3) |
| W2-19 program memory | 5: load-only for the controller |
| W2-20 DMA injection | 10: a sending peripheral has its own router port |
| W2-21, W2-22 | wording fixed (12, Q8) |
| W2-23 word 0 | 27: reserved; meshsim flags writes to it |

WHAT TO DO
1. **Check each answer.** Mark W2-01 to W2-23 closed, partly closed or open, with a sentence of evidence.
2. **Review the new design in your area** as in round 1. Questions worth working through:
   - **Natural alignment inside big SRAM:** every place v1.1 relied on unaligned or spanning access (the
     sort's `PTR` layouts, bucket writes of mixed sizes, `GROUP` sources, record buffers, the augmented
     ordinal, boot images), and what the software must now guarantee. Does anything in the sort need an
     unaligned access?
   - **Bytes in packets, chunks in the engines:** every conversion point, and whether any register is left
     ambiguous about its unit.
   - **The tag-window LPDDR path:** how tiles learn a window's address, ordering and completion within a
     window, how the window's count interacts with the tiles' counters, and the read direction (LPDDR to
     tiles), which the decisions don't cover yet.
   - **A sending peripheral on its own router port:** which ports are free on C&C 1-3 and on mesh tiles,
     what src its packets carry, and routing to and from it.
   - **The no-increment bit:** every use of `ROUTER_ADDR` it affects, and commands, which write slot 0 last.
   - **S1's margin:** whether "K plus the controller's reaction plus the read's path" really covers every
     consumer, including the table-window case and any engine started without the controller.
   - **Boot images by address** through region 0x03, and the boot ROM's view of big SRAM.

DELIVERABLE: `WORKER2/REVIEW-2.md`
1. A summary: the five points that matter most, one line each.
2. A status table for W2-01 to W2-23.
3. New findings, numbered on from **W2-24**, in round 1's format.
4. Spec sections to update, as changed by these answers.
5. Questions for the owner.
