# What v1.35's meshsim does for each check the spec promises

*For the edits to the preamble (line 28) and section 9 (line 2942). Line numbers are `v1.3/spec/machine-spec.md`'s
(`ops` = `replayer-opcodes.md`); code paths are under `sim/`. Searched: `src/units/`, `src/check.h`, `tests/mach.cpp`,
`tests/checker.cpp`, 2026-11-12, again after merging `main` the same day (invariant 8's strict mode and the Tracer's facts landed then). A "not found" means that search found no check; it is a finding, to confirm.*

**The mechanisms.**
- **F**: FATAL, every build: the run aborts with a message naming the access (`src/base/fatal.h`).
- **S**: the core stops (`stop_loudly`, `src/units/rv32.h:215`), counted, every build.
- **C+R**: counted by the unit in every build, and recorded as a fact, reported by name in the debug and suite builds
  (`src/check.h`).
- **R**: recorded as a fact in the debug and suite builds only (`if constexpr (ck::kOn)`), not counted otherwise.
- **C**: counted by the unit in every build; no fact recorded (its `Kind` exists in `check.h` with no record site, or
  none exists).
- **N**: not found.

## The invariants of section 9

| inv. | what | mechanism | where |
|---|---|---|---|
| 1 | no FIFO overflow | C+R | router FIFOs counted at t + 1 (D28), `FIFO_OVERFLOW` from the harness's scan (`tests/checker.h:30`); the gather queue's and the write engine's overflow links (`gather.h:33`, `tile_ctl.h:64`) |
| 2 | one access per area per cycle; no buffer both read and written in a run | first half by construction (the area queues); second half **not found**: `check_disjoint` is a `MachConfig` flag and `DISJOINT` a `Kind`, with no record site |
| 3 | the timetable holds | C | `rotations_late` (`ag.h:90`); `ROTATION_LATE` has no record site |
| 4 | expected arrivals land first (done early) | C+R | `CTL_DONE_EARLY`, `regblock.h:158` |
| 5 | no bad packets | C+R; misaligned F | `DIR_BAD_PACKET` `director.h:127`, `INJ_*` `inject.cpp:48–53`; misaligned mode 0 `director.h:124` |
| 6 | no `HIST` count past 17 bits | C | the overflow link `hist_ovf` (`tile_ctl.h:64`); `HIST_OVERFLOW` has no record site |
| 8 | overwritten-unread results counted; strict mode | C+R | `CMEM_REPLY_OVERWRITE` `tile_ctl.h:531`, reported under `strict_replies`; `CMEM_COMMAND_UNREAD` `:539`; the read-port queue full: C (`tile_ctl.h:368`) |
| 9 | no unexpected arrival | C+R | `DIR_UNEXPECTED`, `director.h:161` |
| 10 | gathers are local | C+R | `INJ_GATHER_ELSEWHERE` `inject.cpp:51`; `ag.h:332` (C) |
| 11 | controller-memory writes keep to their places | C+R | `DIR_COMMAND_BLOCK`, `DIR_REPLY_BLOCK`, `DIR_PAST_CMEM`, `DIR_COMMAND_KIND` (`director.h:154–208`) |
| 14 | read-port results and incoming traffic don't overlap | C+R | `DIR_READPORT_OVERLAP`, `director.h:121` (`check_read_port_overlap`) |
| 15 | no errors | C+R | `ERRORS_SET`, `tile_ctl.h:177` |
| 16, every peripheral's | no `PULSE_DMA` while busy; no unknown output queue | F | `periph_slot.cpp:308`, `:311` |
| 16 | no `PULSE_RUN` while running | F (the replayer) | `replayer.cpp:272` |
| 16 | no credit to a full counter | C | `event_counts.h:26` |
| 16 | no change to `DMA_HDR`/`DMA_ADDR` mid-transfer | **not found** | |
| 16, the replayer's | bad write; `RUN_CONFIG` while running; reserved expansion bits | F | `replayer.cpp:252`, `:312–331`, `:450` |
| 16, the replayer's | the rest (two sources, …) | C+R | `PERIPH_VIOLATION`, `replayer.cpp:46` |
| 16, the Tracer's | its undefined behaviour | F | `tracer.cpp:106` |
| 16, the Tracer's | word 0 mid-segment, and its other flags | C+R | `TRACER_VIOLATION`, `tracer.cpp:97` |
| 17 | a request names its counters; arrives armed | C+R | `DIR_UNEXPECTED` |
| 17 | at most 1, 3 and 1 requests outstanding | **not found** | |
| 17 | tag windows written as the file system directs; none during a group run | **not found** (the file system is deferred, item 119) | |
| 17 | no stream *d* = 67–125 or 127; no *d* = 126 on a mesh tile | C+R / C | `INJ_NO_ENDPOINT` `inject.cpp:50`; `ag.h:330–331` |
| 17 | a target with a receiver; `REQUEST_RECEIVE` never empty | C | `reqbus.h:369`, `:452` |
| 18 | one source per order-placed destination | C+R for a queue port (`replayer.h:186`, `V_TWO_SOURCES`); **not found** for modes 1–3 into big SRAM |
| 19 | everything in bounds | C+R / C | `DIR_PAST_SRAM` `director.h:174`, `WE_PAST_SRAM` `write_engine.h:127`; `inject.h:355`, `rv32.h:453` (C) |
| 20 | pointers and runs aligned | F / C+R / C | a read-queue chunk `inject.h:353` (F); `CTL_RECORD_BASE_ODD` `rv32.h:600`; `ag.h:328–329`, `tile_ctl.h:370` (C) |
| 21 | no reserved encodings | F / C+R | modes 6–7 `director.h:136`, `ag.h:325`; `DMA_CONFIG` digit `regblock.h:180` (F); `DIR_COUNT_CTR0`, `CTL_RESERVED_ACCESS` |
| 22 | uncounted traffic is mode 0 only | R | `DIR_UNCOUNTED_*`, `director.h:141–143`, facts only |
| 23 | no silent traps | S | `rv32.h:207, 242, 261, 269, 378, 379` |
| 24 | K covers the write path | F for K below its formula (`tests/mach.cpp:165`); the overtaking read: **not found** in `src/` (`measure_s1` is a harness option) |
| 25 | boot keeps its order | C+R / R | `DIR_OVERLAY_AFTER_COMMAND`, `DIR_COMMAND_KIND` |
| 26 | no ignored start | F | `ag.h:145` |

## The "(meshsim stops / errors / flags)" phrases outside section 9

| line | the phrase is about | mechanism | where |
|---|---|---|---|
| 157, 218, 393, 2565 | an access past the end of a memory | C+R / C | as invariant 19 |
| 158, 901 | an offset that aliases | C | `rv32.h:453` |
| 159, 200 | a write to 0x00–0x0F after boot | C+R | `DIR_RESERVED_BYTES`, `director.h:202` |
| 186, 214, 372 | a misaligned packet (UB) | F | `director.h:124` |
| 197, 200 | count-only with counter 0 | C+R | `DIR_COUNT_CTR0`, `INJ_COUNT_CTR0` |
| 200 | a counted packet in a reserved mode | F | `director.h:136` |
| 219 | size 11 (UB) | C+R, **not F** | `DIR_SIZE_UNDEF`, `director.h:131` |
| 294, 2585 | a request dropped at the root | C | `reqbus.h:369` |
| 468 | a misaligned stream source or destination | C | `ag.h:328–329` |
| 473, 2968 | a stream with *d* = 126 from a mesh tile ("errors loudly") | C | `ag.h:330–331` |
| 653, 2971 | two units feeding one queue port | C+R | `V_TWO_SOURCES` |
| 753 | K below its formula | F | `tests/mach.cpp:165` |
| 878, 1056, 2899, 2976 | the silent traps | S | `rv32.h` |
| 901, 1195, 2663, 2684 | a reserved word of the block or a window | C+R / C | `CTL_RESERVED_ACCESS` `rv32.h:509`; `rv32.h:485` |
| 1162 | a peripheral crediting a full counter | C | `event_counts.h:26` |
| 1249 | `DMA_HDR` or `DMA_ADDR` changed mid-transfer | **not found** | |
| 1430 | an expansion word with a reserved bit (UB) | F | `replayer.cpp:450` |
| 1433 | `shift_offset` set on a row other than `VirtualShiftData…` | C+R | `V_OFFSET_IMM`, `replayer.h:189`, through `PERIPH_VIOLATION` (`replayer.cpp:46`) |
| 1602; ops 325 | a misaligned memory row | C+R | `V_MISALIGNED`, `replayer.h:194` |
| 1631; ops 10, 339 | a zero shift mask | C+R | `V_UNDEFINED`, `replayer_alu.h:167–168`, `replayer.cpp:511` |
| 1739, 1799, 1818 | the replayer's bad writes (UB) | F | `replayer.cpp:252, 312–331` |
| 1839 | a pass with no state write since the last | C+R | `V_NO_FLUSH`, `replayer.h:185` |
| 1839 | a run while the replayer runs (undefined; ruled, owner, 2026-11-12) | F | `replayer.cpp:272` |
| 2183 | an inline expansion with no `EXPANSION_TEMPLATE` store | C+R | `V_INLINE_NO_TEMPLATE`, `tracer.cpp:516` |
| 2270 | a Tracer transfer asking for more than its credits cover (UB) | **not found** (searched `tracer.cpp`, `periph_slot.cpp`) | |
| 2455 | staging before arming | C+R | `DIR_UNEXPECTED` |
| 2762 | `PULSE_DMA` while a run runs (undefined; ruled, owner, 2026-11-12) | F | `ag.h:145` |
| 2775 | a counter sum past 2²⁰ − 1 | C+R | `CTL_COUNTER_WRAP`, `regblock.h:216` |
| 2962 | the Tracer's word 0 mid-segment | C+R | `V_ADVICE_UNCONSUMED`, `TRACER_VIOLATION` (`tracer.cpp:97`) |
| ops 254 | a non-contiguous PEXT mask, a statistic, not a check | counted | `pext_non_contiguous`, `replayer.cpp:509` |

## What follows for the edits

Line 28 states the owner's three mechanisms as the rule. The cases that differ (`FINDINGS.md` 9–13) are listed in
`spec/CHANGES-v1.3-to-v1.35.md`, as they stand; the list is to be brought to the freeze.
