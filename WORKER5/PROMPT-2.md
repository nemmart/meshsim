WORKER5, round 2. Pull first.

Two jobs, in this order.

1. Bring the spec up to date with the decisions log's sections 37-42, which came after your first round:
   - 37: the boot ROM jumps only if a command has landed (it tests EVENTS "command landed", not STATUS, since GO clears the status bit); interrupts are enabled by boot part 2, not the ROM; the report set stays fixed with zeros for absent peripherals; the first 16 bytes of data memory are reserved (overlays start at 0x10); the no-increment bit stays set until cleared; the dump goes down LVDS with an FPGA, the UART otherwise.
   - 38: request receive queues live in the request tree, one per target: 128 for C&C 1, 256 for C&C 2, 128 for C&C 3; a tile keeps at most 1, 3 and 1 requests outstanding to each; error reports are the exception. Rewrite 3.3 and 4.18.
   - 39: the replayer's status words are 32 ROWS_RUN, 33 EXPANSION_OFFSET, 34 EXPANSION_OPCODE.
   - 40: every request names its own reply address (anywhere in data memory); seven counters, codes 1-7; WRITE_COUNTER_1-7 at 0x860-0x878, the read port's registers at 0x87C and 0x880; EVENTS renumbered (counters [6:0], counters done [7], GO [8], command landed [9], DMA run done [10], request FIFO [11], request pending [12], peripherals [15:13]).
   - 41: the report count is whatever the exact set comes to (define it); mcycle and mtime exist; "command landed" stays pending after the first command and the ROM relies on it; a stream's d may be 126 in hardware, controllers only by convention, meshsim errors on a mesh tile; F6 in guarantee B (rd_running falls with DMA run done).
   - 42: answers to your questions. W5-05: port B takes one 128-bit write per cycle, built however the implementer likes. W5-04 and W5-21 are with Tony (Q16): keep them as open items.
   Update CHANGES-v1.2.md and QUESTIONS.md to match. Push.

2. Check the recipes against the spec. WORKER6 is updating the recipes and the draft header for the same sections in parallel; start this part only once WORKER6/REPORT-2.md exists. Check wip/sw/RECIPES.md, wip/sw/CONVENTIONS.md, wip/sw/mesh-v1.2-draft.h and WORKER6/check/ against the spec: register offsets and bits, the boot sequence and ROM, the handler, the counters and events, requests and replies, the replayer's loop. Also check WORKER6/REPORT.md's ten items for the spec session, and its note that the handler must read EVENTS_PEEK, never EVENTS, and skip the write-only pulse words.
   Don't edit the recipes or the header. Report every mismatch in WORKER5/RECIPE-CHECK.md: where, what the spec says, what the recipe or header says, and which one you think is right. Questions for the owner go in QUESTIONS.md as before.

This prompt is the go-ahead for wip/spec/ and WORKER5/. Run `git pull --rebase` before every push.
