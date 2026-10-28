# Decisions toward v1.2

One entry per decision, newest section last. Labels follow the repository's convention: O owner decision,
T confirmed by Tony (controller core). The spec session works from this log; `spec/machine-spec.md` stays
the authority once an entry is written into it. Starting point: `PLANNING-NOTES-2026-10-27.md`.

---

## 1. Condition handling (2026-10-28)

Closes planning notes section 7, "Condition handling", and to-do item 3.

### 1.1 The controller core (T)

- Tony's RV32 core has **two inputs**: a **hard interrupt**, which takes a trap to the handler, and a
  **wakeup**, which resumes the core from `WFI` without a trap.
- The wakeup input is **level-sensitive**: if it is high when the core reaches `WFI`, the core does not
  sleep, even if it went high before the `WFI`.
- The hard interrupt **takes the trap even while the core is in `WFI`**.
- The core implements the **CSR instructions and the machine-mode trap CSRs**, but **no privilege modes**:
  machine mode only, without the CSRs for privilege levels.
- **Open (T):** the exact list of implemented CSRs, to define which CSR instructions are legal and which
  still raise the illegal-instruction condition.

### 1.2 Spec changes to the controller (O; 4.11)

- **CSR instructions become legal** for the CSRs the core implements (1.1). 4.11 currently makes every CSR
  instruction illegal.
- **Bus errors, illegal instructions, `ecall` and `ebreak` trap to the handler** instead of halting the
  controller. 4.11 currently halts the controller on each.

### 1.3 Wake (O; 4.12)

- **The `WAKE_*` registers are unchanged.** The existing wake term (4.12's formula over `WAKE_EVENTS`,
  `WAKE_MASK`, `WAKE_REQUIRED` and the `done` gate) drives the core's wakeup input directly.

### 1.4 The handler (O; 4.13)

- **A condition raises the core's hard interrupt.** The handler reads the state (conditions, `TILE_STATUS`,
  counters, `mepc`/`mcause`/`mtval`, `PERIPH_STATUS_0`-`7`, x1-x31: about 50 words) and sends it to C&C 1
  as request-bus reports (about 17; header word: op, sequence number; three data words each).
- **Parking:** the handler then **clears `WAKE_MASK` and `WAKE_REQUIRED` and loops on `WFI`**, so no event
  and no GO can wake it. Only HALT recovers the tile (HALT → RUN resets `WAKE_MASK`, 4.12).
- **Conditions during the `done` gate:** the gate controls only `WFI` wakes. A condition raised between
  `done` and GO still takes the hard interrupt.

### 1.5 Error lines and the C&C pin (O)

- **Every tile has a dedicated error line to C&C 1**, driven by hardware from its conditions (no software
  involved), with a **fixed latency**. It still reports a tile whose request path is stuck or whose handler
  has itself failed.
- C&C 1 sees the lines as **three 32-bit words**: two for the 64 tiles (one bit per tile) and one for the
  C&C controllers and other non-tile units. Their bit assignment is part of the endpoint map (to-do item 11).
- **The three words fold into C&C 1's conditions**, and C&C 1's conditions drive its own hard interrupt, so
  C&C 1 is interrupted whenever any core fails, itself included.
- **The fold is live:** it follows the error lines; it does not latch. C&C 1's own conditions stay sticky,
  as everywhere.
- **The C&C pin:** an output pin on the ASIC, high while C&C 1 has any condition.
- **C&C 1's HALT halts everyone but itself.** Entering HALT clears the halted units' conditions (4.13), so
  their error lines drop, the fold clears and the pin goes low.
- **So the pin stays high only when C&C 1 itself has failed**, and the host steps in. The pin also pulses
  during normal recovery, so the host treats only a sustained high as a failure.
- **C&C 1's phase watchdog stays** as the backstop for a core that is stuck without raising a condition.

### 1.6 REPORT is withdrawn (O; 6.6)

- **The REPORT debugging command is removed.** It works only when a failed tile still reaches
  `wait_for_command()` and the mesh still carries its reply, which are the cases that fail. The handler's
  request-bus reports (1.4) and the error lines (1.5) replace it.

### 1.7 Spec text these decisions change

- **Section 2 (global nets) and 6.6:** the error line is now one line per tile at fixed latency into C&C 1's
  three words (1.5), replacing "ORed in a tree into one sticky error bit at C&C".
- **4.11:** CSR instructions and traps (1.2).
- **4.13** ("bus errors, illegal instructions, `ecall` and `ebreak` halt the tile's controller only"),
  **4.17.7, 6.1 and 4.19.11** ("a halted controller" stops the peripheral and the sender): with traps the
  controller no longer halts itself, so each needs rewording. What stops the peripheral is pending (Q3).
- **6.6:** REPORT removed (1.6). C&C reuses the tile controller design, so C&C 1 gets the same traps and
  can take its own interrupt (1.5).

---

## 2. The link word (2026-10-28)

Decided with Tony (O, T). Replaces 3.1's 169-bit word.

- **The link word is 256 bits on every router link**, fixed, so later field changes don't resize the
  links, FIFOs and route blocks again (102 bits in v1.0, 169 in v1.1).
- **Fields (181 bits):** payload 128, address 32, size 2, counter 2, mode 3, src 7, dst 7. **75 bits
  reserved:** sent as 0 and ignored by every receiver; FIFOs and route blocks carry all 256 bits without
  interpreting them. Bit positions to be laid out.
- **Cost:** router FIFOs about 50 % larger than at 169 bits, roughly +0.04-0.06 mm² per tile [estimate];
  256 wires per link direction, with the channel width to confirm in physical design.
- **Pending:** the 32-bit address (Q6) and the 3-bit mode's code assignment (Q7).

## 3. Counters (2026-10-28)

Decided with Tony (O, T). Replaces 4.12's four named down-counters.

- **The packet names its counter** in the link word's 2-bit counter field: **0 uncounted; 1, 2 and 3
  general-purpose counters**, assigned to whatever a program needs. Counting no longer depends on the mode.
- **A fixed delay K:** the traffic director accepts a packet, and its counter is decremented **K cycles
  later**, through a K-stage delay line carrying the counter id. K is large enough that the packet's write
  has reached its destination by then. **K = 8 for now** (a parameter).
- At most one packet per cycle leaves the director, so a counter takes at most one decrement per cycle.
  `mem_idle` and `cm_idle` drop out of the done rule: a counter at 0 means everything it counted is in place.
- **Unexpected arrival stays:** a counted packet for a counter already at 0 still sets the condition. A
  packet wrongly sent with counter 0 lands silently, a sender error like a wrong mode today.
- **What K must cover** (S1 below): port B and the peripheral are fixed at 2 cycles. For big SRAM, a write
  only has to have **entered its area's queue** within K: the queue is served in arrival order, so a read
  issued after the counter reaches 0 queues behind the write and cannot overtake it.
- **Follow-on:** the wake events' per-counter done bits ([3:0] today) become done events for counters 1-3;
  [9] counters done stays. HALT flushes the delay line with the counters.
- **Pending:** the counter field in every packet source (Q8).

---

## Big SRAM: to validate

| # | what | source |
|---|---|---|
| S1 | **A write reaches its area's queue within K = 8 cycles** of the director accepting it (input FIFO, pointer stage, a spanning write's second part). Tony estimates about 6. meshsim to flag any counter decrement before its write is queued | 3 |
| S2 | **Reads come back in order per requesting client** (address generator, GATHER engine), whichever areas they hit; read-port reads in any order | 4.1, 4.5 |
| S3 | The area macros: compiler numbers for a 128 × 32K area at 1 GHz, one access per cycle; the array's read latency; density and arrangement | spec item 1 |
| S4 | The area request-queue depth for the hardware (8 in meshsim) | spec item 2 |
| S5 | Queueing against the group-run timetable: the bound the 64 tokens and return queues must absorb, and the placement rules that keep each phase within one access per area per cycle | spec item 4 |
| S6 | The `PTR` and `HIST` macros: a write lands intact when port A reads the address port B is writing | spec item 5 |
| S7 | Write combining of consecutive 32- or 64-bit writes to one row | spec item 3 |
| S8 | The big-SRAM size per tile (4.25 MB tentative), set by the sumcheck memory squeeze | spec item 107 |

---

## Pending questions

A running list; each entry names where it came from. Resolved entries move into a decision section above.

| # | question | from | status |
|---|---|---|---|
| Q1 | Tony's exact list of implemented CSRs, to define which CSR instructions are legal | 1.1 | waiting on Tony |
| Q2 | How long a high on the C&C pin counts as "sustained" for the host | 1.5 | open |
| Q3 | What stops the peripheral and the sender when a condition traps: the hardware on taking the interrupt, or the handler? (Today a halted controller stops them at once.) | 1.7 | open; suggestion: the hardware, so a failed handler can't leave them running |
| Q4 | The HALT design: one line or separate lines for tiles and controllers, which peripherals stop, what HALT clears, the hold before RUN, per-tile halt | planning notes section 7 | open |
| Q5 | Should an area serve writes ahead of queued reads? Not needed for S1 if K covers only the path to the area queue | 3 | open; for Tony |
| Q6 | The link word's 32-bit address: what it covers | 2 | next topic |
| Q7 | The 3-bit mode: which of today's nine modes merge or go, now that counting is separate | 2 | open |
| Q8 | The counter field in every packet source: `STREAM` entries, the send port's header, the sender, the read port's setup, the GATHER engine's gather values, and memory-system requests | 3 | open |
