# Worker 3, round 3: sign-off before implementation

Your round-2 review (`WORKER3/REVIEW-2.md`) was gone through with the owner and Tony. Since then the design
also settled booting and reset: **the boot ROM is a minimal `wait_for_command()`** (31), and **the chip never
resets itself**: on any error C&C 1's handler starts the file system flushing and Ingress/Egress raises an
**"I'm toast" pin**; the outside world then issues a hard or soft reset (35). The next step is WORKER4, who
ports meshsim to this design (`WORKER4/PROMPT.md`). This round asks one thing: **are you satisfied the design
is ready to implement?**

REPOSITORY AND RULES: as in `WORKER3/PROMPT.md`. Pull first. This prompt is the go-ahead to write and push
`WORKER3/REVIEW-3.md` and to keep using `WORKER3/NOTES.md`; change nothing else.

READ
- `wip/planning/DECISIONS-v1.2.md`: "Where the design stands", then sections **31-35**, and every section
  your round-2 findings point to below. Superseded points are marked in place; later sections win.
- `WORKER4/PROMPT.md`, to see what will be built from the log.

HOW YOUR ROUND-2 FINDINGS WERE ANSWERED

| finding | answered by |
|---|---|
| W3-24 credits leak | 10: every queue's credits set to 2 at `PULSE_RUN`, the expansion queue's again at each expansion; code and log refills are full halves; idle queues masked out |
| W3-25 a queue-of-items event | 33: deferred with the file system and the host interface |
| W3-26 reset values, GO into the ROM | 31: the reset zeroes all control state; the ROM waits for GO |
| W3-27 the C structs | 14: pointers, a macro for the standard members, `…Regs` names |
| W3-28 C&C 1's registers | 28: revised as you suggested (`RESET` then removed by 35) |
| W3-29, W3-30 | 32 |
| W3-31 stale text | fixed in place |
| W3-32 the trace word | 32: a named simulator-only word |
| W3-33 the stream count | 30: 17 bits |

SETTLED: DON'T RAISE AGAIN
These were decided deliberately; reopen one only if you find it makes the design fail, not because a
hardware check would be nicer:
- Software bugs are meshsim's job, not the hardware's (27): out of bounds, running past the end, reserved
  modes and counter codes, `ecall`/`ebreak` and other traps with no `ERRORS` bit (W1-18).
- The full 256 bits are stored for now (2); trimming comes later, for power.
- Everything in section 33 (deferred) and the file-system side of 25 and 35.

WHAT TO DO
1. Mark each of your round-2 findings **closed**, **partly closed** or **open**, with a sentence of evidence.
2. Read sections 31-35 as new design, in your area (registers, peripherals and the software). Report only what would make WORKER4 build the
   wrong thing or get stuck: a contradiction, an undefined behaviour the simulator must model, a race.
3. Check that `WORKER4/PROMPT.md` asks for the design as decided, in your area.

DELIVERABLE: `WORKER3/REVIEW-3.md`, short
1. **Verdict:** ready to implement, or not, in one paragraph.
2. A status table for your round-2 findings.
3. Anything that blocks implementation, numbered on from **W3-34**, with a suggested resolution. Keep
   everything else (wording, nice-to-haves) to a short list at the end.
