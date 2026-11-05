# WORKER24 notes: the review for figure 9.1 (the Tracer)

Read: `wip/planning/DECISIONS-v1.3.md` sections 1-25 (at 71332f8, then the owner's answers up to 352df7d),
`DECISIONS-v1.26.md` items 1-12, spec 4.17 (peripherals; 4.17.9 the DMA), and the figure set's style
(`wip/diagrams/src/common.py`, `rtl.py`, figures 1.3 and 3.1).

Each finding: what the decisions say, the gap or contradiction, and its status. **Answered** items are drawn as
decided; **open** items are drawn with the open dash (`DASH["open"]`, orange-brown) and listed in the figure's
"open" notes.

## Answered by the owner (2026-11-04, DECISIONS-v1.3 up to 352df7d)

1. **The "peripheral bus" is three paths, not one** (spec 4.17). Answer: **two data buses plus the window.**
   Inbound, packets from the traffic director in peripheral-write mode (mode 0) into the region, parts 0, 2 and 4;
   outbound, the sender (the DMA) reads the processor's state and the output queues, builds packets and injects them
   into a router input of its own; and C&C 3's register window, a control path. Drawn that way.
2. **Reading the state at done** (section 24 had the end state going "as it is" to a tile, which wants the start
   state). Answer: everything is read out at done and nothing writes a state between runs. At done *n*, output queue
   0's words 0 and 2-66 are window *n* + 1's start and words 1 and 67 are run *n*'s totals; C&C 3 sends the state to
   tile *n* + 1, and run *n*'s totals and streams to tile *n* (section 24, 14713f3).
3. **Where the output totals live** (word 1 was the advice size, word 67 cleared at load). Answered with 2 and by the
   owner's ruling (section 23, 7374646): **the stream word carries four streams**, code, read log, write log and advice.
   The advice field is the segment's advice size at load and the live count remaining, counting down as advice is popped;
   at done the Tracer writes only the three output fields and word 67, so nothing is overwritten and **output queue 0 is
   part 0 read in place**. The replayer ignores the advice field. Its bits (23, a value count only) are SP1's to place:
   split across the v1.26 spares [63:51] and [127:115], or a rearranged word.
4. **A possible deadlock in the log drain** (one log FIFO feeding output queues 2 and 3 in row order, the DMA sending
   one queue at a time). Answer: in the first implementation the DMA writes directly into the tiles' memory, so no
   replayer credits pace it and C&C 3 interleaves the queues' transfers (ba91410); later, C&C 3's memory and LPDDR
   files. **Still open:** the output queues' depths, and the interleaving rule that keeps the drain moving (drawn open).
   Section 24's DMA bullet, which still said transfers were sized to the replayer's credits, is fixed (df077e4).
5. **The stream-ready wake flags.** Answer: dropped (481fe13). The flags are **expand, done, advice and DMA done**;
   C&C 3 drains by the queue lengths at done and the DMA waits on its queue.
6. **Section 3's pipelining was stale.** Answer: drawn as section 2, drain then go (marked superseded in 14713f3).
7. **A start pulse.** Answer: none (352df7d). Exactly two pulses, `PULSE_RUN` and `PULSE_DMA`; **the write to word 0**
   flushes the queues, latches the sizes, clears the counters, sets the credits and invalidates the instruction and
   data caches and the jump LRU, without flushing dirty lines. A mid-segment word-0 rewrite is UB.
8. **The range comparator's bounds.** Answer: from word 66 (section 23; section 25 updated in 14713f3).
9. **Where the jump table lives.** Answer: a file read over the fill bus (sections 19, 21); section 18's dedicated
   128 KB SRAM is drawn as open.
10. **Who feeds part 2** (the read queue). Answer: C&C 3 (section 13).

## Open (drawn with the open dash)

11. **The fill bus:** arbitration among its three requesters (instruction, data, mapping misses); whether misses
    block the core or overlap; a write-back buffer for the data cache's dirty victims; and the error response from the
    memory system's walker (a write to a hole, an access past the file's length, section 25), which has to reach the
    Tracer's `ERRORS`.
12. **The jump-table probe's bound:** section 25 says "within its bound"; section 18 has the host checking that no key
    lands more than two lines from home, and measures every key in its home line. The hardware's bound (one line? two?
    three?) isn't stated.
13. **The offset subtractor on the fetch path.** Lines are tagged by their line in `.pages`, whose offset is the guest
    address minus `lowest_address` (section 12), so instruction fetch subtracts too, not only the data path's range
    comparator. Whether the two paths share it, or the comparator's subtract is also the data cache's offset, is
    unsaid.
14. **The per-stream compression enable bits** (section 3: "a per-stream enable bit") have no register or field.
15. **Bit numbers:** the Tracer's `EVENTS` bits beyond [0] run done (= done?) and [1] DMA done, and its `ERRORS` bits
    (jump target not in the map, write to a hole, beyond the file's length, outside the provable range, advice
    underflow). Section 24 proposes one bit per stream; none are numbered.
16. **Proposed, not ruled on:** the queue-length registers at words 36-39 (state, code, read log, write log); the
    template number returned in the expansion queue's first word or a register beside `EXPANSION_OPCODE`; the advice
    flag at half free. Drawn as proposed.
17. **The output queues' depths** (with 4), and **the packers** (one per stream, committing the last partial 128-bit
    word at done): their widths and where set 2's output joins them are implied, not stated.
18. **The stream word's layout** (16 + 17 + 1 bits per stream, v1.26 item 2) and **word 66's bit positions** (roots,
    depths, log2 K, `lowest_address`): "to propose" / "for SP1".
19. ~~When the expansion tables load~~ **answered** (owner, 2026-11-04): the expansion lookup table (the decode table,
    4 KB, 2K × 16 bits, 0x1000) and the expansion lines (the template ROM, 8 KB, 0x2000) are loaded **once, at startup**,
    over the same bus from C&C 3 that fills the queues; drawn with a new dotted dash, `DASH["startup"]`, added to
    `common.py` for it.
20. **The core's internals** are not designed yet (section 25: "its own core"). The figure's split into fetch and
    decode, the expansion sequencer, the row executor and the working state is the figure's, for orientation, and is
    marked so. The "about 4 cycles per row" is section 11's [estimate].
21. **The upper bound of the range check** (section 25): the cell index below K, to confirm against a16z's prover or
    the layout's top.

## Figure notes

- Figure 9.1 is new **Part 9** of the set; the README gets the part and its row.
- **The split** (owner, 2026-11-04): 9.1 too busy, so its output side collapses into one "output queues" box, drawn in
  full in 9.2 (the output path); 9.3 is the 68-word state record as a memory block.
- PNGs are ignored by the repository (`.gitignore`: `wip/**/png/`), so the PNG is rendered and checked locally but not
  committed, like the other figures'.
- Helpers: `common.py`'s, plus one dash style, `DASH["startup"]` ("1.5 3"): loaded once at startup, drawn in its
  path's colour (purple here), distinct from the orange-brown "open" dots. The memory system is drawn as a literal black box.
- **9.1 simplified** (owner, 2026-11-04): the right side is just the four boxes (advice queue, expansion queue, expansion
  lookup table, expansion lines) on one bus from C&C 3 that carries on into the core, plus one control line for the
  window; the output side is one small "output LRUs and queues" box pointing to 9.2.
