# Worker 2: replayer specification

You are writing the full replayer section of the mesh sorter's v1.1 hardware specification. This session writes specification only.

INPUTS (attached)
- mesh-sorter-v1.1-sim.tgz: the current tree. Read CHANGES-v1.1.md first, then spec/machine-spec.md (section 4.17 is a stub reserving the replayer's hooks) and sw/REGISTERS.md.
- DESIGN-NOTES.md at the top of the tree, "Replayer and mesh v1.1: design notes": the owner's design. Everything except the request-bus section is your input; the allocation table is binding.

A PARALLEL SESSION (WORKER1_PROMPT.md) is specifying the request bus at the same time. To keep the two mergeable, you own only:
- section 4.17, rewritten in full: the run log and checkpoint; the expansion word and operand codes; the rd = x0 policy; the decode table and its hardware index; the ROM and its contents; the ALU and its fixed 3- and 5-cycle schedules; the register file; the three queues, their mode-8 ports and credit accounting; REPLAYER_NEED, the squelch and the controller loop; the address map and ownership; padding; halt; the output row record; the resident replay region, as the software contract it implies;
- mode 8 in 3.1's mode table and the traffic director (4.8);
- DMA_REPLAYER_REMAINING in 4.12;
- register rows 0x844–0x850 in 8.1 and new 8.x sections for them; TILE_PULSE [2] and [3]; TILE_STATUS [3], [4] and [12]; TILE_CONDITIONS [11]–[13].
Do not edit the request bus, mode 9, the C&C counting rule or registers 0x854 onward.

RULES
- Follow the spec's conventions. Anything the notes leave open goes to section 13 as (Proposed), with reasoning. Examples: the queue-port windows, the exact REPLAYER_NEED and REPLAYER_STATUS bit layouts, the output row's field layout, which row opcodes take 3 and which 5 cycles (list them as Open where the source doesn't settle it).
- Facts about Jolt's expansions must come from the tracer source (tracer-standalone branch of jolt-asic-planning, tracer/src/tracer/), cited by file and line. If you have no access to it, mark the item Open rather than guessing.
- Don't edit meshsim/, sort/, kit/ or the PDFs. Don't push to any repository.

WORKING METHOD
- Read first, then propose a plan with your proposals for the open details and anything in the notes you find inconsistent, and wait for my go-ahead before editing.
- 4.17 is long; write it subsection by subsection, keep a running notes file so the work survives a context compaction, and save interim archives to /mnt/user-data/outputs.

DELIVERABLES
1. The tree as a .tgz, plus replayer.diff, a unified diff of spec/ and sw/REGISTERS.md against the input.
2. An addendum to CHANGES-v1.1.md: the sections changed, the proposed items, and a handoff for the simulator session.
