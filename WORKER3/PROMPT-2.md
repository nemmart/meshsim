# Worker 3, round 2: check the resolutions of your review, and update the migration inventory

Your first review (`WORKER3/REVIEW.md`, findings W3-01 to W3-23) was gone through with the owner. The
answers are in `wip/planning/DECISIONS-v1.2.md`. Since you reviewed, the design moved a long way, mostly
through WORKER1's and WORKER2's reviews: **no HALT state**, a **global reset** as the only stop, **boot
from ROM** (a marker starts each unit), **traps are terminal**, **natural alignment for every access**,
**byte offsets**, **four counters** with a 3-bit counter field, and the engines as their controllers'
peripherals. Check that the register and peripheral design holds together, and bring your migration
inventory up to date.

REPOSITORY AND RULES: as in `WORKER3/PROMPT.md`. Pull first. This prompt is the go-ahead to write and push
`WORKER3/REVIEW-2.md` and to keep using `WORKER3/NOTES.md`; change nothing else.

READ
- The log's "Where the design stands", then the whole log: almost every section has changed since round 1.
  Superseded points are marked in place. Pay most attention to 5 (the map, renumbered: 0x02 boot ROM, 0x03
  big SRAM, 0x04-0x06 peripherals), 7, 10, 11, 12, 14, 16, 18 and 20-30. **Section 28 (C&C 1's registers) is
  the integrator's proposal, made at the owner's request, and is to be reviewed: say what you would change.**
- Your own `REVIEW.md` and `NOTES.md`, and WORKER1's and WORKER2's reviews for context.

HOW YOUR FINDINGS WERE ANSWERED

| finding | answered by |
|---|---|
| W3-01 C&C 1's permanent errors | 21: UART line errors are status bits; C&C 1's own errors are fatal; 24: the error pin |
| W3-02 peripheral events | 10: counted events (the replayer's low-water lines are credits, depth 2) and live events |
| W3-03 `wait_tables()` | 17: `PULSE_DMA` clears DMA run done; helpers may poll `STATUS.rd_running` |
| W3-04 `ROUTER_ADDR` | 7: a no-increment bit, [0] |
| W3-05 mode 3 to C&C | 25: LPDDR writes through tag windows in the RW engine's region |
| W3-06 stale live bit | 10: spurious wakes allowed but not wanted; software re-checks |
| W3-07 `STATUS` positions | 18: renumbered compactly |
| W3-08 *d* | 12: translated, not copied |
| W3-09 NULL | 27: data memory word 0 reserved, meshsim flags writes |
| W3-10 reset PC, loader | 22: the boot ROM at 0x02000000 |
| W3-11 program memory | 5: load-only |
| W3-12 `mem_idle` | 17, 18: gone from the hardware |
| W3-13 bad packets | 26 |
| W3-14 standard fields | 10: standard bits and access rules |
| W3-15 rules for the windows | 10: spec-update items |
| W3-16 names | 14: one C struct per block (`tile.EVENTS`, `peripheral1.EVENTS`); "DMA" keeps both meanings |
| W3-17 two address formats | 5, 26, 29: bytes in packets, chunks in the engines |
| W3-18 C&C 1's registers | 28: the integrator's proposal, to review |
| W3-19 three counters | 30: four counters; `STREAM_COUNT`'s count is 16 bits |
| W3-20, W3-21, W3-22 | 29 |
| W3-23 Q12 | 23: log files through the file system |

WHAT TO DO
1. **Check each answer.** Mark W3-01 to W3-23 closed, partly closed or open, with a sentence of evidence.
2. **Review the register and peripheral design as it now stands**, as in round 1. Questions worth working
   through:
   - **The complete map:** the controller block (11, with 30's changes), the standard window (10), C&C 1's
     window (15, 21, 24, 28), region 0xFF's layout, the table window, and the boot ROM. No overlaps, every
     v1.1 register accounted for, every bit of the common header block (12, 30) consistent everywhere.
   - **Counted events (10):** the consume-one-credit rule against how the replayer's refill loop and the
     UART would actually be written; whether a depth per bit is enough, or a peripheral needs more.
   - **The struct-per-block header (14):** draft the structs for the tile, the standard window and the
     replayer, and check that every side-effecting register works as a `volatile uint32_t` member.
   - **Section 28:** what you would change in C&C 1's register set.
   - **The replayer as peripheral 1** under the standard window, with counted events and no sender of its own.
3. **Update the migration inventory:** every file in `wip/sw/`, `wip/sort/` and `wip/meshsim/` that must
   change, why, and a rough size, against the design as it now stands. Flag in particular: code that relies
   on unaligned or spanning access (26), on `ebreak` or `ecall` (20), on HALT or the RUN/HALT nets (23), on
   uncounted traffic other than commands and boot (17), on `PTR[64]` staging (17), on chunk-unit or
   word-unit packet addresses (5), and on stream counts above 65,535 (30).

DELIVERABLE: `WORKER3/REVIEW-2.md`
1. A summary: the five points that matter most, one line each.
2. A status table for W3-01 to W3-23.
3. New findings, numbered on from **W3-24**, in round 1's format.
4. The updated migration inventory.
5. Spec sections to update, and questions for the owner.
