# Worker 2, round 3: sign-off before implementation

Your round-2 review (`WORKER2/REVIEW-2.md`) was gone through with the owner and Tony. Since then the design
also settled booting and reset: **the boot ROM is a minimal `wait_for_command()`** (31), and **the chip never
resets itself**: on any error C&C 1's handler starts the file system flushing and Ingress/Egress raises an
**"I'm toast" pin**; the outside world then issues a hard or soft reset (35). The next step is WORKER4, who
ports meshsim to this design (`WORKER4/PROMPT.md`). This round asks one thing: **are you satisfied the design
is ready to implement?**

REPOSITORY AND RULES: as in `WORKER2/PROMPT.md`. Pull first. This prompt is the go-ahead to write and push
`WORKER2/REVIEW-3.md` and to keep using `WORKER2/NOTES.md`; change nothing else.

READ
- `wip/planning/DECISIONS-v1.2.md`: "Where the design stands", then sections **31-35**, and every section
  your round-2 findings point to below. Superseded points are marked in place; later sections win.
- `WORKER4/PROMPT.md`, to see what will be built from the log.

HOW YOUR ROUND-2 FINDINGS WERE ANSWERED

| finding | answered by |
|---|---|
| W2-24 the stream count | 30: 17 bits, [16:0] |
| W2-25 K | 32: K = max(2·L + 3, L + 4, `cm_latency`, `rp_latency`) |
| W2-26 S1's thinnest case | 32: guaranteed by K; meshsim checks table-window accesses after done |
| W2-27 big-SRAM images | 31: counted, after the first command |
| W2-28 tag-window fan-in | 25: a constraint on the file system's design, deferred |
| W2-29, W2-30, W2-31, W2-05 | 33: deferred with the file system and sending peripherals |
| W2-32 misaligned engine accesses | 32: dropped, still counted, `ERRORS` [1] |
| W2-33 big SRAM out of range | 32: undefined; meshsim errors |
| W2-34 alignment of pointers and runs | 32: stated per register in the spec update |
| W2-35 counter codes 5-7 | 32: accepted as uncounted; meshsim errors |
| W2-36 stale text | fixed in place |

SETTLED: DON'T RAISE AGAIN
These were decided deliberately; reopen one only if you find it makes the design fail, not because a
hardware check would be nicer:
- Software bugs are meshsim's job, not the hardware's (27): out of bounds, running past the end, reserved
  modes and counter codes, `ecall`/`ebreak` and other traps with no `ERRORS` bit (W1-18).
- The full 256 bits are stored for now (2); trimming comes later, for power.
- Everything in section 33 (deferred) and the file-system side of 25 and 35.

WHAT TO DO
1. Mark each of your round-2 findings **closed**, **partly closed** or **open**, with a sentence of evidence.
2. Read sections 31-35 as new design, in your area (the packet path). Report only what would make WORKER4 build the
   wrong thing or get stuck: a contradiction, an undefined behaviour the simulator must model, a race.
3. Check that `WORKER4/PROMPT.md` asks for the design as decided, in your area.

DELIVERABLE: `WORKER2/REVIEW-3.md`, short
1. **Verdict:** ready to implement, or not, in one paragraph.
2. A status table for your round-2 findings.
3. Anything that blocks implementation, numbered on from **W2-37**, with a suggested resolution. Keep
   everything else (wording, nice-to-haves) to a short list at the end.
