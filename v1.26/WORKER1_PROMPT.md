# Worker 1: request bus specification

You are adding the tile request bus to the mesh sorter's v1.1 hardware specification. This session writes specification only.

INPUTS (attached)
- mesh-sorter-v1.1-sim.tgz: the current tree. Read CHANGES-v1.1.md first (including the owner changes at its end), then spec/machine-spec.md (the authority on everything already specified) and sw/REGISTERS.md.
- DESIGN-NOTES.md at the top of the tree, "Replayer and mesh v1.1: design notes": the owner's decisions. Your inputs are its "Request bus" and "Shared-resource allocation" sections. The allocation table is binding.

A PARALLEL SESSION (WORKER2_PROMPT.md) is specifying the replayer at the same time. To keep the two mergeable, you own only:
- a new section for the request bus and the tile's request port: the 8-ary, 8-bit tree, its header byte, arbitration, backpressure, the 64-bit request format in 128 B units, REQUEST_SET_W0–W2 and REQUEST_SEND_32/_64/_128, the 2 KB per-tile FIFO and its stall rule;
- the memory system's contract, as part of C&C (section 6): reads and their mode-7 tagged completions, write tokens (mode 7) and the token-gated write stream, ordering (any order, except at most one outstanding read per replayer queue), and debug messages;
- mode 9 (big-SRAM write at an explicit 21-bit address) in 3.1's mode table and the traffic director (4.8);
- 4.12's counting rule: C&C's packets uncounted, except mode 7, which always counts in DMA_REPLY_REMAINING;
- register rows 0x854–0x868 in 8.1, a new 8.x for the request registers, and TILE_STATUS [11] request_busy.
Do not edit section 4.17, mode 8, or any other row the allocation table gives the replayer.

RULES
- Follow the spec's conventions: the markers (Proposed) and (Open), the evidence labels, section 13 for open items. Anything the design notes leave open goes to section 13 as (Proposed), with your reasoning.
- The memory system's internals (its cache, LPDDR scheduling) are out of scope; specify only what a tile sees.
- Don't edit meshsim/, sort/, kit/ or the PDFs. Don't push to any repository.

WORKING METHOD
- Read first, then propose a plan with your proposals for the open details, and wait for my go-ahead before editing.
- Keep a running notes file so the work survives a context compaction; save interim archives to /mnt/user-data/outputs.

DELIVERABLES
1. The tree as a .tgz, plus request-bus.diff, a unified diff of spec/ and sw/REGISTERS.md against the input.
2. An addendum to CHANGES-v1.1.md: the sections changed, the proposed items, and a handoff for the simulator session (classes and tests to add or change).
