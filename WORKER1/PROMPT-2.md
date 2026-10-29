# Worker 1, round 2: check the resolutions of your review

Your first review (`WORKER1/REVIEW.md`, findings W1-01 to W1-17) was gone through with the owner and Tony.
The answers are now in `wip/planning/DECISIONS-v1.2.md`, mostly as new sections 19-24, and they changed
the design more than any single finding asked for: **traps are terminal**, there is **no HALT state**, a
**global reset** is the only stop, and every unit **boots from a ROM** and starts itself when its image's
marker lands. Check that the answers hold together.

REPOSITORY AND RULES: as in `WORKER1/PROMPT.md`. Pull first. This prompt is the go-ahead to write and push
`WORKER1/REVIEW-2.md` and to keep using `WORKER1/NOTES.md`; change nothing else.

READ
- The log's "Where the design stands", then sections 19-24 in full, and the places they revise: 1.4, 1.5,
  3 (HALT and counting), 5 (the regions were renumbered: the boot ROM is 0x02, big SRAM 0x03, the
  peripherals 0x04-0x06), 15, 20 and 21. Superseded points are marked in place.
- Your own `REVIEW.md` and `NOTES.md`.

HOW YOUR FINDINGS WERE ANSWERED

| finding | answered by |
|---|---|
| W1-01 errors in a halted unit | 19, then 23: no HALT state; the reset clears every error; the unit-error bit is `OR(ERROR_LINES)` |
| W1-02 interrupt semantics | 20: traps are terminal, no `mret`; `MIE` stays 0, so no retrap; the park is a plain loop |
| W1-03 trap state at reset | 21, 22: the reset resets the core (`MIE` = 0); the boot ROM sets `mtvec` first |
| W1-04 torn sequences | 20: nothing resumes; the handler writes every word of what it sends |
| W1-05 Q3 | 21: nothing stops a failed tile's engines and peripherals until the global reset |
| W1-06 halts with packets in flight | 21, 23: no per-unit halt; the reset clears the whole domain |
| W1-07 READY and health | 21: a failed tile's error line interrupts C&C 1 whatever READY says *(check this)* |
| W1-08 halting cuts off reports | 21: C&C 1 drains every report before the reset; the report set is fixed |
| W1-09 mass failure | 21: draining, with the flush's requests behind the reports; timeouts |
| W1-10 C&C 1's permanent errors | 21: UART line errors are status bits; C&C 1's own errors are fatal; 24 |
| W1-11 `ecall`/`ebreak` | 20: no hardware for them; our programs don't use them |
| W1-12 the `done` gate wakes a parked handler | 20: the park is a plain loop, not `WFI` |
| W1-13 line timing | 21: the reset is held long enough; 23 removes the run lines |
| W1-14 C&C 2/3 boot | 22: boot ROM and marker, the same for every unit |
| W1-15 Q4 items | 23: no HALT state |
| W1-16 the pin | 24: a hardware error pin for C&C 1's own errors only, and a software pin |
| W1-17 handler details | 20: handler conventions |

WHAT TO DO
1. **Check each answer.** Does it actually close the finding? Mark each W1-01 to W1-17 closed, partly
   closed or open, with a sentence of evidence.
2. **Review sections 19-24 as new design**, in the same way as round 1: contradictions, races, gaps,
   missed consequences. Questions worth working through:
   - **Traps with no `ERRORS` bit.** A fetch fault, a misaligned access, `ecall` or `ebreak` trap, but set
     no error bit, so no error line rises. How does C&C 1 learn of them? (The handler's reports reach its
     receive queue, but nothing interrupts it.)
   - **The boot ROM:** what it must contain (setting `mtvec`, the wait, the jump, a minimal handler), the
     state it can rely on after reset (`EVENTS_MASK`, "command landed", the counters), and what happens if
     a packet other than the image arrives while it waits, or the marker arrives first.
   - **The global reset's boundary:** C&C 1 and its peripheral are outside the domain. Its request receive
     queue, the FIFOs on tiles 0-2's fifth links, the credits on both sides, and traffic from C&C 1 that
     is in flight when the reset fires.
   - **C&C 1 as recovery manager:** it runs the whole recovery with `MIE` = 0, polling. A second failure
     during the flush, C&C 2 or C&C 3 failing, a flush that never finishes, and setting `MIE` again.
   - **No HALT state across the spec:** every rule in 4.12, 4.13, 6.1-6.3, the invariants and the
     parameters that depends on HALT (`done` forced to 1 in HALT, READY at boot, `run_latency`, "a halted
     tile counts nothing", REPORT's remnants). List each and what replaces it.
   - **The two pins:** whether "C&C 1's errors except the unit-error bit" is well defined, and what the
     host sees in each failure.
   - **Still open in the log:** where `PULSE_GO` lives and how the tiles' READY bits clear (21); the
     boundary queues and the flush timeout (21, proposed).

DELIVERABLE: `WORKER1/REVIEW-2.md`
1. A summary: the five points that matter most, one line each.
2. A status table for W1-01 to W1-17.
3. New findings, numbered on from **W1-18**, in round 1's format (severity, sections, what is wrong, a
   suggested resolution).
4. Spec sections to update, as changed by sections 19-24.
5. Questions for the owner.
