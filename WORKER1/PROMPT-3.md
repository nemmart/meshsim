# Worker 1, round 3: sign-off before implementation

Your round-2 review (`WORKER1/REVIEW-2.md`) was gone through with the owner and Tony. Since then the design
also settled booting and reset: **the boot ROM is a minimal `wait_for_command()`** (31), and **the chip never
resets itself**: on any error C&C 1's handler starts the file system flushing and Ingress/Egress raises an
**"I'm toast" pin**; the outside world then issues a hard or soft reset (35). The next step is WORKER4, who
ports meshsim to this design (`WORKER4/PROMPT.md`). This round asks one thing: **are you satisfied the design
is ready to implement?**

REPOSITORY AND RULES: as in `WORKER1/PROMPT.md`. Pull first. This prompt is the go-ahead to write and push
`WORKER1/REVIEW-3.md` and to keep using `WORKER1/NOTES.md`; change nothing else.

READ
- `wip/planning/DECISIONS-v1.2.md`: "Where the design stands", then sections **31-35**, and every section
  your round-2 findings point to below. Superseded points are marked in place; later sections win.
- `WORKER4/PROMPT.md`, to see what will be built from the log.

HOW YOUR ROUND-2 FINDINGS WERE ANSWERED

| finding | answered by |
|---|---|
| W1-18 traps with no `ERRORS` bit | 27: software bugs, meshsim stops on them; no hardware. Settled |
| W1-19 the memory system across the reset | 25, 35: the soft reset leaves the AXI bus and what lies beyond it, so LPDDR keeps the flushed logs; the flush is best effort |
| W1-20, W1-21, W1-22, W1-25, W1-26, W1-27 | dissolved by 35: the chip never resets itself, and the reset is always the whole chip, from outside |
| W1-23 the boot ROM's contract | 31: the ROM is a minimal `wait_for_command()`; the reset zeroes all control state; GO clears "command landed" |
| W1-24 the marker overtaking the image | 31: big-SRAM data arrives counted, after the first command |
| W1-28 remnants | fixed in place |
| W1-29 `PULSE_GO` and READY | 28: `PULSE_GO` clears READY and drives GO; an expected-tiles mask; the reset clears READY (35) |
| W1-30 one-at-a-time start-up | 28's expected mask; the replayer's ownership rules deferred (33) |

SETTLED: DON'T RAISE AGAIN
These were decided deliberately; reopen one only if you find it makes the design fail, not because a
hardware check would be nicer:
- Software bugs are meshsim's job, not the hardware's (27): out of bounds, running past the end, reserved
  modes and counter codes, `ecall`/`ebreak` and other traps with no `ERRORS` bit (W1-18).
- The full 256 bits are stored for now (2); trimming comes later, for power.
- Everything in section 33 (deferred) and the file-system side of 25 and 35.

WHAT TO DO
1. Mark each of your round-2 findings **closed**, **partly closed** or **open**, with a sentence of evidence.
2. Read sections 31-35 as new design, in your area (errors, traps, reset and boot). Report only what would make WORKER4 build the
   wrong thing or get stuck: a contradiction, an undefined behaviour the simulator must model, a race.
3. Check that `WORKER4/PROMPT.md` asks for the design as decided, in your area.

DELIVERABLE: `WORKER1/REVIEW-3.md`, short
1. **Verdict:** ready to implement, or not, in one paragraph.
2. A status table for your round-2 findings.
3. Anything that blocks implementation, numbered on from **W1-31**, with a suggested resolution. Keep
   everything else (wording, nice-to-haves) to a short list at the end.
