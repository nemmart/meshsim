# The restructuring's map (stage 1)

*v1.3's spec restructured as v1.35's: where every unit goes, the new section 13, the first draft of the edits, and the
questions. No text has moved yet. Line numbers are v1.3's (`v1.3/spec/…`, `v1.3/planning/…`); `inventory.py` in this
folder lists them (`python3 sim/spec/check/inventory.py units|rows|items|files`, run from the repository's root).*

**Contents.** 1, the sources and their destinations. 2, the method the proof follows. 3, the preamble. 4, every
section and subsection. 5, the tables and lists that are split or keep Jolt-specific rows. 6, the new section 13.
7, the edits (first draft). 8, the questions.

## 1. The sources and where each goes

Every source file, its lines, and its destination. Files that move whole keep their names (code and the spec cite
documents by name).

| source | lines | destination |
|---|---|---|
| `v1.3/spec/machine-spec.md` | 3,488 | split: `spec/machine-spec.md` (the platform), `spec/jolt/replayer.md`, `spec/jolt/tracer.md`, `spec/history/machine-spec-v1.3-preamble.md`, `spec/history/machine-spec-v1.3-section-13.md` (sections 3 to 6) |
| `v1.3/spec/replayer-opcodes.md` | 1,303 | `spec/jolt/replayer-opcodes.md`, whole (three edits, section 7) |
| `v1.3/spec/schedule.md` | 98 | `spec/schedule.md`, whole, no edit (its two "to be re-measured" labels stay: item 24) |
| `v1.3/spec/CHANGES-to-v1.25.md` | 930 | `spec/history/CHANGES-to-v1.25.md`, whole, unchanged |
| `v1.3/spec/CHANGES-v1.2.md` | 142 | `spec/history/CHANGES-v1.2.md`, whole, unchanged |
| `v1.3/spec/CHANGES-v1.25.md` | 122 | `spec/history/CHANGES-v1.25.md`, whole, unchanged |
| `v1.3/spec/CHANGES-v1.25-to-v1.26.1.md` | 147 | `spec/history/CHANGES-v1.25-to-v1.26.1.md`, whole, unchanged |
| `v1.3/spec/CHANGES-v1.26.md` | 66 | `spec/history/CHANGES-v1.26.md`, whole, unchanged |
| `v1.3/spec/CHANGES-v1.26.1.md` | 88 | `spec/history/CHANGES-v1.26.1.md`, whole, unchanged |
| `v1.3/spec/CHANGES-v1.26.1-to-v1.3.md` | 115 | `spec/history/CHANGES-v1.26.1-to-v1.3.md`, whole, unchanged |
| `v1.3/spec/CHANGES-v1.3.md` | 98 | `spec/history/CHANGES-v1.3.md`, whole, unchanged |
| `v1.3/planning/DECISIONS-v1.2.md` | 1,587 | `spec/history/DECISIONS-v1.2.md`, whole, unchanged |
| `v1.3/planning/DECISIONS-v1.25.md` | 284 | `spec/history/DECISIONS-v1.25.md`, whole, unchanged |
| `v1.3/planning/DECISIONS-v1.26.md` | 172 | `spec/history/DECISIONS-v1.26.md`, whole, unchanged |
| `v1.3/planning/DECISIONS-v1.3.md` | 1,433 | `spec/history/DECISIONS-v1.3.md`, whole, unchanged |

Fifteen files, 10,073 lines. The history files are exempt from the path check (they stay as written); every other
destination is checked.

**New files** (all their lines "added" in `EDITS.md`): `spec/CHANGES-v1.3-to-v1.35.md`; `spec/history/README.md`,
`spec/jolt/README.md`, `spec/check/README.md`; the title line of each extracted history file; the stubs and pointers
of sections 3 to 5; the new section 13 and the jolt files' "Open items" (section 6). `spec/check/` needs `v1.3/` to
run, so it is left out of the frozen copy like `oracle/`; at the end `EDITS.md` moves to `spec/history/`.

## 2. How the proof works

`check_move.py` reads a machine-readable copy of this map (each range: source file, first and last line,
destination file) and `EDITS.md`, and checks:

1. every non-blank source line is in exactly one destination, byte for byte, in its original order within that
   destination, or is the old text of an edit;
2. every non-blank destination line is a mapped source line, an edit's new text, or listed as added;
3. every v1.3 section number (each heading of `machine-spec.md` and the appendix) is a heading in some destination,
   or a stub naming where it went; every item number 1–158 is in the new section 13's index;
4. every repository path cited in a non-history destination exists in v1.35's tree, relative to its root, or starts
   with `v1.3/`.

Matching is by the map's ranges, not by content, so a line that recurs (a stub repeating a heading, a blank, `---`)
is never ambiguous. **Blank lines are exempt** (the owner, 2026-11-12). **A path**, for check 4, is a backticked token
starting with a folder of this repository (`spec/`, `fw/`, `src/`, `tests/`, `data/`, `pipeline/`, `planning/`,
`tools/`, and v1.3's `sw/`, `tables/`, `meshsim/`, `wip/`, `results/`, `diagrams/`, `sort/`), or a backticked bare
file name of ours (`CHANGES-…`, `DECISIONS-…`, `REGISTERS.md`, `mesh.h`, `RESULTS-…`), which must name a file in
v1.35's tree or start with `v1.3/`. Citations into other repositories (jolt-cpp's `isa/…`, `trace_owner.cpp:…`,
a16z's `tracer/src/…`, `third-party/jolt`) and names that aren't files (`p4/uniform-k128`, `VirtualLoadB/H/W`) are
listed in the script as external, each by line.

## 3. The preamble (lines 1–73)

| lines | what | goes to | why |
|---|---|---|---|
| 1 | the title | `machine-spec.md`, edited to v1.35 | the prompt |
| 3 | what the document specifies | stays | |
| 5 | "Companion material" | stays | heads the new companion list |
| 6–11 | the companions that are history (the `CHANGES-*` files, the decisions logs) | `history/machine-spec-v1.3-preamble.md` | the owner, 2026-11-12: they move with the in-brief blocks |
| (new) | the new companion list for what moved: `OVERVIEW.md`, `GUIDELINES.md`, `jolt/`, `history/`, `CHANGES-v1.3-to-v1.35.md` | added, after line 5 | the owner, 2026-11-12 |
| 12 | `tables/`, the replayer's tables | stays, path edited | a Jolt-specific bullet; the default (stays with its list) |
| 13 | `diagrams/`, still at v1.1 | stays, path edited to `v1.3/diagrams/` | not carried; "still at v1.1" stays true of them |
| 14 | `spec/schedule.md` | stays | |
| 15 | `mesh.h`, `REGISTERS.md` | stays, rewritten (section 7) | the owner's ruling: true before and after regeneration |
| 16 | `CONVENTIONS.md`, `RECIPES.md` | stays, paths edited | "being rewritten for v1.2" is a finding, not a simulator statement |
| 17 | `meshsim/` | stays, rewritten (section 7) | |
| 18 | `results/` | stays, path edited to `v1.3/results/` | not carried |
| 20 | Markers | stays, one sentence replaced (section 7) | which simulator the measurement labels mean |
| 22–28 | how the hardware treats software bugs | stays; 24 edited, 28 gains the defining sentence (section 7) | |
| 30–36 | v1.3 in brief | `history/machine-spec-v1.3-preamble.md` | version history |
| 38 | how v1.3's text is meant | stays | the owner, 2026-11-12 |
| 40–44, 46–51, 53–59, 61–70 | v1.26.1, v1.26, v1.25, v1.2 in brief | `history/machine-spec-v1.3-preamble.md` | version history |
| 72 | `---` | stays | |

## 4. Every section and subsection

One row per heading (from `inventory.py units`), the companion bullets and in-brief blocks included. Ranges run to
the line before the next heading of the same or a higher level; the `---` and blank lines between sections stay with
the platform. **4.21's range is 1976–2358** (2359–2361, the blank, `---`, blank before section 5, stay). Sections and
subsections not named in "why" stay in the platform whole. The sort's own engines (4.4 GATHER, 4.9's augmenting,
4.10's tables) and section 14 stay, as the prompt says.

| file | lines | kind | unit | goes to | why |
|---|---|---|---|---|---|
| `machine-spec.md` | 6-6 | companion | - `spec/CHANGES-v1.3.md`: what changed from v1.26.1, section by section, each it… | spec/history/machine-spec-v1.3-preamble.md | a history document; the preamble gets a new companion list (added) |
| `machine-spec.md` | 7-7 | companion | - `spec/CHANGES-v1.26.1.md`: what changed from v1.26, section by section, each i… | spec/history/machine-spec-v1.3-preamble.md | a history document; the preamble gets a new companion list (added) |
| `machine-spec.md` | 8-8 | companion | - `spec/CHANGES-v1.26.md`: what changed from v1.25 to v1.26, section by section,… | spec/history/machine-spec-v1.3-preamble.md | a history document; the preamble gets a new companion list (added) |
| `machine-spec.md` | 9-9 | companion | - `spec/CHANGES-v1.25.md`: what changed from v1.2, section by section, each item… | spec/history/machine-spec-v1.3-preamble.md | a history document; the preamble gets a new companion list (added) |
| `machine-spec.md` | 10-10 | companion | - `spec/CHANGES-v1.2.md`: what changed from v1.1, section by section, each item … | spec/history/machine-spec-v1.3-preamble.md | a history document; the preamble gets a new companion list (added) |
| `machine-spec.md` | 11-11 | companion | - `planning/DECISIONS-v1.2.md`: the decisions log behind v1.2, kept as the histo… | spec/history/machine-spec-v1.3-preamble.md | a history document; the preamble gets a new companion list (added) |
| `machine-spec.md` | 12-12 | companion | - `tables/`: the replayer's expansion tables (4.19.5, 4.19.6), universal, built … | machine-spec.md (stays; edited) |  |
| `machine-spec.md` | 13-13 | companion | - `diagrams/`: the figure set, numbered part.figure (1.0–9.3; part 9 is the Trac… | machine-spec.md (stays; edited) |  |
| `machine-spec.md` | 14-14 | companion | - `spec/schedule.md`: the 16-group all-to-all schedule (4.2) and the routing tie… | machine-spec.md (stays; edited) |  |
| `machine-spec.md` | 15-15 | companion | - `sw/mesh.h` and `sw/REGISTERS.md`: the tile-program interface and the programm… | machine-spec.md (stays; edited) |  |
| `machine-spec.md` | 16-16 | companion | - `sw/CONVENTIONS.md`, `sw/RECIPES.md`: the software conventions and the recipe … | machine-spec.md (stays; edited) |  |
| `machine-spec.md` | 17-17 | companion | - `meshsim/`: the cycle-level simulator, which models every unit described here … | machine-spec.md (stays; edited) |  |
| `machine-spec.md` | 18-18 | companion | - `results/`: the measured sort. | machine-spec.md (stays; edited) |  |
| `machine-spec.md` | 30-36 | in-brief | v1.3 in brief | spec/history/machine-spec-v1.3-preamble.md | version history |
| `machine-spec.md` | 40-44 | in-brief | v1.26.1 in brief | spec/history/machine-spec-v1.3-preamble.md | version history |
| `machine-spec.md` | 46-51 | in-brief | v1.26 in brief | spec/history/machine-spec-v1.3-preamble.md | version history |
| `machine-spec.md` | 53-59 | in-brief | v1.25 in brief | spec/history/machine-spec-v1.3-preamble.md | version history |
| `machine-spec.md` | 61-70 | in-brief | v1.2 in brief | spec/history/machine-spec-v1.3-preamble.md | version history |
| `machine-spec.md` | 1-3488 | h1 | Machine specification — 64-tile mesh processor, v1.3 | machine-spec.md | the preamble: see its own table |
| `machine-spec.md` | 74-92 | h2 | 1. Conventions | machine-spec.md |  |
| `machine-spec.md` | 93-162 | h2 | 2. System | machine-spec.md |  |
| `machine-spec.md` | 124-137 | h3 | 2.1 Address space (7 bits) | machine-spec.md |  |
| `machine-spec.md` | 138-162 | h3 | 2.2 The 32-bit address map | machine-spec.md |  |
| `machine-spec.md` | 163-325 | h2 | 3. Links | machine-spec.md |  |
| `machine-spec.md` | 165-227 | h3 | 3.1 Link word (packet) | machine-spec.md |  |
| `machine-spec.md` | 228-260 | h3 | 3.2 Latency, throughput and ordering | machine-spec.md |  |
| `machine-spec.md` | 261-325 | h3 | 3.3 Request bus | machine-spec.md |  |
| `machine-spec.md` | 326-2361 | h2 | 4. Tile | machine-spec.md |  |
| `machine-spec.md` | 357-402 | h3 | 4.1 Big SRAM | machine-spec.md |  |
| `machine-spec.md` | 403-536 | h3 | 4.2 Address generator | machine-spec.md |  |
| `machine-spec.md` | 537-590 | h3 | 4.3 Controller read port | machine-spec.md |  |
| `machine-spec.md` | 591-625 | h3 | 4.4 GATHER engine | machine-spec.md |  |
| `machine-spec.md` | 626-678 | h3 | 4.5 SRAM read queue, return path and injection | machine-spec.md |  |
| `machine-spec.md` | 679-706 | h3 | 4.6 Route blocks | machine-spec.md |  |
| `machine-spec.md` | 707-727 | h3 | 4.7 Output queue sets and selectors | machine-spec.md |  |
| `machine-spec.md` | 728-769 | h3 | 4.8 Traffic director | machine-spec.md |  |
| `machine-spec.md` | 770-813 | h3 | 4.9 DMA write engine | machine-spec.md |  |
| `machine-spec.md` | 814-863 | h3 | 4.10 Table memories | machine-spec.md |  |
| `machine-spec.md` | 864-922 | h3 | 4.11 Controller | machine-spec.md |  |
| `machine-spec.md` | 923-1006 | h3 | 4.12 The command and reply blocks, the write counters and the events | machine-spec.md |  |
| `machine-spec.md` | 1007-1088 | h3 | 4.13 Status and errors; traps | machine-spec.md |  |
| `machine-spec.md` | 1089-1102 | h3 | 4.14 Commit ordering | machine-spec.md |  |
| `machine-spec.md` | 1103-1113 | h3 | 4.15 Digits (shared definition) | machine-spec.md |  |
| `machine-spec.md` | 1114-1127 | h3 | 4.16 The fifth router port | machine-spec.md |  |
| `machine-spec.md` | 1128-1252 | h3 | 4.17 Peripherals | machine-spec.md |  |
| `machine-spec.md` | 1146-1151 | h4 | 4.17.1 The peripherals and their types | machine-spec.md |  |
| `machine-spec.md` | 1152-1157 | h4 | 4.17.2 Run: `PULSE_RUN` and `STATUS` [0] | machine-spec.md |  |
| `machine-spec.md` | 1158-1169 | h4 | 4.17.3 Events: counted and live | machine-spec.md |  |
| `machine-spec.md` | 1170-1198 | h4 | 4.17.4 The standard window | machine-spec.md |  |
| `machine-spec.md` | 1199-1210 | h4 | 4.17.5 Writes into a peripheral's region | machine-spec.md |  |
| `machine-spec.md` | 1211-1216 | h4 | 4.17.6 Errors | machine-spec.md |  |
| `machine-spec.md` | 1217-1220 | h4 | 4.17.7 (removed) | machine-spec.md |  |
| `machine-spec.md` | 1221-1231 | h4 | 4.17.8 What every peripheral must do | machine-spec.md |  |
| `machine-spec.md` | 1232-1252 | h4 | 4.17.9 Peripheral output: the DMA | machine-spec.md |  |
| `machine-spec.md` | 1253-1284 | h3 | 4.18 Request port | machine-spec.md |  |
| `machine-spec.md` | 1285-1956 | h3 | 4.19 The replayer (peripheral type 4) | spec/jolt/replayer.md | 4.19 leaves a stub |
| `machine-spec.md` | 1293-1320 | h4 | 4.19.1 Overview | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1321-1410 | h4 | 4.19.2 The run log and the checkpoint | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1411-1476 | h4 | 4.19.3 The expansion word and the operand codes | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1477-1528 | h4 | 4.19.4 The rd = x0 policy and the temporary offset | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1529-1559 | h4 | 4.19.5 The decode table and its index | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1560-1578 | h4 | 4.19.6 The ROM and its contents | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1579-1657 | h4 | 4.19.7 Executing a row: the register file, the ALU and the schedules | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1658-1703 | h4 | 4.19.8 The queues, their ports and credits | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1704-1787 | h4 | 4.19.9 The events, the words and the controller loop | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1788-1836 | h4 | 4.19.10 Its region: the address map, landing and ownership | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1837-1860 | h4 | 4.19.11 Run, done and the errors | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1861-1891 | h4 | 4.19.12 The output row | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1892-1912 | h4 | 4.19.13 The resident replay region (the software contract) | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1913-1918 | h4 | 4.19.14 Cost | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1919-1956 | h4 | 4.19.15 Stream decompression | spec/jolt/replayer.md |  |
| `machine-spec.md` | 1957-1975 | h3 | 4.20 UART | machine-spec.md |  |
| `machine-spec.md` | 1976-2361 | h3 | 4.21 The Tracer (peripheral type 3) | spec/jolt/tracer.md | 4.21 leaves a stub |
| `machine-spec.md` | 1982-2007 | h4 | 4.21.1 Overview | spec/jolt/tracer.md |  |
| `machine-spec.md` | 2008-2055 | h4 | 4.21.2 Its region and the state record | spec/jolt/tracer.md |  |
| `machine-spec.md` | 2056-2168 | h4 | 4.21.3 Control: the window, the pulses and the queues | spec/jolt/tracer.md |  |
| `machine-spec.md` | 2169-2210 | h4 | 4.21.4 Execution | spec/jolt/tracer.md |  |
| `machine-spec.md` | 2211-2250 | h4 | 4.21.5 Memory | spec/jolt/tracer.md |  |
| `machine-spec.md` | 2251-2279 | h4 | 4.21.6 Output: the FIFOs, the compressors, the queues and the drain | spec/jolt/tracer.md |  |
| `machine-spec.md` | 2280-2287 | h4 | 4.21.7 Boundaries | spec/jolt/tracer.md |  |
| `machine-spec.md` | 2288-2314 | h4 | 4.21.8 Errors | spec/jolt/tracer.md |  |
| `machine-spec.md` | 2315-2338 | h4 | 4.21.9 Timing and cost | spec/jolt/tracer.md |  |
| `machine-spec.md` | 2339-2361 | h4 | 4.21.10 The segment on the chip: barriers and deliveries (the software contract) | spec/jolt/tracer.md |  |
| `machine-spec.md` | 2362-2386 | h2 | 5. Timing summary | machine-spec.md |  |
| `machine-spec.md` | 2387-2598 | h2 | 6. Control | machine-spec.md |  |
| `machine-spec.md` | 2393-2421 | h3 | 6.1 Reset, errors and the toast pin | machine-spec.md |  |
| `machine-spec.md` | 2422-2457 | h3 | 6.2 Boot | machine-spec.md |  |
| `machine-spec.md` | 2458-2499 | h3 | 6.3 `done`, READY, GO and `WFI` | machine-spec.md |  |
| `machine-spec.md` | 2500-2505 | h3 | 6.4 Phases that receive | machine-spec.md |  |
| `machine-spec.md` | 2506-2517 | h3 | 6.5 Block copy into controller memory (overlays, tables) | machine-spec.md |  |
| `machine-spec.md` | 2518-2543 | h3 | 6.6 The controllers and their registers | machine-spec.md |  |
| `machine-spec.md` | 2544-2598 | h3 | 6.7 The memory system's contract | machine-spec.md |  |
| `machine-spec.md` | 2599-2608 | h2 | 7. Memory subsystem | machine-spec.md |  |
| `machine-spec.md` | 2609-2939 | h2 | 8. Registers | machine-spec.md |  |
| `machine-spec.md` | 2613-2691 | h3 | 8.1 Register map and general rules | machine-spec.md |  |
| `machine-spec.md` | 2692-2719 | h3 | 8.2 `PARAMS` and `STATUS` (0x800, 0x804, read-only) | machine-spec.md |  |
| `machine-spec.md` | 2720-2723 | h3 | 8.3 `ERRORS` (0x808, read-only) | machine-spec.md |  |
| `machine-spec.md` | 2724-2738 | h3 | 8.4 The send port: `ROUTER_HDR`, `ROUTER_ADDR`, `ROUTER_SET_W0`–`W2`, `ROUTER_SEND_32`, `_… | machine-spec.md |  |
| `machine-spec.md` | 2739-2754 | h3 | 8.5 `DMA_CONFIG` (0x858, read/write) | machine-spec.md |  |
| `machine-spec.md` | 2755-2767 | h3 | 8.6 The pulse registers: `PULSE_DONE`, `PULSE_DMA` (0x80C, 0x810, write-only; read 0) | machine-spec.md |  |
| `machine-spec.md` | 2768-2777 | h3 | 8.7 The write counters and `DMA_RECORD_BASE_ADDRESS` (0x85C–0x878) | machine-spec.md |  |
| `machine-spec.md` | 2778-2784 | h3 | 8.8 The read port: `SRAM_RD_ADDR_QUEUE`, `SRAM_RD_DEST_ADDR` (0x87C, 0x880) | machine-spec.md |  |
| `machine-spec.md` | 2785-2799 | h3 | 8.9 The table window (0xFFFF2000–0xFFFF35FF) | machine-spec.md |  |
| `machine-spec.md` | 2800-2820 | h3 | 8.10 The events and the request port: `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK`, `EVENTS_REQU… | machine-spec.md |  |
| `machine-spec.md` | 2821-2857 | h3 | 8.11 The peripheral windows: the standard words and the replayer's | split | 2821–2841 stay (the standard words); 2842–2856 (the replayer's window) to `jolt/replayer.md`; stub line |
| `machine-spec.md` | 2858-2861 | h3 | 8.12 (removed) | machine-spec.md |  |
| `machine-spec.md` | 2862-2874 | h3 | 8.13 The UART: `UART_TX`, `UART_RX`, `UART_STATUS`, `UART_DIVISOR` (C&C 1's peripheral, wo… | machine-spec.md |  |
| `machine-spec.md` | 2875-2894 | h3 | 8.14 C&C 1's peripheral: Ingress/Egress and the UART | machine-spec.md |  |
| `machine-spec.md` | 2895-2901 | h3 | 8.15 Simulator-only words | machine-spec.md |  |
| `machine-spec.md` | 2902-2939 | h3 | 8.16 The Tracer's window (C&C 3's peripheral) | spec/jolt/tracer.md | the Tracer's window; stub |
| `machine-spec.md` | 2940-2982 | h2 | 9. Invariants (checked by the simulator) | machine-spec.md |  |
| `machine-spec.md` | 2983-3038 | h2 | 10. Parameter summary | machine-spec.md |  |
| `machine-spec.md` | 3039-3067 | h2 | 11. Logic budget (per tile, 12 nm estimates) | machine-spec.md |  |
| `machine-spec.md` | 3068-3093 | h2 | 12. Physical design: the control island | machine-spec.md |  |
| `machine-spec.md` | 3094-3461 | h2 | 13. Open items | machine-spec.md |  |
| `machine-spec.md` | 3462-3488 | h2 | 14. Context: the sort these units serve | machine-spec.md |  |
| `replayer-opcodes.md` | 1-1303 | h1 | Appendix: Replayer opcode semantics | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 19-80 | h2 | 1. Conventions | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 21-31 | h3 | 1.1 Values and notation | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 32-49 | h3 | 1.2 Slots and how a row reads them | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 50-55 | h3 | 1.3 Writing rd | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 56-65 | h3 | 1.4 pc, `pc_increment` and `instr_idx` | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 66-80 | h3 | 1.5 The row record | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 81-311 | h2 | 2. The row opcodes | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 85-164 | h3 | 2.0 Renumbering from v1.2 | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 165-184 | h3 | A. Integer ALU | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 185-197 | h3 | B. Word forms, extension and sign helpers | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 198-207 | h3 | C. Multiply | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 208-240 | h3 | D. Shifts, bitmasks and rotates | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 241-255 | h3 | E. Sub-word memory helpers | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 256-270 | h3 | F. Control flow and pc-relative | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 271-292 | h3 | G. Memory, the log and advice | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 293-304 | h3 | H. Assertions (never evaluated by the replayer) | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 305-311 | h3 | I. Miscellaneous | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 312-329 | h2 | 3. The load and store rows: the memory fields | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 330-346 | h2 | 4. Flags: where the tracer, the spec and a reimplementation can disagree | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 347-370 | h2 | 5. The rd = x0 cross-check against spec 4.19.4 | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 371-480 | h2 | 6. Slot forms and their producers | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 481-561 | h2 | 7. Opcodes that occur only as source instructions (74) | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 562-567 | h2 | 8. Opcodes that never occur (3) | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `replayer-opcodes.md` | 568-1303 | h2 | 9. Test vectors | spec/jolt/replayer-opcodes.md | the appendix, whole |
| `schedule.md` | 1-98 | h1 | All-to-all schedule: sixteen groups of four | spec/schedule.md |  |
| `schedule.md` | 5-12 | h2 | 1. Setting | spec/schedule.md |  |
| `schedule.md` | 13-39 | h2 | 2. The sixteen groups | spec/schedule.md |  |
| `schedule.md` | 40-52 | h2 | 3. Constants that go with it | spec/schedule.md |  |
| `schedule.md` | 53-72 | h2 | 4. Why these groups | spec/schedule.md |  |
| `schedule.md` | 73-82 | h2 | 5. Queue bound (why 16 entries suffice) | spec/schedule.md |  |
| `schedule.md` | 83-98 | h2 | 6. Cost | spec/schedule.md |  |

**Stubs** (added lines, in the style of 8.12's): under the heading of each moved section, in `machine-spec.md`, one
line: *"Moved in v1.35 to `spec/jolt/replayer.md`, with 4.19.1–4.19.15."*; likewise 4.21 (with 4.21.1–4.21.10) and
8.16 to `spec/jolt/tracer.md`. The heading itself moves with its text; the stub repeats it (added), so the number is a
heading in both places. Every subsection of 4.19 and 4.21 is a heading in its jolt file, so code's "spec 4.19.7"
resolves there.

## 5. Tables and lists: the invariants, the parameters, and the Jolt-specific rows elsewhere

**Section 9, every invariant** (2940–2979). The section's text stays; 2942 is edited (section 7). Invariant 16 is
split by whole lines: 2959 and 2960 stay, 2961 and 2962 move, and an added line takes their place, in the list's
form: *"    - the replayer's and the Tracer's: in `spec/jolt/replayer.md` and `spec/jolt/tracer.md` (invariant 16)."*
Each jolt file gets an added line before its bullet: *"From section 9, invariant 16:"*.

| line | invariant | goes to | why |
|---|---|---|---|
| 2944 | 1. **No FIFO overflow:** router FIFOs, including tiles 0–2's fifth-port FIFOs (4.7, 4.16;  | stays |  |
| 2945 | 2. **At most one access per big-SRAM area per cycle,** and no buffer is both read and writ | stays |  |
| 2946 | 3. **The timetable holds:** every rotation starts on its scheduled cycle and never earlier | stays |  |
| 2947 | 4. **Every expected arrival lands first** (an uncounted command write is ignored, since co | stays |  |
| 2948 | 5. **No bad packets:** no packet is addressed to a region it can't reach (0x02, 0x07–0xFE, | stays |  |
| 2949 | 6. **No HIST count exceeds 17 bits.** | stays |  |
| 2950 | 7. *(Withdrawn in v1.2: there is no RUN/HALT line. See CHANGES-v1.2.md.)* | stays |  |
| 2951 | 8. **Overwritten-unread results are counted, not failed:** reply-block words and read-port | stays |  |
| 2952 | 9. **No unexpected arrival:** no counted packet's decrement finds its counter at 0, so cou | stays |  |
| 2953 | 10. **Gathers are local:** a mode-4 packet is only ever sent to the tile itself. | stays |  |
| 2954 | 11. **Controller-memory writes keep to their places** (owner, 2026-10-28): writes into the | stays |  |
| 2955 | 12. *(Withdrawn, 2026-10-26: replies land in the reply block, so a reply can no longer add | stays |  |
| 2956 | 13. *(Withdrawn, 2026-10-26: replies could no longer address a word outside data memory; 1 | stays |  |
| 2957 | 14. **Read-port results and incoming traffic do not overlap** (4.3): no packet from outsid | stays |  |
| 2958 | 15. **No errors:** every unit's ERRORS and every peripheral's stay 0 (4.13, 4.17.6). A pro | stays |  |
| 2959 | 16. **The peripherals' and the replayer's invariants** (4.17, 4.19): | stays | the item heading; its first bullet (2960, every peripheral's) is the platform's |
| 2960 |     - every peripheral's: no PULSE_RUN while a run is in progress; no PULSE_DMA while its  | stays |  |
| 2961 |     - the replayer's: no bad write (UB, meshsim only, 4.19.10); no RUN_CONFIG write while  | `jolt/replayer.md` | the replayer's bullet; a stub line takes its place |
| 2962 |     - **the Tracer's** (4.21; v1.3): no write to an unused or unmapped offset of its regio | `jolt/tracer.md` | the Tracer's bullet; covered by the same stub line |
| 2963 | 17. **The request contract is kept** (3.3, 6.7): | stays |  |
| 2964 |     - a request names its data's and its reply's counters and its reply's address, and its | stays |  |
| 2965 |     - a tile has at most 1, 3 and 1 requests outstanding to C&C 1, 2 and 3; error reports  | stays |  |
| 2966 |     - tag windows are written only as the file system directs, one writer in flight or pac | stays |  |
| 2967 |     - no read is outstanding and no tag-window write is in flight while a group run is run | stays |  |
| 2968 |     - no stream has *d* = 67–125 or 127, no stream on a mesh tile has *d* = 126 (meshsim e | stays |  |
| 2969 |     - every request names a target with a receiver, and every request reaching a receiver  | stays |  |
| 2970 |     - no load of REQUEST_RECEIVE finds the receive queue empty (4.18). **Hardware:** ERROR | stays |  |
| 2971 | 18. **One source per order-placed destination** (4.1, 4.5, 4.9, 4.17.5): a unit never has  | stays |  |
| 2972 | 19. **Everything stays in bounds** (2.2, 4.1): no packet and no load, store or fetch of a  | stays |  |
| 2973 | 20. **Pointers and runs stay aligned** (4.1, 4.2, 4.4, 4.9, 4.17.9): STREAM_SRC and STREAM | stays |  |
| 2974 | 21. **No reserved encodings:** no packet in mode 6–7 (UB), and no count-only packet with c | stays |  |
| 2975 | 22. **Uncounted traffic is mode 0 only:** modes 1–3 are always counted; a running program  | stays |  |
| 2976 | 23. **No silent traps** (4.13): no ecall, ebreak, fetch outside program memory and the boo | stays |  |
| 2977 | 24. **K covers the write path** (4.8, 4.10): K is set at or above the implementation's slo | stays |  |
| 2978 | 25. **Boot keeps its order** (6.2): a unit's overlays and first command are all broadcast  | stays |  |
| 2979 | 26. **No ignored start:** no PULSE_DMA while the address generator is running (4.2). | stays |  |

**Section 10, every row** (2983–3035). Two rows move; an added row takes their place:
*"| replayer (4.19), Tracer (4.21) | in `spec/jolt/replayer.md` and `spec/jolt/tracer.md` |"*. Each jolt file gets the
table's header and its row (the header lines added).

| line | parameter | goes to | why |
|---|---|---|---|
| 2987 | / parameter / default / | stays |  |
| 2989 | / link word / **256 bits**: payload [127:0], address [159:128], size [161:160], counter [1 | stays |  |
| 2990 | / link_latency / 3 on every link direction; a hop is link_latency + 1 = 4 cycles (3.2) / | stays |  |
| 2991 | / start skew between tiles / 0 or +1 cycle (3.2) / | stays |  |
| 2992 | / STREAM, GROUP / STREAM: 256 entries in three 256 × 32-bit arrays (STREAM_SRC, STREAM_DES | stays |  |
| 2993 | / GATHER request queue / 16 (4.4) / | stays |  |
| 2994 | / GATHER value size *k* / 1–8 records; *k* odd: 64-bit copies, *k* even: 128-bit copies (4 | stays |  |
| 2995 | / digit-all-to-all group gap / 0; 4 for the two-hop layers (4.2) / | stays |  |
| 2996 | / mem_chunks / 1,048,576 (4 MB: 8 areas × 128 bits × 32K); any multiple of 65,536 chunks ( | stays |  |
| 2997 | / area request-queue depth / 8 in meshsim; *(Open)* for the hardware (4.1) / | stays |  |
| 2998 | / area tie order / writes first (4.1) / | stays |  |
| 2999 | / big-SRAM array read latency / 2 in meshsim; *(Open)* for the hardware (4.1) / | stays |  |
| 3000 | / big-SRAM read latency / queueing plus the array's read latency (4.1) / | stays |  |
| 3001 | / tokens and return queues / 64 tokens, a shared pool; in-order return queues of per-read  | stays |  |
| 3002 | / queue_depth (router FIFOs) / 16; the loopback FIFO at least the token count, 64 (require | stays |  |
| 3003 | / intra_tile_latency / 1 per stage / | stays |  |
| 3004 | / ts_engine_latency / 1 (1–4 measured) / | stays |  |
| 3005 | / ag_lookahead / 2·ts_engine_latency + 2 entries in each of the address generator's two re | stays |  |
| 3006 | / address-generator lead-in / first request 4·ts_engine_latency + 3 (group run) or 2·ts_en | stays |  |
| 3007 | / pointer stage depth / 2·ts_engine_latency + 2 (4 at the default) (4.9) / | stays |  |
| 3008 | / write-engine input FIFO / 4 (4.9) / | stays |  |
| 3009 | / PTR, HIST request FIFOs / 16 each (4.10) / | stays |  |
| 3010 | / controller read port / request queue of at least 64 entries of 43 bits (address, destina | stays |  |
| 3011 | / controller memories / **32 KB data + 32 KB program**; port B one 128-bit write per cycle | stays |  |
| 3012 | / cm_latency (director to port B) / 2 [built] / | stays |  |
| 3013 | / stage_delay / **64 cycles** (owner, 2026-11-06; was 32 *(Proposed)*): the minimum time f | stays |  |
| 3014 | / reg_load_latency / **2** for the register block and the peripheral windows, **1** for th | stays |  |
| 3015 | / ready_latency / **8 cycles** (owner, 2026-11-06, P1; was 1 in simulation and 8 for estim | stays |  |
| 3016 | / go_latency / **8 cycles** (owner, 2026-11-06, P1; was 1 in simulation): the GO net cross | stays |  |
| 3017 | / **K** / **8** (meshsim's default); a parameter, **implementation-defined**: at least the | stays |  |
| 3018 | / error_latency / the fixed latency of a unit's error line into C&C 1's peripheral: **8 cy | stays |  |
| 3019 | / write counters / seven, WRITE_COUNTER_1–7, 20 bits each; counter field 3 bits, 0 uncount | stays |  |
| 3020 | / boot ROM / in every controller, region 0x02, reset vector 0x02000000; **256 bytes** (64  | stays |  |
| 3021 | / keep-alive watchdog / WATCHDOG in C&C 1's peripheral, off at reset (6.1) / | stays |  |
| 3022 | / flush wait before a soft reset / about 0.1 ms (about 100,000 cycles at 1 GHz), the outsi | stays |  |
| 3023 | / load-use penalty (data memory, program memory, boot ROM) / 0 each (the design target; se | stays |  |
| 3024 | / taken-branch penalty, multiply latency / 1, 3 / | stays |  |
| 3025 | / request bus / a 16-bit, 8-ary tree: 8 row nodes and a root (with an input per sending co | stays |  |
| 3026 | / request FIFO / 2 KB per unit, 128 × 128 bits (4.18) / | stays |  |
| 3027 | / events / 16 of 32 bits used: [6:0] write counter 1–7 done, [7] counters done, [8] GO, [9 | stays |  |
| 3028 | / UART / C&C 1's peripheral only; 8N1; 16-byte TX and RX FIFOs *(Proposed)*; divisor in re | stays |  |
| 3029 | / command and reply blocks / command block 16 words at the top of data memory, 0x00007FC0  | stays |  |
| 3030 | / addresses / 32 bits: region [31:24], byte offset [23:0]; big-SRAM engine pointers 21-bit | stays |  |
| 3031 | / requests / 128 bits; the hardware's fields requestor [6:0] and target [9:7], 8 targets;  | stays |  |
| 3032 | / eject priority / the loopback FIFO is served only in a cycle when every other eject FIFO | stays |  |
| 3033 | / peripherals (4.17) / up to three per unit, regions 0x04–0x06, 0x200-byte windows at 0xFF | stays |  |
| 3034 | / replayer (4.19), peripheral type 4 / peripheral 1 on tiles 0–63; tables built at jolt-cp | `jolt/replayer.md` | the replayer's parameters |
| 3035 | / Tracer (4.21), peripheral type 3 / C&C 3's peripheral 1; the replayer's tables and 68-wo | `jolt/tracer.md` | the Tracer's parameters |

**Jolt-specific rows and bullets outside sections 9 and 10** (sections 5, 8.1, 8.11, 11, 12 and the companion list;
`inventory.py rows` marks a line Jolt-specific when it names the replayer, the Tracer, 4.19, 4.21, 8.16, Jolt, the
run log or an expansion). 8.1 has none: its table names the windows by number only. **8.11 is split by whole lines:**
2821–2841 (the heading, the standard words, peripheral 2 and 3's windows) stay; 2842–2856 (the replayer's window and
its note) move; an added stub line after 2840: *"The replayer's window (peripheral 1 on tiles 0–63): moved in v1.35 to
`spec/jolt/replayer.md`."* The heading keeps its words ("…and the replayer's"): rewording it is not an allowed edit.
Sections 5 and 11 keep their rows (the owner, 2026-11-12): a timing summary and a budget are read whole, and 11's
total includes the replayer. 12's line stays: it places the replayer by the island, a floor-plan fact.

| section | line | row | goes to | why |
|---|---|---|---|---|
| preamble | 12 | - tables/: the replayer's expansion tables (4.19.5, 4.19.6), universal, built from jolt-cp | stays, path edited | the default |
| preamble | 13 | - diagrams/: the figure set, numbered part.figure (1.0–9.3; part 9 is the Tracer, v1.3); d | stays, path edited | the default |
| 5 | 2379 | / replayer / the record's words 0–65 are reloaded before each pass, about 66 cycles from b | stays | the owner, 2026-11-12 |
| 5 | 2380 | / Tracer / a row in tr_row_cycles = 4 cycles, 6 for a multiply, plus stalls: a fill ms_lin | stays | the owner, 2026-11-12 |
| 8.11 | 2846 | / 0 / PARAMS / [7:0] = 4 / type 4, the replayer / 4 / | `jolt/replayer.md` (with 2842–2856) | the replayer's window |
| 8.11 | 2847 | / 1 / STATUS / [0] run in progress; [1] 0 / [0] is v1.1's periph_running (4.19.11) / 0 / | `jolt/replayer.md` (with 2842–2856) | the replayer's window |
| 8.11 | 2848 | / 2 / ERRORS / [0] reserved (v1.2: bad write), [1] queue overrun, [2] invalid expansion, [ | `jolt/replayer.md` (with 2842–2856) | the replayer's window |
| 8.11 | 2849 | / 5–7 / EVENTS, EVENTS_PEEK, EVENTS_MASK / [0] run done (depth 1), [2] code credit, [3] re | `jolt/replayer.md` (with 2842–2856) | the replayer's window |
| 8.11 | 2850 | / 4, 8, 9 / — / / reserved: the replayer has no DMA / — / | `jolt/replayer.md` (with 2842–2856) | the replayer's window |
| 8.11 | 2851 | / 32 / RUN_CONFIG (R/W) / [23:0] rows to run; [24] use the store log; [25] output rd_prev; | `jolt/replayer.md` (with 2842–2856) | the replayer's window |
| 8.11 | 2853 | / 34 / EXPANSION_OFFSET (R) / [15:0]; [31:16] 0 / the row within the current expansion whe | `jolt/replayer.md` (with 2842–2856) | the replayer's window |
| 8.11 | 2854 | / 35 / EXPANSION_OPCODE (R) / [31:0] / the instruction awaiting a controller-fed expansion | `jolt/replayer.md` (with 2842–2856) | the replayer's window |
| 11 | 3058 | / replayer (4.19.14), peripheral 1 on tiles 0–63: ALU and comparator, 128 × 64-bit true du | stays | the owner, 2026-11-12 |
| 11 | 3059 | / **total per tile** / / **≈ 0.52–0.61 mm²** with the replayer and the fifth port, about 3 | stays | the owner, 2026-11-12 |
| 12 | 3089 | - **Paths that cross the gap:** engine table accesses, and the traffic director's port-B w | stays | the default: a row stays with its table |
