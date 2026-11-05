# WORKER1 review: errors, interrupts and recovery (v1.2 decisions)

Scope: decisions 1, 14, 15, 16 (as it meets `WFI` and the `done` gate) and 18's `ERRORS`; pending Q1–Q4;
spec 2, 3.3, 4.11–4.13, 4.17.7, 4.18, 4.19.11, 6.1–6.7 and the invariants (9). References: **D***n* is a line
of `wip/planning/DECISIONS-v1.2.md`, **S***n* a line of `wip/spec/machine-spec.md`, both at commit 2146213.
Anything inferred rather than read is marked *(inferred)*.

## 1. Summary: the five findings that matter most

1. **W1-01 (blocker):** an error raised in a unit that is already halted can't be cleared by C&C 1, so its live unit-error bit, its hard interrupt and the C&C pin stay up. Section 3 now makes this routine.
2. **W1-02 (blocker):** the hard interrupt's semantics are undefined: level or edge, gated by `mstatus.MIE`/`mie` or not, and whether a pending interrupt releases `WFI`. `ERRORS` is sticky, so the interrupt never drops, and the "park in `WFI`" design depends on the answer.
3. **W1-03 (issue):** the trap state at HALT → RUN and at chip reset (`mtvec`, `mstatus.MIE`) is undefined. The resident loader's `ebreak` and the errors that survive into RUN would trap to an undefined address.
4. **W1-04 (issue):** C&C 1, unlike a tile, must return from its handler, and its register interfaces are multi-access sequences that an interrupt can tear. This affects the send port, the request port, consuming `EVENTS` loads and read-modify-writes of `RUN_LINES`.
5. **W1-06 (issue):** per-unit recovery halts a tile mid-phase. That contradicts invariant 7 and 6.1's "no packet in flight" rule, and leaves the failed phase's traffic landing in the halted tile and in its next RUN.

## 2. Findings

### W1-01: errors in a halted unit wedge C&C 1's interrupt and the pin

**Severity:** blocker. **Decisions:** 1.5 (D84–86), 3 (D157), 15 (D447–456). **Spec:** 4.13 (S941), 6.1 (S1727, S1731).

1.5's recovery argument is that halting a failed unit clears its errors, so its error line, the fold and the pin all drop (D84–86). Only the RUN → HALT **edge** clears `ERRORS`, though (S941, S1731), and a halted unit keeps raising them. The routers, the traffic director and the write engine run in HALT (S1727). Section 3 now makes a counted packet at a halted tile set unexpected arrival (D157), where v1.1 suppressed it.

So an error set while the unit is halted holds its line high, and nothing short of RUN followed by HALT drops it again. The line feeds the peripheral's **live** unit-error bit (D451), and through it C&C 1's `ERRORS` [11] and its hard interrupt. Three ordinary cases produce such an error:

- **Recovery itself.** C&C 1 halts a failed tile mid-phase. Its partners in an all-to-all keep sending counted packets to it, which hit counters just cleared by the edge, and the tile's line rises again almost at once.
- **Boot.** Section 3 says boot loads use counter 0 (D158), but a loader image or staging packet sent with a non-zero counter, a bad packet, or a router overflow in a halted tile raises the line during boot. 4.13 intends that error to survive into RUN for the loader to see (S941). Under 15 it interrupts C&C 1 during boot instead, and C&C 1 cannot clear it.
- **C&C 2 and C&C 3.** They are halted from chip reset *(inferred)* and loaded while halted, so the same cases apply to them.

The result is that C&C 1 either retraps continuously (W1-02) or must mask its whole peripheral error. The pin stays high while C&C 1 is healthy, which the host reads as C&C 1's failure (1.5, D86).

**Suggested resolution:** derive the unit-error bit from `OR(ERROR_LINES & RUN_LINES)`, so a halted unit's errors don't interrupt. Keep `ERROR_LINES` raw, so C&C 1 can still read a halted unit's state. An error set during HALT then interrupts C&C 1 when it raises RUN, which is exactly 4.13's "the loader can see them" moment. An `ERROR_LINES_MASK` would also work, but it is one more register to keep right.

### W1-02: the hard interrupt's semantics decide whether parking works

**Severity:** blocker. **Decisions:** 1.1 (D38–45), 1.4 (D62–67), Q1 (D593). **Spec:** 4.11 (S746–750).

`ERRORS` is sticky until HALT (D547), and a non-zero `ERRORS` raises the hard interrupt (D62). The interrupt input is therefore **asserted continuously** from the first error until HALT. 1.1 doesn't say whether it is level or edge, or whether `mstatus.MIE` and `mie` gate it. Each answer breaks something different:

| if the hard interrupt is… | the tile's parked handler | C&C 1 |
|---|---|---|
| non-maskable, or level with MIE re-enabled | retraps at once. 1.1 says the trap is taken even in `WFI` (D42), so each retrap re-runs the handler and sends another ~17 reports: a report storm until HALT | can never return from its handler |
| edge-triggered | parks as intended | **misses every error after the first**: while one unit's line holds the unit-error bit high, a second unit's error makes no new edge (the bit is an OR, D451) |
| standard RISC-V (MEIP, level, gated by `MIE`/`MEIE`) | `MIE` is cleared on entry, so no retrap. But in standard RISC-V a pending interrupt enabled in `mie` **releases `WFI` whatever `MIE` holds**, so the "park" loop spins rather than sleeps | works if its handler halts every failing unit before `mret` and waits for the lines to fall (W1-13) |

1.4's "no event and no GO can wake it" (D66) holds only under the third row, and only if the handler clears `mie.MEIE` before parking.

1.2 also creates **two traps for one cause.** Bus errors and illegal instructions take a synchronous exception (1.2) and also set `ERRORS` [9]/[10], which raises the hard interrupt (D62). The handler must expect the interrupt to be pending on entry. The first row of the table turns that into a retrap.

**Suggested resolution:** fold these questions into Q1 for Tony. Ask whether the input is level-sensitive, which CSR bits gate it, and whether a pending, `mie`-enabled interrupt releases `WFI` with `MIE` = 0. Then record the rule: level-sensitive, gated by `MIE`; the tile handler clears `mie.MEIE` before its `WFI` loop. Q1's CSR list must include at least:

- `mstatus` (MIE and MPIE)
- `mie` and `mip`
- `mtvec`
- `mepc`
- `mcause`
- `mtval`, which 1.4 reads (D63)

`mscratch` is not needed: with data memory at 0, the handler can save x1–x31 with x0-relative stores to a reserved save area in the first 2 KB *(inferred)*.

### W1-03: trap state at HALT → RUN and chip reset is undefined

**Severity:** issue. **Decisions:** 1.1, 1.2, Q4. **Spec:** 4.11 (S794), 4.13 (S941), 6.1 (S1729).

HALT → RUN resets the PC and x1–x31 only (S1729). Nothing says what `mtvec`, `mstatus`, `mie`, `mepc` or `mcause` hold after it or after chip reset, and RISC-V leaves `mtvec`'s reset value to the implementation. This matters in three places:

- **The loader's `ebreak`.** The resident loader runs first after every RUN and ebreaks on an unknown command (`wip/sort/program/sw/loader.S`:6, :32). Under 1.2 that `ebreak` traps, before any code has set `mtvec`. So does any bus error in the loader.
- **Errors that survive into RUN.** These now interrupt the core as soon as interrupts are enabled, so the loader's "check `ERRORS`" step (S941) must run with interrupts off.
- **Overlays.** The handler must stay resident across overlay swaps (6.5), like the loader *(inferred)*.

**Suggested resolution:** HALT → RUN and chip reset set `mtvec` to a fixed resident handler address (next to the loader, which 5 moves anyway, D202) and clear `MIE` and `mie`. The loader enables interrupts after it has checked `ERRORS`. Add these to Q4's "what HALT clears".

### W1-04: C&C 1's handler returns, but its interfaces aren't interrupt-safe

**Severity:** issue. **Decisions:** 1.5/15 (C&C 1 takes the interrupt for every unit's error), 7, 11, 15, 16. **Spec:** 4.11 (S784–788), 4.12 (S886), 4.18.

A tile's handler never returns, but C&C 1's must: other units' errors are routine for it. Several of C&C 1's interfaces are multi-access sequences with hidden state, and an interrupt between two accesses breaks them if the handler uses the same interface:

| interface | what tears |
|---|---|
| **Send port** (`ROUTER_HDR`, `ROUTER_ADDR`, `ROUTER_SET_W0`–`W2`, the send stores) | the handler's packet takes the main code's half-written words; each send clears the W words and advances `ROUTER_ADDR` |
| **Request port, sending** (`REQUEST_SET_W0`–`W2`, then `REQUEST_SEND`) | the same, for requests |
| **Request port, receiving** (`REQUEST_GET_W0`–`W2`, then `REQUEST_RECEIVE`) | if the handler pops reports, the main code's next `REQUEST_RECEIVE` pops a different request from the one whose low words it read: a torn request |
| **`EVENTS` loads** | they consume (S886); a handler load can take an event the main code is about to `WFI` for |
| **`RUN_LINES_0`–`2`** (R/W, D447) | a read-modify-write in the main code, interrupted by a handler that halts a unit, writes the old bit back and **re-runs the failed unit** |
| **`DMA_HDR` and `DMA_ADDR`** of C&C 1's peripheral | the same as the send port |

Two more effects:

- **The sequencer's `WFI` returns early.** RISC-V sets `mepc` past a `WFI` when it takes an interrupt during it *(inferred from the RISC-V privileged spec; Tony to confirm)*. After `mret`, the sequencer's `WFI` for "all READY" or the watchdog therefore returns early, so every C&C 1 wait must re-check its condition in a loop.
- **A nested exception is fatal.** A synchronous exception inside C&C 1's handler overwrites `mepc`/`mcause`, and the interrupted code can't be resumed.

**Suggested resolution:** a C&C 1 software rule: the handler reads `ERROR_LINES`, writes `RUN_LINES` and data memory, and nothing else. Report collection and logging happen in the main loop. Alternatively, the main code disables `MIE` around each sequence. Add set/clear aliases for `RUN_LINES` (or make halting a write-1 pulse) so no read-modify-write is needed. Record that a nested exception in C&C 1's handler is a C&C 1 failure.

### W1-05: Q3 is stale and its suggested answer can't apply to C&C 1

**Severity:** issue. **Decisions:** 1.7 (D101–103), 10 (D334–338), 15 (D458–460), Q3 (D595). **Spec:** 4.13 (S946), 4.17.7 (S1065), 4.19.11 (S1567, S1577).

Q3 has three problems:

- **It names a unit that is gone.** It asks what stops "the peripheral and the sender". The sender is gone: output is now each peripheral's DMA (10, D334), so the question is about the peripheral and its DMA.
- **The suggested answer would stop C&C 1's host I/O.** The suggestion is that the hardware stops the peripheral on taking the interrupt. C&C 1 takes that interrupt for every unit's error, and its peripheral is Ingress/Egress and the UART (D438). Stopping it on each trap would stop host I/O whenever a tile fails. The same applies to C&C 2 and C&C 3 only if they ever return from a handler. A stop on trap must therefore exclude C&C 1, or be keyed on something other than taking the interrupt.
- **It leaves out the rest of the tile.** 4.13 says the engines and the router keep running when the controller stops (S946). After a trap, the address generator, the GATHER engine, the read port and the send port's holding register all carry on.

On the prompt's question of whether anything keeps sending from a failed tile:

- an address-generator run in progress, and a peripheral DMA transfer in progress, both run to their end;
- a DMA waiting for data its controller would have released waits forever and sends nothing;
- every such run is bounded, so no hardware sends without limit;
- the only unbounded source is a retrapping handler (W1-02, first row).

**Suggested resolution:** restate Q3 as "the peripheral and its DMA, and whether the tile's own engines stop too". If the hardware stops them, key the stop on the handler's parking, for example a `PULSE_` the handler fires before its `WFI` loop, with HALT as the backstop. Do not key it on the trap. Note the consequence for 4.19.11: the replayer's invalid expansion "stops the pass so the controller loop exits" (S1577) no longer has that purpose, since the controller now traps.

### W1-06: recovery halts break invariant 7 and leave stale traffic

**Severity:** issue. **Decisions:** 15 (D449), 3 (D157), Q4. **Spec:** 6.1 (S1735), 6.6 (S1833), 9 (S2141).

Invariant 7 says no packet is in flight when the RUN/HALT line changes [built]. 6.1 (S1735) and 6.6's pacing rule (S1833) rely on it. Per-unit recovery (15) halts a failed tile in the middle of a phase, with its partners' traffic in flight by definition, so meshsim's built check fires on every recovery. The traffic then lands twice over:

- **In HALT:** as unexpected arrivals, which is W1-01.
- **In the next RUN:** if C&C 1 runs the tile again before the mesh has drained, the failed phase's packets land after the edge, in the new run. This is the "hold before RUN" left open in Q4 (D462). Here it is a correctness requirement, not a tuning parameter.

**Suggested resolution:** restrict invariant 7 to the normal sequence, and give meshsim a recovery-halt exemption. Define the hold before RUN as "until every unit that was exchanging with the halted one is itself halted or idle". Write into Q4 that the hold has a correctness role.

### W1-07: READY no longer means a tile is healthy

**Severity:** issue. **Decisions:** 1.4 (D68), 15. **Spec:** 6.2 (S1740), 6.3 (S1762, S1767).

READY is automatic on `done` & "command landed" (S1767), and `done` is forced to 1 in HALT (S1762); boot depends on this (S1740). After C&C 1 halts a failed tile, the next broadcast command lands in it and **its READY fires**. A tile that failed between `done` and GO (D68) parks with `done` = 1, and fires READY likewise.

So C&C 1's sequencer can collect all 64 READY bits and send GO with a tile halted or parked. Its partners then wait for its traffic until the watchdog fires, and C&C 1 never learns which tile was the cause. The error line can't tell it either, because halting dropped it. The watchdog and READY bits no longer identify a failed tile (6.6, 4.13's "What earns a condition", S944), and C&C 1 must keep its own record of failed units.

This also leaves the **recovery policy** undecided. After a failure, does C&C 1 stop issuing GOs and report to the host, or continue with the remaining tiles? Per-unit halt (D449) suggests the second, but no mesh program described in the spec runs with a tile missing.

**Suggested resolution:** decide the policy (owner question 1). If C&C 1 aborts, say so in 6.6: on any unit error it issues no further GO. Remove "the watchdog and READY bits identify it" from 4.13 and 6.6.

### W1-08: halting a failed tile cuts off its reports

**Severity:** issue. **Decisions:** 1.4 (D62–67), 1.5, 15, Q2, Q4. **Spec:** 3.3 (S221), 4.18 (S1131), 6.1 (S1727).

The error line arrives at C&C 1 at a fixed latency, which is short *(inferred: tens of cycles)*. The reports take much longer *(derived)*:

- the handler's roughly 50 loads and 17 × 4 stores: 150+ cycles at 2-cycle register loads;
- at least 17 × 8 = 136 cycles at the root (S221);
- plus `reqbus_latency`.

If C&C 1 halts on the line, HALT stops the core mid-handler. The reports already in the request FIFO still drain (S1131, S1727), but the rest are never sent. C&C 1 cannot tell a cut-off report set from a complete one, because 1.4 gives each report a sequence number but no count or end marker.

The planning notes (section 7) also list "the request FIFO" among the things HALT might clear. That would discard reports already queued.

**Suggested resolution:**

- Mark the last report, or put the total in the first.
- Decide whether C&C 1 waits for the last report, with a timeout, before halting. This is owner question 2.
- Keep 4.18's rule that HALT does not touch the request FIFO, and close that option in Q4.
- Q2's "sustained" threshold must exceed whatever wait is chosen (W1-16).

### W1-09: many tiles failing at once saturates the request bus

**Severity:** issue. **Decisions:** 1.4. **Spec:** 3.3 (S218–221), 4.18 (S1131).

One bad broadcast fails all 64 tiles in the same cycle, so the worst case is the normal case for a broadcast bug. The arithmetic *(derived)*:

- 64 × 17 = **1,088 reports** for C&C 1;
- the root forwards one request every 8 cycles for the whole chip (S221): **at least 8.7K cycles**;
- C&C 1 pops each report with three `GET` loads and one `RECEIVE` *(inferred: about 8–16 cycles at 2-cycle loads)* plus storing it, so the real figure is more like 10–17K cycles;
- C&C 1's receive queue is 16 deep, so the root's credits for target 0 run out almost at once.

During that time, head-of-line blocking inside each row (S219) holds every other request from those rows behind the reports, the memory system's included.

There is a second, worse case. A tile's request for a C&C 2 or C&C 3 that is halted or failed holds its row node indefinitely: HALT keeps the receive queue (S1131), and credits return only on a pop (S218). **Every report from that row to C&C 1 is then blocked**, so the report path depends on the health of every other target.

1.5's error lines remain the backstop, and they still work in both cases.

**Suggested resolution:**

- State that reports are best effort, and that the error lines are what C&C 1 relies on.
- Decide whether halting a receiver returns its credits or flushes its queue. That belongs in Q4, as "what HALT clears" for a receiver.
- Consider having the handler send a short report first (`ERRORS`, `mcause`, `mepc`) and the rest after.

### W1-10: C&C 1's own errors are permanent, and nothing resets its peripheral

**Severity:** issue. **Decisions:** 1.5 (D83–87), 15 (D438, D449, D451–452). **Spec:** 4.13 (S919, S941), item 102 (S2420).

C&C 1 never enters HALT, since bit 64 is unused (D449), so its sticky `ERRORS` and its peripheral's sticky bits clear only at chip reset. That has four consequences:

- **No further interrupts.** Any C&C 1 error keeps its hard interrupt asserted, so (W1-02) it takes no further unit-error interrupts. It also keeps the pin high. That matches 1.5's intent that C&C 1's failure goes to the host, but it makes trivial mistakes fatal, such as a `REQUEST_RECEIVE` on an empty queue (receive underflow, [14]).
- **Line noise becomes a C&C 1 failure.** The UART now lives in C&C 1's peripheral, and its errors join that peripheral's sticky `ERRORS` (D452). v1.1 made UART overrun and framing errors write-1-to-clear status bits (item 102, S2420). They are not software errors under 4.13's principle (S919): they come from the line or the host. As peripheral `ERRORS` they would turn line noise into a permanent C&C 1 failure.
- **Nothing halts C&C 1's peripheral.** "Halting a controller halts its peripheral" (D459) has no case for C&C 1. Ingress/Egress cannot be reset short of chip reset.
- **The host's recovery path is undefined.** Nothing says how the host halts, resets or reloads C&C 1.

**Suggested resolution:**

- Keep the UART's line errors as status bits, not `ERRORS`.
- Decide whether C&C 1 needs a way to reset its own peripheral, such as a pulse in its window.
- Specify the host's reset path for C&C 1 (owner question 4).

### W1-11: `ecall`, `ebreak` and fetch faults have no `ERRORS` bit; code relies on `ebreak` halting

**Severity:** issue. **Decisions:** 1.2 (D48–53), 1.5 (D74), 18 (D547–565). **Spec:** 4.11 (S750, S773).

1.2 makes `ecall` and `ebreak` trap, but 18's `ERRORS` has no bit for them. Nor does it have one for an instruction fetch outside program memory, or for a jump to a target that isn't 4-aligned, which RISC-V faults without C. 4.11's bus-error list covers loads and stores only (S773).

These traps therefore raise no error line. If the handler fails, the tile is silent, against 1.5's "it still reports a tile whose … handler has itself failed" (D75). It is also undecided whether `ecall` and `ebreak` are errors at all, now that they can be handled.

Code that depends on `ebreak` halting the controller:

| file | use |
|---|---|
| `wip/sort/program/sw/loader.S`:6, :32 | `ebreak` on an unknown command |
| `wip/sort/program/sw/sort.h`:54 | `CMD_EXIT`: main returns and the core ebreaks |
| `wip/meshsim/src/Controller.java`:16–25, :196–198 | halts on `ecall`/`ebreak` (`HALT_ECALL`, `HALT_EBREAK`) |
| `wip/meshsim/test/Mach.java`:63 (`allEbreak`), `RecipeTest.java`:45, :131, `StageB.java`:131, `RequestBusTest.java`:196 | detect a program's end by its `ebreak` halt |

**Suggested resolution:** map every `mcause` exception code to an `ERRORS` bit. Bus error and illegal instruction could take the fetch and alignment cases, or add one "trap" bit for `ecall`/`ebreak`. Give meshsim a simulator-only exit, such as the existing `DebugHook` or a print register (Q12), and list the code changes in the spec part.

### W1-12: the `done` gate wakes a parked handler on GO

**Severity:** nit. **Decisions:** 1.4 (D66–69). **Spec:** 4.12 (S890).

1.4 says the parked handler, with `EVENTS_MASK` and `EVENTS_REQUIRED` at 0, can be woken by no event and no GO (D66). From the `done` pulse until GO, though, the wake term is GO pending, whatever the mask holds (S890). A tile that fails between `done` and GO (D68) is therefore woken by GO once.

The handler's loop puts it back in `WFI`. After GO the gate is open, the mask is 0 and it stays asleep. It is harmless only because the handler loops.

**Suggested resolution:** reword D66 to say the loop, not the mask, keeps the handler parked.

### W1-13: timing and reset of the error and run lines

**Severity:** issue. **Decisions:** 1.5 (D74), 15 (D447–450). **Spec:** 2 (S76–79), 5 (S1706), 6.1 (S1722), 6.6 (S1833), 10 (S2200).

| gap | why it matters |
|---|---|
| **No parameter for the error line's fixed latency.** Section 10 and the timing summary have `run_latency` only | After C&C 1 clears a unit's `RUN_LINES` bit at *t*, the unit's bit in `ERROR_LINES` falls no earlier than *t* + `run_latency` + the error-line latency + the peripheral's sampling. Under a level interrupt (W1-02), a C&C 1 handler that returns sooner retraps on the same unit |
| **`RUN_LINES_0`–`2` have no stated chip-reset value** | It must be 0, all halted, to keep 6.1's "chip reset puts every tile in HALT" (S1722). Who releases C&C 2 and C&C 3, and when, is unstated |
| **Boot takes at least two stores** (`RUN_LINES_0`, then `_1`), so tiles 32–63 leave HALT at least a cycle after tiles 0–31 | Harmless, since GO synchronises everything after it *(inferred)*. But 6.6's "wait `run_latency` + 1 after a RUN/HALT store" (S1833) now applies to the last store |
| **The per-unit lines' latencies** | Whether they must all equal `run_latency`, as the single line's did (S76), is unstated |
| **An error raised in the cycle of the HALT edge is cleared** (the clear wins, 4.13) | A unit C&C 1 halts for another reason, such as an uncoordinated write (6.2), loses an error raised in that cycle without trace. This is acceptable but should be written down |

**Suggested resolution:** add an error-line latency parameter and the handler rule "return only when the halted units' `ERROR_LINES` bits have fallen". State `RUN_LINES` = 0 at chip reset, and give the per-unit lines one latency.

### W1-14: C&C 2 and C&C 3 have no boot handshake

**Severity:** question. **Decisions:** 15 (D441–449). **Spec:** 6.2 (S1737–1744), 6.3.

6.2's boot ends with a marker and READY (S1740), and READY lines exist for the 64 mesh tiles only (S78). C&C 2 and C&C 3 are halted at chip reset *(inferred)* and loaded by someone over some path (item 82). Nothing says how C&C 1 knows their image has landed before it raises their RUN line, or whether in-order delivery from C&C 1's port is enough.

**Suggested resolution:** owner question 5, to settle with the endpoint map (item 82).

### W1-15: Q4 is missing several items

**Severity:** issue. **Decisions:** 3 (D142), 15 (D462), Q4 (D596). **Spec:** 4.18 (S1131), 6.1.

Q4's remaining list is "what HALT clears, what keeps running, the hold before RUN". These decisions add items it doesn't name:

- the trap CSRs and the interrupt enables (W1-03);
- the K-stage delay line, flushed at the edge (D142): when packets accepted in HALT decrement relative to the edge, and that they set unexpected arrival there (W1-01);
- a receiver's receive queue and its root credits (W1-09);
- the request FIFO, which must keep draining (W1-08);
- the hold's correctness role (W1-06).

There is also a detail: the `REQUEST_SET_W0`–`W2` registers are kept across HALT → RUN as configuration registers (S1131). A handler halted between its `SET_W` stores and `REQUEST_SEND` leaves its words for the loader's first request, which is harmless only if every request writes all four words.

**Suggested resolution:** add these items to Q4.

### W1-16: the pin's "sustained" threshold (Q2) depends on unsettled policy

**Severity:** question. **Decisions:** 1.5 (D83–87), Q2 (D594).

The pin is high while C&C 1 has any error, the live unit-error bit included. It is therefore high for the whole of a normal recovery:

- under W1-08's wait-for-reports, a few hundred cycles per tile;
- in a mass failure (W1-09), about 10⁴ cycles;
- under W1-01, indefinitely.

Q2 can't be settled before those are.

An alternative: drive the pin from C&C 1's errors other than the unit-error bit. The pin then never pulses in normal recovery, and the host needs no threshold. A C&C 1 that hangs without an error would then need the host's own watchdog.

**Suggested resolution:** owner question 3.

### W1-17: smaller points in the handler's description

**Severity:** nit. **Decisions:** 1.4 (D62–64), 10. **Spec:** 4.11 (S773).

- **The state list is out of date.** It still lists `PERIPH_STATUS_0`–`7` (D63). Under 10 the handler reads each present peripheral's window, so the 50-word figure grows by roughly ten words per peripheral.
- **An absent peripheral's window would fault the handler.** Reading that window is an unmapped access, a bus error (S773), inside the handler. The handler must check `PARAMS` [2:0] first.
- **The handler must not consume events.** It should read `EVENTS_PEEK` at both levels, not `EVENTS`, which consumes (D316–317).

**Suggested resolution:** state these in the handler's description, in 4.13 or the software conventions.

## 3. Spec sections to update

| section | change |
|---|---|
| 1 (S59) | register owner prefixes: `TILE_`, `WAKE_`, `SENDER_` go (14) |
| 2, global nets (S75–79) | RUN/HALT becomes one line per unit from C&C 1's `RUN_LINES`. The error line becomes one line per unit at a fixed latency into `ERROR_LINES_0`–`2`, replacing "ORed into one sticky error bit". Add the C&C pin |
| 2.1 (S83–96) | endpoints 65 and 66 become C&C 2 and C&C 3 (15), not the memory system; add 72–74, the engines (with item 82) |
| 4.11 (S746–750, S773) | CSR instructions are legal for the implemented CSRs; traps replace halts; the trap CSRs' reset and HALT → RUN values (W1-03); the fetch and alignment faults (W1-11) |
| 4.12 (S829, S892) | "a halted tile counts nothing" goes (3); unexpected arrival in HALT; `WAKE_` renames and the new bit numbers (14, 16) |
| 4.13 (S902–946) | rename to errors; errors raise the hard interrupt; "Setting a condition does not stop the machine" and the halt sentence (S946) go; "detect new conditions in a phase" (S943) is obsolete, since any error traps; the watchdog-and-READY claim (S944) needs rewording (W1-07); the handler and parking; the new table (18) |
| 4.17.7 (S1063–1066) | "a halted controller stops them" goes; what stops the peripheral and its DMA on a trap (Q3, W1-05); the sender goes (10) |
| 4.17.6, 4.17.9 | the peripheral conditions [13:11] become live per-peripheral bits (10, 18); the sender is replaced by the peripheral's DMA |
| 4.18 (S1131) | HALT and the request FIFO (W1-08); a receiver's queue and credits in HALT (W1-09) |
| 4.19.11 (S1559–1577) | "the controller halting itself" goes; the purpose of "invalid expansion stops the pass" is revisited (W1-05); the conditions become the peripheral's `ERRORS` |
| 5 (S1706) | per-unit RUN/HALT; the error-line latency (W1-13) |
| 6.1 (S1720–1735) | per-unit lines; chip-reset value; the "controller halted by a bus error…" paragraph (S1730) goes; the trap state at HALT → RUN; the "no packet in flight" rule (W1-06) |
| 6.2 (S1737–1744) | boot under per-unit lines (two or three stores); C&C 2 and C&C 3 (W1-14); errors during boot (W1-01) |
| 6.3 | READY from halted or parked tiles (W1-07) |
| 6.6 (S1815–1840) | C&C 1 reuses the controller with traps and one peripheral; error and run lines in its window; REPORT, the settle and the replies go; the error bit as a `WFI` wake source (S1835) becomes the interrupt; the watchdog's home; the C&C pin; the host's recovery path |
| 6.7 (S1888) | the "C&C error flags" (dropped requests, write-stream checks): whether they are C&C 1 `ERRORS` and so interrupt and raise the pin, or separate (owner question 6) |
| 9 | invariant 7 (W1-06); invariant 15's "C&C's error bit" becomes the lines; invariant 9's HALT case (3) |
| 10 | `run_latency` per unit; an error-line latency parameter |
| 13 | items 11 and 14 (C&C's register block and wake sources) are now partly answered by 15; item 55's rationale (W1-05); item 91's "a halted tile counts nothing" (3); item 102's UART errors (W1-10) |

## 4. Questions for the owner

1. **Recovery policy.** After a unit's error, does C&C 1 abort the job (no further GO, halt everything, report to the host), or try to continue? This decides W1-06 and W1-07 and the purpose of per-unit halt.
2. **Halt first or collect first?** Does C&C 1 halt a failed tile when its line rises, or wait for its reports (with a timeout)? Should reports carry a count or an end marker (W1-08)?
3. **The pin.** Should it include the live unit-error bit, and so pulse in every recovery, or only C&C 1's own errors (W1-16)?
4. **C&C 1's own failure.** How does the host halt, reset or reload C&C 1? Is any C&C 1 error meant to be fatal, receive underflow and the UART's line errors included (W1-10)?
5. **C&C 2 and C&C 3.** How are they booted and released, with no READY lines (W1-14)?
6. **The memory system's and the request bus's "C&C error flags"** (6.7, S1888). Do they interrupt C&C 1 and raise the pin?
7. **`ecall` and `ebreak`.** Are they errors, with an `ERRORS` bit and the line, or legitimate traps software may use (W1-11)?

For Tony, added to Q1 (W1-02, W1-03, W1-04):

- Is the hard interrupt level-sensitive, and gated by `mstatus.MIE`/`mie`?
- Does a pending, enabled interrupt release `WFI` with `MIE` = 0?
- After an interrupt taken in `WFI`, where does `mepc` point?
- What are the CSRs' reset values, and is `mtval` implemented?

## 5. For other workers

- **WORKER2:** section 3's "HALT no longer suppresses counting" changes 4.8 and 4.12. W1-01 depends on when a packet accepted near the HALT edge decrements relative to the K-stage flush (D142).
- **WORKER2:** 15 gives endpoint 65 to C&C 2 while 2.1 and 6.7 give it to the memory system (served at 64). The packet path's dst rules follow whichever wins.
- **WORKER3:** C&C 1's READY bits, GO, watchdog and receive-FIFO registers (6.6's list) have no home in 11 or 15. C&C 1's wake sources (all READY, packet received, watchdog) need bits, presumably in its peripheral's `EVENTS`.
- **WORKER3:** 15 gives no word offsets for `ERROR_LINES_0`–`2` and `RUN_LINES_0`–`2` in C&C 1's window. Set/clear aliases for `RUN_LINES` are suggested in W1-04.
- **WORKER3:** the handler and the loader need a resident home and a save area under the 32 KB + 32 KB map (5), and `mesh.h`/start-up changes for `mtvec` (W1-03).
- **WORKER3:** `sort.h` `CMD_EXIT`, `loader.S` and meshsim's tests use `ebreak` as the program's end (W1-11).
