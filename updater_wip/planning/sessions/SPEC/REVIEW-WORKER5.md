# Review request: WORKER5, the cache, the AXI arbiter and the LPDDR model

*From the specification session (`sessions/SPEC/`), branch `v137-spec`, at the commit that adds this file. Your code
is the authority on what the cache's controller, the arbiter and the LPDDR model do (the owner: "it's actually the
spec portion of those codes"). The specification was written from it quickly, at cycle-level detail: that is where
mistakes will be. Read it against your code, not against `CACHE-INTERFACE.md` or `AXI-INTERFACE.md`.*

## What to read

All in `v1.37_wip/spec/`:

| Section | What it states |
| --- | --- |
| `machine-spec.md` 23 (opening), 23.1 | the memory side, and where it sits |
| 23.5 | the files as the hardware sees them: entry, owned bit, level, path |
| 23.6, 23.6.1–23.6.11 | the cache: its memories, the look-up port, the three operations, the fill port's service, **the controller's registers and states (23.6.5)**, an update's sequence, timing, the clients' rules, errors, reset, the simulator's checks |
| 23.10 | a client's AXI port |
| 23.11 | the AXI arbiter: **its registers and its cycle, step by step** |
| 23.12 | `lpddr` with IDs, and the LPDDR model |
| 23.13 | the memory system's checks and parameters |
| 17.11 | the memory system as the memory tile's peripheral 1: its window and `ERRORS` [0] |
| 3 | the rows `lookup`, `lookup_reply`, `lpddr_client`, `lpddr`, `fill` |
| 19, 20 (invariants 37–39), 21.5 (the cache's and the arbiter's rows), 24 (the memory system's row) | the gathered parameters, invariants, timing and budget |

## What I derived and did not run, or could not fully check from the code

**The arbiter (23.11, 23.12):**
1. The register table and its total, **1,539 flip-flops** (264 a client, 483 shared), at `CLIENT_BURSTS` 4,
   `TURN_BEATS` 32, `LPDDR_ADDRESS_BITS` 32, every field counted at its signal's width (`arlen` at 8). The read
   list holds no response; the write list a 2-bit response a burst.
2. The order of the edge's five steps, and that a burst taken in cycle *t* has its first piece on `lpddr` in
   *t* + 1, steps 2 and 3 being one edge.
3. That a burst of at most `TURN_BEATS` beats leaves its slot in the edge that takes it, so `arready` or `awready`
   is 1 again in *t* + 1 when the list has room.
4. That `wready` is 1 for the client whose piece is in turn while the W queue holds at most one beat, and that the
   next AW turn can be given in the edge that takes the previous piece's last W beat.
5. That a write's single response is `OKAY` if every piece's was, else **the first piece's in the order the
   responses came** that was not.
6. "The arbiter is fair and never deadlocks: a slot is served within `LPDDR_CLIENTS` turns of its direction":
   argued, not shown.
7. **The line figures at the model's defaults, idle:** a line read taken in *t* has its first beat at the client in
   *t* + 72 and its last in *t* + 79; a line write taken in *t*, its beats offered at once, has its response at the
   client in *t* + 80. Worked by hand from `axi_arbiter.cpp` and `lpddr_model.cpp`.
8. The model's "when the memory changes" (23.12), restated from `lpddr_model.h`.

**The cache (23.6):**
9. The tag row as 216 bits (8 × (valid, dirty, 22-bit tag) + 8 × 3-bit age), 13.5 KiB in all.
10. **The register table of 23.6.5 and its total, 6,866 flip-flops.** I counted what hardware needs, not the code's
    C++ types: the failed line as 31 bits and a valid bit; the victim as its 22-bit tag; `missed_line` and
    `t_accept` left out (statistics); the walker's `wfresh_` as "a read pending". Five 1,024-bit line registers.
11. The look-up of a depth-2 file on hits answered **6 cycles** after the handshake, as "one to be picked, two
    levels of two, one for the bus".
12. The front end: the source whose miss was just installed or failed first, then round robin over the six sources
    from the one after the last picked.
13. Replays: a request that cannot go on is walked again from the top when the miss handler is idle; a request whose
    fetch failed is answered at its replay, at that line (look-up and update `lpddr_error` at its level, fill read
    `beyond` with the error flag, fill write `error` with no flag).
14. The miss handler's state table, in particular that **fail invalidates the way,** so a clean victim chosen for a
    fetch that then fails is lost from the cache.
15. A discard that meets the update slot's line waits for the slot and is walked again from its first line.
16. A fill write whose index fetch fails is answered `error` and **does not** set the error flag (23.6.9).
17. An update's reply goes out in the cycle after its B response, the bus the slot's first; a walker reply that
    loses the bus waits a cycle.
18. A depth-0 fill: line *l* < 32 is the root's line *l*; a look-up or update at depth 0 is `beyond`.
19. The reset: every tag row invalid with ages 0 to 7 by way.
20. 23.6.8: that the stale-line check catches the third and fifth client rules (an index page changed other than by
    `update`; a page written directly while the Tracer reads it), and not the others.
21. 23.6.7's figures (6, 173, 87, 10, 351, 9, 17 cycles; −2.7 % to +4.4 %) are quoted from your report, not
    re-measured.

**The memory system (17.11):** that its `ERRORS.lpddr_error` is exactly your error flag: set by a write-back's error
and by a fill read's fetch error, by nothing else.

## What I ask of you

Two lists, each item with the section and the line of code:

1. **Where the specification misdescribes what your code does:** a state, a transition, a width, a cycle, an error,
   a count, an omission that matters to a builder.
2. **Where the specification is right and your code is wrong or silent:** a rule it states that your code does not
   keep or check, or a case your code does not handle.

Please do not edit `spec/`: put both lists in `sessions/SPEC/REVIEW-WORKER5-ANSWER.md`, and the specification session
applies them.
