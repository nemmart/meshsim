# Replay inputs: what the replayer harness needs from the tracer

These are the requirements from the replayer session (meshsim's `Replayer`, Stage A harness, and Stage B controller) on the tracer session's output format. Section references are to `spec/machine-spec.md` at mesh-sorter-v1.1-2026-10-26n.

The aim: feed the replayer **only its inputs** for each window, and compare every output row, **byte for byte**, with the tracer's reference rows. Raw streams come first, then compressed.

Anything marked **(optional)** speeds up debugging or coverage but isn't needed for a first comparison.

## 1. Conventions

- **Byte order:** little-endian everywhere.
  - A 64-bit value is 8 bytes, low byte first.
  - A **128-bit word** is 16 bytes: bits [63:0] as a little-endian u64, then bits [127:64]. Byte *k* of a word is bits [8*k* + 7 : 8*k*].
- **Units:** sizes are in bytes and lengths in bits, unless a field says otherwise. Every binary file is a whole number of 16-byte words.
- **Identity:** every file can be identified independently (section 6). The harness checks each file's SHA-256 before it uses it.

## 2. Per program (once per traced program)

| item | file | size | content |
|---|---|---|---|
| decode table | `decode_table.bin` | 4,096 B | 2,048 entries × u16 LE, entry *e* at byte 2*e*. It is exactly what mode 9 writes at 0x00400–0x007FF (4.19.10): the packet at 0x400 + 4·(*e* >> 3) carries entries 8·(*e* >> 3) … + 7, entry *e* in bits [16·(*e* & 7) + 15 : 16·(*e* & 7)]. **Every entry written** (4.19.5's builder rules: unused entries are controller class; entries whose index bits include immediate bits are replicated). |
| ROM | `rom.bin` | 8,192 B | 512 × 128-bit expansion words (4.19.3), word *j* at byte 16*j*: exactly what mode 9 writes at 0x00800 + 4*j*. Words 0 and 1 are the padding row and the shared NOP row (4.19.6). |
| template words | `templates.bin` | 16 B × total words | the controller-fed template words (4.19.3 format), concatenated. Numbers may share words (item 114). |
| template index | `template_index.bin` | 16 B × number count (≤ 128) | one 16-byte entry per template number, in number order (format below); **the count is the file size / 16** (no count or valid field) |

**Template index entry** (as the tracer session delivers it):

| bytes | field |
|---|---|
| 0–3 | **instruction word**, u32 LE. It is the key, and must equal what the replayer presents in `PERIPH_STATUS_1`: the stored opcode-stream word, with bit 0 clear for a compressed instruction (4.19.2, 4.19.9). One entry per distinct instruction word. |
| 4–7 | **word offset** into `templates.bin`, in 128-bit words, u32 LE |
| 8–11 | **length** in words, u32 LE. It must equal the first word's `seq_remaining` + 1. |
| 12 | **`rd0`**: the template's rd = x0 policy, as in its first word's bits [47:46] (0–3). The Stage B controller needs it for ruling N3. |
| 13 | **class**: the instruction's decode class, 1 (controller, inline) or 2 (controller, system) (4.19.5) |
| 14–15 | spare, 0 |

**Requirements on every template word, ROM and controller-fed:**
- **Format and flags.** As in 4.19.3: `seq_remaining` counting down to 0 on the last word. `imm0_pc0` is set in every JAL word, and `next_from_value` on every row that can transfer control. `from_log` is set exactly on the LD, SD and advice rows; `report_c` is set on last words only.
- **The rd = x0 policy.** `rd0` holds the template's policy in its **first word**. Putting it in every word is welcome but not needed: on a mid-sequence start the controller copies it into the first word it sends (4.19.4, ruling N3).
- **No tracer-only words.** `untraced` is never set in a word a replayer can receive (4.19.3, 4.19.11).
- **Advice rows** are written as `VirtualAdvice`, with `from_log` set (4.19.7).
- **A zero immediate beside a constant** uses operand code **145**, immediate 0 (26q, 4.19.3). Code 132 also replays as 0 for an R-format source, so tables built with it still replay the same.
- **The program's controller-fed set** covers every instruction word the decode table sends to the controller (class 1 or 2) **that occurs in any window**, ECALL, EBREAK and MRET included. A presented instruction with no index entry is a harness error.

**(Optional) program metadata** in the manifest:
- **Template-builder version.** The tracer and builder commit, so the template words and the reference rows are known to come from the same build.
- **ROM fill.** Which instructions the ROM holds and which the controller serves (4.19.6), so a test can move an instruction between the two paths.

## 3. Per window

A window is the 65,536 rows one replayer pass runs (4.19.1), or the debug length the manifest gives as `window_rows` (section 4).

### 3.1 The state record

`window_NNNNN.state.bin`, **1,072 bytes**, 67 × 128-bit words (4.19.2):

| word | bytes | content |
|---|---|---|
| 0 | 0–15 | **template bitmap**: bit *i* set if the window uses controller-fed template *i* (item 115). The replayer never sees it. |
| 1 | 16–31 | **state word**: pc [63:0]; `instr_idx` [95:64]; next offset [111:96]; flags [112] code compressed, [113] read log compressed, [114] write log compressed, [115] includes store log (the tracer emits 1); [127:116] zero |
| 2 | 32–47 | **stream word**: exact lengths in bits: code [23:0], read log [55:32], write log [87:64]; the rest zero |
| 3–66 | 48–1,071 | **register image**: image word *i* holds register 2*i* in bits [63:0] and 2*i* + 1 in [127:64]. All 128 registers; register 0's value is ignored. |

The controller sends bytes 16–1,071 to region 0 unchanged (4.19.2, 4.19.10). The flags and lengths must describe **the stream files delivered with this record**.

### 3.2 The three streams

`window_NNNNN.code.bin`, `.rlog.bin` and `.wlog.bin`: each stream is padded with zero bits to a whole 16-byte entry. The exact length in bits is in the stream word and in the manifest.

**Bit order** (4.19.15): least significant bit first within each 128-bit entry, entries in order.

| stream | raw | compressed |
|---|---|---|
| **code** | one 32-bit word per native instruction, four per entry, low first | a 16-entry true LRU (4.19.15) |
| **read log** | 64-bit words, two per entry | a 16-entry true LRU |
| **write log** | 64-bit words, two per entry | an LRU over E[0]–E[14] plus the **last-load entry** E[15] (4.19.15) |

**Code stream contents** (4.19.2):
- Compressed instructions are stored decompressed, with bit 0 clear.
- Padding rows are one word 0x00000000 each, with no log words.
- If the window starts inside an expansion, its first entry is that instruction.

**Log contents, in row order, at most one word per row** (4.19.2):
- **Read log:** LD values, `VirtualAdvice` values (the DIV/REM advice included, which jolt-cpp computes but doesn't log), and the next `instr_idx` of every row that transfers control.
- **The program-end row's word:** a row whose new pc is 0, with `next_from_value` set, takes one read-log word, **0** (4.19.7; the replayer flags any other value). This applies to JAL with imm 0, and to a JALR or branch to 0 if one ever occurs.
- **Write log:** SD and AMO pre-values. An SD row takes one write-log word whenever includes-store-log is 1.

**The compressed encoder must follow 4.19.15 exactly**, in particular:
- Tables reset at each window start.
- A hit is a flag bit 1 and a 4-bit index. A miss is a flag bit 0, a size bit (0 = 32, 1 = 64) and the value; a 32-bit miss is zero-extended.
- **Move to front** on every hit and miss.
- **Write log E[15]:** it is updated by **every** LD row's read-log value, an LD to x0 included (ruling N5). It is coded as **index 15 first** when a value matches it, and the LRU runs over 0–14 only.
- The write-log encoder therefore runs in row order with the read log, not as a separate pass.

**Delivery, in this order of priority:**
1. **Raw**, every stream, with the state record's flags [114:112] = 0.
2. **Compressed** as the tracer would choose, per stream: either form, flags set to match.
3. **Both forms of each stream** for a subset of windows: a separate state record per variant, or one record plus the second set of lengths in the manifest. Then I can run the same window raw, all compressed, and with one stream falling back to raw while the others are compressed.

### 3.3 The reference rows

`window_NNNNN.rows.bin`: **65,536 × 80 bytes** (5 MiB), or the window's debug length × 80 bytes. Row *r* is at byte 80*r*, in the 4.19.12 layout: words 0–4 in order, each 128-bit word little-endian.
- **What the rows show:** what the replayer must produce from the inputs above. Every row of the window, **padding rows included (all zeros)**.
- **`instr_idx`** (word 0 [31:0]): the row's bytecode index, 0 in padding.
- **Word 0 [85:83]:** `rd_is_imm`, `rs1_is_imm` and `rs2_is_imm`, from the bytecode row's slot kinds. They aren't in jolt-cpp's `RiscCycle`, so the converter must take them from the `VirtualInstruction` the row executed.
- **Spare bits** (word 0 [127:86]) are 0.
- **No memory post-value** (4.19.12). `mem_value` is LD's rd after the row (0 for an LD to x0), or SD's pre-value. Rows come from the store-log-on run.
- **If the rows come as `RiscCycle`,** I'll convert them myself, but then I also need the per-row slot kinds and `instr_idx`. The 4.19.12 layout directly is much preferred.

### 3.4 (Optional) diagnostics per window

These make a mismatch quick to locate:
- **Per instruction:** its code-stream index, its row count in this window, and its decode class (ROM or controller, template number). Alternatively, per row, the code-stream index it belongs to.
- **Per row:** which log word it consumed (stream, word index), or a marker that it consumed none.
- **The window's end state:** pc, `instr_idx`, next offset and the full register file after row 65,535. Consecutive windows' state records give the same thing; see the window set below.

## 4. Which windows

From one or two real programs (Cyclone or EVM Zone, as available), each window identified as in section 6:

| purpose | windows |
|---|---|
| ordinary | at least 2 typical windows, **consecutive** (window *k* + 1's state record checks window *k*'s end state) |
| a window starting mid-expansion | at least 1, next offset > 0, ideally inside an inline (controller-fed) and one inside a ROM template |
| controller-fed expansions | a window with inlines whose expansion exceeds 128 words (the expansion low-water path), e.g. BLAKE3 or Keccak |
| atomics and rd = x0 | a window with LR/SC/AMOs; any with rd = x0 instructions (remap and NOP policies) |
| program end | the window containing the final `j .` (the end row's log word 0, then padding) |
| all padding | one window wholly after the end: pc 0, `instr_idx` 0, 65,536 padding words, no log words |
| dense logs | the densest window by log words per row (the stall measurements in Stage B) |
| stream forms | for some of the above, both raw and compressed streams (3.2, item 3) |

**(Optional) debug windows for bring-up.** The tracer session offers a shorter window length, such as 2⁸ rows instead of 2¹⁶, **recorded in the manifest as `window_rows`**. Each stream, the reference rows and the counts then cover that many rows, and the replayer model runs a pass of that length (`Replayer.rowsPerPass`; the default is 65,536). The hardware's pass is always 65,536 rows, so debug windows are for the Stage A harness only.

## 5. Manifest

`manifest.json`, one per delivery:
- **Format version.**
- **Tracer and template-builder commit**, jolt-cpp commit, and the guest program (name, ELF SHA-256, inputs).
- **Program files** with SHA-256 and sizes; the template count.
- **Per window:**
  - its index;
  - its first global row (index × 65,536);
  - the rows before the program's end within it (65,536 if it doesn't end there);
  - each file's name, size and SHA-256;
  - the stream flags and exact bit lengths, again;
  - counts: instructions, controller-fed expansions, read-log and write-log words, padding rows;
  - the template numbers used, the same as the bitmap;
  - a **SHA-256 of the reference rows**, so a whole-window match is one compare before the row-by-row diff.

## 6. Identifiers

Files are named `<program>/window_<NNNNN>.<part>.bin`, where NNNNN is the window index in decimal, zero-padded. Program-level files sit directly under `<program>/`.

A delivery is a tarball of one or more program directories and the manifest.

## 7. What I'll do with it (for reference)

**Stage A.** For each window:
- load the decode table, the ROM and record bytes 16–1,071 by mode 9;
- run one pass, refilling the queues from the stream files and serving each presented instruction from the template index (`ReplayDriver`);
- compare all 65,536 records with `rows.bin`, reporting the first mismatching row and field, with the diagnostics of 3.4 when present;
- run raw first, then compressed, then mixed.

**Stage B.** The same window files are laid out in big SRAM and run by the tile's own controller program in meshsim. They are compared the same way, and the cycles per row, stalls and controller load are measured.
