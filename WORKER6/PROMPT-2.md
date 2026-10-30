WORKER6, round 2. Pull first.

Bring the recipes, the draft header and the check sources up to date with the decisions log's sections 39-42, which came after your first round. WORKER5 is updating the spec for the same sections in parallel, and will then check your work against it.

- 39: the replayer's status words are one field per word: 32 ROWS_RUN, 33 EXPANSION_OFFSET, 34 EXPANSION_OPCODE (replacing RUN_STATUS and PRESENTED).
- 40: every request names its own reply address, anywhere in data memory, beside its reply counter. Give each outstanding request its own reply buffer (this answers your F15); the reply block is now only the default for tile-to-tile replies. Seven counters, codes 1-7: WRITE_COUNTER_1-7 at 0x860-0x878, the read port's registers at 0x87C and 0x880. EVENTS is renumbered: counters 1-7 done [6:0], counters done [7], GO [8], command landed [9], DMA run done [10], request FIFO no longer full [11], request pending [12], peripherals [15:13]; EVENTS_MASK resets to GO | counters done. Revisit your counter convention (v1.1's four mapped 1:1) now that there are seven.
- 41: your F6, F9-F14 and F16 are answered there. In particular: C&C 1 bounds its waits with mcycle (F12); the ROM must not consume "command landed" in its tile branch (F13); a stream may name the broadcast, d = 126, so C&C 1 boots by streaming overlays from its big SRAM at a packet per cycle (F14). Rewrite the boot recipe that way.
- 42: answers to WORKER5's questions, notably ROUTER_ADDR advancing for mode 0 only, each read-port queue entry carrying its own counter, C&C 1's peripheral words 32-45 with EVENTS [2] all tiles READY, [3] watchdog, [4] RX not empty, [5] TX not full, and the exit word at 0xFFFFF9F8.

Keep the book generated from the compiled code, and the code compiling. Update FINDINGS.md (mark F15 and the rest as answered) and write WORKER6/REPORT-2.md when done: WORKER5 waits for it before checking the recipes.

This prompt is the go-ahead for wip/sw/RECIPES.md, wip/sw/CONVENTIONS.md, wip/sw/mesh-v1.2-draft.h and WORKER6/. Run `git pull --rebase` before every push.
