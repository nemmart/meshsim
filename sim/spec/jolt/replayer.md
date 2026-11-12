# The replayer: machine specification, v1.35

*Part of the machine specification (`spec/machine-spec.md`), moved here whole in v1.35 with its section numbers: 4.19, the replayer's part of 8.11, its invariant (section 9, 16) and its parameters (section 10). The opcode appendix is `spec/jolt/replayer-opcodes.md`.*

### 4.19 The replayer (peripheral type 4)

*No figures yet; the figure set gains a replayer part (handoff in `v1.3/CHANGES-v1.1.md`).*

The **replayer** is a tile unit that regenerates the tile's share of a proof's execution trace, 2¹⁶ rows of Jolt's expanded trace, from the run log the tracer engine writes, and streams the rows to the unit that consumes them (the materializer, outside this specification). It recomputes every row it can with a small ALU and takes from the log only the words it cannot compute. It is **peripheral 1, type 4**, on every mesh tile (4.17), and uses the standard window throughout: its pass is a run, its queues' refills are counted credits, and it reports in two words of its own. This section specifies it in full; it follows the owner's design notes *Replayer and mesh v1.1* (updated 2026-10-23) and their shared-resource allocation table.

**Ground truth** (DECISIONS-v1.3 section 17; owner, 2026-11-04, replacing 2026-10-26's parts G and H; item 113). The trace the replayer reproduces, and the Tracer writes (4.21), is **a16z's Jolt's**: the 80-byte trace row and the sequence of pc and `instr_idx` that a16z's verifier checks a proof against. **a16z decides:** jolt-cpp has no verifier of its own; it proves `jolt_verifier::JoltProof` against a16z's `jolt_program` preprocessing, from its submodule `third-party/jolt` at **`a16z/jolt` `7189216`**, the verifier's source, so where jolt-cpp builds a different bytecode or image its proofs fail verification, and following a16z is forced, not a preference. **Gold is the Java emulator in `pipeline/gold/`** (WORKER18): self-contained, it writes the segment file byte-identical to WORKER16's tool, and its rows match jolt-cpp's on both workloads at 2²⁹ and a16z's (at `17a9459`, a descendant of `7189216` with every file the findings rest on unchanged) wherever jolt-cpp's do; where the two differ at the edges (`v1.3/planning/upstream/UPSTREAM-DIFFS.md`) the owner rules case by case, by default for a16z, and gold follows. **jolt-cpp `88dca85` is the pin, checked** (DECISIONS-v1.3 section 14 (c); item 154), its submodule being the verifier's. Both checks passed: `src/tracer/isa` is unchanged since `821e1bd`, and every window's rows, the segment files and `capture_writes`' output identical to `821e1bd`'s on both workloads at 2²⁹ (WORKER16, merged at fa59d9a). So the tables built at `821e1bd` (`data/tables/`) and this section's citations stand. jolt-cpp is checked against **as data, not code** (its rows, its segment files). The formats that are ours (the run log and its streams, the LRU coder, the state record, how template words are delivered) are ours to define, provided the rows that come out match. `spec/jolt/replayer-opcodes.md` is normative for the replayer as built. The rules tagged *(pending the upstream check, item 113)* were found by reading source before gold existed; stage 3 found no difference in any row it exercised, and each tag goes as gold's guests settle its case against a16z (item 113). *(v1.26.1: `LayerZero-Research/jolt` `735355e` was the ground truth and jolt-cpp the gold standard.)*

#### 4.19.1 Overview

**What a pass is.** A run of the replayer (`PULSE_RUN`, 4.19.11) runs from the tile's checkpoint **the rows `RUN_CONFIG` [23:0] names** (4.19.9), typically **65,536**, one window, and stops; that is one **pass** (v1.26.1, DECISIONS-v1.26 item 13; v1.26: always 65,536). The replayer runs many passes per proof, once for every sumcheck stage that needs the trace (46 per Akita proof in the repo's kernel scratchpad plan [estimate]), and every pass starts from the same checkpoint: **part 0's state is the working state** and running changes it in place, so before each pass the controller reloads the record's words 0–65 from its copy, and the state word's write resets the pass (4.19.2, 4.19.8; v1.26, DECISIONS-v1.26 items 10 and 12).

**The parts.**

| part | size | role | section |
|---|---|---|---|
| code queue | 4 KB, 256 × 128 bits | the opcode stream, raw or compressed | 4.19.8 |
| expansion queue | 4 KB, 256 × 128 bits | expansion words the controller supplies (the inlines and the CSR instructions) | 4.19.8 |
| read-log queue | 4 KB, 256 × 128 bits | the read log, raw or compressed: load values, advice, the next `instr_idx` of non-sequential rows | 4.19.8 |
| write-log queue | 4 KB, 256 × 128 bits | the write log, raw or compressed: store rows' pre-values | 4.19.8 |
| decompressors | three: code, read log, write log | 16-entry LRU decoding between each stream's queue and the core | 4.19.15 |
| decode table | 4 KB, 2K × 16 bits, built as 256 × 128 (4.19.10) | instruction → template | 4.19.5 |
| ROM | 8 KB, 512 × 128 bits, dual-ported | the templates: one expansion word per row | 4.19.6 |
| state word, stream word, register image | 16 B + 16 B + 1 KB | the checkpoint, in that order: words 0–65 of the 68-word state record, **and the working state itself** (v1.26): the image is the working register file below | 4.19.2 |
| working register file | the register image above: 1 KB, 128 × 64 bits, true dual-port | the registers a pass reads and writes, written directly by the image's packets (v1.26) | 4.19.7 |
| ALU, comparator, two adders | — | row results, branch outcomes, memory address, pc + imm | 4.19.7 |
| output queue | 32 rows of 80 B | rows waiting for the consumer | 4.19.12 |

**Its interfaces** are the standard peripheral window's (4.17; its words, 4.19.9 and 8.11). Every replayer memory and queue is written by **mode-0 packets to its region, 0x04** (4.19.10). The controller sets the pass's row count and flags in `RUN_CONFIG` (word 32), starts a pass with `PULSE_RUN`, follows it with `STATUS` [0], reads `ROWS_RUN`, `EXPANSION_OFFSET` and `EXPANSION_OPCODE` (words 33–35; v1.26.1, the same words as the Tracer's, 4.17.4), counts its refills on a write counter of its choice, and is woken by the replayer's events, run done, four queues' credits and new expansion, through the tile's live bit `EVENTS` [13] (4.19.9). Its errors, queue overrun, invalid expansion, each stream's underflow and done with input left over (v1.26), are in its own `ERRORS`, which reaches the tile's `ERRORS` [11] and traps the controller (4.19.11). It has no DMA: its rows go to their consumer inside the tile (4.19.12). It keeps v1.1's behaviour, except that **its queues' low-water lines are now counted credits** (4.19.8) and its errors are terminal.

**Rate.** A row takes a fixed 3 cycles, or 5 for a multiply (4.19.7), plus stalls. **Measured at v1.25** [meas: v1.25's acceptance runs, Quest 083 and 084: meshsim's Stage B on every window of both workloads at 2²⁹, `v1.3/planning/sessions/WORKER11-REPORT.md` section 5]: on the windows holding real rows, **3.32 cycles per row on EVM Zone** (per window 3.00–3.59, median 3.32) and **3.16 on Cyclone** (3.00–3.50, median 3.10); over all 8,192 windows, the padding ones included (3.00), 3.21 and 3.11. A pass is so about 218K cycles on average and at most 235K, **about 0.22 ms at 1 GHz with everything local, 0.24 ms at worst**. Stalls are 0.24 % of a pass on EVM Zone and 0.62 % on Cyclone, most of it the fixed image and code loads (190 cycles a pass), the write log's waits and, on Cyclone, controller-fed expansions (4.19.9). The consuming kernels likely need 3–6 cycles per row anyway [estimate].

**The reference** (v1.25; DECISIONS-v1.25 sections 1 and 2). The rows must match Jolt's bit for bit. **v1.3's pin is jolt-cpp `88dca85`, checked** (ground truth, above); its tracer's `isa/` is unchanged from `821e1bd`, so what follows, built and checked at `821e1bd`, stands. The reference is **jolt-cpp `821e1bd`** (`snarkify/jolt-cpp@821e1bd2f433d6ca62300afbbbba031db50e7dc9`, `main` of 2026-10-30), as copied into the standalone tracer on the `tracer-standalone-main` branch of `jolt-asic-planning` (PR #2). v1.2's reference was the earlier copy on `tracer-standalone` (PR #1, commit `4f36b3d`, jolt-cpp of 2026-10-19). The reference workloads are jolt-cpp's own: **EVM Zone, release `evm-zone-4e3451c1d8c5`**, input `trace-29-4599adc50712` (it replaces the EVM Zone program v1.2's measurements used), and **Cyclone**, input/29, unchanged; both pad to 2²⁹ rows. Three functions are normative: `expand_virt_seq` (`isa/virtual.cpp:132`) for what an instruction expands to, `execute_instruction` (`trace_owner.cpp:145`) for what a row computes, and `expand_compressed_chunk` (`trace_owner.cpp:855`, the row at 880–913) for what a row records. Citations in this section are to files under the tracer's `tracer/src/tracer/`. **Those marked 821e1bd, and the three just given, are checked at `821e1bd`; the others were taken at `4f36b3d` and name the same code, whose line numbers may have moved** *(item 137)*. Where this section departs from the reference (log-fed values, unchecked assertions), it says so.

**Status.** The design notes record most of this design as candidate and some of it as decided (the run log, 2026-10-22 and 2026-10-23; the fixed row schedules, every instruction a template, the queues as true FIFOs and the resident region, all 2026-10-23; the allocation table, confirmed 2026-10-23). This section states the notes' design unmarked, as the design to build; its own additions are marked *(Proposed)* and gathered in section 13 (items 33–59), and so are the details the owner accepted as proposals on 2026-10-23. Facts about Jolt's expansions are cited to the tracer; the template enumeration that would confirm them across real programs is open (item 34).

#### 4.19.2 The run log and the checkpoint

The tracer engine writes, per tile, **three streams** and a checkpoint (owner, 2026-10-26: the run log is split in two). As much of them as a stage's free big SRAM allows stays resident (4.19.13); they reach the replayer through its queues, each stream raw or compressed (4.19.15).

| stream | granularity | contents | per tile |
|---|---|---|---|
| **opcode** | one 32-bit word per native instruction, held while its expansion runs | the RISC-V instruction; a compressed one is stored decompressed with bit 0 cleared as its flag | about 43K words (175 KB) at 1.5 rows per native instruction; 256 KB at most [estimate] |
| **read log** | one 64-bit word per row that needs one from it, in row order | load values, advice, and the next `instr_idx` of every row that doesn't continue sequentially | 0.192 words per real row on EVM Zone, 0.206 on Cyclone [meas: Quest 085, below] |
| **write log** | one 64-bit word per row that needs one from it, in row order | store rows' pre-values | 0.156 words per real row on EVM Zone, 0.100 on Cyclone [meas: Quest 085] |

Together the two logs are 0.35 words per real row on EVM Zone and 0.31 on Cyclone, and up to 0.833 in Cyclone's densest windows (0.418 in EVM Zone's): raw, about 175–190 KB for a median window and 437 KB at most [meas: Quest 085]. A pass that needs only one log fetches only that one.

Every stored instruction has bit 1 set, so **bits [1:0] = 00 mark a padding word** (below) and bits [1:0] = 10 a compressed instruction.

**Which rows take a log word, and from which log.** At most one per row, in row order; **the row kind decides the log,** and follows from the expansion word, so no extra encoding is needed *(Proposed: the rule)*: a **store row** (SD, `VirtualStoreB`, `VirtualStoreH` or `VirtualStoreW`) takes the **write log**, every other log row the **read log**. The memory rows are jolt-cpp's `is_load` and `is_store` rows, which at `821e1bd` are LD and SD and the six narrow ones that the sub-word loads and stores, LR.W, SC.W and the 32-bit AMOs expand to (4.19.7):
- **A load row** (LD, `VirtualLoadB/H/W`): the doubleword loaded. A narrow load row loads the whole doubleword holding the access.
- **A store row** (SD, `VirtualStoreB/H/W`; the AMOs' and SC's store rows included): the memory doubleword's pre-value; the post-value is rs2's value. **Write log.**
- **Advice:** `VirtualAdvice` and the DIV/REM advice rows (4.19.7, class L): the advice value.
- **A control transfer:** a row with `next_from_value` set (4.19.3) whose new pc differs from its pc (4.19.7): the next `instr_idx`.

Load rows, advice and control transfers take the **read log**.

The first three are the rows whose expansion word has `from_log` set (4.19.3).

**The densities at v1.25** (words per real row, load / store / advice / non-sequential) [meas: Quest 085, every window of both v1.25 deliveries at 2²⁹, counted from the reference rows and checked word for word against each state record's raw stream lengths]: **0.181 / 0.156 / 0.00001 / 0.011 on EVM Zone (total 0.348)** and **0.163 / 0.100 / 0.0003 / 0.043 on Cyclone (total 0.306)**. Per full window of real rows the total's median is 0.366 on EVM Zone (0.172–0.418) and 0.330 on Cyclone (0.107–0.833). **Cyclone's dense stretches are sustained, not bursts:** its worst 512-, 1,024- and 4,096-row spans hold 0.838, 0.836 and 0.834 words per row, most of it write log (up to 0.754); EVM Zone's are 0.781, 0.683 and 0.600. A 64-window replay pass reads on average **7.7 MB of log raw on EVM Zone, 6.3 MB with each stream in its smaller form**, and 7.1 MB and 2.9 MB on Cyclone [same]. The totals match the derived expectation from v1.2's figures, about a ninth above `4f36b3d`'s 0.315 and 0.277, with v1.25's shorter expansions removing rows but no log words; the mix moved, since the workload changed: EVM Zone's stores rose and its non-sequential words fell. What the logs cost the replayer **at v1.25** is small: read-log waits average 59 cycles per window on EVM Zone and 63 on Cyclone, write-log waits 267 and 222, together about 0.15 % of a pass [meas: v1.25's acceptance runs, Quest 083 and 084: meshsim's Stage B on every window of both workloads at 2²⁹, `v1.3/planning/sessions/WORKER11-REPORT.md` section 5]. **At v1.26, with 4 KB queues** (4.19.8), they cost less: read-log waits average **39.2** cycles per window on EVM Zone and **43.5** on Cyclone, write-log waits **100.0** and **84.5**, together **0.066 %** and **0.063 %** of a pass. The 8 KB control (the same v1.26 model and controller with v1.25's 512-entry logs) waits the same on the read log and **176.2** and **153.6** on the write log, 0.102 % and 0.097 % [meas: Quest 098 and 099 (4 KB) and 100 and 101 (8 KB): meshsim's Stage B on every window of both workloads at 2²⁹, `v1.3/planning/sessions/WORKER20-REPORT.md` section 5; item 143]. The control, not v1.25's figures, is the comparison for the queue size, since v1.26 changed the pass start (4.19.11). The waits are all at a pass's start (4.19.8).

**A requirement on our tracer engine.** The run log differs from jolt-cpp's memory log on purpose. jolt-cpp logs only its load and store rows and `VirtualAdvice` (`trace_owner.cpp:822–855`, 4f36b3d; at `821e1bd` the load and store rows include the six narrow ones): it computes the DIV/REM advice in its interpreter (`trace_owner.cpp:545–630`) and looks a jump's next index up in a pc-to-index table (`finish_instruction`, `trace_owner.cpp:111–126`). The replayer has no divider and no such table, so **the tracer engine must write the DIV/REM advice and the next `instr_idx` of every control transfer into the run log**, in row order with the other log words (item 33). **From v1.3 that engine is the Tracer** (4.21), which writes them by 4.21.4's table, the advice from the host (DECISIONS-v1.3 section 13). The validation harness (item 58) proves the format, the tracer engine's extra words and the replayer together.

**The checkpoint.** Per tile, loaded by mode-0 writes into the replayer's part 0 (4.19.10), **state first** (owner, 2026-10-26), so a sequential load writes the state, then the streams' descriptor, then the registers:
- the **state word** (128-bit word 0): pc [63:0], `instr_idx` [95:64], next offset [111:96] (the rows of the first instruction's expansion that the previous tile already ran); [114:112] reserved (v1.25's compression flags, which moved to the stream word; written 0 and ignored, not reused); [116:115] **reserved** (v1.26's per-pass flags, use the store log and output `rd_prev`, which moved to `RUN_CONFIG` at v1.26.1, DECISIONS-v1.26 item 15; written 0 and ignored, not checked); spare [127:117]. **The state word describes only where the program is.** **Writing it resets the pass** (4.19.8): it flushes the four input queues, clears the size counters, sets the credits and resets the decompressors;
- the **stream word** (word 1; DECISIONS-v1.26 item 2): each stream's size as two numbers, **its words** (the 128-bit entries it occupies as sent, padding included) and **its values out** (the 32- or 64-bit values it decodes to, where the decoder stops), and **its compression flag** (moved from the state word at v1.26), and, for the Tracer, **the advice values** (v1.26.1, DECISIONS-v1.26 item 16, from DECISIONS-v1.3 section 23). The fields are contiguous, by kind (v1.26.1):

| bits | field | the replayer |
|---|---|---|
| [15:0] | code: words | used |
| [31:16] | read log: words | used |
| [47:32] | write log: words | used with the store log |
| [64:48] | code: values out | used |
| [81:65] | read log: values out | used |
| [98:82] | write log: values out | used with the store log |
| [99] | code: LRU-compressed | used |
| [100] | read log: LRU-compressed | used |
| [101] | write log: LRU-compressed | used with the store log |
| [124:102] | **advice: values** (0 to 2²² inclusive; uncompressed 64-bit values, so its words are ⌈values / 2⌉, and it has no flag) | **ignored**: a replayer's advice comes in the read log |
| [127:125] | spare (0) | |

**The advice field is the Tracer's:** a segment's advice size at load, counting down live as the Tracer pops it (DECISIONS-v1.3 sections 23 and 24). A replayer neither checks nor changes it, and a record passes it through unchanged.

A stream is at most 65,536 values (one per instruction or per row) and 33,792 words (65,536 compressed 64-bit misses of 66 bits; raw, 32,768 for a log and 16,384 for code), so 16 and 17 bits suffice. **Words** is the fewest 128-bit entries holding the stream: ⌈values / 4⌉ for raw code, ⌈values / 2⌉ for a raw log. v1.25's stream word gave three exact bit lengths instead (code [23:0], read log [55:32], write log [87:64]); v1.26's placed the word counts at [47:0], the flags at [50:48] and the value counts at [114:64], with no advice field.

- the **register image** (words 2–65): all 128 registers, as 64 words of 128 bits, each a pair, the low register in the low half: image word *i* (128-bit word 2 + *i*) holds register 2*i* in bits [63:0] and 2*i* + 1 in [127:64], so registers 0 and 1 are in the first and 126 and 127 in the last.

**The state record** (owner, 2026-10-26, part G; **68 words in a new order at v1.26**, DECISIONS-v1.26 item 1) is one record per window, 68 × 128-bit words (1,088 bytes), one format for the host, the Tracer and the replayer, landing in part 0 at consecutive offsets from 0x000 (4.19.10):

| word | offset | contents | the replayer |
|---|---|---|---|
| 0 | 0x000 | the state word (above) | used, as the live pc, `instr_idx` and offset; its write resets the pass |
| 1 | 0x010 | the stream word (above) | used: the three streams' fields, for the pass's size checks; the advice field ignored |
| 2–65 | 0x020–0x41F | the register image (above) | used, as the live register file |
| 66 | 0x420 | **the files word:** the guest's memory file and the jump-target map file, each a root and a depth, and the proof's bounds; its layout is the Tracer's: memory root [30:0] and depth [32:31], map root [63:33] and depth [65:64], log₂ K [71:66], 0x8000_0000 − `lowest_address` [102:72] (4.21.2; v1.3) | ignored |
| 67 | 0x430 | **the template bitmap:** bit *i* set if the window uses controller-fed template *i* (4.19.5) | ignored |

**The replayer needs only words 0–65**, v1.25's part-0 image: writes to words 66 and 67 are accepted and ignored, and a controller may simply not send them. **The record loads from byte 0**, so it streams straight into part 0. The tile's controller reads the bitmap from the record itself (in big SRAM, 4.19.13) to trim its templates. v1.25's record was 67 words, the bitmap first, and was sent from byte 16 onward.

**What the record is** (owner, 2026-11-04; DECISIONS-v1.26 item 17; v1.26.1). **The record is the superset of both units' state, plus the window's descriptor:** one 68-word format, each unit using what it needs and ignoring the rest, so the host, gold, the segment files, the Tracer's DMA and the tiles all write one layout. **The test for state is resuming:** what a unit needs to continue execution elsewhere, as RISC-V's `satp` is saved with a hart's context though running never changes it. The row count and the two per-pass flags fail it, so they are in `RUN_CONFIG` (4.19.9); the files word passes, being the Tracer's context.

| words | the Tracer | the replayer | what it is |
|---|---|---|---|
| 0, 2–65 | the program's state | the program's state | state |
| 1, the advice field | the live input count | ignored | state (the Tracer's) |
| 1, the three stream fields | outputs, written at done | inputs, the pass's size checks | the window's descriptor |
| 66, the files word | its context: the files, K, `lowest_address` | ignored | state (the Tracer's) |
| 67, the template bitmap | an output, written at done | ignored (the tile's controller trims its templates by it) | the window's descriptor |

**The window's descriptor is in the record for pragmatism, not principle:** word 1's stream fields and word 67 describe one window rather than state (reset at each run, produced at done, and delivered to tile *n* while the state goes to tile *n* + 1, DECISIONS-v1.3 section 24). They travel with the window because the Tracer's DMA delivers them with it for free; the alternative for the bitmap, four read-only status registers, would have C&C 3 read them at every done and deliver them itself, a second path. The stream fields have the stronger case, since the replayer's checks read them.

**Use the store log** (`RUN_CONFIG` [24], 4.19.9; v1.26.1, DECISIONS-v1.26 items 13 and 15; v1.26's state word [115], v1.25's "includes store log", owner, 2026-10-26). When 1, store rows pop the write log as above. **When 0, the pass doesn't use stores' pre-values:** store rows pop nothing and output a pre-value of 0, the write-log queue is never fed, **the write log's sizes in the stream word are ignored** (neither underflow, `ERRORS` [5], nor leftover, [6], 4.19.11; DECISIONS-v1.26 items 8 and 15), and the controller loop leaves its credit bit out of the mask for the whole pass (4.19.8). Write-log entry 15 may keep updating on load rows (4.19.15); it is never read. A store row's pre-value never feeds a register, so register state is unaffected. The controller chooses it per pass: 1 for a consumer that needs pre-values, 0 otherwise, and then it never fetches the write log.

**Output `rd_prev`** (`RUN_CONFIG` [25], 4.19.9; new at v1.26 as the state word's [116], DECISIONS-v1.26 item 8; moved at v1.26.1, items 13 and 15). When 1, each output row's `rd_prev` field (row word 3 [63:0], 4.19.12) is rd's value before the row, as at v1.25. **When 0, the field is 0 in every row** of the pass, for a consumer that doesn't need it; the register file, every other field and the logs are unaffected. The controller chooses it per pass.

**Nothing in the record changes per pass** (DECISIONS-v1.26 item 15; v1.26.1): before every pass a controller reloads the record's words 0–65 **verbatim** (below), and the pass's own choices, its row count and two flags, are in `RUN_CONFIG` (4.19.9). The stream word, the write log's sizes included, stays exactly as the tracer produced it. *(v1.26: the state word carried the two flags, the one change a controller made to its copy for a pass.)*

Sending all 128 registers avoids special-casing a mid-sequence start, at 1 KB per tile. **A tile that starts inside an expansion** has that instruction as its first opcode-stream entry, and the next offset says where in the expansion to begin. jolt-cpp's chunked replay instead rewinds to the start of the expansion when inline scratch registers are live (`trace_owner.cpp:722–739`); the full image makes that unnecessary. **Register 0** of the image is ignored: the working file's register 0 always reads 0 *(Proposed, item 41)*.

**The working state and re-runs** (DECISIONS-v1.26 item 12; v1.26). **Part 0's state is the working state:** there is no separate set of registers and no copy at a run's start. Writing the record's words 0–65 writes the live pc, `instr_idx` and offset (the state word's fields) and the live register file (the image), and **running changes them in place**: after a pass, part 0 holds the window's end state, its stream word unchanged. So **the controller reloads words 0–65 before every pass**, from the tile's copy of the record (4.19.10, 4.19.13); the state word's write resets the pass (4.19.8), and every pass of the same window starts identically. The cost is v1.25's: 66 packets, about 66 cycles from big SRAM, where v1.25's run copied the image internally in 64. **The checks read the sizes from part 0** (the stream word) **and the flags from `RUN_CONFIG`** (v1.26.1), neither of which can be written while a pass runs (4.19.9, 4.19.10), so nothing needs latching. *(v1.25: part 0 was a stored copy, which each run copied into working registers, latching the flags and lengths; a per-pass change resent the state word alone.)*

**Inline scratch.** Registers 48–127 hold an expansion's scratch values and are 0 between expansions (`isa/virtual.hpp:25–27`). Inline templates end with explicit `ADDI vN, x0, 0` rows that zero each scratch register they used (for example the end of `inlines/blake3.inc`), so the replayer needs no state for them. jolt-cpp also zeroes them defensively at chunk starts (`normalize_inline_vregs`, `trace_owner.cpp:128`); with the full register image the replayer doesn't need to.

**End of trace** *(pending the upstream check, item 113)*. The trace is padded to 2²⁹ rows after the program ends. The program's last real row jumps to pc 0: a JAL with immediate 0 sends pc to 0 (`trace_owner.cpp:281`), and its log word sets `instr_idx` to 0. The tracer writes the padding as real opcode entries: one opcode word 0x00000000 per padding row, with no log words. The padding word maps to the ROM's padding template (4.19.6), an all-zero NOP row with `hold` set, so pc and `instr_idx` stay 0, as in the tracer's padding, where every padded row is zero and decodes as the zero-filled NOP at bytecode index 0 (`trace_owner.hpp:160–163`; real instructions start at index 1, `isa/virtual.cpp:1359`). **From v1.3** (DECISIONS-v1.3 section 25) the end is **any JAL with immediate 0, whatever its rd**, EBREAK and `JAL ra, 0` included, which a16z treats as the pc stalling; the Tracer emits this encoding unchanged (4.21.4).

#### 4.19.3 The expansion word and the operand codes

Every row is described by one **128-bit expansion word**, in place of the tracer's 48-byte `VirtualInstruction` (`isa/virtual.hpp:89–126`). The same format serves the ROM and the words the controller supplies.

| bits | field | contents |
|---|---|---|
| [7:0] | `opcode` | the row opcode, 0–170, numbered in the tracer's order at `821e1bd` (`isa/riscv.inc`, enum `OpCode`; `data/tables/opcode_table.txt` lists all 171); 4.19.7 lists the ones that occur in rows |
| [15:8] | `rd` | operand code (below) |
| [23:16] | `rs1` | operand code |
| [31:24] | `rs2` | operand code |
| [43:32] | `seq_remaining` | rows left in the expansion after this one; 0 on its last row. At most 3,120 (`KECCAK256_ABSORB_PERMUTE` expands to 3,121 rows, `isa/virtual.cpp:1310`) |
| [44] | `next_from_value` | this row can change control flow: when it does (4.19.7), the next `instr_idx` comes from the log |
| [45] | `hold` | pc and `instr_idx` don't advance after this row (padding) |
| [47:46] | `rd0` | the rd = x0 policy of the template (4.19.4): 0 none, 1 NOP (the NOP row is virtual *(pending the upstream check, item 113)*), 2 remap, 3 NOP (the NOP row is not virtual: the source is ADDI). Read from the **first word delivered** for an expansion only: ROM[start] on the ROM path, the first word the controller sends on the controller-fed path (4.19.4) |
| [50:48] | `format` | the source instruction's immediate format, for code 132: 0 R, 1 I, 2 S, 3 B, 4 U, 5 J; 6 and 7 reserved |
| [51] | `virt` | the row's opcode differs from the source instruction's *(Proposed, item 35)* |
| [52] | `imm0_pc0` | Jolt's program-end convention: if this row is a JAL whose immediate is 0, its new pc is 0 (4.19.7). **Set in every JAL word, ROM or controller-fed** (owner, 2026-10-26, item 56) |
| [53] | `report_c` | report `is_compressed` on this row when the instruction is compressed (4.19.12) *(Proposed, item 56)* |
| [54] | `from_log` | the row consumes one log word: rd's value, or an SD row's memory pre-value (4.19.7) *(Proposed, item 56)* |
| [55] | reserved | 0 (DECISIONS-v1.25 section 9): v1.25's `untraced` until 2026-11-01, used only by a tracer. With advice from the host in the read log, the Tracer has no untraced rows, and its templates become the replayer's. **A word with a reserved bit set is UB** (meshsim stops): it can only come from our own table builder (top) |
| [61:56] | reserved | 0 (DECISIONS-v1.25 section 9): formerly the Tracer's `advice_reg`; nothing sets it now. A word with it set is UB (meshsim) |
| [62] | `sign_imm` | the row's immediate is **signed**: Jolt's immediate (`RiscCycle::imm()`, 128 bits, `trace_owner.hpp:63–66`, 821e1bd) is the row's 64-bit immediate sign-extended when set and zero-extended when clear (below). The replayer computes nothing from it; it reports it with the row (4.19.12) |
| [63] | `offset_imm` | the row's **`shift_offset`** is the source instruction's immediate (code 132's value) when set, and 0 when clear (below). Only `VirtualShiftData…` rows use `shift_offset`, and the table builder sets the bit on no other row; the replayer ignores it elsewhere, and meshsim flags it (invariant 16; DECISIONS-v1.25 section 7, Q3) |
| [127:64] | `const` | a constant immediate (code 128) |

**`virt`** *(Proposed, item 35)*. Jolt marks a row virtual when its expansion has more than one row **or its opcode differs from the source instruction's** (`isa/virtual.cpp:183`). The second half is not derivable in hardware, since the replayer never sees the source instruction as a row opcode: SLLI becomes one `VirtualMULI` row and is virtual, while ADD stays one ADD row and is not. The template builder therefore sets `virt` on every word whose opcode isn't the source's, and the replayer derives the rest (below).

**`shift_offset`** (DECISIONS-v1.25 sections 1 and 4). At `821e1bd` jolt-cpp's `VirtualInstruction` has a signed 32-bit `shift_offset` (its byte 12, formerly padding), used only by `VirtualShiftDataB/H/W`: the row's rs2 is the access's **base register**, the byte lane is taken from rs2 + `shift_offset` (4.19.7), and the row's immediate is `shift_offset` (`imm_raw`, `trace_owner.hpp:168–177`, 821e1bd). In every expansion `shift_offset` is either the source instruction's immediate (SB, SH and SW) or 0 (SC.W and the 32-bit AMOs), so the expansion word carries it as one bit, `offset_imm`, rather than a field.

**`sign_imm`** (DECISIONS-v1.25 sections 4 and 5). Jolt reports a row's immediate as a 128-bit value, the 64-bit immediate read as signed or as unsigned by opcode: signed for jolt-cpp's `signed_immediate` set (`isa/riscv.hpp:209`, 821e1bd), which is the load and store rows (the six narrow ones included), the conditional branches and, new at `821e1bd`, `VirtualShiftDataB/H/W`. v1.2 left that set to the row's consumer, which normalized the immediate by opcode; v1.25 makes it **table data**: the table builder sets `sign_imm` from jolt-cpp's own `signed_immediate`, so a change to Jolt's set means rerunning the builder, not changing the hardware. A bit rather than a new operand code, because codes 146–255 are kept reserved (an invalid expansion). The 64-bit immediate itself is unchanged: code 132 already sign-extends the source immediate to 64 bits.

`seq_remaining` = 0 marks the end of an expansion; there is no separate end bit. The per-instruction fields (`rd0`, `format`, `next_from_value`) live in the ROM words, not the decode table, because every instruction is a template (4.19.5).

**Operand codes.** Each of `rd`, `rs1` and `rs2` names where the slot's value comes from:

| code | meaning | slot kind |
|---|---|---|
| 0–127 | register: x0–x31, then virtual registers 32–127 | register |
| 128 | `const` (also the zero placeholder, with `const` = 0) | immediate |
| 129, 130, 131 | the source instruction's rd, rs1, rs2 field (bits [11:7], [19:15], [24:20]) | register |
| 132 | the source instruction's immediate, decoded by `format` and sign-extended to 64 bits (`isa/decoder.cpp:74–212`); 0 for format R | immediate |
| 133 | `mask_trailing_zeros(imm & 0x3f)`: ones in bits [63:*s*], *s* = `imm & 0x3f` (SRLI, SRAI; `isa/virtual.cpp:565–573`, `../utils/math.hpp:134`) | immediate |
| 134 | `1 << (imm & 0x3f)` (SLLI → `VirtualMULI`; `isa/virtual.cpp:596–600`) | immediate |
| 135 | `1 << (imm & 0x1f)`, as a 32-bit value (SLLIW → `VirtualMULIW`; `isa/virtual.cpp:616–620`) | immediate |
| 136 | `(1 << 32) − (1 << (imm & 0x1f))`, the 32-bit window bitmask (SRLIW, SRAIW; `isa/virtual.cpp:575–579`, `629–633`) | immediate |
| 137–144 | temporary *k* = code − 137, resolved as register 40 + *k* + offset (4.19.4) | register |
| 145 | **immediate 0**: an immediate slot whose value is 0, independent of `const` and of the source instruction (owner, 2026-10-27, part L) | immediate |
| 146–255 | reserved: an invalid expansion (4.19.11) | — |

In codes 133–136, `imm` is the source instruction's immediate, the same value as code 132; the masks make the shift-immediate formats' funct bits irrelevant (the tracer decodes shift immediates with their own shamt format, `isa/decoder.cpp:98`, and only these codes ever use them). Templates never contain literal registers 40–47; they use the temporary codes.

**Two immediate slots** (part L). A word holds only one constant, so a row with two immediate slots, a non-zero constant and a zero, writes the zero slot as **code 145**, in ROM and controller-fed words alike. Such rows occur in the controller-fed secp256k1 templates (`LUI rd, #0x1000003d1, #0`) and in the ROM, as the first row of the LR/SC and AMO templates (`LUI v40, #0x80000000, #0`): **17 rows in EVM Zone's tables and 28 in Cyclone's** (15 and 26 controller-fed, plus 2 ROM each) [meas: tracer enumeration]. A slot that is 0 in every instance, with no other constant in the row, is written as code 128 with `const` = 0, as the tracer now does. Before part L the zero slot borrowed code 132, which reads 0 only because every inline is R-format; tables built that way still replay correctly, but new tables use 145.

**The slot kind is committed.** A register slot reads its register; an immediate slot carries its value, and Jolt records which slots are immediates (`rd_is_imm`, `rs1_is_imm`, `rs2_is_imm`, `isa/virtual.hpp:100–102`). So code 0 (x0) and code 128 with `const` = 0 differ: a zero placeholder must not replay as x0.

**The row's immediate** is the sum of its immediate slots' values, as Jolt computes it (`imm_raw`, `trace_owner.hpp:168–177`, 821e1bd), **except on a `VirtualShiftData…` row, whose immediate is its `shift_offset`** (its three slots are registers, 4.19.7); in every template the tracer emits, at most one immediate slot can be non-zero [derived: every word of the v1.25 tables, ROM and controller-fed].

**Derived, not stored:**
- `is_first`: the row is the first of its expansion and the expansion is virtual (`isa/virtual.cpp:186`);
- `is_virtual`: `virt` is set, or the expansion has more than one row (`seq_remaining` > 0, or the row is not the first); for the shared NOP row, `rd0` = 1 (4.19.4);
- `pc_increment`: 0 unless `seq_remaining` = 0, then 2 for a compressed instruction (opcode word bit 0 = 0) and 4 otherwise; only an expansion's last row carries it (`isa/virtual.cpp:1416`);
- `is_compressed`: the instruction is compressed and the word has `report_c` set. Jolt reports it only where `pc_increment` = 2 (`trace_owner.hpp:184`, `trace_owner.cpp:899`), that is on an expansion's last row, so the template builder sets `report_c` on last words only;
- `risc_pc`: the pc register, the same for every row of an expansion (`isa/virtual.cpp:1413–1415`).

**Jolt's conventions are data where they can be** *(Proposed, item 56)*. Each place where the replayer must copy a Jolt convention rather than plain RISC-V is a flag in the expansion word or a code in it, set by whoever builds the tables, so a change in Jolt means new tables rather than new silicon: `virt` (which rows Jolt calls virtual), `rd0` 1 and 3 (whether the rd = x0 NOP row is virtual), `imm0_pc0` (JAL to immediate 0 ends the program), `report_c` (which rows carry `is_compressed`), `from_log` (which rows take a log word), `sign_imm` (which rows' immediates are signed; v1.25), `offset_imm` (which rows take the source immediate as `shift_offset`; v1.25), and the advice rows' operands (4.19.7). What stays in logic is listed in 4.19.7 and item 57.

#### 4.19.4 The rd = x0 policy and the temporary offset

Jolt rewrites an instruction whose rd is x0 before expanding it (`isa/virtual.cpp:307–319`):
- with **no side effect beyond writing rd**, it becomes one NOP row, `ADDI x0, x0, 0` (`isa/virtual.cpp:307–311`);
- with **side effects**, rd is remapped to a throwaway virtual register, v40, and the template's own temporaries then start at v41 (`isa/virtual.cpp:315–319`; temporaries are allocated upward from 40, `isa/virtual.cpp:140`, `148–153`);
- **a few instructions handle rd = x0 themselves** and are expanded unchanged (`rd0_is_handled_internally`, `isa/virtual.cpp:57–102`).

The replayer reproduces this with the template's `rd0` policy and a one-bit **offset**.

**The rule** *(pending the upstream check, item 113)*. For one expansion, offset = 1 when the source instruction's rd field (bits [11:7]) is 0 and the template's `rd0` is remap; otherwise 0. Then:
- temporary *k* (codes 137–144) resolves to register 40 + *k* + offset;
- under remap, the source rd (code 129) resolves to register 40.

Under a **NOP** policy with rd = x0, the instruction becomes the one shared NOP row (4.19.6) in place of its template. The tracer marks that row virtual unless the source instruction is ADDI itself (`isa/virtual.cpp:183`), so the policy says which: `rd0` = 1 makes the NOP row virtual (and so `is_first`) *(pending the upstream check, item 113)*, and `rd0` = 3, used only by ADDI's template, makes it not virtual *(Proposed, item 35)*. The row's virtual-ness is data, not an opcode compare.

**Example.** `lb a5, 4(a0)` against `lb x0, 4(a0)` (`expand_load`, `isa/virtual.cpp:239–247`, 821e1bd):

| row | normal (offset 0) | remapped (offset 1) |
|---|---|---|
| 1 | `VirtualLoadB v40, a0, 4` | `VirtualLoadB v41, a0, 4` |
| 2 | `VirtualWindowMaskB v41, a0, 4` | `VirtualWindowMaskB v42, a0, 4` |
| 3 | `VirtualPextSigned a5, v40, v41` | `VirtualPextSigned v40, v41, v42` |

**Which instructions remap**: those in `has_side_effects_beyond_rd` and not in `rd0_is_handled_internally` (`isa/virtual.cpp:19–55`, `57–102`):

| instructions | served by | effect |
|---|---|---|
| JAL, JALR, LD | one-word ROM templates | rd becomes 40; e.g. `ret` becomes `JALR v40, ra, 0` |
| LB, LBU, LH, LHU, LW, LWU | ROM templates | rd becomes 40, temporaries + 1 |
| the 18 AMOs | ROM templates | rd becomes 40, temporaries + 1; no stored variants |

LR/SC *(pending the upstream check, item 113)*, CSRRW, CSRRS, ECALL, EBREAK, MRET and the inlines are exempt (`rd0` = 0), as is every instruction whose rd field is an immediate (stores, branches, `VirtualAssertEQ`, FENCE; `has_rd0_register`, `isa/virtual.cpp:104–106`, 821e1bd). **`VirtualHostIO` is one of these at `821e1bd`:** it has no operands (the decoder makes all three slots immediates and accepts it only with rd = 0, `isa/decoder.cpp:1264`, 821e1bd), so it is never remapped, and its one-word template has `rd0` = 0; v1.2 remapped it. JAL and JALR also allocate their own throwaway register when rd is x0 (`isa/virtual.cpp:1165–1185`, 821e1bd); after the generic remap that case never arises, and both paths give rd = 40, so one-word remap templates reproduce them. With the decode table's AMO index (4.19.5), every atomic reaches its own template and the offset handles the remap in hardware.

**v1.3: a16z decides two cases differently** (owner, 2026-11-05; DECISIONS-v1.3 section 17; `v1.3/planning/upstream/UPSTREAM-DIFFS.md` D13, D14), and gold follows a16z:
- **D13, the rewrite row isn't virtual:** a16z emits the `ADDI x0, x0, 0` an rd = x0 instruction is rewritten to with no virtual sequence, whatever the source instruction, where jolt-cpp marks it virtual (and first in sequence) unless the source is ADDI. Under a16z's rule `rd0` = 1 and 3 give the same row.
- **D14, LR and SC are remapped:** a16z exempts only ECALL, MRET, EBREAK, CSRRW and CSRRS from the remap, so an LR or SC with rd = x0 has rd remapped to v40 and its temporaries moved up one, like the other side-effecting instructions; jolt-cpp exempts LR/SC (and the inlines, which changes no row of a provable program).

**The hardware is unchanged** (the policy and the offset are data); **the universal tables are not yet**: `data/tables/replayer/` is still built from jolt-cpp `821e1bd`, with `rd0` = 0 on the LR/SC templates and `rd0` = 1 on every NOP-policy template but ADDI's. Neither workload has such an instruction. **Decided (owner, 2026-11-06; item 158): the tables are rebuilt to a16z's rule right after v1.3's freeze, not in v1.3**; it changes no hardware. The rest of this subsection describes the tables as they stand.

**What the remap and the exemptions produce** (owner, 2026-10-26; `spec/jolt/replayer-opcodes.md` section 5) *(pending the upstream check, item 113; v1.3: D13, D14 above)*:
- **The remapped link is really written:** a JAL or JALR with rd = x0 leaves its link (pc + `pc_increment`) in v40, which is not an inline temporary and keeps that value across instructions (it is below 48, so the tracer never zeroes it); the record shows rd = 40 and the link.
- **The exempt instructions emit rows that name x0:** `LD x0` (LR.D), `VirtualPextSigned x0` (LR.W) and `XORI x0` (SC.W, SC.D). Register 0 drops the write (4.19.7), and an LD to x0 reports a memory value of 0 (4.19.7, 4.19.12).
- **EBREAK's single row is `JAL v40, 0`** (4.19.5): it allocates v40 itself, and ends the program.

**The temporaries stay in 40–47.** The tracer asserts that no expansion, remap included, allocates beyond register 47 (`isa/virtual.cpp:148–153`), so every correct template keeps 40 + *k* + offset ≤ 47. The replayer doesn't check it *(Proposed, item 36)*.

**Mid-sequence start.** The offset is recomputed from the opcode-stream entry's rd field and the `rd0` of the template's **first word**, ROM[start], not ROM[start + offset].

**Every expansion word, ROM or controller-fed** (owner, 2026-10-26, part G, ruling N3). The rd = x0 handling applies to every expansion the same way: the ROM is a cache of templates (4.19.6), so a word means the same whichever path delivers it. Today's controller-fed expansions (the inlines and the CSR instructions) happen to be exempt, so their offset is 0, but that describes the current templates, not a rule. The controller-fed path follows the same rule with two mechanisms:
- **`rd0` is read from the first word delivered** for an expansion (4.19.3): on a mid-sequence start the controller sends words from the next offset, so it **copies the template's `rd0` into the first word it sends**;
- **under a NOP policy with rd = x0,** the controller supplies **the single NOP word** (the shared NOP row, 4.19.6) instead of the template. It knows the instruction word (`EXPANSION_OPCODE`) and the template's policy. **That word carries the template's `rd0` (1 or 3) in bits [47:46]**; otherwise the word is ROM word 1's. So the NOP row's virtual flag comes out as on the ROM path: `rd0` is read from the first word delivered (4.19.3), and the NOP word is the only word delivered (owner, 2026-10-26, part I) *(pending the upstream check, item 113)*.

#### 4.19.5 The decode table and its index

**Every instruction is a template.** A pass-through instruction is a one-word template whose operands are codes 129–132 (copy the source instruction's registers or immediate), so there is one datapath: every row comes from an expansion word.

**The decode table** is 2K × 16 bits (4 KB), single-ported. Its **11-bit index** is formed by a small hardware decoder from the opcode-stream word *w* (bit 0, the compressed flag, plays no part):

| *w*[6:0] | index[10] | index[9:0] |
|---|---|---|
| any other opcode | 0 | {*w*[6:2], *w*[14:12] (funct3), *w*[30], *w*[25]} |
| 0101111 (AMO) | 1 | {00, 00, *w*[12] (W/D), *w*[31:27] (funct5)}: separates LR, SC and every AMO |
| 0001011 (custom-0, the inlines) | 1 | {01, *w*[29:25] (funct7[4:0]), *w*[14:12] (funct3)}: one entry per inline |
| 1110011 (SYSTEM) | 1 | {10, 0000, *sel*}: *sel* = funct3 when funct3 ≠ 0 (the CSR instructions); when funct3 = 0, *sel* = 8 for ECALL (*w* = 0x00000073), 9 for EBREAK (0x00100073), 10 for MRET (0x30200073), 11 otherwise (an invalid entry) |

*(Proposed, item 38: the special groups' bit positions; the notes fix only which instruction bits each group uses.)* Index[9:8] = 11 with index[10] = 1 is never formed. The general group's entries for the three special opcodes are never read.

The jolt instruction encodings it must separate (`isa/decoder.cpp`): the inlines are custom-0 with funct7 0x00–0x05 and funct3 0–7 (`isa/decoder.cpp:1210–1249`); custom-2 (0x5B) carries `VirtualRev8W` (funct3 0, funct7 0x05), `VirtualAssertEQ` (funct3 1) and `VirtualHostIO` (funct3 2) (`isa/decoder.cpp:1251–1264`), which the general group separates by funct3; MRET is recognised by its exact encoding (`isa/decoder.cpp:603–606`).

**The entry** uses 11 of 16 bits:

| bits | field |
|---|---|
| [1:0] | `class`: 0 ROM template, 1 controller (inline), 2 controller (system: CSRRW, CSRRS), 3 **invalid**: no instruction Jolt accepts has this index (an invalid expansion, 4.19.11) |
| [10:2] | `start`: the template's first ROM word |
| [15:11] | spare |

There is no length field: each ROM word's `seq_remaining` marks the template's end, including on a mid-sequence start (start + offset). **The padding word** (bits [1:0] = 00) is caught before the lookup and goes to the padding template (4.19.6).

**The tables are universal** (DECISIONS-v1.25 section 5): one decode table, one ROM and one controller-fed set, covering **every RV64IMAC instruction and every inline jolt-cpp supports**, built once per jolt-cpp version by the table builder (an offline tool, `build_tables` in jolt-asic-planning PR #2) and good for any guest program. They are in `data/tables/` (`replayer/decode_table.bin`, `rom.bin`, `templates.bin`, `template_index.bin`), with the opcode table and a manifest. v1.2's tables were per program; trimming them to what one program uses was rejected, because a new guest would need the builder rerun before it could run.

**The table builder's rules** (software contract): every entry is written, because the table isn't reset; **an unused entry is class 3, invalid** (DECISIONS-v1.25 section 5, revising v1.2's controller class): with universal tables an unused index is an encoding that is no valid instruction, which Jolt itself rejects, so reaching one is an error, the invalid expansion v1.2 already raises for class 3 (4.19.11), with no new error bit (owner, 2026-11-01). An instruction whose index bits include immediate bits (the U, J and most I formats) has its entry **replicated** across every index those bits can form. **At `821e1bd`** the table has **306 ROM entries, 17 controller entries** (the 15 inlines, class 1; CSRRW and CSRRS, class 2) **and 1,725 invalid** [built: `data/tables/replayer/listing.txt`]. Program-level choices are still the tracer's: **the code stream carries whichever instruction word the tracer's bytecode decodes,** and the tables cover that word (owner, 2026-10-27). jolt-cpp builds the bytecode from the raw bytes but rewrites them at decode time (`apply_host_io_encoding`, `host_io_encoding.cpp:79–92`): under the legacy raw-ECALL host-I/O encoding a host-I/O ECALL becomes `VirtualHostIO x0, x0, 0`, and the tracer puts its **canonical word 0x0000205B** in the code stream, not ECALL's 0x00000073. So that word's entry is the custom-2 funct3-2 entry, a one-word `VirtualHostIO` template **with no operands** (all three slots immediate 0, `rd0` = 0; 4.19.4; v1.2's template was under the rd = x0 remap), and ECALL's own entry is unaffected (a ROM template, 4.19.6).

#### 4.19.6 The ROM and its contents

**The ROM is 8 KB, 512 × 128-bit words, dual-ported,** loaded by mode-0 writes to the replayer's region (4.19.10); one port serves the replayer's reads and the other the writes, which it accepts even while a pass runs. It is a RAM; "ROM" says that replay never writes it. **It is a cache of templates,** filled by the table builder (owner, 2026-10-26): **any instruction may be controller-fed** through its decode class (4.19.5), so what the ROM holds is a choice, not a requirement. **The goal** (DECISIONS-v1.25 section 5): every expansion except the inlines and the advice generators is in the ROM, so the controller feeds only what is inherently special, and everything executed routinely, the atomics and the system instructions included, comes from the ROM. **The fill at `821e1bd`**, the universal tables (4.19.5) [built: `data/tables/replayer/listing.txt`]:

| content | words |
|---|---|
| ROM words 0 and 1: the **padding** row (word 0: opcode NOP, every operand code 0, `seq_remaining` 0, `hold` 1) and the **shared NOP** row (word 1: `ADDI x0, x0, 0`, i.e. opcode ADDI, `rd` 0, `rs1` 0, `rs2` 128 with `const` 0, `report_c` 1, since it is its instruction's only row) *(Proposed, item 39: their fixed places)* | 2 |
| emulation sequences: the register shifts (12), the sub-word loads (18) and stores (18), the M extension's divisions, remainders, MULH and MULHSU (99) | 147 |
| pass-through instructions, one word each | 42 |
| system instructions: ECALL 7, EBREAK 1, MRET 1 | 9 |
| atomics: LR.D 5, LR.W 7, SC.D 12, SC.W 21, the 18 AMOs 143 (the .W forms 96, the .D forms 47) | 188 |
| **total** | **388 of 512** (92 templates over 306 decode entries) |

v1.2's fill was about 508 of 512, with the atomics partly controller-fed; `821e1bd`'s shorter sub-word and 32-bit atomic expansions (4.19.7) leave **124 words free**. **The CSR instructions are the gap against the goal** (owner, 2026-11-01: the spec follows the tables as delivered): CSRRW and CSRRS, 60 templates and 102 words, would fit, but the decode index (4.19.5) gives each of them one entry, while jolt-cpp's expansion depends on the **CSR number** (each CSR is its own virtual register, v34–v39, named literally in the rows) and on the **register pattern** (rd = x0, rs1 = x0, rd = rs1), which changes the row count from 1 to 3. So they stay controller-fed, keyed as 4.19.9 says, the one deliberate exception to the goal (owner, 2026-11-01; DECISIONS-v1.25 section 6; item 139): CSR instructions are rare in guest code, so feeding them costs almost nothing. rd = x0 remaps need no variant words: the offset rule applies in hardware. **If a later jolt-cpp version's templates don't fit,** the builder reports how many words short it is and which expansions are largest, and the owner decides between a larger ROM and what to feed (DECISIONS-v1.25 section 5).

**How a template is read.** A ROM template is consecutive words from `start` (from `start` + offset on a mid-sequence start), each word one row, until the word with `seq_remaining` = 0. A template that would run past word 511 (a word at 511 with `seq_remaining` > 0, or `start` + offset > 511) is an invalid expansion (4.19.11).

**Loading.** The table and ROM load by mode-0 writes to the replayer's region, broadcast by C&C 1 at boot (4.19.10).

#### 4.19.7 Executing a row: the register file, the ALU and the schedules

**Per row** the replayer:
1. takes the current native instruction from the code queue and finds its row: the next ROM word of its template, or the next word of the expansion queue for a controller-fed instruction;
2. resolves the row's operand codes (4.19.3, 4.19.4) and reads rs1, rs2 and rd_prev from the register file;
3. computes the result with the ALU, or takes it from the log (class L, below); writes rd; forms the memory address as rs1 + immediate;
4. pushes the row record into the output queue (4.19.12), and advances pc and `instr_idx` (below).

An instruction's opcode word leaves the code queue when its expansion's last row (`seq_remaining` = 0) completes. Fetch and decode for the next row overlap the current row; only execution is strictly sequential *(Proposed, item 42)*.

**The register file** is 128 × 64 bits (1 KB), true dual-port, and holds the working copy of all 128 registers. **Register 0 reads 0 and writes to it are dropped**, as the tracer's `save_rd` drops them (`trace_owner.cpp:85–93`). **It is the register image** (v1.26; DECISIONS-v1.26 item 12): the image's packets (part 0, 0x020–0x41F, 4.19.10) write it directly, two registers per 128-bit packet, one packet per cycle, only while the replayer is stopped, and a pass reads and writes it in place; v1.25 filled it from a separate image at each start.

**What a row computes is defined in `spec/jolt/replayer-opcodes.md`** (owner, 2026-10-26, part F; item 112), which is **normative** for it (its sections 1–5): for each of the 94 opcodes that occur in Jolt's rows, its slots, the exact rd formula (widths, extension, wraparound, shift masks), its control transfer, its log word and, for the load and store rows, its memory fields, all derived from `execute_instruction` (`trace_owner.cpp:145`) and checked against it with 252 test vectors; **v1.25 regenerates it at `821e1bd`** from the harness outputs of WORKER9 (DECISIONS-v1.25 section 1). This section keeps the classes, the schedules and the pc and `instr_idx` rules. A row reads each slot's value as `read_reg_vals` does (a register slot reads its register, an immediate slot carries its value, `trace_owner.cpp:78–83`), with three departures from the tracer:
- **class L rows take the log word** (below) instead of reading memory or computing advice;
- **assertions are not evaluated:** the `VirtualAssert…` rows write nothing, and the tracer aborts on a failed assertion while tracing (`release_assert`, e.g. `trace_owner.cpp:334–351`), so a traced program never contains one that fails;
- **control flow** follows the pc rules below, with the next `instr_idx` from the log rather than from a pc-to-index table.

**`VirtualPext` and `VirtualPextSigned` are correct for any mask** (DECISIONS-v1.25 section 1, from v1.3's section 9; revises v1.2's narrowing to contiguous masks, item 43). Jolt's prover proves the general bit-extract the appendix defines (jolt-cpp's PEXT lookup table handles any mask), so a future expansion could use a scattered mask, and the replayer must still compute it: `VirtualPext` gathers the bits of rs1 at rs2's 1-bits into rd's low bits, and `VirtualPextSigned` then sign-extends from the last bit placed; a zero mask gives 0. **How is the implementation's choice**, not the spec's: a single-pass extract network, or an iterative unit taking one run of ones per cycle (the mask's lowest run: two trailing-zero counts, a shifter and a mask generator). **Full speed is promised only for contiguous masks**, which is every mask `821e1bd` produces (each comes from a `VirtualWindowMask…` row, `expand_load`, `isa/virtual.cpp:239–247`, 821e1bd); a row with any other mask may take an implementation-defined number of extra cycles beyond its schedule below. **meshsim** times every mask like any other row and reports a non-contiguous one as a statistic, not an error, since meshsim isn't the hardware (owner, 2026-11-01).

**The memory rows** (DECISIONS-v1.25 section 1; WORKER9's diff of `821e1bd` against `4f36b3d`). jolt-cpp now expands the sub-word accesses and the 32-bit atomics around "load the aligned doubleword, then work on the byte lane":
- **`VirtualLoadB/H/W rd, rs1, imm`** load the doubleword holding the access, like LD: rd = the log word (class L). **`VirtualStoreB/H/W rs1, rs2, imm`** store rs2's doubleword, like SD: the log word is the pre-value and rd is an immediate slot holding the access's immediate. Their address is rs1 + imm with its low bits cleared for the access's width (`memory_address`, `isa/riscv.hpp:241–253`, 821e1bd): the doubleword's address for every access the tracer lets through.
- **A sub-word load** is three rows: the narrow load into a temporary, `VirtualWindowMask…` of (rs1, imm), and `VirtualPext` or `VirtualPextSigned`. **A sub-word store** is six: the narrow load, the window mask, `ANDN` (clear the lane), `VirtualShiftData…` (the new value moved into the lane), `ADD`, and the narrow store. LR.W, SC.W and the 32-bit AMOs use the same rows on a word lane.
- **`VirtualShiftDataB/H/W rd, rs1, rs2`**: rs1 is the value and **rs2 the access's base register**, the lane coming from rs2 + `shift_offset` (4.19.3); v1.2's rs2 was the computed address.
- **A misaligned access is no longer a trace row:** jolt-cpp aborts on one (`checked_memory_address`, `isa/riscv.hpp:257`, 821e1bd), so `VirtualAlignAddr` and the alignment assertions are gone and no trace contains a misaligned memory row, LD and SD included. The replayer doesn't check; meshsim flags one (invariant 16; DECISIONS-v1.25 section 7, Q4), and the row still replays with the address the formula gives.
- **`VirtualHostIO`** has no operands: it reads no register and writes none.

**Row classes and their schedules.** Of the tracer's 171 opcodes at `821e1bd`, 94 occur in Jolt's rows (NOP among them, in padding); 74 occur only as source instructions, which their templates replace, and 3 never occur (`VirtualMove` 130, `VirtualAdviceRemU` 164, `VirtualAdviceRemUW` 168) [derived: the appendix's harness at `821e1bd`; `data/tables/opcode_table.txt`]. Codes are the tracer's at `821e1bd` (`isa/riscv.inc`). **They are renumbered from v1.2's**, and the ALU decodes them: six opcodes are inserted at 110–115 and three removed, so v1.2's codes 0–109 are unchanged, 110–111 move up by 6, 114–153 by 4 and 155–167 by 3, and 112, 113 and 154 are gone. **61 row opcodes change** (52 renumbered, 3 removed, 6 new); `spec/jolt/replayer-opcodes.md` section 2.0 lists every one:

| class | cycles | opcodes (code) |
|---|---|---|
| **L**, log | 3 | the load rows LD 54 and `VirtualLoadB/H/W` 110–112 (rd = the log word); the store rows SD 53 and `VirtualStoreB/H/W` 113–115 (the log word is the memory pre-value; rd is an immediate slot); `VirtualAdvice` 126 (rd = the log word), and the DIV/REM advice opcodes `VirtualAdviceDivU` 163, `VirtualAdviceDiv` 165, `VirtualAdviceRem` 166, `VirtualAdviceDivUW` 167, `VirtualAdviceDivW` 169, `VirtualAdviceRemW` 170, which the templates write as `VirtualAdvice` (below) |
| **M**, multiply | 5 | MUL 43 and `VirtualMULI` 118 (low 64 bits of the product), MULHU 45 (high 64 bits, unsigned), MULW 65 and `VirtualMULIW` 119 (low 32 bits, sign-extended) |
| **S**, simple | 3 | every other row opcode: NOP 0, ADD 1, ADDI 2, SUB 3, XOR 4, XORI 5, OR 6, ORI 7, AND 8, ANDI 9, SLT 16, SLTI 17, SLTU 18, SLTIU 19, BEQ 28, BNE 29, BLT 30, BGE 31, BLTU 32, BGEU 33, JAL 34, JALR 35, AUIPC 36, LUI 37, FENCE 52, SUBW 62, ADDW 63, ADDIW 64, and the virtual opcodes 107–109, 116, 117, 120–125, 127–129, 131–162 |
| source only | — | the shifts SLL–SRAI 10–15 and SLLW–SRAIW 56–61, the sub-word loads and stores 20–27 and LWU 55, ECALL–MRET 38–42, MULH 44, MULHSU 46, DIV–REMU 47–50, UNIMP 51, DIVW–REMUW 66–69, LR/SC and the AMOs 70–91, the inlines 92–106 |

*(Proposed, item 37: the class of every opcode; the notes settle only the principle.)* The class L rows are exactly those whose word has `from_log` set (4.19.3): the flag, not the opcode, decides that a row takes a log word. The replayer **executes 88 opcodes** (v1.2: 85): the S and M classes and the nine executed class L opcodes, the eight memory opcodes and `VirtualAdvice` (`data/tables/opcode_table.txt` marks them S, M and L). A word with any other opcode is an invalid expansion *(Proposed, item 36)*.

**Advice rows are data** *(Proposed, item 56)*. Jolt reports every advice row as `VirtualAdvice` with rs1, rs2 and the immediate all 0 (`trace_owner.cpp:912–917`), though the tracer's DIV/REM advice rows name real source registers (`isa/virtual.cpp:331`, for example) so that its interpreter can compute them. The replayer takes the value from the log, so the template builder writes those rows as `VirtualAdvice rd, x0, x0` with `from_log` set: the record then comes out exactly as Jolt reports it, with no masking logic. (The inlines' own advice rows are already `VirtualAdvice` with two zero immediates, `isa/virtual.cpp:216–218`, and are recorded the same way.)

**The multiplier needs no signed high half.** MULH and MULHSU never reach a row: the tracer expands them through MULHU and MUL (`isa/virtual.cpp:485–503`). So the multiplier produces the low product, the unsigned high product and the low 32 bits sign-extended; the sign corrections the notes provide for MULH are not needed *(Proposed, item 37)*. `VirtualAssertMulUNoOverflow` multiplies only to check, so it is class S.

**The fixed row schedules** (decided 2026-10-23). Rows are strictly sequential, never overlapped, so there are no hazards and no bypass. The register file is true dual-port; every read of a row happens before its write, so a row whose rs1 or rs2 is also its rd reads the old value, as the tracer does (`trace_owner.cpp:903–908`).

| row kind | cycles | schedule |
|---|---|---|
| simple (class S) and log rows (class L) | 3 | 1: read rs1 and rs2. 2: compute (or take the log word), read rd_prev. 3: write rd |
| multiply (class M) | 5 | 1: read rs1 and rs2. 2: four 16 × 64 partial products, read rd_prev. 3–4: carry-save and final add into the 128-bit product. 5: write rd (low or high half, or the low word sign-extended) |

The multiplier is four 16 × 64 multipliers in parallel, a full 64 × 64 array, which is cheap in area. There is no divider: Jolt's DIV and REM use advice plus checks, and the advice comes from the log. **Stalls** (an empty code, expansion or log queue, a full output queue) delay a row's start; they never change its schedule.

**The edge cases** (AUIPC's use of the source instruction's pc, JAL and JALR linking pc + `pc_increment`, `VirtualHostIO` reading and writing nothing, `VirtualShiftData…`'s lane, `VirtualROTRIW`'s min(32, tz(imm)), the shift masks, MULHU, the advice values) are in `spec/jolt/replayer-opcodes.md`. **Extension is per opcode:** several word forms zero-extend rather than sign-extend (`VirtualPow2W`, `VirtualShiftRightBitmaskW`, `VirtualROTRIW` and the `VirtualXORROTW…` forms among them; the appendix's flag F5 lists them), so the appendix's rd formula, not the opcode's name, decides.

**Zero shift masks** (owner, 2026-10-26). The masks reaching `VirtualSRL`, `VirtualSRLI`, `VirtualSRA` and `VirtualSRAI` are **never 0**: each comes from `VirtualShiftRightBitmask` or from the expansion's 2⁶⁴ − 2^shamt, and the tracer guarantees it. The shift is tz(mask), and **the result for a zero mask (tz = 64) is undefined**, as it is in the tracer's C++; meshsim flags one (invariant 16).

**A load row's memory fields** (owner, 2026-10-26) *(pending the upstream check, item 113)*. A load row's record (LD or `VirtualLoad…`) forms its memory value (pre = post) from **the value written to rd**, not from the log word, as the tracer does (`trace_owner.cpp:909–912`, 821e1bd). So an LD to x0, which only LR.D with rd = x0 produces (4.19.4), consumes its read-log word but reports a memory value of 0 (4.19.12).

**pc and `instr_idx`.** pc is computed, never stored per row; `instr_idx` increments by 1 on every row that continues sequentially, because the committed bytecode lays each expansion out contiguously in program order (`isa/virtual.cpp:1405–1420`). The **new pc** of a row:

| row | new pc |
|---|---|
| not its expansion's last row | pc (unchanged) |
| last row, no control transfer | pc + `pc_increment` |
| conditional branch | pc + imm if the comparator says taken, else as above |
| JAL | pc + imm; **0 if imm = 0 and `imm0_pc0` is set**: Jolt's program-end convention (`trace_owner.cpp:281`). The template builder sets `imm0_pc0` in **every JAL word, ROM or controller-fed** (owner, 2026-10-26, item 56), so the replayer matches the tracer, which ends the program on every JAL with imm = 0 |
| JALR | (rs1 + imm) & ~1 (`trace_owner.cpp:287`); MRET and traps are JALR rows through mepc (v36) or mtvec (v34) (`isa/virtual.hpp:36–38`; `isa/virtual.cpp:1098–1162`) |
| `hold` set | pc (unchanged), and `instr_idx` unchanged |
| **new pc = 0, from any row** | **the program ends:** pc = 0 and `instr_idx` = 0, checked before the rules below (`trace_owner.cpp:117–119`). Besides a JAL with imm 0, a JALR whose target is 0 or a branch whose pc + imm wraps to 0 also ends it (owner, 2026-10-26, item 57) *(pending the upstream check, item 113)*. **From v1.3 no trace holds such a JALR or branch:** a16z fails there, and the Tracer stops with its `ERRORS` [7] (4.21.4; DECISIONS-v1.3 section 25), so the rule stays and is never exercised |

Then, as `finish_instruction` does (`trace_owner.cpp:111–126`): **if the new pc differs from pc,** the row transfers control: pc takes the new pc and the next `instr_idx` is the row's log word (`next_from_value` must be set; a new pc of 0 has the log word 0). **Otherwise** pc += `pc_increment` and `instr_idx` += 1, with no log word, **even for a taken branch or a jump whose target is its own pc** *(pending the upstream check, item 113)* (`trace_owner.cpp:117–124`): Jolt treats a jump to itself as sequential, so the row's pc advances past it.

**Hardwired Jolt behaviour** *(Proposed, item 57)*. Three conventions can't be table data, because they depend on values only known while the row runs; the replayer builds them in, and a change in Jolt would change silicon:
- **the self-jump compare:** a control transfer whose new pc equals its pc is sequential (above) *(pending the upstream check, item 113)*;
- **register 0** reads 0 and drops writes (`trace_owner.cpp:85–93`);
- **the row record's memory fields** are formed for the load and store rows only (jolt-cpp's `is_load` and `is_store`: LD, SD and the six narrow opcodes; `trace_owner.cpp:904–913`, 821e1bd), a load's from the value written to rd (so 0 for an LD to x0), the narrow rows' address with its low bits cleared (4.19.12), and JALR clears bit 0 of its target (`trace_owner.cpp:287`);
- **a new pc of 0 ends the program** from any row (the pc table above) *(pending the upstream check, item 113)*.
The ALU's per-opcode arithmetic is hardwired too, by nature: a new Jolt opcode needs new logic. Which rows can transfer control is explicit, not inferred from the row opcode: `next_from_value` is set on branches, JAL and JALR, ECALL's and MRET's JALR rows and EBREAK's JAL, all in the ROM at `821e1bd` (4.19.6), and on any controller-fed row that can transfer control. No pc-to-index table is needed.

**Rate.** A row is 3 cycles, or 5 for class M. **The multiply density, measured at v1.25** [meas: v1.25's acceptance runs, Quest 083 and 084: meshsim's Stage B on every window of both workloads at 2²⁹, `v1.3/planning/sessions/WORKER11-REPORT.md` section 5] [derived exactly: a pass's cycles less 3 per row and its stalls are 2 per class M row, and a padding window leaves 0]: **15.7 % of EVM Zone's rows** (per window median 15.7 %, maximum 28.6 %) and **7.1 % of Cyclone's** (median 4.1 %, maximum 24.7 %). So a 64K-row pass averages 3.32 cycles per row on EVM Zone and 3.16 on Cyclone, at most 3.59, with stalls: about 218K cycles, 0.22 ms at 1 GHz with everything local, or about 10 ms for the 46 passes of an Akita proof [estimate from the measured rate] (4.19.1).

#### 4.19.8 The queues, their ports and credits

Four queues feed the replayer. They are **true FIFOs**: producers push at the tail and the replayer pops, with no slot addresses (decided 2026-10-23). **Every queue holds 128-bit entries** (owner, 2026-10-26): the code and log queues hold their streams as delivered, raw or compressed, and their decompressors unpack them (4.19.15).

| queue | size | entries | half (the refill unit) | normal source |
|---|---|---|---|---|
| code | 4 KB | 256 × 128-bit entries of the code stream | 128 | local (resident or staged, 4.19.13) |
| expansion | 4 KB | 256 × 128-bit expansion words | 128 | always local |
| read log | 4 KB | 256 × 128-bit entries of the read log | 128 | local (resident or staged) |
| write log | 4 KB | 256 × 128-bit entries of the write log | 128 | local (resident or staged) |

All four are dual-ported, so pushes land while the replayer runs. Every refill is local (4.19.13), and a half covers the local refill latency (a few hundred cycles) with room to spare. **Every queue is 4 KB** (DECISIONS-v1.26 item 3, from DECISIONS-v1.3 section 12; v1.25's two logs were 8 KB each): by the measured densities (4.19.2), the logs average 0.31–0.35 words per real row together and at most about 0.83 in Cyclone's densest windows, so a log's half-queue refill, 128 entries or 256 words, lasts at least about 300 rows even if one log carried both densities, some 900 cycles at 3 cycles a row, against a local refill of a few hundred [derived]. **Measured** (item 143; meshsim's Stage B, `replay_ctl.c` on tile 0, every window of both workloads at 2²⁹; cycles per window, average with the largest in brackets) [meas: Quest 098–101, `v1.3/planning/sessions/WORKER20-REPORT.md` section 5]:

| per window | EVM Zone, 4 KB | EVM Zone, 8 KB control | Cyclone, 4 KB | Cyclone, 8 KB control |
|---|---|---|---|---|
| read-log wait | **39.2** (127) | 39.2 (127) | **43.5** (127) | 43.5 (127) |
| write-log wait | **100.0** (255) | 176.2 (383) | **84.5** (255) | 153.6 (383) |
| log waits, share of a pass | **0.066 %** | 0.102 % | **0.063 %** | 0.097 % |
| Stage B cycles per row | 3.2116 | 3.2128 | 3.1105 | 3.1120 |

**The 4 KB queues wait less than 8 KB, because the waits are all at a pass's start, not from the logs' density.** The first refill run sends the code queue's half, then the read log's, then the write log's, a packet a cycle, so a smaller half gets each stream's first entry in sooner: no read-log wait exceeds 127 cycles in any window, and no write-log wait 255 (383 at 8 KB). Window by window the 8 KB write-log wait exceeds the 4 KB one by 0–128 cycles (exactly 128 in 4,753 EVM Zone and 4,259 Cyclone windows), and the read-log waits are identical. **After the start, a 128-entry refill always lands in time**, Cyclone's densest stretches (0.83 log words per row) included. With 4 KB queues every window of both workloads matched in stages A and B, and every 64th in F (Quest 098, 099).

**The ports.** In the replayer's region a range of offsets names a queue (4.19.10): the packet is appended at the tail and the rest of its address is ignored, so the address generator's per-packet stepping stays inside the port. **Every queue port takes 128-bit packets only,** one entry each (owner, 2026-10-26); a 32- or 64-bit packet is a bad write (4.19.11). Each stream is padded to a whole entry, and its value count marks its end (4.19.15).

**Credits** (owner, 2026-10-28 and 2026-10-29; W3-24). Each queue's **credit** is one of the replayer's counted events (4.17.3), **depth 2**, and **each credit is half a queue**: "half a buffer is free, send half a buffer".
- **Initialised by the state write:** **the write to the state word sets every queue's credits to 2** as it flushes the queues (below; v1.26, DECISIONS-v1.26 item 10; v1.25: `PULSE_RUN`), and **each new expansion sets the expansion queue's to 2 again** (as v1.1 reset the expansion queue's pending count at each expansion).
- **Returned by the replayer:** each time it has **popped half a queue's worth of entries** since the last return, it adds one credit to that queue.
- **Taken by the controller:** a consuming load of the replayer's `EVENTS` takes one credit from each queue bit that is set and masked (4.17.3); the program then refills every queue whose credit it took. **A credit is permission to send up to half a queue** (owner, 2026-10-28), not one transfer: the program may split the refill into smaller moves (a 2 KB credit as two 1 KB moves), as long as their total stays within the credit.
- **So the controller can never send more than there is room for,** up to two refills can be outstanding, which keeps a draining queue fed, and the tile's live bit for the replayer stays high while any masked credit remains, so `WFI` can't miss a refill. A queue that empties simply stalls the replayer until data lands.
- **Code and log queues:** every refill except a stream's last is exactly half a queue (a software rule), so their credits balance. Only the expansion queue sees short refills, and its re-initialisation at each expansion keeps a short one from stranding a credit.
- **Credits with nothing behind them** (after a stream's last refill; the write log in a pass without the store log, 4.19.2; the expansion queue once the current expansion is all sent): the loop removes that queue's bit from the replayer's `EVENTS_MASK` once it has nothing more to send, so `WFI` doesn't fall through (4.19.9). There is no gating hardware.
- v1.1's occupancy-and-pending low-water condition, its claim rule (item 50) and the expansion's "still has words to deliver" test (item 51) are replaced by these credits.

**Overrun.** A push into a full queue (occupancy at capacity before the cycle's pop, as for every FIFO, 4.13) is dropped and sets **queue overrun** in the replayer's `ERRORS` (4.19.11): **checked**, since it depends on the credit loop under real timing (owner, 2026-11-01). Whether the dropped push is still counted is not promised (4.19.10). With credits it means the program sent more than it was credited.

**Writing the state word resets the pass** (DECISIONS-v1.26 items 4 and 10, from DECISIONS-v1.3 section 24; in place of v1.25's start clearing the queues). **The write to the state word, part 0 offset 0x000** (4.19.10), as it lands:
- **empties all four input queues**, their contents and occupancy; the output queue (4.19.12) is not touched, and its rows drain to the consumer;
- **sets every queue's credits to 2** (above);
- **clears each stream's size counters**, the entries taken and the values decoded (4.19.15);
- **resets the decompressors' tables** (4.19.15).

Nothing else does any of these but the reset (6.1); **`PULSE_RUN` only starts running** (4.19.11). The state stays a memory, address-mapped in part 0 as at v1.25, not a queue (DECISIONS-v1.26 item 4), and it is the working state (4.19.2). Sequential loading writes the state word first (4.19.10), so the rest of the record lands after the reset:
- **The controller reloads the record's words 0–65 before every pass** (DECISIONS-v1.26 item 12), even re-running the same window: part 0 holds the last pass's end state, and the state write gives the pass clean queues, fresh credits and decoders. It also clears expansion words left from a pass that ended inside a controller-fed expansion (the expansion queue has no sizes, 4.19.11); the code and log queues are empty after any pass that ends without an error (4.19.11, `ERRORS` [6]).
- **Preloading is allowed** (DECISIONS-v1.26 item 11): after the state write, the controller may take credits and push the first refills before `PULSE_RUN`, or pulse first and push after; either works, since the state write has already flushed the queues and set the credits. A push that lands before the state write is flushed by it.
- **Software rule:** before the state write, every queue push has landed, whatever its source: the refill counter's done for the controller's refill runs, and the counters of any other pushes it counts (4.19.13). Over-pushed or late data from one pass can then never leak into the next. After the state write every queue holds two credits, and the controller fills them, a few hundred cycles per pass.

#### 4.19.9 The events, the words and the controller loop

**The replayer's window** (4.17.4; owner, 2026-10-28; the status words one field per word, owner, 2026-10-29; *(Proposed, item 123: the event and error bits, after WORKER3's review)*):

| word or bit | v1.2 | v1.1 |
|---|---|---|
| `PARAMS` [7:0] | type 4 | `TILE_STATUS` [15:12] |
| `STATUS` [0] | run in progress: the ownership state (4.19.10, 4.19.11) | `periph_running` |
| `STATUS` [1] | always 0 (no DMA) | — |
| `ERRORS` | [0] reserved (v1.2: bad write, now UB), [1] queue overrun, [2] invalid expansion, [3] code-stream underflow, [4] read-log underflow, [5] write-log underflow, [6] done with input left over (v1.26) (4.19.11) | the tile's conditions 11–13 |
| `PULSE_RUN` | a pass (4.19.11) | `TILE_PULSE` [2], `PERIPH_RUN` |
| words 4, 8, 9 | reserved (no DMA) | the sender, unattached |
| word 32, `RUN_CONFIG` (R/W) | v1.26.1: [23:0] rows to run, [24] use the store log, [25] output `rd_prev`, [28:26] the Tracer's compression enables (ignored), [31:29] reserved (below) | — |
| word 33, `ROWS_RUN` (R) | rows of this pass completed, [23:0] (v1.26.1; v1.26: word 32, modulo 2¹⁶) | `PERIPH_STATUS_0` [15:0] |
| word 34, `EXPANSION_OFFSET` (R) | the row within the current expansion where the next row starts (v1.26: word 33) | `PERIPH_STATUS_0` [31:16] |
| word 35, `EXPANSION_OPCODE` (R) | the instruction awaiting a controller-fed expansion: the presented item of new expansion (v1.26: word 34) | `PERIPH_STATUS_1` |

**Its events** (4.17.3), in its own `EVENTS`, reaching the tile's `EVENTS` [13] while any masked one is set:

| bit | event | kind and depth |
|---|---|---|
| [0] | **run done:** the pass has ended (`STATUS` [0] has fallen, 4.19.11) | counted, depth 1 |
| [1] | DMA done: never set (no DMA) | — |
| [2] | **code credit** (4.19.8) | counted, depth 2 |
| [3] | **read-log credit** | counted, depth 2 |
| [4] | **write-log credit** | counted, depth 2 |
| [5] | **expansion credit** | counted, depth 2 |
| [6] | **new expansion:** the replayer has reached an instruction whose decode class is controller (1 or 2), presents it in `EXPANSION_OPCODE` and waits | counted, depth 1 |

A load of the replayer's `EVENTS` returns every bit and consumes one count from each bit set under its `EVENTS_MASK` (4.17.3): for a queue bit that is taking one credit, for run done and new expansion clearing the event. A load of `EVENTS_PEEK` consumes nothing. An event the replayer posts in the same cycle as the load survives to the next. Software acts on value & mask. Nothing but the tile's own controller reads the window.

**`RUN_CONFIG`** (word 32, R/W; DECISIONS-v1.26 items 13–15; v1.26.1): **what the next pass runs**, the one register the controller writes in the window besides `EVENTS_MASK` and `PULSE_RUN`:
- **[23:0], the rows to run:** 0 to 16,777,215. The replayer runs exactly that many rows and raises done (4.19.11), so a pass can be shorter than a window, for a short trace (debugging), or, given the streams, longer; it is typically **65,536**, one window. **The record must be sized to the pass:** the done check [6] applies whatever the count (owner, 2026-11-05), so a pass that stops before its streams are used up ends with input left over. **A count of 0 runs nothing** (owner, 2026-11-05): `STATUS` [0] rises at the end of the run store's cycle and falls the next, and run done is posted, as for a DMA with a count of 0 (4.17.9); [6] is checked as for any pass.
- **[24], use the store log; [25], output `rd_prev`** (4.19.2): the pass's two choices, a consumer's per pass.
- **[28:26], the Tracer's per-stream compression enables** (code, read log, write log; v1.3; owner, 2026-11-05): **held and read back, with no effect on the replayer**, which neither checks them nor flags a set bit; its compression is chosen per stream by the record's stream word (4.19.15). So one register serves both units, and a value written for the Tracer means nothing different here. **[31:29] reserved:** read 0, stores ignored.
- **Written while stopped, used by the next `PULSE_RUN`:** a write while a pass runs is UB (meshsim flags it), like a part-0 write (4.19.10). **It is sticky** (owner, 2026-11-05): it holds the last value written across passes, and `PULSE_RUN`, the state word's write and done leave it alone, so passes alike need one write before the first. **It is 0 after the reset**, so a pass started without ever writing it runs nothing and, with input to consume, sets [6].
- **The same word on the Tracer** (4.17.4), replacing its `RUN_CYCLES`: it runs the row count as `RUN_CYCLES` did, at most 65,536, and ignores the two flags (DECISIONS-v1.3 section 24); its [28:26] enable each stream's compression (4.21.3).

**What the controller reads with them** (no read has a side effect; one field per word, so no masking). `EXPANSION_OPCODE` holds the instruction word of the expansion it must supply (the stored opcode-stream word, bit 0 clear for a compressed instruction), from the cycle new expansion is posted until the next one. `EXPANSION_OFFSET` is the row within the current expansion where the next row starts, which is where the controller begins sending the expansion's words (non-zero only for a tile that starts mid-expansion). `ROWS_RUN` counts the rows of this pass in 24 bits (v1.26.1; v1.26 counted modulo 2¹⁶), so rows left = the row count − `ROWS_RUN` while `STATUS` [0] is 1; `PULSE_RUN` clears it, and after a pass it reads the row count. It is a diagnostic: no firmware reads it. These are outputs; the **state word** (pc, `instr_idx`, next offset) is written with the checkpoint at offset 0 of the replayer's region (4.19.10), and is also the live state a pass changes in place (4.19.2; v1.26).

**The controller loop** (W3-24, W3-27). Every `WFI` has exactly the sources its mask allows. `rp` is the replayer's window (`peripheral1`), `QUEUES` its credit bits that have something left to send, `REFILL` the done event of the write counter the loop uses for its refills (say counter 4, `EVENTS` [3]), and `PERIPH1` the tile's `EVENTS` [13]; GO, [8], stays in the tile's mask throughout (4.12) but can't arrive mid-pass, and `EVENTS_REQUIRED` is 0.

```
    rp->RUN_CONFIG    = 65536 | STORE_LOG | RD_PREV ; the pass's rows and flags: sticky, so once for passes alike
    load words 0-65 of the record (state word first) ; verbatim; resets the pass (4.19.8): every pass
    rp->EVENTS_MASK   = RUN_DONE | NEW_EXP | QUEUES   ; the write log only with the store log
    tile->EVENTS_MASK = GO | PERIPH1                  ; only the replayer can come
    rp->PULSE_RUN     = 1                             ; starts running; the credits are already 2
loop:
    WFI                                               ; the replayer's live bit
    ev = rp->EVENTS & rp->EVENTS_MASK                 ; claims: one credit from each set queue bit
    if ev == 0: goto loop                             ; a spurious wake: re-check (4.12)
    if ev.run_done: exit
    if ev.new_expansion: look up key(rp->EXPANSION_OPCODE) (inlines and CSR only),
                         start at rp->EXPANSION_OFFSET; add EXPANSION to rp->EVENTS_MASK
    for each credit taken: queue a refill of min(half, what that stream has left)  ; its words, from the stream word
    for each stream with nothing left: remove its bit from rp->EVENTS_MASK
    tile->WRITE_COUNTER_4 = packets; write STREAM entries; start the run
    tile->EVENTS_MASK = GO | REFILL | RUN_DONE        ; squelch: only the refill's counter and its run can wake it
    tile->EVENTS_REQUIRED = REFILL | RUN_DONE         ; both: its packets have landed and its tables are free (4.10)
    WFI; load tile->EVENTS; EVENTS_REQUIRED = 0       ; consumes REFILL and RUN_DONE
    tile->EVENTS_MASK = GO | PERIPH1                  ; credits returned meanwhile fall through
    goto loop
```

The squelch is a mask setting: while the run streams, the replayer's credits accumulate, and only the refill counter's done together with DMA run done (`EVENTS` [10]) can wake the core, so the next iteration may rewrite the `STREAM` entries (4.10; as recipe 26 does). The counter counts the run's packets K cycles after the director accepts them, by which time each is in its queue (4.19.10), so the second `WFI` returns when every word has landed, and the load after it consumes the event. **Waiting on the refill counter's own done rather than counters done** (owner, 2026-10-26) means an LPDDR fetch outstanding on another counter (4.19.13) doesn't hold the refill up. Credits returned during the run stay counted and, the tile's bit being live, make the next `WFI` fall through once the mask is restored. GO can't arrive mid-pass and no other tile writes the replayer's region during a pass (4.19.13), so the refill counter counts only the loop's own refills.

**Data from LPDDR** (4.19.13) never goes straight into a queue (owner, 2026-10-26): the overflow is fetched into big SRAM and refilled locally like resident data, so every refill is a local run and the loop is the same for all of them.

**What a stream has left** is counted in 128-bit words, from the stream's word count in the stream word (4.19.2): every refill but the last is a half, 128 entries, and the last is what remains (v1.26; v1.25 rounded the bit length up). The write log's count is used only with the store log.

**Between passes.** The loop exits only at the top, after its last refill run has landed (its counter's done consumed), so every queue push has landed when the next pass's reload writes the state word and resets the pass (4.19.8). The loop as written starts the pass before its first refills; a loop may instead take the two credits and push the first refills between the reload and `PULSE_RUN` (preloading, DECISIONS-v1.26 item 11).

**Short runs, not interruption.** The address generator is not interruptible. Instead every run is short: a 1 KB chunk is 64 packets, about 64 cycles, so nothing waits long for the engine, and the controller re-checks the queues after each chunk. Streaming occupies about 11–20 % of a pass [estimate]. Group runs (the sort's all-to-alls) stay exclusive and don't coincide with replay.

**Controller-fed templates and their numbers** (owner, 2026-10-26, part G; item 114; **the key, v1.25**: DECISIONS-v1.25 section 5 and `data/tables/README.md`). The controller-fed set is universal like the rest of the tables (4.19.5): its **template words** (`templates.bin`) and an **index** (`template_index.bin`) of 16-byte entries, each the entry's **key** (bytes 0–3), the template's word offset (4–7) and length in words (8–11), its `rd0` (12) and its decode class (13), with 14–15 spare. **The entries are sorted by key, ascending, and a template's number is its entry's position**, 0–127; numbers may share template words, so the limit of **128** counts index entries, not template storage. A universal set can't list every instruction word, so **the key is the instruction word with its register fields normalized**, and the controller computes **key(*w*)** from the presented word *w* (`EXPANSION_OPCODE`) before the lookup. key(*w*) keeps every bit of *w* except:
- **an inline** (class 1, *w*[6:0] = 0001011): rd [11:7], rs1 [19:15] and rs2 [24:20] are **cleared**, one entry per inline. Its template resolves any registers through codes 129–131, x0 and aliasing included;
- **CSRRW or CSRRS** (class 2, *w*[6:0] = 1110011, funct3 1 or 2): rd and rs1 are **renamed by first appearance**: x0 stays 0, a non-zero rd becomes 1, and rs1 becomes 0 if x0, 1 if it equals a non-zero rd or if rd is x0, else 2. The CSR field [31:20] is kept. These five patterns per CSR and instruction are exactly the cases jolt-cpp's expansion distinguishes (rd = x0, rs1 = x0, rd = rs1), which change its row count from 1 to 3 (4.19.6).
Operand codes 129–131 still resolve against the presented word, not the key. **A word whose key has no entry is an error:** jolt-cpp rejects it (a CSR it doesn't support), so a traced program has none. **At `821e1bd`** the set has **75 numbers**, the 15 inlines and 60 CSR keys (CSRRW and CSRRS for jolt-cpp's six CSRs, mstatus, mtvec, mscratch, mepc, mcause and mtval, five patterns each), in 13,821 words [built: `data/tables/replayer/listing.txt`]. v1.2 keyed each program's set by the exact instruction word. A tile that keeps only the templates its window uses keeps every word that any of its numbers references (item 115).

**EBREAK's expansion** is one word (owner, 2026-10-26): **JAL, rd = temporary 0 (v40), immediate 0, with `imm0_pc0` and `next_from_value` set**, as the tracer emits it (`JAL v40, 0`, `isa/virtual.cpp:1164–1192`). EBREAK is exempt from the rd = x0 remap, so the offset is 0 and temporary 0 is v40. It ends the program. **v1.25 puts it in the ROM** with ECALL and MRET (DECISIONS-v1.25 section 6; v1.2 fed it from the controller) *(pending the upstream check, item 113)*.

**Controller-fed expansions** are only **the inlines (custom-0) and the CSR instructions, CSRRW and CSRRS** (DECISIONS-v1.25 section 6: "the controller feeds the inlines, the advice generators and the CSR instructions, and nothing else"; the advice generators are the Tracer's, v1.3). The CSR instructions are the one deliberate exception to the ROM goal (4.19.6): their expansion depends on which CSR and on the register pattern, which a single decode entry can't see, and they are rare in guest code. **At v1.25** [meas: v1.25's acceptance runs, Quest 083 and 084: meshsim's Stage B on every window of both workloads at 2²⁹, `v1.3/planning/sessions/WORKER11-REPORT.md` section 5]: **Cyclone** averages 6.6 controller-fed expansions per window of real rows (1,154 of its 5,670 such windows have any, at most 176), and their stalls average 812 cycles a window, 0.26 % of a pass; **EVM Zone** has 665 in all, in 4 windows (at most 230), 11 stall cycles a window on average. v1.1 measured about 13 per window on Cyclone, about 0.4 % of a pass, with ECALL, EBREAK and MRET still among them [meas: design notes]. **The controller-fed set reaches the tiles by broadcast** (DECISIONS-v1.3 section 25; v1.3). C&C 3 holds all of it, since the Tracer can meet any template, and ORs each window's template bitmap (the record's word 67, set by the Tracer for the inlines only, 4.21.4) into the segment's used set. **When the proving stages start**, once the segment is traced, it broadcasts **all 60 CSR templates** (102 words, always) **and the inlines the segment used**, with their index entries, to every tile once; **each tile keeps the CSR templates and the inlines its own record's bitmap names**, with every word any of those numbers references, in big SRAM (4.19.13). So every tile has its templates before its replayer runs. *(The broadcast's mechanics, a size-first command so each tile arms its counter, item 115.)* (v1.26.1: every tile preloaded the whole set.)

#### 4.19.10 Its region: the address map, landing and ownership

The rules for writes into a peripheral (4.17.5) apply: mode 0 to region 0x04, from any source, landing `rp_latency` after the director, counted by the packet's counter field. This subsection is the replayer's map of its region.

**The map.** Everything the replayer reads is written by mode-0 packets to region 0x04, whose 24-bit **byte offset** steps by 4, 8 or 16 per packet (3.1, 4.2, 4.11); the packet address is 0x04000000 | offset. **Offset bits [22:20] select a part** *(Proposed, item 44; v1.1's word-address bits [20:18], now in bytes)*:

| offset (bytes) | offset[22:20] | part | size | sizes taken | ports |
|---|---|---|---|---|---|
| 0x000000–0x00000F | 0 | **state word:** pc [63:0], `instr_idx` [95:64], next offset [111:96], live while a pass runs; [116:112] reserved (v1.26's flags [115], [116] are in `RUN_CONFIG` from v1.26.1); [127:117] spare (4.19.2). **Its write resets the pass** (4.19.8) | 16 B | 128 | single |
| 0x000010–0x00001F | 0 | **stream word** (v1.26; rearranged at v1.26.1): per stream its words, values out and compression flag; words [15:0], [31:16], [47:32], values out [64:48], [81:65], [98:82], flags [99], [100], [101], for code, read log and write log; advice values [124:102], ignored (v1.26.1; 4.19.2) | 16 B | 128 | single |
| 0x000020–0x00041F | 0 | **register image:** 64 × 128-bit words, image word *i* (registers 2*i* low, 2*i* + 1 high) at 0x20 + 16*i*; **the working register file itself** (4.19.7; v1.26) | 1 KB | 128 | single |
| 0x000420–0x00043F | 0 | **the record's words 66 and 67** (the files word and the template bitmap, 4.19.2): **accepted and ignored** while stopped; a write while a pass runs is UB like any part-0 write (meshsim flags it; DECISIONS-v1.26 items 9 and 11) (v1.26) | 32 B | 128 | — |
| 0x001000–0x001FFF | 0 | decode table: entry *e* in the packet at 0x1000 + 16·(*e* >> 3), bits [16·(*e* & 7) + 15 : 16·(*e* & 7)] | 4 KB | 128 | single |
| 0x002000–0x003FFF | 0 | ROM: word *j* at 0x2000 + 16*j* | 8 KB | 128 | dual |
| 0x100000–0x1FFFFF | 1 | code queue port | 4 KB queue | 128 | dual |
| 0x200000–0x2FFFFF | 2 | read-log queue port | 4 KB queue | 128 | dual |
| 0x300000–0x3FFFFF | 3 | write-log queue port | 4 KB queue | 128 | dual |
| 0x400000–0x4FFFFF | 4 | expansion queue port | 4 KB queue | 128 | dual |
| everything else | 0, 5–7 | unmapped | — | — | — |

**One part per queue** (owner, 2026-11-01; v1.25, CL1, as v1.2.1): every queue port is a whole 1 MB part, so **offset bits [22:20] alone name the destination**: 0 the memories, 1–4 the code, read-log, write-log and expansion queues, 5–7 unmapped. A queue port is in effect one address; its range only lets an advancing stream keep hitting it, and its size means nothing. (Before, the two logs shared part 2, 512 KB each, at 0x200000 and 0x280000, and the expansion queue was part 3 at 0x300000, making the decode two-level: bits [22:20], then bit 19 for the logs.)

*(Proposed, items 44–46: the parts, the offsets in part 0 and the sizes each part takes.)* In 128-bit words (offset / 16) the state word is 0, the stream word 1, the register image 2–65 (owner, 2026-10-26: state first), the record's ignored words 66–67, the decode table 256–511 and the ROM 512–1023. **Part 0's words 0–67 are the state record's words 0–67** (4.19.2), so a record lands at consecutive offsets from 0x000. Every part takes 128-bit packets only, so every write is 16-byte aligned; a misaligned one is UB (4.1; DECISIONS-v1.25 section 8b; v1.2: a bad packet at the director).

**How a controller loads part 0** (owner, 2026-10-26, recommended practice; v1.26: before every pass, DECISIONS-v1.26 item 12). Part 0 is the working state (4.19.2), so **before every pass the controller reloads the record's words 0–65**, state word first, with the replayer stopped (`STATUS` [0] = 0), counted on a write counter *n*, and pulses `PULSE_RUN` once counter *n*'s done says every word has landed:
- **From big SRAM** (the usual way): a stream run of the address generator from the tile's copy of the record (4.19.13), **starting at the record's byte 0** (v1.26; v1.25 started at byte 16, past its leading bitmap), 66 packets (or all 68: words 66 and 67 are accepted and ignored, 4.19.2), about 66 cycles, as v1.25's internal copy took 64. The words go **verbatim**: the pass's row count and flags are in `RUN_CONFIG` (4.19.9), so nothing in the record changes for a pass (v1.26.1; v1.26 set the flags in the controller's copy of word 0).
- **From data memory**, through the send port: `ROUTER_HDR` once (dst = its own id, mode 0, counter *n*) and `ROUTER_ADDR` = 0x04000000; a write of the number of 128-bit words to `WRITE_COUNTER_n`; per word `ROUTER_SET_W0`–`W2` then `ROUTER_SEND_128`, the address advancing by 16 each send (4.11). The 66 words are about 264 stores (worked example in `REGISTERS.md`).

The state word's write resets the pass (4.19.8), and the image's packets write the working register file directly (4.19.7). *(v1.25 kept part 0 as a stored copy across passes and resent only the state word for a flag change; the working register file wasn't addressable and each start filled it from the image.)*

**A bad write is UB** (meshsim) (owner, 2026-11-01, under the principle at the top): a packet that addresses an unmapped offset, has a size its part doesn't take, or writes a single-ported memory while a pass runs is a static software mistake, which simulation catches. The hardware does nothing special, and meshsim stops naming the write. v1.2 dropped it, still counted, and set the replayer's `ERRORS` [0], now reserved (4.19.11).

**Landing.** The traffic director hands each packet to the replayer's write port, one per cycle; the write lands a fixed **`rp_latency`** = 2 cycles later, with no queue on the path, within K as every peripheral must (4.17.5) *(Proposed, item 48: the value)*.

**Implementation note: the decode table's organization** (v1.25; a note, not a requirement). The decode table is logically 2,048 × 16 bits (4.19.5), but it is naturally **built as 256 × 128 bits, one row per 128-bit packet**: entry *e* lives in row *e* >> 3, bits [16·(*e* & 7) + 15 : 16·(*e* & 7)], exactly as the map above places it. A write is then **one row in one cycle**, which keeps the write port taking a packet per cycle and landing it within `rp_latency`, and a lookup reads row *e* >> 3 and selects 16 bits by *e* & 7. A 16-bit-wide build would need eight banks, one per *e* & 7, to take a packet per cycle. The implementation may organize it differently, as long as it takes a packet per cycle.

**Counting** follows the rule for every write (4.17.5): a packet **from any source** decrements the counter its counter field names, K cycles after the director accepts it, and a counter's done means every counted write that the rules allow has landed. Whether a push dropped as a **queue overrun** is still counted is not promised *(for Tony: it is free if counting stays at the director)*; the overrun itself is checked (4.19.8). C&C 1's boot loads are uncounted mode-0 writes (6.2). A packet from another tile is allowed and counted; since a queue port takes one source at a time (invariant 18) and the controller loop counts only its own refills (4.19.9), software keeps other tiles' writes out of a running pass (4.19.13). An arrival for a counter at 0 sets unexpected arrival (4.12).

**Ownership** follows `STATUS` [0] (W1-30: "idle" in place of HALT):

| state | state word, stream word, image, the record's words 66–67, decode table | ROM | queue ports |
|---|---|---|---|
| **stopped** (`STATUS` [0] = 0) | written (the state word's write resets the pass) | written | pushed; a push before the state write is flushed by it, one after it is kept (preloading, 4.19.8) |
| **running** (`STATUS` [0] = 1) | not written: UB (meshsim), words 66–67 included; the replayer changes the state word's pc, `instr_idx` and offset and the image in place | written (second port) | pushed |

v1.1's HALT row (item 54) is gone with HALT. **A push while stopped** is accepted like any other, with no error, and simply lands in the queue *(Proposed, accepted, item 76)*. **v1.26** (DECISIONS-v1.26 items 10 and 11): a push that lands before the state write is flushed by it, which is what happens to a late push from the previous pass (the software rule of 4.19.8 keeps it from crossing the state write); **a push after the state write is kept**, whether it lands before `PULSE_RUN` or after: **preloading is allowed**. Boot loads (6.2) find every replayer stopped, since the reset leaves it idle.

**`RUN_CONFIG`** (window word 32, 4.19.9) follows the same rule from the window: written while stopped; **a store while a pass runs is UB** (meshsim) (v1.26.1, DECISIONS-v1.26 item 13).

#### 4.19.11 Run, done and the errors

**`PULSE_RUN`: a pass** (4.17.2). Sets `STATUS` [0], giving the replayer its single-ported memories until it finishes, clears the rows-run count, and **runs from the working state as it is** (4.19.2): pc, `instr_idx` and the offset in the state word, the registers in the image, the sizes read from part 0, and **the row count and flags from `RUN_CONFIG`** (4.19.9; v1.26.1). **It does nothing else** (DECISIONS-v1.26 items 10 and 12): it copies nothing (part 0 is the working state), and the pass's resets (the queues flushed, the credits set to 2, the size counters cleared, the decompressors reset) were done by the state word's write (4.19.8). The first row starts at once; **with a row count of 0 there is none**, and the pass ends in the next cycle (4.19.9). Every pass of the same window starts identically because the controller reloads words 0–65 before it (4.19.10); a run without that reload would carry on from the last pass's end state, with its queues, credits and counters as they were: a violation (invariant 16), and **meshsim flags a pass started with no state write since the previous pass**. *(v1.25: the run emptied the queues, set the credits, reset the decompressors, copied the state word into working registers, latched the flags and lengths and copied the image into the working file, 64 cycles.)* **Run only, no resume,** as for every engine: a run while the replayer runs is ignored (meshsim flags it, 4.13).

**`STATUS` [0], run in progress.** 1 from the end of the run store's cycle until the pass's last row has been pushed into the output queue; it reads 0 while the replayer is idle. v1.1's `replay_wait` is gone (owner, 2026-10-26): the new-expansion event says the replayer is waiting.

**Done.** The pass ends after **`RUN_CONFIG` [23:0] rows** (v1.26.1; typically 65,536, which v1.26 fixed), stopping exactly there, even inside an expansion: `STATUS` [0] falls and **run done** (`EVENTS` [0]) is posted. **At done the replayer checks its input** (`ERRORS` [6], below): the code and log queues are empty after a pass without an error, every stream's words and values used exactly. Nothing is reset: anything left in the queues (expansion words of an expansion the pass ended inside) stays until the next state write flushes them (4.19.8), and **part 0 holds the window's end state** (pc, `instr_idx`, offset and registers after the pass's last row). Rows still in the output queue drain to the consumer.

**After a trap** of its controller (4.13) the replayer carries on, unserviced: it stalls when a queue runs dry, until **the reset** (6.1), which empties its queues and resets its run state; its memories, part 0 (the working state) included, keep their contents (6.2).

**The errors** (the replayer's `ERRORS`, sticky until the reset; any one sets the tile's `ERRORS` [11], which traps the controller, 4.17.6) *(Proposed, item 123: the bit positions)*:

| bit | error | set when | kind |
|---|---|---|---|
| 0 | reserved | v1.2's bad write, now UB (4.19.10): never set | — |
| 1 | queue overrun | a push found its queue full and was dropped (4.19.8) | P |
| 2 | invalid expansion | a decode-table class 3 (an encoding no instruction Jolt accepts has, 4.19.5); an expansion word with a reserved operand code (146–255), `format` 6 or 7, or an opcode the replayer doesn't execute (4.19.7); a template running past ROM word 511 (4.19.6) | P, and H on a corrupted table or ROM |
| 3 | code-stream underflow | the core pops a code value past the stream's values out, or its decoder needs an entry past the stream's words (4.19.15) | P, and H on a corrupted stream |
| 4 | read-log underflow | the same for the read log | P, and H |
| 5 | write-log underflow | the same for the write log; never with the store log off (`RUN_CONFIG` [24] = 0, 4.19.2) | P, and H |
| 6 | done with input left over | at the pass's end (after its last row, **whatever the row count**, 0 included: owner, 2026-11-05), some stream has not had **exactly** its words taken from its queue and its values decoded: the write log only with the store log (4.19.15). One bit: the controller reads the queue occupancies to see which stream | P, and H |

(Item 55, accepted as written by the owner on 2026-10-23: the kinds and the opcode check.) **An invalid expansion or an underflow stops the pass** as if it had ended: `STATUS` [0] falls and run done is posted (the underflows, v1.26: DECISIONS-v1.26 items 6 and 7, owner, 2026-11-01; they take over v1.25's "a pop past a stream's length", which was an invalid expansion). Since every error now traps its controller terminally (4.13), this matters only for meshsim's reports. The owner expects an invalid expansion never to happen, since the tracer catches bad expansions. **Why the sizes are checked in hardware:** a record and its streams that disagree, or a refill that sent the wrong words, depend on the run's data, which simulation of our firmware may not reach (the principle at the top); a starved pass becomes a named error at once, not a hang caught by C&C 1's watchdog (DECISIONS-v1.3 section 24). Queue overrun leaves the replayer running, and [6] is set as the pass ends normally.

#### 4.19.12 The output row

**The record** *(Proposed, item 59: the layout; the field semantics are upstream Jolt's)*. Each row is 640 bits, five 128-bit words, carrying what Jolt's 80-byte trace row `RiscCycle` carries (jolt-cpp's port: `trace_owner.hpp:19–94`) plus `instr_idx`, which Jolt's provers take from a separate column (`trace_columns.inc:53`), and less the memory post-value, which the consumer derives (an LD's is rd's value after the row, an SD's is rs2's value; `trace_owner.hpp:230–235`). The values are those jolt-cpp's `expand_compressed_chunk` records (`trace_owner.cpp:893–927`), checked against gold and a16z as the ground truth says (4.19, item 113). **Serialized, the record is 80 bytes: words 0–4 in order, each 128-bit word little-endian** (byte 0 is word 0's bits [7:0]; owner, 2026-10-26, ruling N11). This is our layout, not `RiscCycle`'s field order:

| word | bits | field | value |
|---|---|---|---|
| 0 | [31:0] | `instr_idx` | the row's bytecode index (0 in padding) |
| 0 | [39:32] | `opcode` | the row opcode; `VirtualAdvice` (126) for every advice row (4.19.7) |
| 0 | [47:40] | `rd_reg` | rd's register number, 0 if rd is an immediate slot |
| 0 | [55:48] | `rs1_reg` | rs1's register number, 0 if an immediate slot |
| 0 | [63:56] | `rs2_reg` | rs2's register number, 0 if an immediate slot |
| 0 | [79:64] | `inline_sequence_remaining` | `seq_remaining` |
| 0 | [80] | `is_compressed` | 4.19.3 |
| 0 | [81] | `is_virtual` | 4.19.3 |
| 0 | [82] | `is_first_in_sequence` | 4.19.3 |
| 0 | [85:83] | `rd_is_imm`, `rs1_is_imm`, `rs2_is_imm` | the slot kinds (4.19.3), as the bytecode commits them |
| 0 | [86] | `imm_signed` | the word's `sign_imm` (4.19.3): Jolt's 128-bit immediate (`imm()`) is word 1's `imm` sign-extended when 1 and zero-extended when 0 (DECISIONS-v1.25 section 7, Q1; v1.25) |
| 0 | [127:87] | spare | 0 |
| 1 | [63:0] | `pc` | the source instruction's pc (0 in padding) |
| 1 | [127:64] | `imm` | the row's immediate (4.19.3): the sum of its immediate slots, or a `VirtualShiftData…` row's `shift_offset` sign-extended to 64 bits; 0 for an advice row. Jolt's raw `_imm` (`imm_raw`, `trace_owner.hpp:168–177`, 821e1bd), which a consumer extends to 128 bits by `imm_signed` (v1.2's consumers normalized it by opcode) |
| 2 | [63:0] | `rs1` | rs1's value before the row; 0 if an immediate slot |
| 2 | [127:64] | `rs2` | rs2's value before the row; 0 if an immediate slot |
| 3 | [63:0] | `rd_prev` | rd's value before the row; 0 if an immediate slot; **0 in every row of a pass with output `rd_prev` off** (`RUN_CONFIG` [25], 4.19.2; v1.26, moved at v1.26.1) |
| 3 | [127:64] | `rd` | rd's value after the row; 0 if an immediate slot, and 0 for x0 |
| 4 | [63:0] | `mem_addr` | for a load or store row (LD, SD and the six narrow ones, 4.19.7): *a* = rs1 + imm, and for the narrow rows *a* − (*a* & (8 − *width*)), *width* = 1, 2 or 4 bytes for B, H and W (`memory_address`, `isa/riscv.hpp:241–253`, 821e1bd), which is the doubleword's address for every access the tracer lets through; else 0 |
| 4 | [127:64] | `mem_value` | for a load row, **rd's value after the row**: the doubleword loaded, and 0 for an LD to x0 (4.19.7) *(pending the upstream check, item 113)*; for a store row, the memory pre-value from the log (0 in a pass without the store log, 4.19.2); else 0 |

A padding row's record is all zeros, as Jolt's padded rows are (4.19.2).

**The output queue** holds 32 rows (about 2.5 KB) between the replayer and its consumer, with ready/valid backpressure: when it is full, the replayer stops before its next row. It absorbs consumer slowdowns and the replayer's own stalls on expansion misses (tens of cycles). The consumer's side of it, its width and its rate, belong to the materializer and are *(Open, item 59)*; until it is specified, meshsim drains it with a sink that takes one row per cycle.

#### 4.19.13 The resident replay region (the software contract)

The trace stays in big SRAM for the whole proof, in a **replay region** per tile, a software-managed cache. **Residency is best effort** (owner, 2026-10-26): each stage keeps as much of a window's streams resident as its free big SRAM allows, and the rest streams from LPDDR through a **staging buffer in big SRAM** and is refilled locally. No fixed size is guaranteed; normal programs fit, and adversarial ones (dense windows) stream, which the host prices. The hardware knows nothing of it: it sees only the packets the controller's streams and the memory system send to its region. This subsection is the contract software keeps so that the hardware above works.

**Layout.**
1. **This window's templates first:** only the controller-fed templates its 64K rows actually use (often none; a window of BLAKE3 calls needs about 20 KB [estimate]). The Tracer lists the inlines in the record's word 67 (4.21.4); the tile keeps those and the 60 CSR templates from C&C 3's broadcast (4.19.9; v1.3).
2. **Then code and the two logs** as 4 KB blocks in consumption order, code, read-log and write-log blocks interleaved, so "resident" is a single boundary. A pass that needs only one log keeps and fetches only that one.
3. **The rest,** beyond what the stage's free big SRAM holds (with a 512 KB region, at most about 190 KB in the densest windows [meas: design notes]), stays in LPDDR and is **fetched over the request bus into big SRAM** (mode-0 writes into region 0x03, 4 KB requests, counted on a write counter of their own) ahead of the replayer, then **refilled locally like resident data** (owner, 2026-10-26). The direct read from LPDDR into a queue port is no longer part of the replayer's contract. The controller loop's refill waits on its refill counter's done alone (4.19.9), so a fetch in flight on another counter never holds a refill up (owner, 2026-10-26, item 94). Before refilling from a fetched block, the controller waits for that fetch's counter.

The median window's trace is about 335 KB and the densest about 700 KB [meas: design notes]. At the stages whose scratchpad margin is too small, the controller shrinks or evicts the region and reloads it afterwards (at 512 KB per tile, about 33 MB per chip, about 2 ms [estimate]). The same mechanism can spill compact, expensive-to-rebuild intermediates; anything the trace can regenerate is better replayed (about 0.2 ms per pass) than spilled. In sumcheck tails the controller can prefetch the next pass's streams from LPDDR (about 21 MB per chip, about 1.3 ms at 16–17 GB/s [estimate]), into areas the remaining rounds don't use, with C&C 1 pacing the fill so it never fans in.

**The rules software keeps:**
- **Stream to a queue port from its window's base** (4.19.10), in 128-bit packets, with runs of at most a half; a refill is min(half, entries remaining) (4.19.8). Local refills may have two outstanding.
- **No LPDDR read targets a queue** (part C): fetches go into the staging buffer, and the loop refills from there.
- **No other tile writes the replayer's region during a pass,** and a queue port takes one source at a time (invariant 18): the region may be written from any source (4.17.5), but the controller loop counts only its own refills (4.19.9).
- **Load the decode table and the ROM while the replayer is stopped** (4.19.10), and **reload the record's words 0–65 before every pass** (4.19.10): part 0 is the working state, and the state word's write resets the pass (4.19.8). The tile keeps each window's record in big SRAM for this. Pushes after the state write are kept, so the first refills may precede `PULSE_RUN` (preloading).
- **Before the state write, every queue push has landed** (owner, 2026-10-23; v1.26: was before a start): the refill counter's done for the controller's refill runs (4.19.9). The rule is software's (invariant 16), kept by waiting on those counters before writing part 0: the replayer can't see a push in flight, since every packet reaches it through one in-order landing path (meshsim doesn't check it).
- **Every refill except a stream's last is exactly half a queue,** and a credit's refill never exceeds half (4.19.8).

**Code is not in big SRAM.** Each tile controller has its own program and data memories, and stage overlays are broadcast by C&C 1 at stage boundaries into a parked controller (6.5), so the earlier plan's 256 KiB code reservation in big SRAM is reclaimed for data.

#### 4.19.14 Cost

**Memory per tile** (v1.26): 4 + 4 + 4 + 4 KB of queues (code, expansion, read log, write log; v1.25's logs were 8 KB each), an 8 KB ROM, a 4 KB decode table, 1 KB of register file, which is the register image (4.19.7; v1.25 had a separate 1 KB image beside it), 32 B of state and stream words (words 66–67 aren't stored), about 2.5 KB of output queue (32 × 80 B) and 384 B of decompressor tables: **about 32 KB** [estimate] (v1.25: about 41 KB). Across 64 tiles that is about 2.0 MB (v1.25: 2.6 MB), roughly 6–8 mm² per chip with the logic [estimate, v1.25's 7–10 mm² scaled by the memory], against 48 MB for the 768 KB-per-tile design planned earlier.

**Logic per tile:** the ALU (with `VirtualPext` for any mask, 4.19.7) and comparator, the 128 × 64-bit true dual-port register file, two adders (memory address, pc + imm), the operand resolver, the decode index logic, four queue readers and the credit counters, three decompressors with their size counters (4.19.15): about 0.09–0.14 mm² with its memories [estimate; v1.25's 0.1–0.15 with 8 KB more of queues]. Section 11 adds it to the tile's budget; its placement is in section 12.

#### 4.19.15 Stream decompression

*(Owner, 2026-10-26, part D.)* Each of the three streams (code, read log, write log, 4.19.2) arrives **raw or compressed**, chosen per window and per stream by the tracer and recorded in the checkpoint's **stream word**, each stream's compression flag beside its sizes (4.19.2, 4.19.10; v1.26: v1.25 kept the flags in the state word [114:112]). A **decompressor** per stream sits between its queue and the replayer core: the queue holds the stream as delivered, in 128-bit entries, and the credits count those entries (4.19.8).

**Bit order.** A stream is read **least significant bit first within each 128-bit entry,** entries in queue order; every field and value is read least significant bit first. An entry leaves its queue when its last bit has been consumed, or when the stream's last value ends inside it.

**Raw streams.** Code: 32-bit opcode words, four per entry. Logs: 64-bit words, two per entry. Low word first in both.

**Compressed streams** are bit-packed against a **16-entry table** of 64-bit values, E[0] the most recent:
- **hit:** a flag bit 1, then a 4-bit index *i*; the value is E[*i*];
- **miss:** a flag bit 0, a size bit (0 = 32 bits, 1 = 64 bits), then the value, 32 or 64 bits. **A 32-bit miss is zero-extended to 64 bits.** Code values are opcode words, always 32-bit misses or hits.

**True LRU, move to front** (the replacement rule, which the tracer's encoder must match exactly). For the code and read-log streams it runs over all sixteen entries; for the write log, over entries 0–14 (below):
- **hit *i*:** output E[*i*]; then E[1..*i*] ← E[0..*i* − 1] and E[0] ← the old E[*i*] (for *i* = 0 nothing moves);
- **miss *v*:** output *v*; then E[1..15] ← E[0..14] (the old E[15] is dropped) and E[0] ← *v*. A miss whose value is already in the table is legal: it is inserted again, and the older copy stays where it moved to.
- **Reset:** all sixteen entries are 0 after every state write (4.19.8; v1.26, DECISIONS-v1.26 item 10; v1.25: at every run's start), so checkpoints and mid-window starts need no table state.

**The write log's last-load entry** (owner, 2026-10-26). The pre-values of sub-word stores, AMOs and SC, and of many plain read-modify-write SDs, equal the value of the LD just before them, which the read log has already delivered. So the **write-log table splits into 15 LRU entries (E[0]–E[14]) and one last-load entry, E[15]**; the code and read-log tables stay plain 16-entry LRUs.
- **Update:** on every **load row**, LD and `VirtualLoadB/H/W` (DECISIONS-v1.25 section 7, Q2), the doubleword it loads (popped from the read log) is copied into write-log E[15]. At `821e1bd` every sub-word store, SC.W and 32-bit AMO loads its doubleword with a `VirtualLoad…` row just before its `VirtualStore…` row, whose pre-value is that same doubleword. Rows that aren't loads (advice, next indices) don't update it. The tracer's encoder applies exactly the same rule.
- **An LD to x0** (owner, 2026-10-26, part G, ruling N5) *(pending the upstream check, item 113)*: **E[15] takes the value popped from the read log,** as for any LD, even though the row writes nothing and **its record reports a memory value of 0** (4.19.7, flag F4 of `spec/jolt/replayer-opcodes.md`). The two rules are separate: the decompressor sees the log value, the record sees what rd holds. The tracer's encoder applies the same rule.
- **hit 15:** output E[15]; nothing is reordered.
- **hit *i* < 15:** output E[*i*]; then E[1..*i*] ← E[0..*i* − 1] and E[0] ← the old E[*i*].
- **miss *v*:** output *v*; then E[1..14] ← E[0..13] (the old E[14] is dropped) and E[0] ← *v*. E[15] is untouched.
- **Reset:** E[15] is 0 after each state write, with the rest.
- **Encoder rule:** a value matching both E[15] and an LRU entry is coded as **index 15**, so the LRU order isn't disturbed. The decoder doesn't depend on that choice; the rule makes the tracer's encoding deterministic.
- **Order within the pass:** a load row's read-log pop updates E[15] before any later row's write-log pop, since rows are strictly sequential (4.19.7).

The table update happens as each value is produced; the core takes one code value per instruction and at most one log value per row, so each unpacker produces at most one value per row.

**The stream's end and its checks** (v1.26; DECISIONS-v1.26 items 2, 5 and 6). The stream word gives each stream's size as two numbers (4.19.2): **its words**, the 128-bit entries it occupies as sent (16 bits; at most 33,792 [derived: 65,536 values × at most 66 bits ÷ 128]), and **its values out**, the 32- or 64-bit values it decodes to (17 bits; at most 65,536). **The decoder stops at the value count:** the bits after a stream's last value, to the end of its last word, are padding and **can be anything**, compressed or not; the tracer need not zero them (v1.25: zero bits, ended by an exact bit length, which v1.26 drops). The replayer counts, per stream, the entries it has taken from the queue and the values it has decoded, **from 0 at the state word's write** (4.19.8), against the sizes **read from the stream word in part 0**, which can't change while a pass runs (4.19.2):
- **Underflow, at once:** a consumer popping a value past the stream's values out, or the decoder needing an entry past the stream's words, sets that stream's underflow bit, `ERRORS` [3] code, [4] read log or [5] write log, and stops the pass (4.19.11).
- **Leftover, at done:** when the pass ends, each stream must have had **exactly** its words taken and its values decoded; otherwise `ERRORS` [6] (4.19.11). A stream holding a whole extra padding word is so an error: the tracer pads only within the last word, so **words** is the fewest entries that hold the coded values (⌈values / 4⌉ raw code, ⌈values / 2⌉ a raw log). A coded stream whose words are all read but which decodes to fewer or more values is corrupt, and is caught by one rule or the other.
- **The write log is excluded from both** when the pass doesn't use the store log (`RUN_CONFIG` [24] = 0, 4.19.2; v1.26.1, DECISIONS-v1.26 item 15): its sizes stay in the stream word as the tracer produced them, and are ignored.
- **The expansion queue has no sizes and no check:** fed per expansion, it ends by `seq_remaining` (DECISIONS-v1.3 section 24).
- An empty queue with words still to come is not an error: the replayer stalls until data lands (4.19.8).

**Cost:** three 16 × 64-bit tables (about 384 B of registers; the write log's E[15] is its last-load register, with a mux from the read-log path), three bit unpackers and three pairs of size counters, words and values (4.19.14).


From 8.11, the peripheral windows:

**The replayer's window** (peripheral 1 on tiles 0–63; 4.19.9; the words, owner, 2026-10-29; *(Proposed, item 123: the event and error bits)*):

| word | register | bits | function | reset |
|---|---|---|---|---|
| 0 | `PARAMS` | [7:0] = 4 | type 4, the replayer | 4 |
| 1 | `STATUS` | [0] run in progress; [1] 0 | [0] is v1.1's `periph_running` (4.19.11) | 0 |
| 2 | `ERRORS` | [0] reserved (v1.2: bad write), [1] queue overrun, [2] invalid expansion, [3] code-stream underflow, [4] read-log underflow, [5] write-log underflow, [6] done with input left over (v1.26) | 4.19.11 | 0 |
| 5–7 | `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK` | [0] run done (depth 1), [2] code credit, [3] read-log credit, [4] write-log credit, [5] expansion credit (depth 2 each), [6] new expansion (depth 1) | 4.19.8, 4.19.9 | 0 |
| 4, 8, 9 | — | | reserved: the replayer has no DMA | — |
| 32 | `RUN_CONFIG` (R/W) | [23:0] rows to run; [24] use the store log; [25] output `rd_prev`; [28:26] the Tracer's compression enables (code, read log, write log), held and ignored; [31:29] reserved (read 0) | what the next pass runs (4.19.9): written while stopped (a write while running is UB, meshsim), used by the next `PULSE_RUN`; sticky. A count of 0 runs nothing and posts run done. The Tracer's word 32 too (4.17.4). v1.26.1 | 0 |
| 33 | `ROWS_RUN` (R) | [23:0]; [31:24] 0 | rows of this pass completed (v1.26.1: 24 bits; v1.26: word 32, modulo 2¹⁶); cleared by `PULSE_RUN`, the row count after a pass; rows left = the row count − rows run while `STATUS` [0] is 1. v1.1's `PERIPH_STATUS_0` [15:0] | 0 |
| 34 | `EXPANSION_OFFSET` (R) | [15:0]; [31:16] 0 | the row within the current expansion where the next row starts (0 to 3,120), where the controller begins sending a controller-fed expansion's words. v1.1's `PERIPH_STATUS_0` [31:16]; v1.26: word 33 | 0 |
| 35 | `EXPANSION_OPCODE` (R) | [31:0] | the instruction awaiting a controller-fed expansion: the stored opcode-stream word, bit 0 clear for a compressed instruction (4.19.2). Set when new expansion is posted and held until the next. v1.1's `PERIPH_STATUS_1`; v1.26: word 34 | 0 |

Words 32–35 are the same on the Tracer (4.17.4; DECISIONS-v1.26 item 14). Its words 33–35 have no read side effects; a store to any is a bus error. *(Item 53's modulo-2¹⁶ count is superseded: v1.26.1.)* Its C struct is `ReplayerRegs`, `PeripheralStd` followed by these words (8.1).

From section 9, invariant 16 (the peripherals' and the replayer's invariants):

    - the replayer's: no bad write (UB, meshsim only, 4.19.10); no `RUN_CONFIG` write while a pass runs (UB, meshsim only, 4.19.9; v1.26.1); no queue overrun, no invalid expansion, no stream underflow and no input left over at done (hardware: its `ERRORS` [1]–[6], 4.19.11); no other tile writes its region during a pass (4.19.13); every refill except a stream's last is exactly half a queue, and no refill exceeds the credit taken for it (4.19.8); no LPDDR read targets a replayer queue (its overflow goes through big SRAM, 4.19.13); every opcode word and log word a pass is given is consumed in order, and a pass consumes exactly its tile's log; every queue push, whatever its source, has landed before the state write (4.19.8); no part-0 write while a pass runs, words 66–67 included (4.19.10); the record's words 0–65 are reloaded before every pass (4.19.10; v1.26); no zero mask reaches `VirtualSRL`, `VirtualSRLI`, `VirtualSRA` or `VirtualSRAI` (4.19.7; the result is undefined, owner, 2026-10-26); `offset_imm` is set only on `VirtualShiftData…` rows (4.19.3); no memory row is misaligned (4.19.7; jolt-cpp aborts on one); a non-contiguous mask reaching `VirtualPext` or `VirtualPextSigned` is correct and only **reported**, as a statistic (4.19.7); and the replayer's rows equal gold's and a16z's (the ground truth, 4.19; jolt-cpp's at the pin wherever they agree; the validation harness, item 58; the vectors of `spec/jolt/replayer-opcodes.md` section 9; item 113);

From section 10, the parameter summary:

| parameter | default |
|---|---|
| replayer (4.19), peripheral type 4 | peripheral 1 on tiles 0–63; tables built at jolt-cpp `821e1bd`, the pin `88dca85`, checked identical (v1.3, 4.19); universal tables (ROM 388 of 512 words used, decode table 306 ROM, 17 controller and 1,725 invalid entries, 75 controller-fed template numbers); 88 executed row opcodes; expansion word `offset_imm` [63], `sign_imm` [62]; a pass of `RUN_CONFIG` [23:0] rows, typically 65,536 (words 32–35 shared with the Tracer, v1.26.1); code, expansion, read-log and write-log queues of 256 × 128 bits each (halves of 128; v1.26, the logs were 512), two credits of half a queue each, reset with the credits, size counters and decompressors by the state word's write; a 68-word state record, whose words 0–65 are part 0, the working state itself; per stream, 16-bit word and 17-bit value counts (the stream word's advice field ignored); three LRU decompressors of 16 × 64 bits; ROM 512 × 128 bits; decode table 2K × 16 bits, built as 256 × 128 (4.19.10); register file 128 × 64 bits; output queue 32 rows of 640 bits; rows of 3 or 5 cycles; no start copy (the record is reloaded before each pass, 66 packets); about 32 KB of memory per tile |

## Open items (section 13's numbering)

Settled items are listed in `spec/machine-spec.md` section 13's index; their record is `spec/history/machine-spec-v1.3-section-13.md`.

| item | what is to be settled | section | status |
|---|---|---|---|
| 59 | Decided: the output row's layout; open: the consumer side, its width and rate, the placement against the materializer | 4.19.12 | decided, part open |
| 113 | Decided: the ground truth (a16z, `7189216`); open: each case tagged "pending the upstream check", until gold settles it | 4.19, 4.19.12 | decided, part open |
| 140 | Open, with Tony: the PEXT method, and so a non-contiguous mask's cycles | 4.19.7 | decided, part open |
| 35 | The `virt` bit stored per word; the NOP row's virtual flag (pending the upstream check, 113) | 4.19.3, 4.19.4 | proposed |
| 38 | The decode index's special groups (AMO, custom-0, SYSTEM) | 4.19.5 | proposed |
| 39 | The padding and shared NOP rows at ROM words 0 and 1 | 4.19.6 | proposed |
| 42 | Fetch and decode overlap the previous row | 4.19.7 | proposed |
| 56 | Jolt's conventions kept as table data | 4.19.3, 4.19.4, 4.19.7 | proposed |
| 57 | The replayer's hardwired Jolt behaviour (two rules pending the upstream check, 113) | 4.19.7 | proposed |
| 36 | Decided: what the replayer executes; still proposed: it doesn't check that temporaries stay in 40–47 | 4.19.4, 4.19.7 | decided, part proposed |
| 115 | Decided: C&C 3 broadcasts the used set; still proposed: the size-first command | 4.19.2, 4.19.13 | decided, part proposed |
| 123 | Decided: the status words; still proposed: the event and error bits | 8.11, 4.19.9, 4.19.11 | decided, part proposed |
| 103 | Still proposed in its text: SD rows take the write log; open: measure the read/write split | 4.19.2, 4.19.8 | unclear |
| 137 | Re-cite the `4f36b3d` file:line references at `821e1bd` | 4.19 | unclear |
| 158 | Rebuild the universal tables to a16z's rule for LR/SC with rd = x0 and the NOP rows' virtual flag | 4.19.4 | decided, not yet done |
| 139 | Possible later addition: a SYSTEM-group decode putting the CSR templates in the ROM | 4.19.6, 4.19.9 | decided, part later |
