# WORKER5 report: the machine spec, v1.2

**Deliverables** (all on `main`):
- `wip/spec/machine-spec.md`, now **v1.2**: one current description of the machine, written from
  `wip/planning/DECISIONS-v1.2.md` (sections 1–36, as of dd6c651) over v1.1.
- `wip/spec/CHANGES-v1.2.md`: what changed from v1.1, section by section, each item citing its decision.
- `WORKER5/QUESTIONS.md`: 21 questions; 5 ruled by the owner, 16 open with proposed answers.
- `WORKER5/NOTES.md`: working notes, the section map and the decisions digest.

Nothing outside `wip/spec/` and `WORKER5/` was changed.

## What I changed

Every section the log touches, rewritten to describe the machine as it is, not how it changed; what v1.2
removes is removed, not marked. In brief (the full list is `CHANGES-v1.2.md`):

- **Sections 1–3:** the 256-bit link word; the new **2.2, the 32-bit address map**; five modes; the counter
  field; the bad-packet table; C&C 1–3 as tile instances with their engines; the new nets and pins.
- **4.1–4.16:** natural alignment and the misaligned-access rules; no spanning; the `STREAM` layout; the
  read port; the traffic director with K, its formula and the delay line; waiting for the tables by events;
  the controller (CSRs, traps, 32 KB + 32 KB, the boot ROM, `ROUTER_HDR`/`ROUTER_ADDR`); four write
  counters and the renumbered `EVENTS`; `PARAMS`/`STATUS`/`ERRORS`, terminal traps and the handler's
  conventions; the fifth ports at tiles 0–2.
- **4.17 rewritten as Peripherals** (up to three, regions, the standard window, counted and live events,
  errors, the DMA); 4.17.7 a stub.
- **4.19, the replayer,** on the standard window, with **its low-water lines as counted credits** (depth 2,
  reset at `PULSE_RUN` and at each new expansion, returned per half queue consumed, unneeded bits masked by
  the loop), the controller loop rewritten, its map in byte offsets, ownership stopped/running, and its
  errors in its own window. The stream-length bound is corrected (about 4.3 Mbit).
- **4.20:** the UART inside C&C 1's peripheral.
- **Section 6:** 6.1 reset, errors and the toast pin (one watchdog; hard and soft reset from outside; what the
  reset does; no reset hold, with the reason); 6.2 the boot ROM exactly and the boot sequence; 6.6 the
  controllers and C&C 1's peripheral; 6.7 and 7 the memory system as C&C 2's peripheral, requests naming
  their counters, writes by tags.
- **Section 8:** complete register tables: the controller block, the common header layout, `PARAMS`/`STATUS`,
  `ERRORS`, the send port, the pulses, the counters, the read port, the table window, events and the request
  port, the standard window and the replayer's, the UART's words, **8.14 C&C 1's peripheral** and **8.15 the
  simulator-only words**; 8.12 a stub.
- **9:** every invariant a meshsim check, with its hardware counterpart named; 7 withdrawn; 19–26 new.
  **10, 11, 12:** parameters, the budget rescaled (≈ 0.52–0.61 mm² per tile), the control island. **13:** a
  status table for items 1–116 plus an inline note on each, and new items 117–133. **14:** names updated.

**The hardware/meshsim split** (D27) is stated once in the front matter and applied rule by rule: each rule
says **(hardware)** or **(meshsim)**, and section 9 names the hardware error bit, where there is one, beside
each invariant.

**Numbering:** v1.1's numbers kept; stubs at 4.17.7 and 8.12; new subjects at 2.2, 8.14, 8.15.

## Left *(Proposed)*, *(Open)* or *(Deferred)*

All are in section 13:
- **Proposed, from the log:** the table window's address (120); the handler's park as a plain loop; the
  bad-packet table's four proposed rows (132); `stage_delay` = 32; `rp_latency` = 2 (48); 128-bit staging
  (28); 65 as C&C 2 and the target numbering (82, 83).
- **Proposed by me** (the open questions): the replayer's window (123); register word numbers, `WATCHDOG`'s
  load, `LED`'s reset and the exit word (131); the small rules in 133 (misaligned `PTR` advancing,
  `ROUTER_ADDR` for mode 0 only, single-cycle program-memory loads, the read port's counter per entry, the
  watchdog counting cycles, `error_latency` 1 in simulation); the 32 KB banking (127); the handler's report
  set (122).
- **Open:** the CSR list and reset values (121, Q1); the soft reset's keep list (124); requests dropped at the
  root (126); the controller memories' area and timing (127); K's margin as a meshsim check (128, S1); a
  stream's source address (129, Q9); the reserved bits' storage (117).
- **Deferred** (D33): the sending peripherals' ports (118); the file system and the RW engine (119); the 1×4
  test chip and mesh size as a parameter (130); the host-interface words.

## For the recipes session (WORKER6)

I compared the spec with `wip/sw/mesh-v1.2-draft.h` (at 97e69de). It agrees on the controller block's
offsets, the exit and trace words, `PARAMS`/`STATUS`/`ERRORS`/`EVENTS` bits, the common header, the
`STREAM` layout, C&C 1's peripheral words and event bits, and the replayer's event bits and byte map.
Differences and things to know:

1. **The replayer's words 32 and 33:** the spec names them `REPLAYER_STATUS` and `REPLAYER_OPCODE` (v1.1's
   original names); the draft has `RUN_STATUS` and `PRESENTED`. Both are proposed (W5-09); one should be
   picked before the header is generated.
2. **The UART's words** follow D28's order in both: 42 TX, 43 RX, 44 divisor, 45 status.
3. **`ROUTER_ADDR` advances only for mode 0** in the spec *(Proposed, W5-03)*; the draft's comment says "by
   its size" without a mode. Recipes that send modes 1–4 through the send port shouldn't rely on either.
4. **Counter conventions:** the spec leaves the choice to software; its two examples happen to match the
   recipes' 1:1 convention (the search loop on counter 2, 4.3; the replayer's refills on counter 4, 4.19.9).
5. **The replayer's loop** (4.19.9) follows W3-REVIEW-2 §5: wake on `EVENTS` [10]; claim with one load of the
   replayer's `EVENTS` (one credit per set, masked queue bit); mask out a queue with nothing left; squelch
   by clearing `EVENTS` [10] while a refill streams; wait on the refill counter's done; re-check after every
   wake (spurious wakes are allowed).
6. **The boot ROM** (6.2) is written out exactly; the tile branch pulses `done` and loops `WFI` + `EVENTS`
   until GO, the C&C branch sets the mask to [6] and restores it; both then load slot 0, then slot 1, and
   jump. The ROM never writes data memory.
7. **The handler** (4.13): reports `PARAMS`, `STATUS`, `ERRORS`, `EVENTS_PEEK`, the counters, `mepc`/`mcause`/
   `mtval`, x1–x31 and each present peripheral's words 0–9; checks `PARAMS` [2:0] first; reads only
   `EVENTS_PEEK`; writes every word of each request. The ops and order of the fixed report set are item 122:
   the recipe may be where they are first written down.
8. **Uncounted traffic** is commands and boot overlays only; big-SRAM data is always counted (W5-15), which
   matters for the boot and staging recipes.

In the second round I'll check `RECIPES.md`, `CONVENTIONS.md` and the draft header against the spec.
