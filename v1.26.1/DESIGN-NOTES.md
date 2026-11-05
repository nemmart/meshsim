# Replayer and mesh v1.1: design notes

Oct 22, 2026 (updated Oct 23, 2026) · Niall Emmart

Exported from the Claude Doc "Replayer and mesh v1.1: design notes". The doc is the master copy; this file is a snapshot for worker sessions.

## Status and scope

This records the replayer design and the mesh v1.1 changes agreed in the owner sessions of 2026-10-22 and 2026-10-23. Everything here is **candidate** unless marked decided; nothing has been written to any repository.

Updated 2026-10-23 with the queues, ROM, decode table, fixed ALU schedules, resident replay region, request bus and the shared-resource allocation table. It builds on three sources: the architecture brief (2026-10-21), the mesh-sorter v1.0 package, and the replayer expansion-encoding note of 2026-10-22. Where this note differs from the encoding note, this note is newer. Tracer facts are from the jolt-cpp tracer copy on the `tracer-standalone` branch of `jolt-asic-planning` (paths relative to `tracer/src/tracer/`).

The meshsim simulator is now a planning reference, not the definition of the hardware.

## Context

Each ASIC proves a shard of 2^22 rows; its 64 tiles replay 2^16 consecutive rows each. A row is one row of Jolt's expanded trace, not one RISC-V instruction.

- **Tracer engine.** One RV64 per chip starts from the memory image, executes the shard's rows and writes the run log to LPDDR4X.
- **Replayer.** Each tile regenerates its rows from its share of the run log, at about 80 B per row, and streams them into the materializer.
- **Materializer.** Turns trace rows into initial sumcheck data in the tile's 4 MB SRAM. Out of scope here: it differs per sumcheck.
- **Pipelining.** While the chip proves proof N, the tracer engine traces proof N+1.

A tile's full trace is about 5 MB (2^16 × 80 B), more than its 4 MB SRAM, so the trace is never stored whole: it streams from replayer to materializer.

**LPDDR budget.** With the trace resident, LPDDR carries per proof and chip: about 1.4–8 MB of update stream, about 24–28 MB of tracer writes (opcodes plus log), one load of the trace into the tiles (about 21 MB), and overflow, reload and spill traffic at the tight stages. That is a few ms of the 100 ms per proof at about 17 GB/s, plus the tracer engine's own memory reads [estimate].

## Run log

The tracer engine writes two streams per tile, plus a checkpoint. The replayer has a small ALU, so it recomputes every row it can and takes from the log only the words it cannot compute. The streams live in the tile's 512 KB resident replay region in big SRAM (below) and reach the replayer through its queues (decided 2026-10-22 and 2026-10-23; the value-per-row design is the fallback).

| Stream | Granularity | Contents | Per tile |
| --- | --- | --- | --- |
| Opcode | One 32-bit word per native instruction, held during its expansion | The RISC-V instruction, compressed ones stored decompressed with bit 0 cleared as the flag | about 43K words (175 KB) at 1.5 rows per native, 256 KB at most |
| Memory log | One 64-bit word per row the ALU can't compute | See below | 0.28–0.32 words per row on average, up to 0.833 in the densest windows [measured]: about 160 KB on average, 437 KB at most |

**What needs a log word:**

- LD: the value loaded.
- SD: the memory pre-value; the post-value is rs2.
- Advice: VirtualAdvice and the DIV/REM advice opcodes.
- A row whose next `instr_idx` isn't + 1 (jumps, taken branches, traps, returns): the next `instr_idx`. Its target PC and a branch's outcome are computed.

Measured on both 2^29 workloads (LD / SD / advice / non-sequential, words per row): EVM Zone 0.170 / 0.115 / 0.0001 / 0.030, total 0.315; Cyclone 0.147 / 0.090 / 0.0002 / 0.039, total 0.277. Per 64K-row window the median is 0.29–0.32 and the maximum 0.833 in both; the worst sliding 512-, 1,024- and 4,096-row spans are all about 0.835, so dense stretches are sustained, not bursts. Per chip a replay pass reads 9.1–11.1 MB of log on EVM Zone and 4.0–15.4 MB on Cyclone.

**Checkpoint per tile.** An initial register image (all 128 registers, 64 words of 128 bits) and one 128-bit state word, both loaded through the replayer mode:

| Bits | State word field |
| --- | --- |
| 63:0 | PC |
| 95:64 | `instr_idx` |
| 111:96 | Next offset in the sequence |
| 127:112 | Spare |

Sending all 128 registers every time avoids special-casing a mid-sequence start, at 1 KB per tile. A tile that starts inside a sequence has the instruction in progress as its first opcode-stream entry.

**Re-runs.** The replayer runs many times per proof, once per sumcheck stage that needs the trace. The start pulse copies the register image into the working register file (64 cycles, two registers per cycle) and reads the state word into working copies; replay never writes either, so every run starts from the same point.

**Inline scratch.** Inline templates end with explicit `ADDI vN, x0, 0` rows that zero each scratch register they used, so the replayer needs no extra state for registers 48–127.

**End of trace.** The trace is padded to 2^29 rows after the program ends, and the tracer writes the padding as real opcode entries: one opcode word 0x00000000 per padding row, with no log words. Every stored instruction has bit 1 set, so bits [1:0] = 00 identify padding before the table lookup (otherwise its index would hit LB's entry). The padding word maps to the padding template in the ROM: an all-zero NOP row with `hold` set, so pc and `instr_idx` stay 0 as in the tracer's padding. The program's last real row jumps to pc 0, and its log word sets `instr_idx` to 0.

## Replayer

The replayer turns the opcode stream plus the memory log into 80 B trace rows. It has a small, multi-cycle ALU and runs at a fixed 3 cycles per simple row and 5 per multiply, about 3.3 on average.

**Per row it:**

1. Takes the current native instruction from the opcode stream and finds its row through the decode table and its ROM template.
2. Resolves the row's operands (operand codes below) and reads rs1, rs2 and rd_prev from the register file.
3. Computes the result with the ALU, or takes it from the log for LD, SD and advice rows; writes rd; forms the memory address as rs1 + immediate.
4. Pushes the row into the output queue.

**`instr_idx`.** Increments by 1 on every row that continues sequentially: the committed bytecode is laid out in program order with each expansion contiguous. When the next row is not sequential, the next `instr_idx` comes from the log. Which rows can do that is explicit, not inferred from the row opcode: a `next_from_value` bit in the row's expansion word: in the ROM for branches, JAL, JALR and template rows, and in controller-fed words for MRET, ECALL and trap returns. A conditional branch takes a log word only when it is taken; the comparator decides. No PC-to-index table is needed.

**PC.** Computed, not stored:

| Case | Next PC |
| --- | --- |
| Sequential, or inside a sequence | pc + 2 or 4 at the sequence end, else unchanged |
| Conditional branch | pc + imm if the comparator says taken, else sequential |
| JAL | pc + imm |
| JALR | rs1 + imm |
| MRET, traps | mepc (v36) or mtvec (v34) from the register file |

**ALU.** Covers the 168 row opcodes bit-exactly with Jolt. Shifts mostly don't survive as shifts in Jolt (SLLI becomes VirtualMULI, SLL a power of two and a multiply, the right shifts use bitmask virtual ops), so they fall into the multiply or virtual-op paths. The multiplier is four 16 × 64 multipliers in parallel, a full 64 × 64 array, which is cheap in area. There is no divider: Jolt's DIV and REM use advice plus checks.

**Fixed row schedules** (decided 2026-10-23). Rows are strictly sequential, never overlapped, so there are no hazards and no bypass. The register file is true dual-port; when rs2 = rd, the read returns the old value.

| Row kind | Cycles | Schedule |
| --- | --- | --- |
| Simple: add, sub, logic; and log rows (LD, SD, advice) | 3 | 1: read rs1 and rs2. 2: compute, read rd_prev. 3: write rd. |
| Multiply (MUL, MULH, MULHU, MULHSU, and virtual ops that multiply) | 5 | 1: read rs1 and rs2. 2: four 16 × 64 partial products, read rd_prev. 3–4: carry-save and final add into the 128-bit product, with MULH's sign corrections as one more carry-save input. 5: write rd (low or high half). |

**Rate.** About 3.3 cycles per row on average with about 10% multiply rows (to be measured), so a 64K-row pass is about 216K cycles plus about 1.3K of controller-fed expansion stalls: about 0.22 ms at 1 GHz with everything local. That is about 10 ms for the 46 replay passes of an Akita proof. The consuming sumcheck kernels likely need 3–6 cycles per row anyway, so replay and consumer are roughly matched.

**Logic.** The ALU and comparator, the 128 × 64-bit true dual-port register file, two adders (memory address, pc + imm), the operand resolver, the decode index logic, three queue readers and the credit counters: about 0.1–0.15 mm² per tile with its memories [estimate].

**Output queue.** A queue between replayer and materializer with ready/valid backpressure: when it is full the replayer stops. It absorbs materializer slowdowns and the replayer's own stalls on expansion misses (tens of cycles), so its depth is tens of 80 B rows, a few KB.

## Expansion word and operand codes

Every expanded row is described by one 128-bit word, replacing the tracer's 48 B `VirtualInstruction`. The same format serves the ROM and the words the controller sends.

| Bits | Field | Contents |
| --- | --- | --- |
| 7:0 | `opcode` | Row opcode, one of 168 |
| 15:8 | `rd` | Operand code |
| 23:16 | `rs1` | Operand code |
| 31:24 | `rs2` | Operand code |
| 43:32 | `seq_remaining` | Rows left after this one; 0 on the last row (maximum 3,120) |
| 44 | `next_from_value` | This row can change control flow: when it does, the next `instr_idx` comes from the log |
| 45 | `hold` | pc and `instr_idx` don't advance after this row (padding) |
| 47:46 | `rd0` | rd = x0 policy for the template: 0 none, 1 NOP, 2 remap, 3 reserved (first word of a template) |
| 50:48 | `format` | The source instruction's immediate format for code 132: 0 R, 1 I, 2 S, 3 B, 4 U, 5 J |
| 63:51 | spare | 13 bits |
| 127:64 | `const` | Constant immediate; on secp256k1 VirtualAdvice rows, `advice_source` |

The per-instruction fields (`rd0`, `format`, `next_from_value`) live in the ROM words, not the decode table, because every instruction is a template (next section).

`seq_remaining` = 0 marks the end of an expansion; no separate end bit is needed.

**Operand codes.** An operand draws from four places:

| Code | Meaning |
| --- | --- |
| 0–127 | Register: x0–x31, then virtual 32–127 |
| 128 | Immediate = `const` (also the zero placeholder, with `const` = 0) |
| 129 / 130 / 131 | The source instruction's rd / rs1 / rs2 |
| 132 | The source instruction's immediate, decoded by its format and sign-extended to 64 bits |
| 133 | `mask_trailing_zeros(imm & 0x3f)` (SRLI, SRAI) |
| 134 | `1 << (imm & 0x3f)` (SLLI → VirtualMULI) |
| 135 | `1 << (imm & 0x1f)` (SLLIW → VirtualMULIW) |
| 136 | 32-bit window bitmask (SRLIW, SRAIW) |
| 137–144 | Temporary k = code − 137, resolved as register 40 + k + offset (rd = x0 section) |
| 145–255 | Reserved |

In codes 133–136, `imm` is the source instruction's immediate, the same value as code 132. Templates never contain literal registers 40–47; they use the temporary codes.

Code 0 (x0) and code 128 with `const` = 0 differ: the `*_is_imm` flags are committed, so a zero placeholder must not replay as x0.

**Derived, not stored:** `is_first` (first word of a sequence), `is_virtual` (row opcode differs from the source, or the sequence has more than one row), `pc_increment` (0 unless `seq_remaining` = 0, then 2 or 4) and `risc_pc`.

## Decode table and ROM

**Every instruction is a template** (decided 2026-10-23). A pass-through instruction is a one-word template whose operands are codes 129–132 (copy the source instruction's registers or immediate), so there is one datapath: every row comes from a ROM word.

**Decode table: 2K × 16 bits (4 KB).** An 11-bit index is formed by a small hardware decoder that picks instruction bits by opcode group:

| Opcode group | Index bits |
| --- | --- |
| Most instructions | opcode[6:2], funct3, bit 30, bit 25 |
| AMO (0101111) | an AMO marker, W/D (funct3 bit 0), funct5 (bits 31:27): separates LR, SC and every AMO |
| custom-0 (inlines) | the funct7 family and funct3, one entry per inline |
| SYSTEM | bits 31:20 for ECALL, EBREAK and MRET, and funct3 for the CSRs |

The entry is 11 bits used of 16:

| Bits | Field |
| --- | --- |
| 1:0 | `class`: 0 ROM template, 1 controller (inline), 2 controller (system), 3 reserved |
| 10:2 | `start`: first ROM word |
| 15:11 | spare |

There is no length field: each ROM word's `seq_remaining` marks the template's end, including on a mid-sequence start (start + offset). Unused entries default to a controller class, so an unknown instruction becomes a software question, not an error. The padding word (bits [1:0] = 00) is caught before the lookup and goes to the padding template.

**ROM: 8 KB, 512 × 128-bit words, dual-ported, loaded at boot.** It holds every template the hardware serves:

| Content | Words |
| --- | --- |
| Emulation sequences (shifts, sub-word loads and stores, M ops) | about 172 |
| Pass-through instructions, one word each | about 45 |
| Atomics: LR.D 5, LR.W 9, SC.D 12, SC.W 26, 18 AMOs 237 | 289 |
| Shared NOP and padding row | 2 |
| **Total** | **about 508 of 512** |

The fit is tight; reusing identical words or a slightly larger ROM restores headroom. rd = x0 remaps need no variant words: the +1 temporary rule applies in hardware. Atomics in the ROM leave the controller only inlines and system instructions.

**Loading.** The table and ROM load through the replayer write mode, by C&C broadcast at boot.

## rd = x0 policy and the temporary offset

When a side-effecting instruction has rd = x0, the tracer rewrites rd to a throwaway virtual register, v40, before expanding it; the template's own temporaries then start at v41. The replayer reproduces this with a one-bit offset.

**The rule.** For one expansion, offset = 1 when the source rd field is x0 and the template's `rd0` policy is remap; otherwise 0. Then:

- temporary k (codes 137–144) resolves to 40 + k + offset;
- under remap, source rd (code 129) resolves to 40.

Under the NOP policy with rd = x0, the instruction becomes one shared NOP row.

**Example.** `lb a5, 4(a0)` against `lb x0, 4(a0)`:

| Row | Normal (offset 0) | Remapped (offset 1) |
| --- | --- | --- |
| 1 | VirtualAlignAddr v41, a0, 4 | VirtualAlignAddr v42, a0, 4 |
| 2 | LD v41, v41, 0 | LD v42, v42, 0 |
| 3 | VirtualWindowMaskB v40, a0, 4 | VirtualWindowMaskB v41, a0, 4 |
| 4 | VirtualPextSigned a5, v41, v40 | VirtualPextSigned v40, v42, v41 |

**Which instructions remap** (in `has_side_effects_beyond_rd` and not in `rd0_is_handled_internally`, `isa/virtual.cpp`):

| Instructions | Served by | Effect |
| --- | --- | --- |
| JAL, JALR, LD, VirtualHostIO | One-word ROM templates | rd becomes 40; e.g. `ret` becomes JALR v40, ra, 0 |
| LB, LBU, LH, LHU, LW, LWU | ROM templates | rd becomes 40, temporaries +1 |
| The 18 AMOs | ROM templates | rd becomes 40, temporaries +1; no stored variants |

LR/SC, CSRRW/CSRRS, ECALL, EBREAK, MRET and the inlines are exempt. With the decode table's AMO index (funct5 and W/D), every atomic reaches its own template, and the +1 rule handles the remap in hardware. On a mid-sequence tile start the replayer recomputes the offset from the opcode-stream entry.

## Queues and the controller loop

Three queues feed the replayer. They are true FIFOs: producers push at the tail and the replayer pops, with no slot addresses (decided 2026-10-23).

| Queue | Size | Entries | Refill unit (half) | Normal source |
| --- | --- | --- | --- | --- |
| Code | 4 KB | 1,024 × 32-bit opcodes | 512 | always local |
| Expansion | 4 KB | 256 × 128-bit words | 128 | always local |
| Value (log) | 8 KB | 1,024 × 64-bit words | 512 | local, or LPDDR on overflow |

All three are dual-ported, so pushes land while the replayer runs. Code and expansion data are always resident, so a half covers the local refill latency (a few hundred cycles) with room to spare; values may come from LPDDR, so their queue is the larger one.

**Queue ports.** In the replayer write mode (mode 8), an address range names a queue: the packet is appended at the tail and the rest of the address is ignored. A 128-bit packet pushes 4 opcodes, 2 values or 1 expansion word. Each port decodes only its high address bits, so the address generator's per-packet address stepping stays within the port.

**Credits.** Each queue tracks occupancy (landed entries) and pending (entries promised but not landed). Its need bit is set while capacity − occupancy − pending ≥ half. Reading the bit adds half to pending; each arrival moves an entry from pending to occupancy. Up to two refills can be outstanding, which keeps a draining queue fed, and a queue that empties simply stalls the replayer until data lands. The controller sends min(half, words remaining); a short final push overstates pending, which only suppresses requests that aren't needed, and the count resets at the next expansion or pass. It may also deliver one half as several smaller chunks; the queue doesn't care.

**Wake events** (decided 2026-10-23). The replayer's requests are bits [17:13] of the tile's shared `WAKE_EVENTS` register: [13] code low-water, [14] value low-water, [15] expansion low-water (only while the current expansion still has words to deliver), [16] new expansion (`REPLAYER_OPCODE` and `REPLAYER_STATUS` say which), [17] done. Bits [7:0] are the request bus's 8 tags and [12:8] the system events (below); [31:18] are reserved for future units. A load of `WAKE_EVENTS` returns every pending event and clears only those under `WAKE_MASK`, and `WAKE_PEEK` shows them without clearing; software acts on value & mask. Low-water bits are live conditions: a load that returns one under the mask credits half that queue; new expansion and done are sticky until returned, and returning new expansion credits the first 128 expansion words.

**The controller loop.** Every `WFI` has exactly the sources its mask allows:

```
loop:
    WAKE_MASK = GO | replayer bits;  WFI     ; only a replayer event can wake it (GO never comes mid-pass)
    ev = WAKE_EVENTS & replayer bits         ; consumes those events
    if ev.done: exit
    if ev.expansion: decode REPLAYER_OPCODE (inlines and system only)
    WAKE_MASK = GO | counters done           ; only the copy's completion can wake it
    set DMA_REPLAYER_REMAINING; write STREAM entries; start the run
    WFI; read WAKE_EVENTS                    ; consumes counters done
```

GO ([8]) stays in every mask (spec item 79): GO only arrives as the release after `done`, so it never wakes a wait spuriously, and keeping it in the mask guarantees the helper's load consumes it.

The wake term is `(WAKE_EVENTS & WAKE_MASK) ≠ 0`, with GO and counters done among the events (below). `DMA_REPLAYER_REMAINING` counts the tile's own mode-8 arrivals on landing. Events posted during a run stay pending and, the wake being a level, make the next `WFI` fall through once the mask is restored. GO can't arrive mid-pass, and no other counter is set.

**All wakes are events** (owner, 2026-10-23). GO and the local wake fold into `WAKE_EVENTS`, so `WFI` wakes on `(WAKE_EVENTS & WAKE_MASK) ≠ 0` and nothing else:

| Bits | Event |
| --- | --- |
| [7:0] | request tags (8) |
| [8] | GO, C&C's barrier release |
| [9] | counters done: set on the 1 → 0 transition that leaves every down-counter at 0, once `mem_idle` and `cm_idle` hold (today's local wake with its qualifier, as a sticky event) |
| [10] | command landed (mailbox slot 0) |
| [11] | engine run done (`rd_done`) |
| [12] | request FIFO no longer full |
| [17:13] | the replayer: code, value and expansion low-water, new expansion, done |
| [31:18] | reserved for future units |

`WAKE_MASK` resets to GO and counters done, and the `mesh.h` wait helpers follow every `WFI` with a load of `WAKE_EVENTS`, so a program using the helpers behaves as in v1.0. The `done` gate is a gate, not a mask setting: from the `done` pulse until GO, only GO can wake `WFI` and counters done can't be set, while `WAKE_MASK` is left untouched. Because counters done stays pending until a load consumes it, no `WFI` can swallow another's wake.

**`WAKE_REQUIRED`** (owner, 2026-10-23; R/W at 0x870, reset 0) adds an AND to the mask's OR: wake when every required event is pending. The wake term is

`wake = ((WAKE_EVENTS & WAKE_MASK & ~WAKE_REQUIRED) ≠ 0) OR (WAKE_REQUIRED ≠ 0 AND (WAKE_EVENTS & WAKE_REQUIRED) == WAKE_REQUIRED)`

A mask bit that isn't required wakes on its own (the escape hatch, for example GO or an error while waiting on a set of reads); a required bit never wakes alone. With `WAKE_REQUIRED` = 0 the term is just `(WAKE_EVENTS & WAKE_MASK) ≠ 0`. Clearing is unchanged: a load clears the events under `WAKE_MASK`, so required events meant to be consumed are put in the mask too. One `WFI` then waits for several request tags, or a copy and a read together.

**`WAKE_PEEK`** (owner, 2026-10-23; R at 0x874): returns exactly what a load of `WAKE_EVENTS` would, but never clears anything. It's an alias of the same state with no storage of its own: read `WAKE_PEEK` to look, `WAKE_EVENTS` to consume.

**Consuming and the `done` gate** (owner, 2026-10-23). Hardware never consumes an event on wake: the `mesh.h` wait helpers follow every `WFI` with a load of `WAKE_EVENTS`, so GO and counters done never linger. The `done` gate stays a gate: from the `done` pulse until GO, only GO ([8]) can wake `WFI`, counters done can't be set, and `WAKE_MASK` is never written by hardware except at reset and HALT → RUN, so the program's own mask is back in force after GO.

**Big-SRAM read order is per client** (owner, 2026-10-23). Big SRAM returns each client's reads (the address generator, GATHER, the read port) in that client's issue order; different clients' reads may complete in any order relative to each other, so a gather's scattered reads never block a stream's sequential ones. Software rule (an invariant, flagged by meshsim): a tile never has two clients sending order-placed packets to the same destination at once, modes 2–4 into the same `PTR` entry or mode 8 into the same replayer queue.

**Queues are cleared by the start** (owner, 2026-10-23). The replay start empties all three queues; there is no preloading and no end-of-pass reset. Before pulsing start, the controller waits for every queue push it issued to land (counters done for local refills, the tag events for memory reads), so over-pushed or late data from the previous pass can never leak into the next. After the start, the empty queues raise their low-water events at once and the controller fills them, a few hundred cycles per pass.

**Short runs, not interruption.** The address generator is not interruptible. Instead every run is short: a 1 KB chunk is 64 packets, about 64 cycles, so nothing waits long for the engine, and the controller re-checks the queues after each chunk. Streaming occupies about 11–20% of a pass. Group runs (the sort's all-to-alls) stay exclusive and don't coincide with replay.

**Controller-fed expansions** are only inlines (custom-0) and ECALL, EBREAK, MRET, CSRRW and CSRRS: about 13 per 64K-row window on Cyclone, about 0.4% of a pass in stalls. Inline templates for the current window are resident in big SRAM (next sections).

## Replayer address space and memory

Everything the replayer reads is written through the replayer write mode (mode 8). Its addr is in 32-bit words and steps by packet size; the map below is in 128-bit words (packet addr = 4 ×). Each region is aligned so its low bits index it directly.

| 128-bit words | Region | Size | Ports |
| --- | --- | --- | --- |
| 0–63 | Initial register image: word i holds registers 2i (low half) and 2i + 1 (high half) | 1 KB | Single |
| 64 | State word: PC [63:0], `instr_idx` [95:64], next offset [111:96] | 16 B | Single |
| 256–511 | Decode table, 2K × 16 bits | 4 KB | Single |
| 512–1023 | ROM, 512 × 128 bits | 8 KB | Dual |
| 2^16 window | Code queue port | 4 KB queue | Dual |
| 2 × 2^16 window | Value queue port | 8 KB queue | Dual |
| 3 × 2^16 window | Expansion queue port | 4 KB queue | Dual |

The working register file (1 KB, true dual-port) isn't addressable; each start fills it from the image. The 128-bit memories take only 128-bit packets; the decode table takes 16-bit entries, eight per 128-bit packet. The exact port windows are for the spec session to fix.

**Ownership.** Implied by `replay_running`: while stopped, the traffic director may write every replayer memory; while running, only the dual-ported ROM and queues accept writes, and any other mode-8 write sets "bad replayer write". A halt hands everything back.

**Replayer memory per tile:** 4 + 4 + 8 KB of queues, 8 KB ROM, 4 KB table, 1 KB working registers, about 1 KB image and state, about 2.5 KB output queue (32 × 80 B): **about 32.5 KB**, plus the tile's 2 KB request FIFO. Across 64 tiles that is about 2.2 MB, roughly 7–10 mm² per chip with the logic [estimate], against 48 MB for the original 768 KB-per-tile design.

## The resident replay region

The trace stays in big SRAM for the whole proof, in a **512 KB replay region** per tile, a software-managed cache (decided 2026-10-23):

1. **This window's templates first:** only the controller-fed templates its 64K rows actually use (often none; a window of BLAKE3 calls needs about 20 KB). The tracer lists the set with the checkpoint.
2. **Then code and values** as 4 KB blocks in consumption order, opcode and value blocks interleaved, so "resident" is a single boundary.
3. **Overflow** beyond 512 KB, at most about 190 KB in the densest windows, stays in LPDDR and is fetched in 4 KB requests over the request bus as the replayer reaches it.

The median window's trace is about 335 KB and the densest about 700 KB. At the stages whose scratchpad margin is too small, the controller evicts the region and reloads it afterwards (up to 512 KB per tile, about 33 MB per chip, about 2 ms). The same mechanism can spill compact, expensive-to-rebuild intermediates; anything the trace can regenerate is better replayed (about 0.2 ms per pass) than spilled.

**Code is not in big SRAM.** Each tile controller has 32 KB instruction and 32 KB data memories; stage overlays are broadcast by C&C at stage boundaries into a parked controller, so the repo plan's 256 KiB code reservation is reclaimed for data.

**Big-SRAM size.** Any multiple of 256 KB up to 8 MB, reported in `TILE_STATUS`; banks are at most 512 KB each, so every 512 KB-aligned block has its own port. Adding a ninth bank of 256 or 512 KB late in the project is a capacity change, not an architecture change.

## Controller interface

| Where | Field | Meaning |
| --- | --- | --- |
| `TILE_PULSE` [2] | replay start | Sets `replay_running`, giving the replayer its single-ported memories until it finishes or halts; empties all three queues; copies the register image into the working file and reads the state word. A start while running is ignored and flagged; there is no resume |
| `TILE_STATUS` [3], [4] | `replay_running`, `replay_wait` | Running; stopped for a new expansion |
| `WAKE_EVENTS` [17:13], `WAKE_MASK`, `WAKE_REQUIRED` | 5 events | The replayer's wake events in the shared register pair (previous section) |
| `REPLAYER_OPCODE` (R) | [31:0] | The instruction word needing a controller-fed expansion |
| `REPLAYER_STATUS` (R) | rows run [15:0], next offset [31:16] | Rows left = 65,536 − rows run |
| `DMA_REPLAYER_REMAINING` (R/W) | count | Down-counter for local mode-8 arrivals |

Offsets and bit positions are in the allocation table below. Done is `replay_running` = 0.

**Halt.** C&C's HALT, or the controller halting itself, stops the replayer at once: `replay_running` drops, queues and stop state reset, and it stays idle until the next start.

**Conditions** (raise and run, as in v1.0): bad replayer write; queue overrun (a push beyond a queue's capacity); invalid expansion (a reserved opcode, operand, rd0 or format code, or a template start out of range).

## Mesh v1.1: where it stands

The mesh changes are specified and simulated; **the spec (mesh-sorter v1.1, `spec/machine-spec.md`) is authoritative** and this note no longer repeats its tables. In brief: a 169-bit link word with a 21-bit addr and a 2-bit size (00 = 32, 01 = 64, 10 = 128 bits, 11 undefined); one packet per element; `STREAM` as three arrays (`STREAM_SRC`, `STREAM_DEST`, `STREAM_COUNT`); 128-bit writes and four-bank controller memories; the regrouped register map, `TILE_STATUS` and `TILE_CONDITIONS`.

**Owner changes after the simulator session, in the spec but not yet in meshsim:** the send port reordered (`ROUTER_SET_W0`–`W2` hold the low words, and `ROUTER_SEND_32`, `_64`, `_128` supply the packet's top word, so words are stored in ascending order); 21-bit chunk addresses everywhere; the big-SRAM size in 256 KB units in `TILE_STATUS` [31:27], with banks of at most 512 KB as the only geometry guarantee. `CHANGES-v1.1.md` lists what meshsim needs.

**Plan:** two spec sessions (the request bus; the full replayer, §4.17), then one simulator session for everything pending, then the sorter port, once. The spec's §4.17 is a stub; its "Design direction" paragraph is superseded by this note.

## Request bus

Tiles read and write LPDDR, and send debug messages, over a dedicated **request bus** (decided 2026-10-23).

**Network.** An 8-bit, 8-ary tree with the tiles as leaves: one node per row of 8 tiles, then a root node, then a 64-entry queue at the memory system and C&C. Each node has one 8 B buffer per input and round-robins over occupied inputs; with all leaves at the same depth that is exactly fair per tile and starvation-free. Per-hop ready/valid backpressure means nothing is dropped. A request reaches the root in about 15–20 cycles; the tree is in order per tile.

**Framing.** The tree prepends a header byte: sender [5:0], filled in by the tree, and size [7:6] in the link word's encoding (00 = 32, 01 = 64, 10 = 128 bits). Only 64-bit requests are defined now; the others are room to grow.

**The 64-bit request**, in 128 B units:

| Bits | Field |
| --- | --- |
| [1:0] | target: memory system, C&C, two spare |
| [3:2] | op: read, write, debug message, spare |
| [5:4] | destination kind: big SRAM (explicit address), controller memory, replayer queue, spare |
| [8:6] | tag, for completions: 8 outstanding requests per tile |
| [9] | spare |
| [35:10] | LPDDR address, 128 B units: 8 GB |
| [47:36] | count − 1, 128 B units: up to 512 KB |
| [63:48] | tile address, 128 B units: the whole 21-bit space |

The minimum transfer is 128 B (8 packets, 8 cycles on the mesh) and buffers are 128 B aligned. Requests to C&C may use the bits after target and op as they like.

**Tile interface,** mirroring the send port: `REQUEST_SET_W0`–`W2`, then `REQUEST_SEND_32`, `_64` or `_128` supplying the top word, words stored in ascending order. Each tile has a **2 KB request FIFO** (256 requests); a send store stalls while it's full, and `request_busy` shows it. There's no ack for issuing.

**Reads.** The memory system streams the block to the tile as ordinary mesh packets (by destination kind: an explicit-address big-SRAM write, mode 5, or mode 8 to a queue port), then a **mode-10 wake event carrying the tag**, which sets that tag's bit in `WAKE_EVENTS`. The completion follows its data on the same route, so it proves the data has landed. The memory system may answer reads in any order, including cache hits, **except** that a queue's data must arrive in order: software keeps **at most one outstanding read per queue**.

**Writes** use a token: the tile requests, the memory system replies with a token, a mode-10 event on the request's tag, when it's ready, and only then does the tile stream its block, so at most one tile streams to the memory system at a time and there is never fan-in. An acknowledgement that the data is in LPDDR is optional, needed only before another tile reads the region.

**Debug messages.** Tokenized printf: a format id plus arguments, with the format strings kept on the host, sent to C&C or to a debug log in memory. Heavy printing just stalls the printing tile.

## Shared-resource allocation

Both spec sessions must follow this table, so nothing is renumbered later.

| Resource | Allocation |
| --- | --- |
| Mode 8 | Replayer write: memories and queue ports. Counted in `DMA_REPLAYER_REMAINING` when it comes from the tile itself |
| Mode 9 | Big-SRAM write at an explicit 21-bit address (memory-system read data; at C&C, a write stream's data); uncounted |
| Mode 10 | Wake event from the memory system: sets `WAKE_EVENTS` bit payload [2:0] (the tag), kind [5:4] = read completion, write token or write ack; writes nothing; uncounted |
| Modes 11–15 | Reserved |
| Endpoint ids | 0–63 tiles; 64 C&C; 65 the memory system (reserved; today served at C&C's port); 66–125 future engines; 126 broadcast; 127 none. `STREAM_DEST`'s *d* is 7 bits: 0–63 a relative tile, 64–125 an absolute endpoint |
| Counting | Packets from any non-tile source (64–125) are never counted, as in v1.0 for C&C; completions and tokens are mode-10 events, not counted packets |
| Registers | 0x844 `WAKE_EVENTS`, 0x848 `WAKE_MASK`; 0x84C `REPLAYER_OPCODE`, 0x850 `REPLAYER_STATUS`, 0x854 `DMA_REPLAYER_REMAINING`; 0x858–0x86C `REQUEST_SET_W0`–`W2`, `REQUEST_SEND_32`, `_64`, `_128`; 0x870 `WAKE_REQUIRED`, 0x874 `WAKE_PEEK` |
| `WAKE_EVENTS` | [7:0] request tags; [8] GO; [9] counters done; [10] command landed; [11] engine run done; [12] request FIFO no longer full; [17:13] the replayer; [31:18] reserved for future units. `WAKE_MASK` resets to [8] and [9] |
| `TILE_PULSE` | [2] replay start; [3] unallocated |
| `TILE_STATUS` | [3] `replay_running`, [4] `replay_wait`, [11] `request_busy` (request FIFO full); [20:12] spare; [31:27] big-SRAM size |
| `TILE_CONDITIONS` | [11] bad replayer write, [12] queue overrun, [13] invalid expansion; [14+] reserved |

Confirmed by the owner, 2026-10-23, and implemented in the merged v1.1 spec (8.10, 8.11).

## Design choice: ALU and memory log

The replayer recomputes rows with a small ALU, keeps the compressed trace (about 5 B per row) resident in big SRAM, and feeds itself through small queues, rather than storing 768 KB per tile (owner decisions, 2026-10-22 and 2026-10-23).

| | Value per row (fallback) | ALU and resident trace (candidate) |
| --- | --- | --- |
| Dedicated replayer memory per tile | 768 KB | about 32.5 KB |
| Trace storage per tile | 12 B per row, dedicated | about 5 B per row, in a 512 KB region of big SRAM |
| Logic | No ALU | A small ALU: 3 cycles per simple row, 5 per multiply |
| Replay pass, all local | about 0.13 ms | about 0.22 ms |
| Per chip, dedicated memory | 48 MB | about 2.2 MB, roughly 7–10 mm² with logic [estimate] |

**Pass count.** The repo's kernel scratchpad plan (`docs/kernel-scratchpad-plan.md` in `jolt-asic-planning`) counts **46 replay passes per Akita proof**, about 10 ms at 0.22 ms each. Its stage margins under replay are only about 219–304 KB at 3.75 MiB, plus the reclaimed 256 KiB code reservation, which is why the resident region needs evict-and-reload at the tightest stages. Windowed streaming (several rounds per pass) can cut the memory-driven passes; stage 4's seven register-address passes exist because of address-first binding, whose order the target verifier may fix.

**Prefetch in sumcheck tails.** Each sumcheck round halves the data, so after the first few rounds both scratchpad and the memory path free up: after one round the bound tables take half the space, after two a quarter, and the mesh is idle during sumcheck. The controller uses that tail to prefetch the next replay pass's opcodes and log from LPDDR into big SRAM, so the pass runs from cache at the replayer's full rate. Filling all 64 tiles takes about 21 MB per chip on average, about 1.3 ms at about 16–17 GB/s, so the prefetch must start early enough in the rounds to finish. The prefetched streams must land in areas the remaining rounds don't use (a placement rule), and C&C paces the fill so it never fans in, since the mesh has no flow control.

**Why not full re-execution.** Re-executing in every tile would also need the guest memory image, through caches into LPDDR. The memory log is needed in any design, and the value stream only extends it to compute results.

**Code store, not adopted.** The measured instruction footprint per 64K-row window has a median of 9 KB (EVM Zone) and 12 KB (Cyclone), and a maximum of 50 KB in both; a 16 KB cache fits 96.6% and 53.2% of windows [measured]. An i-cache can't be streamed the way a ring can, and it needs fetch by PC, tags and a C decompressor per tile. The resident opcode stream and its 4 KB queue do the job with no misses.

## Open items

- [x] Spec session A: the request bus (spec 3.3, 4.18, 6.7, 8.10; items 60–75), merged 2026-10-23.
- [x] Spec session B: the full replayer (spec 4.17.1–4.17.14, 8.11; items 33–59), merged 2026-10-23.
- [x] Spec item 55: an invalid expansion stops the pass, accepted as written (it should never happen; the tracer catches it).
- [x] Wake pass (worker 3): all wakes as events, `WAKE_REQUIRED`, `WAKE_PEEK`, 8 tags, per-client read order, the start clearing the queues; in the spec as of archive 2026-10-23c.
- [ ] One simulator session after both: the send-port reorder, 21-bit addresses, the size field, the request bus and the replayer; then the sorter port, once.
- [ ] Enumerate `expand_virt_seq` against the generated bytecode of both ELFs: template contents, the single-immediate property, `is_virtual` on rewrites and remapped rows, inline scratch zeroing, and padding rows.
- [ ] Specify the ALU: exact semantics of all 168 row opcodes, which take 3 and which 5 cycles, and tests against the tracer.
- [ ] Measure MUL and MULH density per 64K-row window.
- [ ] Measure per-window replayer memory: backing store, controller-fed expansions, distinct templates (prompt written).
- [ ] A cache simulation for the tracer engine's line size.
- [ ] Redo the per-stage scratchpad plan with the 512 KB replay region, reclaimed code reservation and windowed streaming.
- [ ] Settle whether the target verifier fixes the read-write binding order (stage 4's address passes).
- [ ] Check the incremental per-group sums for register read-write address rounds against the repo's Twist formulation.
- [ ] The ROM fit (about 508 of 512 words): word sharing or a slightly larger ROM.
- [ ] The materializer and sumcheck engines: interfaces, and how the address generator feeds compute queues with backpressure (channels or short runs).
- [ ] The memory system's side of the request bus: ordering, the cache, and the write-token scheduler.
