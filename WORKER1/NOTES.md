# WORKER1 running notes

Review of the v1.2 decisions: errors, interrupts and recovery. Prompt: `WORKER1/PROMPT.md`.
Inputs read: `wip/planning/DECISIONS-v1.2.md` (all), spec 2, 3.3, 4.11-4.14, 4.17, 4.18, 4.19.11, 6.1-6.7, 9,
open items touching C&C/HALT; planning notes section 7. Code checked: `wip/sort/program/sw/loader.S`,
`sort.h`, `wip/meshsim/src/Controller.java`, tests using `HALT_EBREAK`.

## Status
- [x] Read everything listed above
- [x] Candidate findings (below)
- [ ] REVIEW.md written and pushed

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
