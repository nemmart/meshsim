# WORKER13 questions

Spec gaps, contradictions and missed differences found while writing `wip/spec/CHANGES-to-v1.25.md`. Each has a
proposed answer. Answered ones are kept, marked.

| # | question | where | proposed answer | status |
|---|---|---|---|---|
| Q1 | The replayer's type: 4.17.1 said "1 the replayer", against 4.17.4, 4.19 and 8.11 (4) | spec 4.17.1 | 4 (D46) | **answered**: fixed by WORKER10 (`b7794e9`); v1.2 erratum E1 |
| Q2 | The opcode renumbering was stated as "three lower" | spec 4.19.7 | the exact ranges, +6 / +4 / +3, 61 opcodes | **answered**: fixed by WORKER10 (`0b4ba20`); appendix 2.0 |
| Q3 | 8.1's general rule "a load takes 1 or 2 cycles" | spec 8.1 | 2 (block, windows) / 1 (table window, memories), D45 | **answered**: fixed; v1.2 erratum E2 |
| Q4 | Sections 11 and 12 call item 134 open; section 10's K row cites 4.7 | spec 10, 11, 12 | item 134 closed by D45; 4.8 | **answered**: fixed (`2a107ab`); v1.2 errata E5, E6 |
| Q5 | **4.12 still says "The load costs 1 or 2 cycles, fixed for the register on every tile"** (the wait helpers' load of `EVENTS`). Same staleness as E2, in v1.2 and in `wip/` | spec 4.12, "A wake consumes nothing" | "The load costs 2 cycles (a register-block load, 4.11), the same on every unit" | open: spec fix for WORKER10; v1.2 erratum (REPORT) |
| Q6 | **The 4.19 intro's "peripheral 1, type 1" in frozen v1.2** is fixed in `wip/` (CHANGES-v1.25's errata row) but `v1.2/ERRATA.md` E1 names only 4.17.1 | v1.2 spec 4.19 intro | add the 4.19 intro to E1 (or a new entry) | open: v1.2 erratum (REPORT) |
| Q7 | **D48 vs the reserved-word rule.** D48 (W4-06): a peripheral without a DMA reads 0 at words 8–9 and **a load of them is not flagged, only a store**. The spec (4.17.4, v1.2 and `wip/`) calls words 4, 8, 9 "unused (reserved)", and 8.1 / 4.17.4 say a reserved word's access is flagged by meshsim. So the spec, read literally, flags the handler's reads that D48 exempts (the 24-report set reads `DMA_HDR`, `DMA_ADDR` on every window) | spec 4.17.4, 8.1; D48 | 4.17.4: "A peripheral without a DMA reads 0 at words 8 and 9 and ignores stores to them, and 4 is a pulse that does nothing; meshsim flags a store, not a load (DECISIONS-v1.2 section 48)" | open: spec fix; v1.2 erratum (REPORT) |
| Q8 | **Item 111's text** in `wip/` still says "1 or 2 cycles, implementation-defined"; v1.25's status table says only "changed: extends to every peripheral window word". E3 lists it as a v1.2 erratum, corrected by the integrator, but `wip/` still has it | spec 13, item 111 | add "*(superseded by DECISIONS-v1.2 section 45: 2 cycles, 1 for the table window)*" to item 111, or mark it in the status table | open: spec fix |
| Q9 | `CHANGES-v1.2.md` says 2.1's "72–74 gone"; v1.1's 2.1 never listed 72–74 (73 was only in 3.3's target table and items 82/83) | CHANGES-v1.2 row 2.1 | harmless wording: "v1.1's proposed engine endpoints (73) are gone" | low; no action needed |
| Q10 | v1.1's own contradiction on the gather request's ordinal ([63:32] in 3.1's table, [51:32] elsewhere) | v1.1 3.1 | [51:32], as v1.2 says; noted in the guide (L9) so the RTL checks which it built | for the RTL team |
