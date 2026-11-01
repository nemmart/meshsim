# Worker 3: wake-event and queue-start spec pass

You are applying two owner rulings to the mesh sorter's v1.1 hardware specification. This session writes specification only, and is small.

INPUTS (attached)
- mesh-sorter-v1.1-2026-10-23a.tgz: the current tree, with the request bus and the full replayer merged. Read CHANGES-v1.1.md, especially part h (the two rulings) and part g (the merge), then spec/machine-spec.md and sw/REGISTERS.md. DESIGN-NOTES.md at the top of the tree holds the owner's design; its "All wakes are events" and "Queues are cleared by the start" paragraphs are the rulings in full.

THE RULINGS (CHANGES-v1.1.md part h has them in full)
1. All wakes are events, and WAKE_EVENTS is renumbered: [7:0] request tags (8 tags, down from 16);
   [8] GO; [9] counters done (set on the 1 -> 0 transition that leaves every down-counter at 0, once
   mem_idle and cm_idle hold: today's local wake with its qualifier, as a sticky event); [10] command
   landed (mailbox slot 0); [11] engine run done (rd_done); [12] request FIFO no longer full;
   [17:13] the replayer ([13] code, [14] value, [15] expansion low-water, [16] new expansion,
   [17] done); [31:18] reserved for future units. WAKE_MASK resets to [8] | [9]. The done gate becomes
   a mask setting applied on the done pulse. A load still returns every pending event and clears
   only those under the mask. The request's tag shrinks to [8:6] ([9] spare); mode 10's payload
   carries the tag in [2:0], kind still [5:4].
2. WAKE_REQUIRED, a new R/W register at 0x870 (reset 0):
   wake = ((WAKE_EVENTS & WAKE_MASK & ~WAKE_REQUIRED) != 0)
          OR (WAKE_REQUIRED != 0 AND (WAKE_EVENTS & WAKE_REQUIRED) == WAKE_REQUIRED).
   A mask bit not required wakes alone (escape hatch); a required bit only counts toward
   all-pending; clearing stays under the mask. This is the only WFI wake term.
3. The replay start clears all three replayer queues; there is no preloading and no end-of-pass
   reset. Software rule: before a start, every queue push the controller issued has landed (counters
   done for local refills; the tag events for memory reads).
4. Big-SRAM read order is per client, not global: each client's reads (address generator, GATHER,
   read port) return in the order that client issued them; different clients' reads may complete in
   any order relative to each other. This replaces the global in-order rule in 4.1, 4.5 and item 74
   (the token FIFO becomes an in-order return queue per client). The read port needs no order.
5. New software rule, an invariant, flagged by meshsim: a tile must not have two clients sending
   order-placed packets to the same destination at once (modes 2-4 into the same PTR entry of the
   same receiver, or mode 8 into the same replayer queue).

SCOPE
Every place the spec or REGISTERS.md describes the WFI wake, the request tags (16 -> 8), the local-wake latch, GO, the done gate,
WAKE_EVENTS and WAKE_MASK, or the queues' reset and preloading. At least: 4.12, 4.13, 4.17.8,
4.17.9 (the controller loop: its "squelch" now sets WAKE_MASK to counters done, not 0), 4.17.11,
3.1 and 3.3 (mode 10's tag, the request's tag field), 6.1, 6.3, 6.7, 8.1 (0x870), 8.10, 8.11, the invariants, section 13 (update or supersede items 30, 73 and 74 and any others the
rulings touch; new items from 76), and REGISTERS.md 2.3.2 and 2.6. Search for "local wake",
"latch", "GO", "done gate", "preload" and "reset" rather than relying on this list.

RULES
- Follow the spec's conventions; owner rulings are written as decided, your own additions as (Proposed).
- Don't edit meshsim/, sort/, kit/, the PDFs or DESIGN-NOTES.md. Don't push to any repository.
- Read first, then propose a short plan, and wait for my go-ahead before editing.

DELIVERABLES
The tree as mesh-sorter-v1.1-<date><letter>.tgz, named by the archive naming rule in CHANGES-v1.1.md, a unified diff of spec/ and sw/REGISTERS.md against the input, and a part i in
CHANGES-v1.1.md listing the sections changed and the simulator handoff (classes and tests).
