# WORKER1 running notes

Review of the v1.2 decisions: errors, interrupts and recovery. Prompt: `WORKER1/PROMPT.md`.
Inputs read: `wip/planning/DECISIONS-v1.2.md` (all), spec 2, 3.3, 4.11-4.14, 4.17, 4.18, 4.19.11, 6.1-6.7, 9,
open items touching C&C/HALT; planning notes section 7. Code checked: `wip/sort/program/sw/loader.S`,
`sort.h`, `wip/meshsim/src/Controller.java`, tests using `HALT_EBREAK`.

## Status
- [x] Read everything listed above
- [x] Candidate findings (below)
- [x] REVIEW.md written and pushed (W1-01..W1-17; line refs checked against 2146213)

## Candidate findings (D = decisions line, S = spec line)
1. Errors raised in a halted unit (D157 counted arrival in HALT; S1727 director runs in HALT) keep its line
   high; only the RUN->HALT edge clears ERRORS (S941, S1731), so C&C 1 cannot clear its live unit-error
   bit (D451-456) or the pin. Fix idea: unit-error bit = OR(ERROR_LINES & RUN_LINES).
2. Interrupt semantics: level/edge, MIE/MEIE gating, WFI with a pending masked interrupt (RISC-V resumes).
   ERRORS is sticky, so the interrupt stays asserted: parked handler retraps or spins. Q1 must list
   mstatus, mie, mip, mtvec, mepc, mcause, mtval; mscratch avoidable (x0-relative save area).
3. CSR state at HALT->RUN undefined (mtvec, MIE). Loader ebreak (loader.S:32) now traps to undefined mtvec;
   halted-load errors survive into RUN (S941).
4. C&C 1 must resume from its handler: non-atomic register sequences (ROUTER_*, REQUEST_SET/GET,
   consuming EVENTS loads, RUN_LINES RMW, DMA_HDR/ADDR); mepc after WFI makes the sequencer's WFI return
   early; nested synchronous exception loses mepc.
5. Q3: names the sender (gone, D10); hardware stop on trap can't apply to C&C 1 (routine interrupts,
   peripheral = Ingress/Egress + UART). Address generator, GATHER, read port, send port keep running (S946).
6. Per-unit recovery halts mid-phase: contradicts invariant 7 (S2141, S1735, S1833); stale traffic into the
   next RUN; hold before RUN (Q4).
7. READY no longer means healthy: halted tile has done forced 1 (S1762) -> READY on next command (S1740);
   parked-after-done tile too (D68). Recovery policy (abort vs continue) undecided.
8. Halt vs report collection: line arrives in tens of cycles, ~17 reports take >= 136 cycles at the root
   plus the handler; halting early cuts reports; no end marker. HALT must not clear the request FIFO.
9. Mass failure: 64 x 17 = 1,088 reports, >= 8.7K cycles at one request per 8 cycles (S221); row HOL
   (S219) blocks memory-system requests; a halted C&C 2/3 stops returning credits -> rows' reports to
   C&C 1 blocked.
10. C&C 1's own errors are permanent (bit 64 unused, D449); nothing resets its peripheral; UART line
    errors (item 102: W1C status) would become fatal; host recovery path undefined.
11. ecall/ebreak/fetch faults have no ERRORS bit (D547-566) -> no error line. Code relies on ebreak halt
    (loader.S, sort.h CMD_EXIT, Mach.java:63, Controller.java:16-25,196-198).
12. Done gate wakes the parked handler on GO (S890) despite D66 "no GO can wake it"; harmless with the loop.
13. Timing: no error-line latency parameter (section 10); handler must wait run_latency + line latency;
    RUN_LINES chip-reset value unstated; two stores to release tiles 0-63; clear-wins at the edge.
14. C&C 2/3 boot: no READY lines; 6.2 doesn't cover them.
15. Q4 misses: CSRs, interrupt input, K delay line, request FIFO, stale REQUEST_SET_W across HALT.
16. Pin/Q2: depends on 8 and 1.
17. Handler details: non-present peripheral window -> bus error; use EVENTS_PEEK; word count.

## Round 2 (PROMPT-2.md): resolutions in decisions 19-24, commit 21ae57a

REVIEW-2.md written and pushed (W1-18..W1-30). Read: overview, 1.4, 1.5, 3, 5, 15, 19-24, pending. Status draft:
closed 01 02 04 05 12 15 17; partly 03 06 07 08 09 10 11 13 16; closed-for-2/3 14 (C&C 1's own boot open).

New candidates (D = decisions line at 21ae57a):
18. Traps with no ERRORS bit (fetch fault, ecall/ebreak, handler re-entry) raise no line: C&C 1 not
    interrupted; on C&C 1 the error pin stays low. Reports sit in the receive queue and back up the tree.
    Fix: hardware sets a sticky ERRORS "trap" bit on every trap entry.
19. Memory system / LPDDR across the reset: not in 21's domain list (D660); in-flight reads land in
    rebooting units; write token held by a dead tile; cache write-back of flushed logs; LPDDR refresh.
20. Boundary: request-bus framing torn at reset (S213, 8 beats, no header); root credits vs C&C 1 queue;
    C&C 1's outgoing path to tile 0 delivers stale packets after release (stale slot-0 write -> ROM jumps);
    no FIFO-empty status for C&C 1.
21. C&C 1's own state survives the restart (D699-704): counters (can't be cleared), sticky events,
    READY_LINES, mesh receive FIFO, own request FIFO; a late counted arrival -> C&C 1 fatal.
22. Errors at C&C 1 caused by other units (dst-64 counted packet, receive-FIFO overflow, bad write
    streams, 6.7's flags) would be C&C 1 own errors -> error pin -> host power cycle for a tile bug.
23. Boot ROM contract: EVENTS_MASK reset lacks command landed [5] (D497); which slot holds the address;
    command landed stays set after the jump -> first done fires READY at once (stale READY); done's reset
    value; image covering slot 0 (D248); ROM handler must send the fixed report set (D705) -> not
    "a few dozen bytes" (D762) and freezes the protocol in silicon.
24. Marker ordering (D756) holds per kind only (S3.2 line 158, S1771): broadcast image + unicast marker.
25. Reset timing: run_latency gone, no reset_latency/release pacing; packets into routers still in reset.
26. Loops: handler re-entry (D637) resends reports; persistent fault -> reset loop; cascades; flush.
27. Pins: host view table; chip-reset path for C&C 1 undefined (D797); C&C 1's own boot.
28. Remnants contradicting 23: D69, D87, D145, D161, D320, D557, D619, D624, D668, D681, D692, D716,
    D719-722, D724; D204 vs ROM fetch; region table row 0xFF displaced.
29. PULSE_GO vs READY_LINES clear; expected-units mask.
30. Units started one at a time: counted traffic to a sleeping unit; replayer ownership "in HALT".

Round-2 addendum (b8bd504): owner Qs 1, 3, 6 partly resolved (25, 26, 28); 2, 4, 5, 7 and Tony open;
W1-29 resolved by 28 (proposal). Appended to REVIEW-2.md section 6.
