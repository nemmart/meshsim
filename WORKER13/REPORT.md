# WORKER13 report

## What was delivered

- **`wip/spec/CHANGES-to-v1.25.md`**: one change guide for the RTL engineer, covering two paths to v1.25, from v1.1
  (what the RTL implements) and from v1.2 (the frozen waypoint). It also has a note for a reader on `v1.2.1`.
  - **Matches spec commit `c2de4ae`.**
  - Organized by hardware unit, in v1.25 spec order: link word and network (L), address map (A), the tile's engines (T),
    the controller and registers (C), peripherals (P), the replayer (R), control/reset/boot (K), C&C 1–3 (X).
  - Every entry gives v1.1, v1.25 with its spec section, kind, **step** (v1.2 / v1.25 / both) and why (decision cited).
    "Both" entries also give the v1.2 state.
  - The replayer section uses the split the walk found: v1.1 → v1.2 changed only its **interface**, and v1.2 → v1.25
    only its **rows and tables**. R1–R14 are v1.2, R19–R42 are v1.25, and R3 and R15–R18 are both.
  - Section 1: overviews for both readers, plus "removed outright" and "entirely new" tables.
  - Section 3: register and field tables with v1.1 / v1.2 / v1.25 columns. They cover the block, `PARAMS`/`STATUS`,
    `ERRORS`, `EVENTS`, the header registers, the standard window, C&C 1's words, and the replayer's words, region,
    expansion word and output row. The opcode renumbering is summarized and points to appendix 2.0.
  - Section 4: parameters, invariants, and the new hardware and meshsim checks.
  - Section 5: everything the change documents don't say, the v1.2 errata it relates to, and CLEANUP CL1–CL5 with
    their steps.
- **`WORKER13/NOTES.md`**: the section-by-section walk (pass A: v1.2 → v1.25 diff; pass B: v1.1 → v1.2, every section).
- **`WORKER13/opcode-map.txt`**: every row opcode's v1.2 → v1.25 code, generated from the two appendices.
- **`WORKER13/QUESTIONS.md`**: Q1–Q10. Q1–Q8 are answered by spec fixes and errata; Q9 and Q10 are low priority (below).

## Findings, and where they went

- **`CHANGES-v1.2.md` stops at decision 44.** D45 (load latencies), D46 (peripheral types, replayer 1 → 4), D47, D48
  and D49 (K is the implementation's) are in the spec without an entry. The guide carries them with their step (v1.2).
  They are erratum E4, and the integrator's fixes E1–E3, E9 and E10 follow from them.
- **Spec errors fixed in `wip/`** (by WORKER10 and the integrator):
  - 4.17.1's "1 the replayer";
  - 4.19.7's "three lower" renumbering (actually +6 / +4 / +3; 61 row opcodes change);
  - 8.1's and 4.12's "1 or 2 cycles";
  - item 111;
  - 11/12's "item 134 open";
  - section 10's K citation;
  - D48 unstated.
- **Unstated changes the guide adds:**
  - one K for every destination;
  - a write to an absent peripheral is now uncounted;
  - v1.1's own ordinal-bits contradiction, resolved to [51:32];
  - the read-port entry grows to 43 bits;
  - a misaligned `PTR` still advances;
  - `STATUS.done` resets to 0;
  - reserved words read 0 instead of raising a bus error;
  - the UART's DIVISOR and STATUS words swap order;
  - the DMA's count is 12 bits;
  - C&C 1–3 have 2 MB of big SRAM;
  - a replayer's misaligned write moves to the director;
  - PEXT with a zero mask gives 0;
  - the template index's entry format.

## v1.2 errata

These are errors in frozen v1.2 itself, which I reported to the integrator. I did not edit `v1.2/`. All four are now
in `v1.2/ERRATA.md`:

| where in `v1.2/` | the error | correct | now |
|---|---|---|---|
| `spec/machine-spec.md` 4.12, "A wake consumes nothing": "The load costs 1 or 2 cycles, fixed for the register on every tile (4.11)" | the stale latency rule, as E2 | the load of `EVENTS` costs **2 cycles** (a register-block load; DECISIONS-v1.2 section 45) | **E10**; fixed in `wip/` (`c2de4ae`) |
| `spec/machine-spec.md` 4.19 intro: "peripheral 1, type 1" | the replayer's type, as E1 (which named only 4.17.1) | **type 4** (section 46) | **E1 extended**; fixed in `wip/` |
| `spec/machine-spec.md` 4.17.4: "A peripheral without a DMA leaves words 4, 8 and 9 unused (reserved)", with 8.1's flagging of reserved accesses | contradicts section 48 (W4-06): words 8–9 read 0, and a load is not flagged, only a store | words 8–9 read 0; meshsim flags a store, not a load | **E9** (CL5); stated in `wip/` (`88a46bd`) |
| `spec/machine-spec.md` 4.19.11: "The other two conditions leave the replayer running. The other two errors leave the replayer running." | duplicated sentence (editorial) | one sentence | **E11**; fixed in `wip/` |

The final read-through found no further error in v1.2.

## Read-through

I checked these sections of the guide against the spec at `c2de4ae`:
- the register block offsets (8.1);
- `PARAMS` and `STATUS` (8.2) and `ERRORS` (8.3);
- `ROUTER_HDR` and `ROUTER_ADDR` (8.4);
- the read port (8.8) and the table window and `STREAM_*` layouts (8.9);
- `EVENTS` and the request port (8.10);
- the UART word order (8.13) and C&C 1's words (8.14);
- the boot ROM's five steps and the boot sequence (6.2);
- the replayer's map after CL1, the CL2 note and D48's wording (4.19.10, 4.17.4).

All of them match. The v1.2 → v1.25 entries come from a complete mechanical diff (pass A). The v1.1 → v1.2 entries come
from a section-by-section diff of every section (pass B).

## Left open (not blocking)

- **Q9:** `CHANGES-v1.2.md` says 2.1's "72–74 gone", but v1.1's 2.1 never listed them. This is wording only; no action
  needed.
- **Q10:** v1.1 gave the gather request's ordinal as both [63:32] and [51:32]. The RTL team should check which one it
  built (guide L9).
- **The guide's stamp.** A later spec change to the hardware needs a new entry and a re-stamp. The spec's open items
  that an RTL engineer must track (117–124, 126, 129, 140, 44–48) are listed in `NOTES.md` under section 13.
- **CL4** (the StageB time limit in `wip/`) is still WORKER11's. It is a test harness, so the guide has no entry for it.
