# WORKER2 review: the v1.2 decisions on the packet path

Scope: decisions 2-7, 12, 13 and 17, the big-SRAM list S1-S9, and pending Q5 and Q8-Q10. References: **D***n*
is line *n* of `wip/planning/DECISIONS-v1.2.md`, **S***n* is line *n* of `wip/spec/machine-spec.md`, and **PN** is
`wip/planning/PLANNING-NOTES-2026-10-27.md`. *(inferred)* marks what I derived rather than read.

## 1. Summary: the five findings that matter most

1. **W2-01 (blocker).** K = 8 cannot be a fixed guarantee for big SRAM. The write path's latency grows with
   `ts_engine_latency`, the 4-entry input FIFO, spanning writes, the `PTR`/`HIST` request FIFOs and write
   combining. A late write is silent: its counter reads done while a later read can overtake it.
2. **W2-02 (issue).** Guarantee 17 C says gather values don't pass the traffic director. In the spec they do,
   through the loopback. If they really bypass it, a counter can take two decrements in a cycle, which section 3
   rules out, and the token and injection rules change unrecorded.
3. **W2-04 (issue).** The LPDDR write stream has no v1.2 form. v1.1's mode 3 to C&C is now the augmented write,
   and nothing says what regions, modes and counters mean at endpoints 64-74. Section 15 and the planning notes
   also disagree on whether the engines are endpoints or peripherals.
4. **W2-06 (issue).** The bad-packet, alignment and range rules for the 32-bit map are not written. Read
   literally, `ERRORS` [1]'s "misaligned address" newly rejects controller-memory writes and read-port results
   that v1.1 allowed at any word.
5. **W2-10 (issue).** `DMA run done` is sticky and nothing clears it at `PULSE_DMA`, so section 17's wait can
   fall through on a stale event. Replacing the polled `ts_busy` with consume-once events also loses
   `wait_tables()`'s call-anywhere test that the tables are free.

## 2. Findings

### W2-01: K = 8 cannot guarantee that a big-SRAM write has reached its area queue

**Severity:** blocker. **Decisions:** 3 (D132-D140), 17 A (D509), S1 (D575), S7 (D582). **Spec:** 4.1 (S280-S291),
4.8 (S661), 4.9 (S667, S684-S685), 4.10 (S738), 10, item 3 (S2280), item 48.

Section 3 defines a counter at 0 as "everything it counted is in place" because each write has reached its area
queue within K cycles of the director accepting it (D139). The path from the director to the area queue is not
of fixed length:

| stage | cycles | evidence |
|---|---|---|
| director to the input FIFO | 1 | S667 |
| waiting in the 4-entry input FIFO | 0 to about 6: up to 3 writes ahead, each 2 cycles if it spans | S291, S684 |
| pointer stage: the `PTR` round trip | 2·`ts_engine_latency` + 1, so 3 at L = 1 and 9 at L = 4 | S685 |
| the packet's own spanning part 1 | +1 | S280-S283 |
| into the area queue | 1 | S286-S287 |

At the default L = 1 the unloaded path is about 5-6 cycles, which matches Tony's estimate of 6 (D575). But
`ts_engine_latency` is a placement-dependent parameter measured over 1-4 (section 10). At L = 3 or 4 the pointer
round trip alone uses up K. With three writes queued ahead in the FIFO, the worst case at L = 1 is about 12
cycles *(inferred)*. A full FIFO is a legal state, since an overflow is only an error when it actually happens
(S291).

Guarantee A makes it worse. The `HIST` count-on-arrival update and the `PTR` update each sit behind a 16-entry
request FIFO (S738), so "update complete" has no fixed bound either.

Three more terms make K depend on undecided values:

- **Write combining (S7, item 3).** A write held to combine with the next one to the same row has not entered
  its queue.
- **Port B.** Its delay `cm_latency` is 2 [built].
- **The peripheral.** Its delay `rp_latency` = 2 is still *(Proposed)* (item 48), and the replayer's placement
  is open (item 59).

The failure is silent. The counter reaches 0 and done fires. A read issued then (the next pass, a read-port
read, a gather) can reach the area before the late write and return stale data, and no error is raised. Only
meshsim's S1 check would see it (D575).

**Suggested resolution:** carry the counter id with the write and decrement at the true landing point. For big
SRAM that is where the write enters its area queue, after its `PTR`/`HIST` update. For port B and the
peripheral it is their fixed landing. The delay line then serves only port B and the peripheral, where K =
max(`cm_latency`, `rp_latency`) holds. If a constant K is kept, define it as a formula over these parameters and
the FIFO depths, and add an (H) error for a big-SRAM write that reaches its queue after its decrement.

### W2-02: guarantee 17 C contradicts the gather path, and with it section 3's one decrement per cycle

**Severity:** issue. **Decisions:** 3 (D135), 17 C (D511), Q8 (D607). **Spec:** 4.4 (S514, S528, S530), 4.5 (S540,
S575-S577), 4.3 (S466).

17 C says gather values "are made after the traffic director and don't pass it". In the spec they do pass it:

- each value "goes out as a 64-bit mode-2 packet to the tile itself" (S514);
- everything that leaves a tile is a big-SRAM read return turned into a packet (S540);
- a value's cost is counted through "the one injection point and the one eject" (S530).

The packet loops back through the router into the director, where v1.1 counts it (S528). Read-port results take
the same path (S466). So, as specified, every counted packet passes the director. K can start there for gather
values too, and section 3's "a counter takes at most one decrement per cycle" (D135) holds.

If 17 C is meant as a change, it is undeclared and has consequences the log doesn't list:

- a second decrement source on the same counters, contradicting D135;
- an arbiter at the write engine between the director and the GATHER engine, which changes the input-FIFO
  overflow analysis (S291) and W2-01's latency;
- the injection point's sources (S575-S577) and 4.4's rate change;
- S9's token accounting, which holds a loopback read's token until it leaves the loopback FIFO, no longer
  applies to gather values.

**Suggested resolution:** replace C with "gather values pass the director like any loopback packet and are
counted there". Settle Q8's open source here: the values take the counter named in the gather request's own
counter field (W2-03), or a `DMA_CONFIG` field.

### W2-03: counting still depends on the mode, and a counted reserved-mode packet hangs silently

**Severity:** issue. **Decisions:** 3 (D131), 6 (D218-D223). **Spec:** 3.1 (S141), 4.4 (S528).

Section 3 says "Counting no longer depends on the mode" (D131), but two modes are exceptions:

- **Mode 4, the gather request,** must not be counted. Its *k* values are counted, so counting the request too
  would make the program arm *n*·(*k* + 1) (S528).
- **Modes 5-7** are discarded "uncounted" (D218).

The rule is really "the counter field counts for modes 0-3", and the log should say so, including what a
mode-4 packet's counter field means. W2-02 suggests a use for it.

A consequence the log doesn't record: a reserved-mode packet sent with a non-zero counter is dropped without a
decrement, so its counter never reaches 0 and the tile hangs with no error. In v1.1 the same mistake set
bad-mode-or-size (S141). That a hang still needs no error follows 4.13's principle, but a typo in a mode field
now costs a watchdog timeout and a search through the program instead of a named error.

**Suggested resolution:** state the rule as above. Either decrement on discard when the counter field is
non-zero, which turns the mistake into an early done or an unexpected arrival, or record the silent hang as an
accepted consequence next to D225.

### W2-04: the LPDDR write stream, and what addresses mean at endpoints 64-74, are undefined

**Severity:** issue. **Decisions:** 5 (D179-D200), 6 (D229), 15 (D458-D461). **Spec:** 2.1 (S83-S96), 4.2
(S367, S381), 4.11 (S792), 6.6 (S1817-S1820, S1832), 6.7 (S1868-S1882), invariant 17. **PN** section 5.

In v1.1 a tile writes LPDDR by streaming its block to d = 64 as mode 3. Each packet's addr is its chunk offset
within the write, and C&C's port sends mode 3 to the memory system and everything else to its receive FIFO
(S792, S1832, S1868-S1882; invariant 17). v1.2 makes mode 3 the augmented write and folds the explicit-address
write into mode 0 (D229). The decisions don't say what replaces the write stream: which destination it goes to,
what region and offset its packets carry, or how the receiver tells write data from other mode-0 packets.

The map in section 5 is defined for a tile, and three things leave its meaning at a controller or engine unclear:

- **What a controller is.** PN section 5 makes C&C 1-3 "a normal tile instance with 2 MB of big SRAM", so
  regions 0x00-0x05 would mean what they mean on a tile. The spec's 6.6 still says C&C has no big SRAM, no
  engines, and a receive FIFO that ignores mode and address (S1819, S1832).
- **What an engine is.** 15 makes the engines (72 Ingress/Egress, 73 Updater/Binder/RW, 74 Tracer) their
  controllers' peripherals (D458). PN section 5 puts them on each controller's +8 link with endpoint ids of their
  own, routed "over the mesh to tile d[2:0], up its fifth link, along the column". So is a packet for the
  Updater sent to dst 73, or to dst 65 in region 0x03? If both, what does an engine do with a packet's region
  and counter fields?
- **Endpoint 65.** 2.1 and 6.7 give it to the memory system, served at 64 (S89). 15 gives it to C&C 2 (raised by
  WORKER1).

The sender-side rules change with this: 4.2's "*d* = 64 only with mode 3" and "65-125 reserved" (S367, S381), and
invariant 17's mode-3 clauses.

**Suggested resolution:** add a short "addresses at the non-tile endpoints" entry. For each of 64-66 and 72-74, say
which regions exist, what mode 0 at each region does, and whether the counter field is honoured. Pick the write
stream's form, for example dst 73, mode 0, with the offset within the write in region 0x02 *(a suggestion)*.

### W2-05: memory-system requests must now carry an address and counters, and replies need their own

**Severity:** issue. **Decisions:** 3 (D131), 6 (D230-D232), Q8 (D607). **Spec:** 4.12 (S826-S831), 6.7 (S1848-S1880).

In v1.1 the receiver's counter followed from the mode. A request named a destination kind and a tile address in
128 B units, and the answer was counted in that kind's counter, with replies in `DMA_REPLY_REMAINING`
(S1861-S1864). In v1.2 the sender picks the counter, so every answer must be told which one to use. A request
therefore has to carry:

- the destination as a 32-bit address, or a region plus an offset (this replaces the destination kind);
- the data's counter;
- for requests with value answers, the reply's counter. A write has a token reply and may have an
  acknowledgement (S1874-S1877), and the program may want them on a different counter from any data.

The same applies between tiles. D230 says replies are "counted as the receiver armed", so every exchange now
needs an agreed counter number, a convention the log doesn't record. The step per packet in the read table (+4
chunks, +4 words; S1861) also needs restating in Q10's units.

**Suggested resolution:** list these fields in 6.7's "the request's fields" (S1848), and narrow Q8 to the
gather values and these request fields (W2-22).

### W2-06: bad-packet, alignment and range rules for the new map

**Severity:** issue. **Decisions:** 5 (D200), 6 (D218, D240), 18 (D552). **Spec:** 3.1 (S141), 4.1 (S277, S305-S309),
4.2 (S366), 4.3 (S459-S466), 4.11 (S771, S791), 4.17.7 (S1066), invariant 5.

`ERRORS` [1] becomes "bad packet: bad size, misaligned address, or a region it can't reach" (D552), but nothing
lists which cases count.

- **"Bad size."** Is it size 11? 3.1 says size 11 is "undefined behaviour, not a bad packet" and that no
  hardware checks it (S141). Or is it a size a destination doesn't take? v1.1's size rules were:
  - augmented at 128 bits is bad (v1.1 mode 5, S141). This must stay for v1.2's mode 3; it is not restated.
  - command writes above 32 bits were bad. D240 leaves them open.
  - 128-bit replies were bad. Under mode 0, a 128-bit write at word 2·src also overwrites source src + 1's pair.
- **Alignment per region.** v1.1 required alignment only for the explicit-address big-SRAM write (S277, S141).
  Port B writes 1, 2 or 4 words "at any word address" (S771), and read-port results land at any word, with the
  destination advancing by the size from wherever it starts (S461). If "misaligned" applies to regions 0x00 and
  0x01, then unaligned controller-memory writes become bad packets, as do read-port results, block copies and
  DMA or send-port runs that start unaligned. v1.1 allowed all of these. If it doesn't apply, say so per region,
  and say what "aligned" means in Q10's units.
- **Range.** None of these cases has a rule:
  - an offset past the end of its memory: data memory is 32 KB in a 24-bit offset, and big SRAM is past the
    tile's size, where v1.1 read 0, dropped writes and raised no error (S305-S309);
  - a write that runs off the end of a region;
  - regions 0x06-0xFE;
  - a peripheral region with no peripheral fitted. v1.1 raised bad peripheral write (S1066); D552 implies bad
    packet now, but doesn't say so.
- **Modes 1-4 and the address.** Do they ignore their address, or is its region checked? A stream's
  `STREAM_DEST` is now a full 32-bit word, and v1.1 said "ignored for other modes" (S366).

**Suggested resolution:** one table in 3.1 that gives, for each mode and region: the sizes it takes, the
alignment it needs, what happens out of range, and the error. Keep v1.1's any-word port B unless the owner wants
otherwise (question 5).

### W2-07: Q10's consequences: one map means bytes, and big-SRAM addresses come in two units

**Severity:** question. **Decisions:** 4 (D175), 5 (D179, D190-D194, D203), 11, 13 (D402). **Spec:** 4.3 (S459),
4.4 (S518), 8.7, 8.9 (S2060).

Q10 asks whether the 24-bit offset counts bytes or 32-bit words. Two consequences bear on the answer:

- **"One map for packet addresses and the controller's own loads and stores"** (D179). The controller's half of
  that map is in bytes: the registers at 0xFFFFF800, the windows at 0xFFFFFA00 and up, the table window at
  0xFFFF2000. With word offsets, the same location has two different 32-bit addresses, one for a C pointer and
  one for a packet. Every `STREAM_DEST`, `ROUTER_ADDR`, `DMA_ADDR` and `SRAM_RD_DEST_ADDR` value taken from a C
  pointer would then need a shift, as v1.1's `CM_WORD()` did. Bytes make the single map real. 24 bits still
  cover 16 MB, above big SRAM's 8 MB maximum. The cost is two low bits that must be 0 in every packet (W2-06).
- **Big-SRAM addresses stay in 21-bit chunks elsewhere.** `PTR` (S2060), `STREAM_SRC` (Q9), `SRAM_RD_ADDR_QUEUE`
  (S459) and `DMA_RECORD_BASE_ADDRESS` (S518) all keep chunk addresses. So big SRAM has two address units
  whichever way Q10 goes: ×4 apart with bytes, the same with words. The augmented ordinal, (target chunk − base)
  / 2, is unaffected, since both of its terms stay in chunks.

**Suggested resolution:** choose bytes, and say in 4.2-4.4 that the chunk-addressed registers stay in chunks, or
convert them with Q9.

### W2-08: auto-advancing addresses: wrap or carry into the region?

**Severity:** issue. **Decisions:** 4, 7 (D248), 10 (`DMA_ADDR`), 13 (D402). **Spec:** 4.1 (S305), 4.2 (S383),
4.3 (S461).

Five addresses step by each packet's size:

- `STREAM_DEST`, per element;
- `ROUTER_ADDR`, at each send;
- `DMA_ADDR`, per packet;
- `SRAM_RD_DEST_ADDR`, per queued read;
- the memory system's deliveries.

v1.1 wrapped each within its field: 2²¹ for chunk addresses (S305, S383) and 14 bits for the read port (S461).
The decisions don't say whether a 32-bit address wraps within its 24-bit offset or carries into the region
byte. With a carry, a run past the end of data memory continues into region 0x01, program memory. With a wrap,
it stays in region 0x00.

**Suggested resolution:** the offset wraps and the region never changes, stated once in 3.1 and referenced from
each register. `SRAM_RD_DEST_ADDR` wraps within its 18 bits.

### W2-09: `ROUTER_ADDR`'s auto-advance breaks section 7's one-store claim for replies

**Severity:** issue. **Decisions:** 6 (D230), 7 (D248, D255). **Spec:** 4.11 (S788), 8.4.

v1.1's send port advanced addr only for modes 3, 6 and 9 (by the size) and mode 7 (+1). It left addr unchanged
for every other mode, replies included, whose addr is unused (S788). In v1.2 a reply is a mode-0 write at the
receiver's word 2·src, and `ROUTER_ADDR` advances "at each send as today" (D248). After one reply it points one
or two words past the sender's pair. The next reply to another tile, after a dst-only store, lands on the wrong
words. Every reply therefore needs a `ROUTER_ADDR` store too, so D255's "a change of dst alone, the common case
in replies and one-word exchanges, is still one store" doesn't hold.

The same applies to one-word exchanges to many tiles at the same address. It also affects C&C's commands: the
arguments in slots 1-15 come first, then slot 0, below them, so `ROUTER_ADDR` must be re-stored before slot 0.
"As today" also needs a meaning for modes 1-4, presumably no advance.

**Suggested resolution:** a no-advance bit in `ROUTER_HDR`'s spare [17:0], set by the reply and exchange helpers
*(a suggestion)*. Otherwise correct D255 and accept the extra store.

### W2-10: `DMA run done` is stale-prone, and event-only waits lose the "tables free" test

**Severity:** issue. **Decisions:** 16 (D477), 17 (D502, D513), 18. **Spec:** 4.2 (S342), 4.10 (S740), 4.12 (S886),
4.14 (S957). **sw:** `mesh.h:416`.

17's software wait is `EVENTS_REQUIRED` = "write counter *n* done and DMA run done", then `WFI` (D502). DMA run
done ([6]) is sticky, set on the rise of `rd_done` (S342), and cleared only by a load that consumes it under
`EVENTS_MASK`. A required bit not also in the mask is never consumed (S886). Nothing clears it at `PULSE_DMA`.

A [6] left from an earlier run therefore satisfies the next wait at once, and the program touches
`STREAM`/`GROUP` while the new run reads them, which is undefined (S740). Additive arming exists to stop exactly
this for the counters (D145-D147). [6] has no equivalent.

The second problem is the change of kind:

- `wait_tables()` polls a level (`rd_running | ts_busy`, `mesh.h:416`). It is safe to call anywhere, and the
  sort and the recipes call it at the top of a pass without knowing what ran before. That is about 15 calls in
  the sort and 22 in `RECIPES.md`.
- An event fires once. A helper that waits for [6] after the previous run's [6] was already consumed hangs. One
  that finds an unconsumed [6] falls through early.

**Suggested resolution:** `PULSE_DMA` clears [6], as an arming write clears its counter's done. Keep a level
test for helpers: `rd_running` is still in `STATUS` [0] (D534), so say that a helper may poll it, rather than
D513's "no program needs to poll it".

### W2-11: what 17's events don't cover that `ts_busy` did

**Severity:** issue. **Decisions:** 3 (D158), 17 (D498-D513). **Spec:** 4.10 (S740), 4.14 (S957), 6.2.

`ts_busy` is the OR of three things (S957):

- an engine table request in the last `ts_engine_latency` + 1 cycles;
- a `PTR` or `HIST` request queued or arriving;
- an RMW too recent to be readable.

It covers every arrival, counted or not. 17 covers only counted arrivals and the tile's own run. Two gaps:

- **Uncounted arrivals.** Section 3 sends boot loads and staging into a halted tile with counter 0 (D158).
  Staging lands through `PTR[64]`, which the loader also writes (S681). Any other counter-0 packet in modes 1-3
  also updates `PTR`. No event says these updates are done.
- **What "complete" means in A.** A must mean "readable by a controller load through the window", the third term
  of `ts_busy`, and it must include `HIST`'s count-on-arrival behind its own FIFO (W2-01). B holds by
  construction if the read-ahead never reads past the run's last entry *(inferred from S342 and the read-ahead
  rules, S419)*; say so in 4.2.

**Suggested resolution:** add a rule that no uncounted packet in modes 1-3 arrives while the program may write
`PTR` or `HIST`, and define A as above.

### W2-12: when "command landed" is set, and which writes set it

**Severity:** issue. **Decisions:** 6 (D233-D240). **Spec:** 4.12 (S803), 6.2 (S1740), 6.3 (S1771), 4.8 (S661).

D233 says the director decodes the slot-0 address and sets "command landed". If it sets it at acceptance, the
event (and the READY it triggers) comes before the slot-0 write lands through port B, `cm_latency` = 2 later
(S661). A program that sleeps on [5] alone could then load slot 0 before it is written *(inferred: the margin
depends on the register-load latency)*. 6.3's argument, that in-order delivery means the whole command has
landed when the event is set (S1771), needs the event to follow the write.

D240 leaves open whether a 64- or 128-bit write covering slot 0 counts. Two more cases need a sentence:

- the controller's own store to slot 0 (presumably no event, since only the director decodes);
- the boot marker in HALT (S1740), which should keep working unchanged.

**Suggested resolution:** set the event when the slot-0 write lands in port B. Have any director write that
covers slot 0 set it; a 128-bit write at slot 0 also writes slots 1-3, which is harmless.

### W2-13: invariants to rewrite, and invariant 11 no longer checkable as written

**Severity:** issue. **Decisions:** 3, 6 (D229-D243), 13, 17. **Spec:** 9 (S2135-S2163).

| inv. | v1.1 text depends on | v1.2 |
|---|---|---|
| 4 | `mem_idle` and `cm_idle`; "an uncounted command write sitting in port B" | counters only (D136); "uncounted packets" (W2-17) |
| 5 | v1.1's bad-packet list | W2-06's table; modes 5-7 are discarded, not bad |
| 8 | "mode-8 words" | writes into the reply block |
| 9 | an arrival at a counter at 0, checked at acceptance | detected at the decrement (D150); see W2-16 |
| 10 | mode 1 | mode 4 |
| 11 | mode 6 stays in controller memory, outside both blocks | **a convention** (D238) |
| 13 | command writes ≤ 32 bits, replies < 128 | W2-06 |
| 14 | "C&C's command writes (mode 7)" | writes to the command block |
| 17 | *d* = 64 only with mode 3; mode-3 packets to C&C | W2-04 |
| 18 | modes 2, 4, 5 into a `PTR` entry; mode 9 into a port | modes 1-3; peripheral regions |

Invariant 11 can't tell a command from a data write any more, because both are mode 0 counter 0 into region
0x00. meshsim needs a checkable form.

**Suggested resolution:** for invariant 11, writes into the command block come only from C&C 1-3. Writes into
the reply block are 32 or 64 bits at the sender's own pair. Any other mode-0 write to regions 0x00 or 0x01 stays
inside its memory and outside both blocks.

### W2-14: the 256-bit word costs more than section 2 says

**Severity:** issue. **Decisions:** 2 (D110-D122). **Spec:** 3.2 (S153), 4.7, 4.16, 11 (S2223, S2231, S2238).

D122's +0.04-0.06 mm² per tile covers "router FIFOs". By section 11's own scaling, more storage widens:

- v1.0 → v1.1 took the link-FIFO line from 0.05 to 0.08 mm² for 27.7 → 46.0 Kbit, about 1.6 µm² per bit
  including the wider selectors *(inferred)*.
- At 256 bits each entry gains 87 bits. That applies to the 17 link FIFOs' 272 entries, the fifth port's 160
  entries (in every tile since 2026-10-26, S2238), and the loopback FIFO's 48 extra entries (S2231). This is
  41.8 Kbit, about **+0.065 mm²** *(inferred)*.
- Section 2 doesn't count the padding stages (169 → 256 flops each, S153), the send holding register, the
  director's input stage, the write engine's input FIFO, or the metadata that grows with the 32-bit address.
  The metadata is in the address generator's 64-entry return queue, the read port's 64 entries (37 bits → about
  43), and the area-queue entries.

The total is still well inside the 1.5 mm² per tile, but section 11 needs re-doing line by line. Of the added
bits, 75 per entry are constant zero, about 36 Kbit of flops per tile across those 480 entries *(inferred)*. The
no-resize argument applies to wires and interfaces. Whether a FIFO must physically store reserved bits is for
the owner (question 9). Wires go to 256 per direction, 512 per neighbour; the channel check is already noted.

### W2-15: Q5 misses that writes ahead of reads break read-before-write reuse

**Severity:** question. **Decisions:** Q5 (D597), 3 (D140). **Spec:** 4.1 (S273, S295), 6.7 (S1880).

Q5 is right that S1 doesn't need it. But serving an area's writes ahead of its queued reads breaks two
read-before-write orderings that v1.1 relies on, both of which hold only because an area serves in arrival
order (S273):

- **The source buffer.** It may be overwritten once its stream run has finished, because every read has then
  been taken by its area ahead of any later write (S1880).
- **The local sort's ping-pong passes** (S295). Pass *n* + 1 writes the buffer pass *n* just read, and pass *n*'s
  last reads can still be in their area queues when the next pass's writes arrive *(inferred)*.

**Suggested resolution:** if Q5 is ever taken, a write may pass only reads to other rows. Record the dependency
in Q5's entry.

### W2-16: meshsim checking unexpected arrival at acceptance diverges from the hardware

**Severity:** nit. **Decisions:** 3 (D150-D156). **Spec:** 4.13, invariant 9.

The hardware sets unexpected arrival when the decrement happens, K cycles late, and names the last real word
(D150). meshsim "checks at acceptance and names the stray" (D155). If that check also sets meshsim's `ERRORS` [2],
the simulated register and error line differ in timing, and sometimes in cause, from the hardware's.

**Suggested resolution:** model the hardware's bit at the decrement, and report the stray as a separate
diagnostic. Otherwise record the difference in `DEVIATIONS.md`.

### W2-17: dropping `mem_idle` and `cm_idle` leaves uncounted writes with no completion

**Severity:** issue. **Decisions:** 3 (D136, D158), 18 (D538). **Spec:** 4.1 (S302), 4.12, 4.14, 6.4 (S1803).

v1.1's counters done waited for `mem_idle` and `cm_idle`, so even uncounted writes had cleared before done. In
v1.2 nothing waits for a counter-0 write. That covers commands, boot loads, staging, and any program's deliberate
uncounted write. 6.4's "a tile that only sends still waits for `mem_idle` before `done`, because its own loopback
writes count" (S1803) loses its mechanism, and `mem_idle` survives only as a diagnostic `STATUS` bit (D538).

**Suggested resolution:** a rule that a running program sends no uncounted writes except commands, or keep
`mem_idle`/`cm_idle` as a documented, pollable completion for uncounted writes (see W2-10 on "diagnostic only").

### W2-18: the delay line at the HALT edges

**Severity:** issue. **Decisions:** 3 (D142, D157). **Spec:** 4.12, 6.1. Related: WORKER1's W1-01.

"HALT flushes the delay line with the counters" (D142) doesn't define either edge:

- **RUN → HALT.** If the flush isn't in the same cycle as the counter clear, a decrement still in the line hits
  a counter just cleared to 0 and sets unexpected arrival in the halted tile. That raises its error line, which
  is W1-01's problem.
- **HALT → RUN.** Decrements of packets accepted in HALT's last K cycles come out in RUN. If the loader arms a
  counter within K cycles of RUN (its first instructions at `0x1F00`), such a packet uses up one of the *N*
  (D150's trade-off).

**Suggested resolution:** flush on the RUN → HALT edge, in the clearing cycle, and also flush on HALT → RUN.

### W2-19: does the controller access program memory?

**Severity:** question. **Decisions:** 5 (D197). **Spec:** 4.11 (S773), 4.8 (S655).

"The controller sees 0x00, 0x01 and 0xFF" (D197). In v1.1 the core is strict Harvard: a load or store to
instruction memory is a bus error (S773), and port B has the director as its only writer (S655). Does v1.2 let
the controller load and store region 0x01, or does "sees" mean only that it fetches from there?

### W2-20: S9 and a peripheral's DMA injecting at its own tile

**Severity:** issue. **Decisions:** 10 (DMA), S9 (D577). **Spec:** 4.5 (S575-S577, the token rule), 4.17.9 (S1080).

In v1.1 mesh tiles' peripherals have no output, and the sender's port goes to a non-mesh router port (S1080).
v1.2 gives every peripheral window a standard DMA. C&C 1-3 are tile instances (PN section 5) whose peripherals
have output. If a DMA injects at its tile's injection point, then:

- the injection point gains a third source with no priority defined (S575-S577);
- a DMA packet to the tile itself, or a broadcast, enters the loopback without a token, so S9's "the loopback
  cannot overflow" no longer holds.

**Suggested resolution:** say where a DMA injects. If it uses the injection point, its packets to the tile
itself or broadcast take a token, as the send port's do.

### W2-21: header-layout wording

**Severity:** nit. **Decisions:** 4 (D170), 12 (D383-D392), 13.

- 12 names [31:25] "dst", but in `STREAM_COUNT` it is *d*: a relative offset for 0-63, absolute for 64-125
  (S367). The 7-bit block that "hardware copies" (D120) is [24:18] only. *d* is translated, not copied.
- `SRAM_RD_DEST_ADDR` (13) also follows the layout at [21:18], but D392's list leaves it out.

### W2-22: pending-question housekeeping

**Severity:** nit. **Decisions:** Q8 (D607), Q list (D587-D607).

Q8 still lists the `STREAM` entries, the send port's header, "the sender" and the read port as open, but 4, 7,
10 and 13 decide them. What is left is the gather values (W2-02) and the memory-system requests, including the
reply counter (W2-05). Q8 also sits after Q12, out of order.

### W2-23: the all-zero header and the null pointer land on the same word

**Severity:** nit. **Decisions:** 5, 6 (D225).

With data memory at address 0, a C null-pointer store and the accepted all-zero header (D225) both hit data
memory word 0 silently. Reserving that word in the linker script gives meshsim one place to flag both
*(suggestion)*.

## 3. Spec sections to update (packet path)

| section | what changes |
|---|---|
| front matter, "Changes" | v1.2's changes list |
| 1 | the addresses convention (21-bit chunks → the 32-bit map, Q10); register name prefixes |
| 2, 2.1 | the endpoint ids 65, 66, 72-74 and what 65 is (W2-04) |
| 3.1 | the 256-bit word and its layout; the mode table; counting by field (W2-03); the bad-packet table (W2-06); address wrap (W2-08) |
| 3.2 | padding stages at 256 flops |
| 4.1 | `mem_idle`'s role (W2-17); "mode 3" → mode 0 to region 0x02; the clients' list; ordering across phases (W2-01, W2-15) |
| 4.2 | the `STREAM` layout (*d* to `STREAM_COUNT`, gap to `STREAM_SRC`); modes in "Uses"; the sender-side error list; the destination address step; the visit's addr; `PULSE_DMA`; B (W2-11) |
| 4.3 | `SRAM_RD_DEST_ADDR` (13); mode 0 to the tile itself; the counter field; data memory only |
| 4.4 | mode 1 → 4; values mode 2 → 1; the values' counter; the path through the director (W2-02) |
| 4.5 | the read queue's fields (a 32-bit destination, the counter); the injection fields; the DMA as a source (W2-20) |
| 4.6 | routing to 64-74 (PN section 5) |
| 4.7, 4.16 | FIFOs of 256 bits; the cost |
| 4.8 | the director by mode and region; the delay line; counting in HALT (D157); "command landed" decode |
| 4.9 | the mode numbers; the explicit-address write as mode 0; count-on-arrival for modes 1-3 only; counting |
| 4.10 | the access rule without `ts_busy` (W2-10, W2-11); the window at 0xFFFF2000 |
| 4.11 | the 32 KB + 32 KB regions; packed words withdrawn; `ROUTER_HDR`/`ROUTER_ADDR` and their advance (W2-09); the controller's reach (W2-19) |
| 4.12 | commands and replies as mode 0 (W2-12); three general counters and K; the counting policy; counters done without `mem_idle` |
| 4.13 | `ERRORS` [1] and [2]; `ts_busy` withdrawn |
| 4.14 | commit ordering under K; counters-ready withdrawn |
| 4.15 | "mode 4" → mode 2 |
| 4.17.5, 4.19.10 | the peripheral write as a region; the replayer's map as offsets in region 0x03 |
| 6.2 | boot and the marker as mode-0 writes with counter 0; staging as mode 1 |
| 6.3, 6.5 | command and copy recipes in mode 0 with counter fields |
| 6.4 | "a tile that only sends still waits for `mem_idle`" (W2-17) |
| 6.6, 6.7, 7 | C&C's port and receive path; the request fields; write streams; replies (W2-04, W2-05) |
| 8.1, 8.4, 8.7-8.9 | the send port, the counters, the read port, the table window and `STREAM` layouts |
| 9 | invariants 1, 4, 5, 8-11, 13, 14, 17, 18 (W2-13) |
| 10 | the link word; K (new); the controller memories; the packed word (gone); the counters |
| 11 | the link-FIFO, fifth-port and loopback lines; metadata widths (W2-14) |
| 13 | items 3 (write combining vs K), 48 (`rp_latency` now bounds K), 74, 82, 116 |

## 4. Questions for the owner

1. Should a big-SRAM write decrement at its area queue (no K for big SRAM), or should K be a formula with an (H)
   error for a late write? (W2-01)
2. Do gather values pass the director as 4.4 says, or bypass it as 17 C says? Which counter do they use? (W2-02)
3. At each of 64-66 and 72-74: which regions exist, and what is the LPDDR write stream's form? Are the engines
   endpoints, peripherals, or both? (W2-04)
4. Q10: bytes or words? Do the chunk-addressed registers stay in chunks? (W2-07)
5. Does controller memory keep v1.1's any-word writes, or require alignment? (W2-06)
6. Should `ROUTER_ADDR` get a no-advance option? (W2-09)
7. Should `PULSE_DMA` clear `DMA run done`? (W2-10)
8. Should a reserved-mode packet with a non-zero counter decrement, to avoid a silent hang? (W2-03)
9. May an implementation leave the 75 reserved bits out of FIFO storage until they are used? (W2-14)
10. Does the controller load and store program memory? (W2-19)

## 5. For other workers

- **WORKER1:** W2-18 gives the RUN → HALT flush timing that W1-01 asks about. Flush in the clearing cycle, or a
  late decrement raises the line.
- **WORKER1:** reserved modes are now discarded with no error, and a counted one hangs (W2-03). That changes what
  the watchdog, rather than `ERRORS`, must catch.
- **WORKER1:** Q3 (what stops a peripheral on a trap) should cover a DMA's packets in flight to its own tile and
  their tokens (W2-20).
- **WORKER3:** `wait_tables()` has about 15 calls in the sort, 22 in `RECIPES.md`, more in meshsim's recipe tests,
  and text in `CONVENTIONS.md` and `REGISTERS.md`. W2-10's fix keeps a polled `rd_running`, so `STATUS` is not
  "diagnostic only".
- **WORKER3:** `mesh.h`'s reply and exchange helpers must re-store `ROUTER_ADDR`, or use a no-advance bit (W2-09).
  `CM_WORD()` goes.
- **WORKER3:** "the controller sees 0x01" against 4.11's bus errors on instruction-memory access (W2-19). Data
  memory word 0 is also the null pointer (W2-23).
