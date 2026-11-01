# WORKER2 review, round 2: the resolutions on the packet path

Scope: the answers to W2-01 to W2-23 and the new design in my area. References: **D***n* is line *n* of
`wip/planning/DECISIONS-v1.2.md` as of `b8bd504` (1,096 lines); **S***n* is line *n* of `wip/spec/machine-spec.md`;
**PN** is the 2026-10-27 planning notes. *(inferred)* marks what I derived rather than read.

## 1. Summary: the five points that matter most

1. **W2-28 (blocker).** Tag windows let several tiles write LPDDR at once. Their streams all funnel into C&C 2's
   single fifth link, where no flow control stops a FIFO overflowing. v1.1's one-token-at-a-time rule existed to
   prevent exactly this, and it is gone.
2. **W2-24 (issue).** `STREAM_COUNT`'s new 16-bit count holds at most 65,535. The sort writes counts up to
   65,536, the keys per tile, and the field truncates that to 0, a silent empty stream. The spare bit [16] fixes
   it.
3. **W2-25 (issue, good news).** Natural alignment removes the only thing that made the big-SRAM write path
   variable. The path is now fixed at about 2·`ts_engine_latency` + 3 cycles, so K can be a formula and W2-01
   can close as a guarantee rather than a measurement.
4. **W2-27 (issue).** "When the marker lands the whole image is in place" (22) holds only for images in
   controller memory. Big-SRAM images, now loaded by address into region 0x03, take the slower write path, so
   the marker can land first. The same applies to staging before a command.
5. **W2-31 (issue).** A sending peripheral's own router port is underspecified:
   - the src its packets carry, which they share with their controller;
   - routing inside the controllers' routers, which are "normal tile instances" but not on the torus;
   - a DMA sending to its own unit through an eject FIFO with no flow control.

## 2. Status of W2-01 to W2-23

| finding | status | evidence |
|---|---|---|
| W2-01 K for big SRAM | **partly closed** | S1 is restated (D1064), and spanning is gone (D954-D960), which makes the path fixed. But S1 still rests on a measurement, quotes the old spanning figure ("about 12 cycles"), and the table-window case has no margin. See W2-25 and W2-26. |
| W2-02 gather values | **closed** | 17 C corrected (D617). Mode 4's counter field passes to its values (D251). |
| W2-03 counting by mode | **closed** | D246-D250. The silent hang is accepted. Counter values 5-7 are a new loose end (W2-35). |
| W2-04 endpoints, LPDDR stream | **closed in principle** | 25 (D917-D944). The new path raises W2-28 to W2-30. |
| W2-05 request fields | **open** | D937-D942 leave counters open. The "answers uncounted" option conflicts with 17 (W2-29). |
| W2-06 bad packets | **mostly closed** | 26's table (D981-D988). Misaligned *engine* accesses and the out-of-range rule are new gaps (W2-32, W2-33). |
| W2-07 bytes or words | **closed** | Bytes (D225-D231); the units rule (D962-D965). Wording nits in W2-36. |
| W2-08 advancing past the end | **closed** | Undefined; meshsim errors (D975-D978). |
| W2-09 one-store reply | **closed** | The no-increment bit (D292-D297). Its position is still "(proposed: [0])" though the layout uses it (W2-36). |
| W2-10 stale DMA run done | **closed** | `PULSE_DMA` clears it (D631), and helpers may poll `rd_running` (D635). The text names the bit [6] instead of [7] (W2-36). |
| W2-11 uncounted arrivals | **partly closed** | Images go by address and modes 1-3 are always counted (D619-D624). 25's tag-window writes are uncounted writes from running tiles (W2-29). |
| W2-12 command landed | **closed** | Set when the slot-0 write lands in port B, director writes only (D272-D277). |
| W2-13 invariants | **closed** | 27 (D990-D1007), with invariant 11's checkable form. Invariant 18 must add the DMA as a client (W2-31). |
| W2-14 cost | **closed** | D126-D135. |
| W2-15 Q5 | **closed** | Strict arrival order (D1086). |
| W2-16 meshsim timing | **closed** | D167-D171. |
| W2-17 uncounted completion | **partly closed** | D625-D628. Tag windows (W2-29) and big-SRAM staging before a command (W2-27) still leave uncounted writes without a completion signal. |
| W2-18 delay line at HALT | **closed (moot)** | D153; no HALT state (23). |
| W2-19 program memory | **closed** | Load-only (D215-D218). |
| W2-20 DMA injection | **partly closed** | Its own router port (D423-D428). S9 holds, but src, routing and flow control are open (W2-31). |
| W2-21, W2-22 | **closed** | 12 (D480-D486), Q8 (D1089). |
| W2-23 word 0 | **closed** | D1000-D1002. |

## 3. New findings

### W2-24: a 16-bit stream count can't hold the sort's own streams

**Severity:** issue. **Decisions:** 4 (D179-D186), 30 (D1051-D1054). **Spec:** 4.2, item 25 (S2311). **Code:**
`sort/program/sw/local.c:33, 63, 64, 79`.

30 narrows the count to 16 bits, a maximum of 65,535 elements. Item 25 records that the sort writes counts up
to "the keys per tile, 65,536 at 64 × 65,536". The four streams in `local.c` above are exactly those:

- the gather stream;
- the two bucket passes;
- the step-1 input stream.

`STREAM_COUNT_ENTRY` masks the count, so 65,536 becomes 0, a count-0 padding stream (4.2). It sends nothing, no
error is raised, and the tile waits forever for arrivals that never come. 30's rationale is that "a stream that
long already runs about 64K cycles, longer than any one transfer should hold the system". That fits mesh
transfers, but these are local passes. They are meant to run the whole tile's data in one go, and they hold
nothing else up.

**Suggested resolution:** use bit [16], which 30 left spare, for a 17-bit count (up to 131,071), as item 25
proposed. Otherwise the sort splits every whole-tile pass into two streams, and `STREAM_COUNT_ENTRY` gets a
check.

### W2-25: with spanning gone, the write path is fixed, so K can be a formula

**Severity:** issue (a decision to take; it closes W2-01). **Decisions:** 3 (D140-D145), 26 (D954-D960),
S1 (D1064). **Spec:** 4.1 (S287-S291), 4.9 (S684-S685), 4.13 (S927). **meshsim:** `WriteEngine.java`.

In v1.1 the write engine stalled for exactly two reasons (`WriteEngine.runCycle`):

- **a spanning write's second part;**
- **an area refusing a write.** That can't happen: a write is accepted when the area queue's count is below its
  depth, and the count is at most depth − 1 after every cycle (S287-S290). This is *(inferred)*, and it
  contradicts S291's "a sustained overload of one area fills the write engine's input FIFO".

26 removes spanning (D958), so the write engine never stalls. The input FIFO then holds at most one packet, and
every big-SRAM write, explicit-address ones included (they pass the pointer stage in order), reaches its area
queue a fixed time after the director accepts it:

> 1 (into the write engine) + 2·`ts_engine_latency` + 1 (the `PTR` round trip, S685) + 1 (into the area queue)
> = **2·L + 3**

That is 5 cycles at the default L = 1 and 11 at L = 4 *(inferred)*.

Guarantee A behaves the same way. The `PTR` and `HIST` requests are issued when the packet enters the pointer
stage, and the request FIFOs never back up, because every engine issues at most one request per cycle (S927's
(H) note). An update is readable through the window about L + 4 cycles after acceptance *(inferred)*.

What follows:

- **S1's "about 12 cycles at L = 1" is stale.** It included spanning.
- **K can be stated rather than measured:** K ≥ max(2·L + 3, L + 4, `cm_latency`, `rp_latency`). K = 8 satisfies
  it up to L = 2. Write combining (S7) is the only remaining variable term; it must either fit under K or be
  dropped.
- **Write-engine input overflow becomes a hardware-fault detector (H)** rather than a program error (S927).
- **S1 becomes a guarantee.** meshsim's measurement turns into a check of the formula.

### W2-26: S1's thinnest case is a store, and the "reaction" isn't enforced

**Severity:** issue. **Decisions:** 17 A (D615, D629), S1 (D1064). **Spec:** 4.10 (S732, S740, S2067), 4.12 (S842).

S1's thinnest case is "the controller reading `PTR` or `HIST` through the table window right after done", but
`PTR` can't be loaded at all; a load is a bus error (S2067). The real hazard is a **store**. A program sets
`PTR[p]` or clears `HIST` for the next phase right after done. If the last increment for that entry is still in
flight, it either wins outright when the two meet (S732) or lands after the store and overwrites it. The new
pointer is lost silently. No read path adds margin here, because the controller's window access takes one cycle.

S1 also counts "the controller's reaction (waking from `WFI`, loading `EVENTS`, at least 4 cycles)", but nothing
enforces it:

- the load of `EVENTS` is a helper convention, not a hardware step;
- a program may poll a counter instead (S842), with a 2-cycle register load;
- a program woken through `EVENTS_REQUIRED` may touch the window in the very next cycle.

**Suggested resolution:** make A a hardware guarantee through K alone, with K ≥ L + 4 from W2-25, and drop the
reaction from the table-window case. Alternatively, interlock a window access behind a pending request for the
same table, which is `ts_busy` as a stall instead of a polled bit.

### W2-27: the marker doesn't prove that big-SRAM images or staging have landed

**Severity:** issue. **Decisions:** 6 (D272-D277), 17 (D619-D628), 22 (D865-D872), 23 (D889). **Spec:** 4.8 (S661),
4.9.

22 argues that "packets from one source to one destination arrive in order, so when the marker lands the whole
image is in place". That holds for images in controller memory. The marker and the image share port B's fixed
`cm_latency` path, and "command landed" is set when the slot-0 write lands there (D272).

Since 17, big-SRAM images and staging go to region 0x03 by address (D619-D623). Those writes take the write
engine's path, 2·L + 3 cycles (W2-25), against port B's 2. So the marker can land, and the unit can start
itself, up to 2·L + 1 cycles (3 to 9) before the image's last big-SRAM write reaches its queue *(inferred)*.
In-order arrival orders the packets at the director, not their landing in two different memories.

The same applies to staging into a running unit, which ends with a command (D627, D889): the command lands in
port B ahead of the staged big-SRAM writes. The boot ROM's reaction (wake, read slot 0, jump), plus the loaded
program's lead-in before its first big-SRAM read, almost certainly covers the gap *(inferred)*. But that is S1's
margin argument, not the in-order one, and it should be stated as such.

**Suggested resolution:** restrict 22's sentence to controller memory. Cover big-SRAM images and staging by S1's
margin with K's formula (W2-25), or require a unit's first big-SRAM read to wait K cycles after the marker.

### W2-28: concurrent tag windows fan in with no flow control

**Severity:** blocker. **Decisions:** 25 (D928-D936). **Spec:** 3.2, 4.7, 4.16 (S982), 6.7 (S1874-S1882).

v1.1 handed out **one write token at a time across the chip** so that "write streams never fan in at the
'to C&C' output" (S982). The mesh has no flow control, and a router FIFO that fills loses packets (3.2, 4.7).
25 replaces the token with a pool of tags, each owning a window, and has the file system "direct the tiles to
write the window". With more than one tag out, several tiles stream at once to dst 65.

Every one of those packets reaches C&C 2 through one path: tile 1's fifth-port output, one packet per cycle
(PN section 5; 4.16). Two tiles streaming at full rate fill its 16-entry feeder FIFOs within about 16 cycles,
and the fifth link overflows *(inferred)*. Nothing in 25 paces the tiles or limits windows in flight.

**Suggested resolution:** either the file system grants **one window in flight at a time** (v1.1's rule, the
tag pool then serving only to pipeline the RW engine), or the writers pace themselves to a stated share of one
packet per cycle. Say which in 6.7, and add it to invariant 17.

### W2-29: tag-window writes contradict "no uncounted writes but commands"

**Severity:** issue. **Decisions:** 17 (D625-D628), 25 (D932, D940-D942), 27 (D998).

17 rules that "a running program sends no uncounted writes except commands", because completion is the counters
alone (D625). 25 has running tiles stream their LPDDR writes as "plain mode-0 packets to dst 65". These must be
uncounted. C&C 2 can't arm one of its four counters for every tile's window, and a counted packet at a counter
of 0 raises unexpected arrival, a C&C 2 error, and so a chip-wide reset (21). Completion comes back the other way,
through the RW engine and the file system, not through the writer's counters.

25 also leaves open that answers might "arrive uncounted under a convention" (D940). That breaks the same rule:
an uncounted answer has no completion signal, since `mem_idle` and `cm_idle` are gone (18).

**Suggested resolution:** add tag-window writes to 17's exception list with "counter 0, completion by the tag"
(and to meshsim's uncounted-traffic check, D998). Close D940 as "answers are counted".

### W2-30: the tag-window protocol is not yet specified

**Severity:** question. **Decisions:** 25 (D928-D942). **Spec:** 3.3 (targets), 6.7.

To write a block, a tile needs answers to these, and the decisions don't give them yet:

- **Learning the window.** How does a tile get the window's address? Presumably as a reply to its request to
  C&C 2, but on which counter, and is it a 32-bit address in C&C 2's region 0x04 or later?
- **Length and completion.** What does the RW engine count to decide a window is complete? The file system's
  request to the engine carries "the LPDDR destination and the tag", but no length. A block shorter than the
  window never completes unless the length is given. A block covered twice, or with a gap, hangs or completes
  wrongly, as v1.1's "each chunk once" rule covered (S1874-S1882).
- **Larger writes.** A block bigger than 4 KB needs several windows. Are they granted in sequence?
- **The writer's acknowledgement.** A tile must know its data is in LPDDR before it hands the region to another
  tile (v1.1's write acknowledgement, S1877). 25 returns the tag to the file system, not to the writer.
- **The read direction.** LPDDR to tiles, which the decisions don't cover yet. A read request now goes to C&C 2's
  firmware, which drives the RW engine, whose DMA sends through C&C 2's own port. That puts software in every
  read's path, where v1.1's memory system was hardware. The latency and throughput that implies need a
  statement.
- **3.3's target table.** It names target 3 as "the Updater/Binder/RW engine (endpoint 73)". Endpoint 73 is
  gone, so target 3 needs a new meaning or none.

### W2-31: a sending peripheral's own router port: src, routing and flow control

**Severity:** issue. **Decisions:** 10 (D423-D428), 25 (D921-D926). **Spec:** 4.5, 4.6 (S607), 4.7, 4.16; invariants
17 and 18. **PN** section 5.

- **src.** A link input carries the src its sender stamped. 72-74 are gone, so a DMA's packets must carry their
  unit's id (64-66) *(inferred)*. The controller and its engine then share the receiver's `PTR[src]` and the
  reply pair 2·src. Invariant 18 (one client per order-placed destination) must list the DMA as a client.
  6.7's one reply-producing request per responder must treat the controller and its engine as one responder.
- **Routing.** 25 makes C&C 1-3 "normal tile instances", but their routers are not on the torus. A normal route
  block computes dimension-order hops from `me` and src over the 8 × 8 grid. PN section 5 routes everything for
  64-66 through tile *d*[2:0]'s fifth link, and 4.6 treats only src 64 as tile 0 (S607). The spec needs:
  - the controllers' routing (everything not for `me` leaves on the mesh link);
  - routing for src 65 and 66 (as tiles 1 and 2);
  - where on each controller's router the DMA port attaches.
- **Flow control.** A DMA packet to its own unit (Ingress data into C&C 1's big SRAM, say) ejects through its
  input's 16-entry eject FIFO. That FIFO round-robins with arrivals from the mesh and has no back-pressure. A DMA
  sending one packet per cycle while tile traffic arrives overflows it (router overflow). Unlike the mesh, a
  local DMA can simply wait, so its router input should give it ready/valid back-pressure.
- **Which tiles qualify.** "Only a tile with a free link can have a sending peripheral; C&C 1-3 have them"
  (D427). Mesh tiles 3-63 have a fifth port that is built and unconnected (4.16), which reads as a free link.
  Say whether mesh tiles may use it.
- **Cost.** Section 11 should count each controller's extra route block and the feeder FIFOs on its outputs, as
  it does for the fifth port.

### W2-32: what happens to a misaligned engine access

**Severity:** issue. **Decisions:** 26 (D954-D961), 18 (D671-D676). **Spec:** 4.2, 4.3 (S462), 4.4, 4.9.

26 makes every 64- and 128-bit big-SRAM access aligned and checked, and a misaligned one "raises the alignment
error". For a packet, the director drops it uncounted, as v1.1 did. For the engines, the access and its count
are undefined:

- **The address generator** with a misaligned `STREAM_SRC`. Does it skip the element, read rounded down, or send
  the packet anyway? The receiver's counter depends on the answer.
- **The GATHER engine** with an odd `DMA_RECORD_BASE_ADDRESS`. The same question applies to its values.
- **A write through a misaligned `PTR`.** It is found only after the pointer read, when the director has
  already scheduled the decrement (3). Is the write dropped but counted, so done fires over missing data, or
  written rounded?
- **The read port.** v1.1 drops a misaligned read and sets read-port error [7] (S462). Does it now set [1] as
  well, or instead?

**Suggested resolution:** one rule in 4.1: a misaligned engine access is dropped, still counted where a packet
was promised, and sets [1]. The read port keeps [7].

### W2-33: "the high offset bits are ignored" doesn't fit big SRAM's sizes or its engines

**Severity:** issue. **Decisions:** 26 (D967-D972). **Spec:** 4.1 (S305-S309), 4.2, 4.4, 10.

"An offset past the end of a memory is undefined: the high offset bits are ignored, so the access aliases into
the memory" (D969). That describes a power-of-two memory. Big SRAM is "a multiple of 256 KB": 4.25 MB is
tentative for the mesh tiles (S8), and the last area may be partial (4.1). An offset between the end of such a
memory and the next power of two has nothing to alias to.

v1.1 defined both cases (S305-S309):

- chunk addresses wrap mod 2²¹;
- a chunk past the end reads 0, and writes to it are dropped.

The engines generate chunk addresses themselves: `STREAM_SRC` plus *e*·size, `PTR` advancing, the GATHER engine's
base + 2*k*·ordinal. 26 doesn't say whether v1.1's rule survives for them.

**Suggested resolution:** keep v1.1's rule for big SRAM (reads 0, writes dropped, no error), and say that "the
high bits are ignored" applies to the 32 KB controller memories only. meshsim errors on both, as decided.

### W2-34: the alignment rule for pointers and runs, stated precisely

**Severity:** nit. **Decisions:** 26 (D955-D961), 10 (DMA remainders). **Spec:** 4.9 (S694), 4.17.9 (S1092).

- "A `PTR` must be aligned to the size of the packets landing through it" (D955). For the augmented write (mode
  3) the write is the 64-bit record, so its `PTR` must be even even when its packets are 32-bit, as in step 1
  (`local.c:79`). The rule should say "the size of the writes".
- "Mixing 32- and 64-bit packets through one `PTR` breaks it" (D960) is too strong. Descending sizes from an
  aligned start keep every packet aligned. The DMA's remainder pattern (128s, then 64, then 32, S1092) is that
  case. What breaks is the *next* transfer through the same pointer or `DMA_ADDR` after a remainder: a 7-word DMA
  from a row boundary leaves the address at chunk 3. Rule: a run that continues where another stopped needs the
  earlier run's length to be a multiple of its own size.
- 4.9's "whether the buffer starts at an even or an odd chunk" (S694) goes.

### W2-35: counter values 5-7

**Severity:** nit. **Decisions:** 30 (D1045-D1047).

A counter field of 5-7 is "undefined in hardware". On a mode 0-3 packet it might decrement nothing, which hangs,
or alias onto a counter, which corrupts silently. Defining 5-7 as uncounted is free and makes the outcome
predictable.

### W2-36: stale text after the round

**Severity:** nit.

| where | text | fix |
|---|---|---|
| D17 | "a 2-bit counter" | 3-bit (30) |
| D179 | "96 bits, 88 used" | 87 used |
| D209 | the renumbering note splits 5's table before the 0xFF row | move the note below the table |
| D227 | "keeps its 32-bit chunk addresses" | "its 21-bit addresses of 32-bit chunks" |
| D292 | "*(proposed: [0])*" | the layout at D290 already uses [0] |
| D359, D671 | "sticky until HALT" | until the reset (23) |
| D495 | "units per Q10" | bytes (5) |
| D591, D593 | "Gone from v1.1: the fourth counter's done"; "HALT → RUN" | a fourth counter exists (30); the reset |
| D632 | "so [6] always refers" | [7] |
| D964 | "`GROUP` sources"; "the augmented ordinal" in chunks | `GROUP` holds no address; the ordinal counts records |
| D1064 | "about 12 cycles at `ts_engine_latency` 1" | 2·L + 3 without spanning (W2-25) |

## 4. Spec sections to update, as changed by these answers

| section | what changes |
|---|---|
| 2, 2.1 | endpoint 65 is C&C 2; 72-74 go; C&C 1-3 are tile instances (25) |
| 3.1 | natural alignment for every write; the bad-packet table (26); a 3-bit counter counted for modes 0-3; counter values 5-7 (W2-35) |
| 3.2, 3.3 | the fan-in rule for LPDDR writes (W2-28); request target 3 (W2-30) |
| 4.1 | spanning removed; every access aligned; arrival order (Q5); the out-of-range rule (W2-33); misaligned engine accesses (W2-32) |
| 4.2 | sources aligned to the read size; the count's width (W2-24); the chunks/bytes split between `STREAM_SRC` and `STREAM_DEST`; *d* rules (25) |
| 4.3 | destination in bytes, aligned to the size; the read-port error versus [1] |
| 4.4 | the values' counter from the request (6); the base aligned to a record |
| 4.5 | part 1 of a spanning read goes; injection keeps two sources (10) |
| 4.6, 4.7, 4.16 | routing to and from 64-66; the controllers' routers; DMA ports (W2-31) |
| 4.8 | the counter field; "command landed" at port B; K and its formula (W2-25) |
| 4.9 | the spanning write goes; augmented needs an even `PTR`; input overflow becomes (H) |
| 4.10 | A as a hardware guarantee; window stores after done (W2-26) |
| 4.11 | the map with the boot ROM; program memory load-only; the no-increment bit; out-of-bounds aliasing in controller memory |
| 4.12 | four counters; replies at `REPLY` + 8·id; commands |
| 4.13 | [1] "bad packet or misaligned access"; [4]'s kind; errors sticky until the reset |
| 4.14 | commit ordering as S1 with K's formula |
| 4.17, 4.19.10 | peripheral windows, landing within K, DMAs on their own ports; the replayer's map in bytes (addr[20:18] becomes offset bits [22:20]), with 128-bit writes aligned everywhere |
| 6.1, 6.2 | reset and the boot ROM; images by address; the marker's guarantee limited to controller memory (W2-27); `PTR[64]` no longer special |
| 6.6, 6.7, 7 | C&C 2 and its RW engine; tags and windows; request fields and counters (W2-28 to W2-30) |
| 8 | the register block (11, 30); `STREAM_COUNT`; `SRAM_RD_DEST_ADDR` |
| 9 | invariants per 27; 18 gains the DMA client; 17 gains the window rules |
| 10 | K as a formula; counter field 3 bits; `STREAM_COUNT` width; the boot ROM |
| 11 | the link word's cost (2); the controllers' DMA ports |
| 13 | items 3 (write combining against K), 25 (the count), 74, 82 |

## 5. Questions for the owner

1. **Tag windows:** one window in flight chip-wide (v1.1's token rule), or a pacing rule for the writers? (W2-28)
2. **The stream count:** use the spare bit [16] for 17 bits, so the sort's 65,536-element passes fit? (W2-24)
3. **K:** state it as max(2·L + 3, L + 4, `cm_latency`, `rp_latency`) now that the write path is fixed, with
   write combining either under K or dropped? (W2-25)
4. **The table window after done:** guarantee it by K alone, or interlock window accesses against pending
   `PTR`/`HIST` requests? (W2-26)
5. **Big-SRAM images and staging:** rely on S1's margin after the marker, or require a wait of K? (W2-27)
6. **Big SRAM out of range:** keep v1.1's reads-0, writes-dropped rule, with aliasing for controller memory only?
   (W2-33)
7. **Misaligned engine accesses:** dropped and still counted, with [1] set? (W2-32)
8. **The DMA's port:** confirm src is the unit's id, and that the port has back-pressure. (W2-31)
9. **The window protocol:** how a tile learns its window, the length the engine counts to, the writer's
   acknowledgement, and the read direction. (W2-30)
10. **Answers to requests:** close D940 as "always counted", given 17's rule? (W2-29)
