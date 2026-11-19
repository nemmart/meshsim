# v1.3's machine specification: the preamble's history

*Moved whole from `v1.3/spec/machine-spec.md` in v1.35: the companion bullets for the history documents (lines 6–11) and each version's "in brief" (lines 30–36, 40–70), unchanged; their paths are v1.3's.*

- `spec/CHANGES-v1.3.md`: what changed from v1.26.1, section by section, each item citing `planning/DECISIONS-v1.3.md`; **`spec/CHANGES-v1.26.1-to-v1.3.md` is the RTL's guide from v1.26.1 to v1.3**, unit by unit, each item marked whether it affects the RTL.
- `spec/CHANGES-v1.26.1.md`: what changed from v1.26, section by section, each item citing `planning/DECISIONS-v1.26.md` section 3; **`spec/CHANGES-v1.25-to-v1.26.1.md` is the RTL's guide from v1.25 straight to v1.26.1**.
- `spec/CHANGES-v1.26.md`: what changed from v1.25 to v1.26, section by section, each item citing `planning/DECISIONS-v1.26.md`.
- `spec/CHANGES-v1.25.md`: what changed from v1.2, section by section, each item citing its decision; `planning/DECISIONS-v1.25.md` is the decisions log behind it.
- `spec/CHANGES-v1.2.md`: what changed from v1.1, section by section, each item citing its decision. A section removed in v1.2 keeps its number here as a one-line stub pointing there.
- `planning/DECISIONS-v1.2.md`: the decisions log behind v1.2, kept as the history. Where a log and this document differ, this document is the authority.
**v1.3 in brief** (in full, with the decisions behind each item: `CHANGES-v1.3.md`). **v1.3's focus is the Tracer**, C&C 3's peripheral (`planning/DECISIONS-v1.3.md` sections 1–25):
- **4.21, the Tracer** (peripheral type 3): the RV64 row machine that executes the guest and writes each window's run log for the replayers. It shares the replayer's record, region map, tables and controller-fed path, and runs a segment of 2²² rows as 64 runs of a window each. Its region and record (word 66: the files, log₂ K, `lowest_address`), control (two pulses, the word-0 write resetting everything, `RUN_CONFIG` with the compression enables [28:26], words 36–43), execution (the templates, advice from the host, the jump LRU and hash probe, the end at any JAL with immediate 0), memory (16 KB caches, the (root, depth, line) fill bus, the range comparator), output (two FIFOs, two-pass compression, output queues drained by service requests with credits, done last), boundaries, errors and timing.
- **The peripheral DMA counts 128-bit words** (4.17.9): every packet 128 bits, no remainders.
- **The controller-fed templates are broadcast** by C&C 3 when proving starts, and each tile trims them by its record's bitmap (4.19.9, 4.19.13).
- **The ground truth is a16z's Jolt** (`7189216`, the verifier's source), **gold the Java emulator**, and jolt-cpp `88dca85` the pin, checked (4.19).
- The Tracer's side of the memory system (6.7, 7), its window (8.16), invariants (9), parameters (5, 10) and items 144–158 (13).
- **The segment on the chip** (4.21.10, 6.3): C&C 1's two barriers around the trace, the per-window "armed" handshake, and the final GO that starts the replay.

**v1.26.1 in brief** (a point release; in full: `CHANGES-v1.26.1.md`). v1.26.1 is v1.26 with **exactly the four changes of `planning/DECISIONS-v1.26.md` section 3**, made with the Tracer's design so that both units start from the final layouts, and nothing else:
- **`RUN_CONFIG`, window word 32** (4.19.9, 8.11): the rows a pass runs ([23:0], so a pass can be shorter or longer than a window) and the two per-pass flags, use the store log [24] and output `rd_prev` [25]; [28:26] the Tracer's per-stream compression enables, which the replayer ignores (owner, 2026-11-05); sticky, 0 at reset. The same word on the Tracer, which ignores [24] and [25].
- **Words 32–35 are the same on both units** (4.17.4): `RUN_CONFIG`, `ROWS_RUN` (now 24 bits), `EXPANSION_OFFSET`, `EXPANSION_OPCODE`; the replayer's three status words move up one.
- **The state word's [115] and [116] are reserved** (4.19.2): the record describes only where the program is, and is loaded verbatim before every pass. The checks follow `RUN_CONFIG` [24].
- **The stream word is rearranged, contiguous, with a fourth field, advice** (4.19.2), which the replayer ignores. The record is stated as the superset of both units' state plus the window's descriptor (4.19.2).

**v1.26 in brief** (in full, with the decisions behind each item: `CHANGES-v1.26.md`). v1.26 is v1.25 with **exactly the replayer changes decided for v1.3** (`planning/DECISIONS-v1.26.md`), and nothing else:
- **The state record is 68 words, in a new order** (4.19.2): the state word, the stream word, the register image, then the files word and the template bitmap, which the replayer ignores; it loads from byte 0.
- **The stream word gives each stream's size as its 128-bit words and its values out, and its compression flag** (4.19.2); bit lengths are gone, and a stream's padding can be anything (4.19.15). The state word keeps two per-pass flags, use the store log and the new **output `rd_prev`** (moved to `RUN_CONFIG` at v1.26.1).
- **Every queue is 4 KB** (4.19.8): the read-log and write-log queues shrink from 8 KB.
- **Part 0's state is the working state** (4.19.2, 4.19.11): writing the record's words 0–65 writes the live register file, pc, `instr_idx` and offset, with no copy at a run's start, so the controller reloads them before every pass. **Writing the state word resets the pass:** it flushes the input queues, clears the size counters, sets the credits and resets the decompressors (4.19.8); `PULSE_RUN` only starts running. Preloading the queues is allowed.
- **The replayer checks its pops against the sizes** (4.19.11, 4.19.15): new `ERRORS` [3]–[5], a stream's underflow, and [6], done with input left over.

**v1.25 in brief** (a milestone between v1.2 and v1.3; in full, with the decisions behind each item: `CHANGES-v1.25.md`). v1.25 is v1.2 with the replayer and its tables brought up to **jolt-cpp `821e1bd`**, so the hardware matches today's Jolt, plus **section 8's package** of DECISIONS-v1.25 (last below). Elsewhere the machine is v1.2's:
- **The reference** is jolt-cpp `821e1bd`, checked on EVM Zone `evm-zone-4e3451c1d8c5` and Cyclone (4.19.1).
- **Jolt's new expansions** for the sub-word loads and stores and the 32-bit atomics: six new row opcodes (`VirtualLoadB/H/W`, `VirtualStoreB/H/W`), `ShiftData*` reading the base register plus a `shift_offset`, `VirtualHostIO` without operands, `VirtualAlignAddr` and the alignment assertions gone, and the opcodes renumbered (4.19.7).
- **The expansion word** gains `offset_imm` [63] and `sign_imm` [62]; the tracer-only fields `untraced` [55] and `advice_reg` [61:56] are **reserved** (section 9 of the decisions; 4.19.3).
- **`VirtualPext` and `VirtualPextSigned` are correct for any mask**, at full speed for contiguous ones (4.19.7).
- **The tables are universal**, one set per jolt-cpp version for every program: the ROM holds every expansion but the inlines and the CSR instructions (388 of 512 words), invalid encodings are invalid decode entries, and controller-fed templates are found by a normalized key (4.19.5, 4.19.6, 4.19.9).
- **Section 8's package** (DECISIONS-v1.25 section 8): **how the hardware treats software bugs**, checked rules or UB with no third kind (above); **misalignment is UB** everywhere but a jump target, which still traps (3.1, 4.1–4.5, 4.9, 4.11, 4.13); **the gather unit copies 128 bits at a time when *k* is even**, about 2 cycles per value at *k* = 2 (4.4); and **a 256-byte boot ROM** whose handler sends a two-report dump (6.2, 4.13).

**v1.2 in brief** (in full, with the decisions behind each item: `CHANGES-v1.2.md`):
- **The link word** (3.1) is 256 bits: a 128-bit payload, a 32-bit byte address, size, a 3-bit counter field, a 3-bit mode, src and dst; 74 bits reserved.
- **One 32-bit address map** (2.2) for packets and for the controller's own loads and stores: data memory, program memory, the boot ROM, big SRAM, up to three peripherals, and the registers, each a region named by the top byte.
- **Six modes** (3.1): write at address (commands and replies included), big-SRAM write by source, bucket write, augmented write, gather request, and **count only**, which decrements its counter and writes nothing (fences and "done" signals); 6–7 are reserved and discarded.
- **Seven write counters** (4.12): a packet names the counter it decrements, K cycles after the traffic director accepts it; counting no longer depends on the mode.
- **Natural alignment everywhere**, checked in hardware; spanning big-SRAM accesses are gone (3.1, 4.1).
- **Registers** (section 8): the controller's block and each peripheral's window start with the same words (`PARAMS`, `STATUS`, `ERRORS`, pulses, events, DMA). Pulses are one register per action. v1.1's conditions are now **errors**, and the wake registers are `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK` and `EVENTS_REQUIRED`.
- **Peripherals** (4.17): up to three per tile, each with a region, a standard register window, counted or live events, and optionally a DMA. The sender, mode 9 and the service lines are gone. The replayer (4.19) is peripheral 1 where present, its low-water lines now credits.
- **Errors are terminal** (4.13): an error raises the controller's hard interrupt; the handler reports to C&C 1 and parks. **There is no HALT state** (6.1): the chip raises its "toast" pin and the outside world resets it, hard or soft; every unit then starts in its **boot ROM** (6.2).
- **The three controllers** (2, 6.6) are tile instances at endpoints 64–66, with their engines as their peripherals; every chip-level function of C&C 1 is in its peripheral.

