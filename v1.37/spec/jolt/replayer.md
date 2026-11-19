# The replayer: machine specification, v1.36

*Section 17.8 of the machine specification (`spec/machine-spec.md`), which keeps a stub under that number; the
section numbers here are the machine specification's. 17.8 and 17.8.1–17.8.14 are the hardware contract, the RTL's
(Snarky One's) 17.8: what the replayer does. 17.8.15–17.8.24 keep what v1.35's 4.19 states beyond that contract,
rewritten in v1.36's terms: the ground truth, the measurements, Jolt's rules behind the tables, the software's
loop and resident region, the cost, the simulator's checks and the open items. Where the two parts differ, the
contract rules. The opcode appendix is `spec/jolt/replayer-opcodes.md`; v1.35's text is
`spec/history/replayer-v1.35.md`, and `spec/CHANGES-v1.35-to-v1.36.md` says what changed.*

### 17.8 The replayer, the compute tile's peripheral

The replayer is every compute tile's peripheral 1, type 4, version 0. It
regenerates the tile's share of a proof's execution trace, Jolt's expanded trace,
one **row** at a time, from the tile's **record** (17.8.2) and three **streams**
(17.8.3), and sends each row out of its row port (17.8.11). It computes every
value it can with its ALU, and takes from the streams only the instructions and
the values it cannot compute. Its work, a **pass**, runs the rows `RUN_CONFIG`
names (17.8.10).
This section replaces v1.35's 4.19 (`spec/history/replayer-v1.35.md`).
- **`PARAMS`:** type 4, version 0, and no constants, [31:16] 0.
- **No DMA.** Its block 1 reads 0 (17.1), and `STATUS.dma_busy` and
  `EVENTS.dma_done` stay 0. It sends no packet: its rows leave by the row port.
- **The reference.** Its rows equal, bit for bit, those of jolt-cpp `821e1bd`'s
  interpreter (`execute_instruction` and `expand_compressed_chunk`). Where
  jolt-cpp and a16z's Jolt at `7189216` differ, a16z's decides; gold
  is the Java emulator of `pipeline/gold/`, and jolt-cpp `88dca85`, the pin, has
  `821e1bd`'s rows on both workloads (items 113 and 154; 17.8.15). The tables
  stay `821e1bd`'s until they are rebuilt to a16z's rd = x0 rule (item 158):
  table data (17.8.5).
- **Normative beside this section:** `spec/jolt/replayer-opcodes.md`
  sections 1–5, what each row opcode computes (17.8.7), and
  `data/tables/opcode_table.txt`, the row opcodes' codes and classes.

| Part | Size | Holds | Section |
| --- | --- | --- | --- |
| code queue | 256 × 128 bits, 4 KiB | the code stream, as sent | 17.8.8 |
| read-log queue | 256 × 128 bits, 4 KiB | the read log, as sent | 17.8.8 |
| write-log queue | 256 × 128 bits, 4 KiB | the write log, as sent | 17.8.8 |
| expansion queue | 256 × 128 bits, 4 KiB | the expansion words the core feeds | 17.8.8 |
| decoders | one per stream: a table of 16 × 64 bits, 16 × 32 for code, and two size counters | each stream's values | 17.8.12 |
| state word, stream word | 128 bits each | where the program is; the streams' sizes | 17.8.2 |
| register file | 128 × 64 bits, 1 KiB, true dual-port | the 128 registers, the record's register image | 17.8.7 |
| decode table | 2,048 × 16 bits, 4 KiB | each instruction's template | 17.8.6 |
| ROM | 512 × 128 bits, 8 KiB, two-port | the templates' expansion words | 17.8.6 |
| ALU | a 64 × 64 multiplier, a comparator and adders | each row's values | 17.8.7 |
| output queue | 32 × 640 bits, 2.5 KiB | the rows for the row port | 17.8.11 |

#### 17.8.1 Its words and bits

Its words, at offsets from its window's base, 0xFFFF_FA00, are the Tracer's too
(17.9.1).

| Word | Offset | Register | Access | Fields |
| --- | --- | --- | --- | --- |
| 32 | 0x080 | `RUN_CONFIG` | R/W | [23:0] `rows`, the rows the next pass runs; [24] `store_log`, the pass uses the write log (17.8.3); [25] `rd_prev`, the rows carry `rd_prev` (17.8.11); [28:26] `tracer_compression`, the Tracer's (17.9.1), held and read back, with no effect here |
| 33 | 0x084 | `ROWS_RUN` | R | [23:0] the rows of this pass completed |
| 34 | 0x088 | `EXPANSION_OFFSET` | R | [15:0] the row offset at which the last posted core-fed expansion starts, 0–3,120, held until the next is posted (17.8.2, 17.8.13) |
| 35 | 0x08C | `EXPANSION_OPCODE` | R | [31:0] the code value of the instruction awaiting a core-fed expansion (17.8.6) |

Every other word of blocks 2–7 is unmapped (5.3).

- **`RUN_CONFIG`** says what the next pass runs (17.8.10).
  - `rows`: typically 65,536, a window. The pass runs exactly that many, so its
    streams are sized to it: the leftover check applies whatever the count.
  - A store while `STATUS.busy` is 1 is UB (1.3).
  - It holds the last value stored across passes: `START`, the state write and
    the pass's end leave it as it is. The reset zeroes it.
- **`ROWS_RUN`.** `START` clears it, and each row pushed into the output queue
  adds 1, so after a pass it reads `rows`, or fewer if an error stopped it.
- **`EXPANSION_OPCODE`** is set when `new_expansion` is posted, and holds until
  the next is.
- **`STATUS`** has the standard bits alone (17.1).

**`EVENTS`:**

| Bit | Event | Kind | Set by |
| --- | --- | --- | --- |
| 0 | `done` | counted, 1 | the pass's end (17.8.10) |
| 1 | `dma_done` | counted, 1 | nothing: it has no DMA |
| 2 | `code_credit` | counted, 2 | the code queue's credit (17.8.8) |
| 3 | `read_log_credit` | counted, 2 | the read-log queue's |
| 4 | `write_log_credit` | counted, 2 | the write-log queue's |
| 5 | `expansion_credit` | counted, 2 | the expansion queue's |
| 6 | `new_expansion` | counted, 1 | the pass reaching an instruction of decode class 1 or 2, whose expansion it waits for (17.8.6) |

**A fill** sets a credit to its depth, 2, besides the one-model causes (7.2): the
state write fills all four, and a new expansion `expansion_credit` (17.8.8). A
fill sets 2 whatever a consuming load takes in that cycle.

**`ERRORS`**, sticky until the reset, packed from bit 0 (5.4): v1.35's bits
less one, its reserved [0] dropped.

| Bit | Error | Kind | Set by |
| --- | --- | --- | --- |
| 0 | `queue_overrun` | P | a push into a full queue (17.8.8) |
| 1 | `invalid_expansion` | P (H on a corrupted table or ROM) | 17.8.10 |
| 2 | `code_underflow` | P (H on a corrupted stream) | the code stream (17.8.12) |
| 3 | `read_log_underflow` | P (H on a corrupted stream) | the read log (17.8.12) |
| 4 | `write_log_underflow` | P (H on a corrupted stream) | the write log (17.8.12) |
| 5 | `leftover` | P (H on a corrupted stream) | a pass ending with input left over (17.8.10) |

#### 17.8.2 The record

A window's **record** is 68 words of 128 bits, 1,088 bytes, one format for the
host, the Tracer and the replayer. It lands in the replayer's region at
consecutive offsets from 0, word *w* at 16·*w* (17.8.9).

| Word | Offset | Contents | The replayer |
| --- | --- | --- | --- |
| 0 | 0x000 | the state word | the live `pc`, `instr_idx` and row offset; its write resets the pass (17.8.8) |
| 1 | 0x010 | the stream word | the streams' sizes and coding, for the decoders and the checks (17.8.12) |
| 2–65 | 0x020–0x41F | the register image | the register file itself (17.8.7) |
| 66 | 0x420 | the files word, the Tracer's (17.9.2) | accepted and ignored |
| 67 | 0x430 | the template bitmap: bit *i* set if the window uses core-fed template *i* | accepted and ignored: the core's (17.8.13) |

**The state word** says where the program is:

| Bits | Field |
| --- | --- |
| [63:0] | `pc` |
| [95:64] | `instr_idx`, the next row's bytecode index |
| [111:96] | `offset`, the **row offset**: the rows of the current instruction's expansion already run; at a pass's start, those the previous window ran |
| [127:112] | reserved: written 0, and ignored, not checked |

**The stream word** gives each stream's size and coding.

| Bits | Field |
| --- | --- |
| [15:0], [31:16], [47:32] | `code_words`, `read_log_words`, `write_log_words`: the 128-bit entries each stream occupies as sent |
| [64:48], [81:65], [98:82] | `code_values`, `read_log_values`, `write_log_values`: the 32- or 64-bit values each decodes to |
| [99], [100], [101] | `code_compressed`, `read_log_compressed`, `write_log_compressed`: 1 for an LRU-coded stream, 0 for a raw one (17.8.12) |
| [124:102] | the advice field, the Tracer's (17.9.2): ignored |
| [127:125] | spare, 0 |

- **Words** is the fewest entries that hold the stream's values: ⌈values / 4⌉
  for raw code, ⌈values / 2⌉ for a raw log.
- The write log's three fields are used only when `RUN_CONFIG.store_log` is 1.

**The register image.** Image word *i*, record word 2 + *i*, holds register 2*i*
in its bits [63:0] and register 2*i* + 1 in [127:64].

**The record is the working state.** Its words 0–65 are the live state, with no
copy: a pass starts from the state word and the register file as they are,
changes them in place, and leaves the window's end state there, the stream word
unchanged. So every pass starts from a reload of the record's words 0–65,
verbatim (17.8.13).

#### 17.8.3 The streams

| Stream | A value | One for each |
| --- | --- | --- |
| code | 32 bits, an instruction | instruction of the trace, held while its expansion runs |
| read log | 64 bits | load row, advice row and control transfer, in row order |
| write log | 64 bits | store row, in row order |

- **Instructions.** A code value is the instruction's word, a compressed
  instruction's decompressed with bit 0 cleared. Bit 1 is 1 in every
  instruction, so bits [1:0] = 00 mark a **padding word**.
- **Log words.** A row takes at most one, by its expansion word's `from_log`
  (17.8.4) and its control transfer (17.8.7):
  - a **load row**, LD or `VirtualLoadB/H/W`: the doubleword loaded, from the
    read log;
  - a **store row**, SD or `VirtualStoreB/H/W`: the memory doubleword's value
    before the store, from the write log;
  - an **advice row**, `VirtualAdvice`: the advice, from the read log;
  - a **control transfer**, a row with `next_from_value` whose new pc differs
    from its pc, its expansion's last (17.8.7): the next `instr_idx`, from the
    read log.

  `from_log` is set on exactly the first three, and a store row's opcode sends
  its word to the write log.
- **Without the store log** (`RUN_CONFIG.store_log` 0), a store row takes no word
  and its row's `mem_value` is 0 (17.8.11). The write log is never fed, and its
  sizes are ignored: neither `write_log_underflow` nor `leftover` looks at it.
  The registers are the same either way.
- **The end of the trace.** The program's last row jumps to pc 0, its log word
  0 (17.8.7). The trace is then padding words, one a row with no log words, each
  of which runs ROM word 0 (17.8.6), so `pc` and `instr_idx` stay 0.

#### 17.8.4 The expansion word and the operand codes

Each row is one 128-bit **expansion word**. An instruction's words, in order, are
its **expansion**, and their source its **template**, in the ROM or fed by the
core (17.8.6).

| Bits | Field | Contents |
| --- | --- | --- |
| [7:0] | `opcode` | the row opcode, 0–170, in jolt-cpp `821e1bd`'s numbering (`data/tables/opcode_table.txt`) |
| [15:8] | `rd` | an operand code (below) |
| [23:16] | `rs1` | an operand code |
| [31:24] | `rs2` | an operand code |
| [43:32] | `seq_remaining` | the rows left in the expansion after this one, at most 3,120; 0 on its last row |
| [44] | `next_from_value` | the row can transfer control, its next `instr_idx` then from the log; only on its expansion's last row (17.8.7) |
| [45] | `hold` | `pc` and `instr_idx` do not advance after the row (padding) |
| [47:46] | `rd0` | the template's rd = x0 policy (17.8.5): 0 none, 1 NOP with a virtual NOP row, 2 remap, 3 NOP with a NOP row that is not virtual. Read from the first word delivered for an expansion only |
| [50:48] | `format` | the instruction's immediate format, for code 132: 0 R, 1 I, 2 S, 3 B, 4 U, 5 J; 6 and 7 an invalid expansion (17.8.10) |
| [51] | `virt` | the row's opcode differs from the instruction's |
| [52] | `imm0_pc0` | a JAL whose immediate is 0 jumps to pc 0 (17.8.7) |
| [53] | `report_c` | the row reports `is_compressed` for a compressed instruction (17.8.11) |
| [54] | `from_log` | the row takes a log word (17.8.3). Set with `next_from_value`, UB (1.3) |
| [61:55] | reserved | 0: a word with one set is UB (1.3) |
| [62] | `sign_imm` | the row's immediate is signed: reported with the row (17.8.11); the replayer computes nothing from it |
| [63] | `offset_imm` | a `VirtualShiftData…` row's `shift_offset` is the instruction's immediate, code 132's value, and 0 otherwise. Set on any other row, UB (1.3) |
| [127:64] | `const` | a constant immediate, code 128 |

**Operand codes.** Each of `rd`, `rs1` and `rs2` names its slot's value:

| Code | The value | Slot |
| --- | --- | --- |
| 0–127 | register 0–127: x0–x31, then virtual registers 32–127 | register |
| 128 | `const` | immediate |
| 129, 130, 131 | the register the instruction's rd, rs1 or rs2 field names, its bits [11:7], [19:15] or [24:20] | register |
| 132 | the instruction's immediate, decoded by `format` and sign-extended to 64 bits; 0 for format R | immediate |
| 133 | ones in bits [63:*s*], *s* = *imm* & 0x3f | immediate |
| 134 | 1 << (*imm* & 0x3f) | immediate |
| 135 | 1 << (*imm* & 0x1f), a 32-bit value | immediate |
| 136 | 2³² − (1 << (*imm* & 0x1f)) | immediate |
| 137–144 | temporary *k* = code − 137: register 40 + *k* + the temporary offset (17.8.5) | register |
| 145 | 0 | immediate |
| 146–255 | reserved: an invalid expansion (17.8.10) | |

- ***imm*** in codes 133–136 is code 132's value.
- **The slot's kind is fixed.** A register slot reads its register, and an
  immediate slot carries its value, so code 0 (x0) and code 128 with `const` 0
  differ in the row (17.8.11).
- **The row's immediate** is the sum of its immediate slots' values, except on a
  `VirtualShiftData…` row, whose immediate is its `shift_offset`.

**Derived, not stored:**
- `is_first`: the row is the first of its expansion, and the expansion is
  virtual;
- `is_virtual`: `virt` is set, or the expansion has more than one row
  (`seq_remaining` > 0, or the row is not its first); the shared NOP row's by its
  `rd0` (17.8.5);
- `pc_increment`: 0 unless `seq_remaining` is 0, then 2 for a compressed
  instruction (its code value's bit 0 is 0) and 4 otherwise;
- `is_compressed`: the instruction is compressed and the word has `report_c`.

#### 17.8.5 The rd = x0 policy and the temporary offset

Jolt rewrites an instruction whose rd field is 0 before expanding it. The
template's `rd0` and a one-bit **temporary offset** reproduce it.
- **Remap.** The temporary offset is 1 when the instruction's rd field (bits
  [11:7]) is 0 and `rd0` is 2, and 0 otherwise. Under it, code 129 resolves to
  register 40, and temporary *k* to register 40 + *k* + 1.
- **NOP.** With `rd0` 1 or 3 and the rd field 0, the instruction runs the **shared
  NOP row**, ROM word 1, in place of its template: a one-row expansion, its row
  the first whatever the row offset. Under `rd0` 1 that row is virtual, so also
  `is_first`; under 3 it is not virtual. On the core-fed path the replayer takes
  the one word the core sends, for its `rd0` (17.8.13), and runs ROM word 1.
- **The first word delivered.** `rd0` is read from ROM[`start`] on the ROM path,
  even when the expansion starts at a row offset, and from the first word the
  core sends on the core-fed path (17.8.13).

#### 17.8.6 The decode table and the ROM

**Every instruction is a template**: an instruction passed through unchanged is a
one-word template whose operands are codes 129–132. The **decode table**,
2,048 × 16 bits, gives each instruction its template, by an 11-bit index formed
from its code value *w* (bit 0 plays no part):

| *w*[6:0] | index[10] | index[9:0] |
| --- | --- | --- |
| any other | 0 | {*w*[6:2], *w*[14:12], *w*[30], *w*[25]} |
| 0101111, AMO | 1 | {00, 00, *w*[12], *w*[31:27]} |
| 0001011, custom-0 (the inlines) | 1 | {01, *w*[29:25], *w*[14:12]} |
| 1110011, SYSTEM | 1 | {10, 0000, *sel*}: *sel* is *w*[14:12] when it is not 0; when it is 0, *sel* is 8 for *w* = 0x0000_0073 (ECALL), 9 for 0x0010_0073 (EBREAK), 10 for 0x3020_0073 (MRET), and 11 otherwise |

The index with [10:8] = 111 is never formed.

| Entry bits | Field |
| --- | --- |
| [1:0] | `class`: 0 a ROM template; 1 a core-fed template, an inline; 2 a core-fed template, CSRRW or CSRRS; 3 invalid |
| [10:2] | `start`: the template's first ROM word |
| [15:11] | spare |

- **A padding word** reads no entry: it runs ROM word 0, a one-row expansion,
  its row the first whatever the row offset.
- **Class 0.** The expansion is the ROM's words from `start`, or from `start` +
  the row offset at a pass's start, through the first word with `seq_remaining`
  0.
  A template past word 511 is an invalid expansion, at the row that would read
  past it: a word at 511 with `seq_remaining` > 0 runs, and the expansion's next
  row is the invalid one; `start` + the row offset > 511 makes its first row
  invalid.
- **Class 1 or 2: a core-fed expansion.** The replayer presents the instruction's
  code value in `EXPANSION_OPCODE` and its row offset in `EXPANSION_OFFSET`,
  posts `new_expansion`, fills `expansion_credit` (17.8.8), and takes the
  expansion's words from the expansion queue, through the first word with
  `seq_remaining` 0. The core finds and sends them (17.8.13).
- **Class 3** is an invalid expansion.

**The ROM** is 512 expansion words. The pass reads it on one port, and the write
port writes it on the other, during a pass too (17.8.9). The hardware runs word
0 for a padding word and word 1 as the shared NOP row (17.8.5), which software
loads with the padding row (opcode NOP, every operand code 0, `seq_remaining` 0,
`hold` 1) and the shared NOP row (`ADDI x0, x0, 0`: opcode ADDI, `rd` 0, `rs1` 0,
`rs2` 128, `const` 0, `report_c` 1).

**The tables are data.** The decode table, the ROM's other words and the core-fed
set are software's: for jolt-cpp `821e1bd`, `data/tables/replayer/`, which
cover every RV64IMAC instruction and every inline jolt-cpp supports.

#### 17.8.7 A row

1. Take the current instruction from the code decoder, and its row's word: the
   template's next ROM word, or the expansion queue's next word.
2. Resolve the operand codes (17.8.4, 17.8.5), and read rs1's, rs2's and rd's
   values from the register file.
3. Compute rd's value with the ALU, or take the log word; write rd; form the
   memory address, rs1 + the row's immediate.
4. Push the row into the output queue (17.8.11), and advance `pc` and
   `instr_idx` (below).

The instruction leaves the code decoder no later than its expansion's last row
completes.

**The register file** holds the 128 registers. Register 0 reads 0, and a write to
it is dropped. It is the record's register image: the image's packets write it,
two registers a packet (17.8.9).

**What a row computes** is `spec/jolt/replayer-opcodes.md`'s sections 1–5: for each
row opcode, its slots, rd's value, its control transfer, its log word, and a load
or store row's memory fields. A row reads each register slot's register and takes
each immediate slot's value, with three departures from jolt-cpp:
- a row with `from_log` takes its log word (17.8.3), where jolt-cpp reads memory
  or computes advice;
- the assertions, `VirtualAssert…`, are not evaluated, and write nothing;
- control flow follows the rules below, the next `instr_idx` from the log.

**Classes.** The ALU executes the 88 opcodes `data/tables/opcode_table.txt` marks S, M
or L; any other opcode is an invalid expansion (17.8.10).

| Class | Opcodes | Cycles | Schedule |
| --- | --- | --- | --- |
| L, log | SD 53, LD 54, `VirtualLoadB/H/W` 110–112, `VirtualStoreB/H/W` 113–115, `VirtualAdvice` 126 | 3 | 1: read rs1 and rs2. 2: take the log word, read rd. 3: write rd |
| M, multiply | MUL 43, MULHU 45, MULW 65, `VirtualMULI` 118, `VirtualMULIW` 119 | 5 | 1: read rs1 and rs2. 2: four 16 × 64 partial products, which may be kept in carry-save form, read rd. 3–4: their sum, the 128-bit product. 5: write rd: its low half, its high half, or its low word sign-extended |
| S, simple | the 74 others | 3 | 1: read rs1 and rs2. 2: compute, read rd. 3: write rd |

- **Rows never overlap.** Each row's reads precede its write, so a row whose rs1
  or rs2 is its rd reads the old value: there are no hazards and no bypass. The
  next row's fetch and decode may overlap a row.
- **Stalls** delay a row's start, never change its schedule: an empty code or log
  queue with entries to come, an empty expansion queue in a core-fed expansion,
  and a full output queue. What a stall costs is the implementation's.
- **Exact rows, not exact cycles.** The rows, their order and their contents are
  exact, and a row takes exactly its schedule when nothing stalls. The cycles of
  a pass are the implementation's (17.8.16).
- **`VirtualPext` and `VirtualPextSigned`** are correct for any mask. A
  contiguous mask, every mask the tables make, keeps the schedule; another may
  take extra cycles, as many as the implementation's.
- **A zero mask** reaching `VirtualSRL`, `VirtualSRLI`, `VirtualSRA` or
  `VirtualSRAI` is UB (1.3).
- **A misaligned memory row** replays with the address its formula gives. No
  trace has one: jolt-cpp aborts on it.

**`pc` and `instr_idx`.** The row's pc is its instruction's, the same for every
row of its expansion. Its **new pc** is pc, but for the opcodes that set one
(`spec/jolt/replayer-opcodes.md` 1.4) on its expansion's last row:

| Row | New pc |
| --- | --- |
| not its expansion's last, whatever its opcode | pc |
| a branch, taken | pc + the immediate |
| JAL | pc + the immediate; 0 if the immediate is 0 and `imm0_pc0` is set |
| JALR | (rs1 + the immediate) & ~1 |

So `next_from_value` on a row that is not its expansion's last never transfers:
the row takes no log word for it (17.8.3) and is sequential, below, or at pc 0
the program's end, with no log word (as v1.35's 4.19.7).

Then, in this order:
1. **`hold`:** `pc` and `instr_idx` stay, on any row.
2. **A new pc of 0** ends the program, from any row: `pc` and `instr_idx` are 0,
   the row's log word being 0.
3. **A new pc other than pc** transfers control: `pc` takes it, and `instr_idx`
   the row's log word. A row without `next_from_value` whose new pc differs from
   its pc is UB (1.3).
4. **Otherwise**, a jump to the row's own pc included: `pc` += `pc_increment`,
   and `instr_idx` += 1, with no log word.

The new pc is compared with pc in all 64 bits, for any pc: nothing requires a pc
to be even, and nothing checks it.

**Jolt's conventions in the hardware.** Four depend on values known only as the row
runs, so a change in Jolt changes the hardware: a new pc equal to pc is sequential;
register 0; the memory fields, formed for the load and store rows only (17.8.11);
and a new pc of 0 ending the program. Every other convention is table data:
`virt`, `rd0`, `imm0_pc0`, `report_c`, `from_log`, `sign_imm`, `offset_imm`, and
the advice rows' operands.

#### 17.8.8 The queues, the credits and the state write

Four input queues, each a FIFO of `REPLAY_QUEUE_ENTRIES` = 256 entries of 128
bits:

| Queue | Its part of the region (17.8.9) | Holds |
| --- | --- | --- |
| code | part 1 | the code stream, as sent |
| read log | part 2 | the read log, as sent |
| write log | part 3 | the write log, as sent, with the store log |
| expansion | part 4 | the expansion words the core feeds, one an entry |

- **Pushing.** A `WRITE` into a queue's part appends its payload at the tail. A
  queue takes pushes during a pass too.
- **Popping.** When an entry leaves its queue is the implementation's, at most
  one a cycle from each queue: a stream's entry no later than the use of its
  last bit, or of the stream's last value when that ends in it (17.8.12), and an
  expansion word no later than its row takes it. An entry leaves early only
  while the replayer holds the bits of it not yet used.
- **Overrun.** A push into a full queue is dropped, and sets
  `ERRORS.queue_overrun`. It is still counted (12.3). "Full" is the count before
  the cycle's pop (7.4).

**Credits.** Each queue's credit is a counted event of depth 2 (17.8.1), each
credit half a queue: leave to push up to 128 entries.
- **Filled** to 2 by the state write, the expansion queue's also by each new
  expansion (17.8.6). At a new expansion the expansion queue is empty: a word
  left in it is UB (1.3).
- **Returned:** each time 128 entries have left a queue since its last return,
  its credit gains 1. The count of entries left restarts at the state write, and
  the expansion queue's also at each new expansion.
- **Taken** by a consuming load of `EVENTS` (17.2): one from each credit it
  returns set that `EVENTS_MASK` selects.

**The state write.** The write of the state word, offset 0, as it lands:
- empties the four input queues, but not the output queue, whose rows drain;
- fills the four credits;
- clears the streams' size counters, and zeroes the decoders' tables (17.8.12).

Nothing else does these but the reset. The write port's writes land in the order
it takes them, so a push the director accepts after the state write is kept,
before `START` or after it, and one accepted before it is lost.

#### 17.8.9 Its region

The replayer's region, 0x04 on a compute tile, takes its record, its tables and
its queues' pushes. Offset bits [22:20] pick a part, 0 to 4, with bit 23 0:

| Offset | Bits [22:20] | Contents | Size |
| --- | --- | --- | --- |
| 0x00_0000 | 0 | the state word (17.8.2) | 16 bytes |
| 0x00_0010 | 0 | the stream word | 16 bytes |
| 0x00_0020–0x00_041F | 0 | the register image: image word *i* at 0x20 + 16*i* | 1 KiB |
| 0x00_0420–0x00_043F | 0 | the record's words 66 and 67: accepted and ignored | 32 bytes |
| 0x00_1000–0x00_1FFF | 0 | the decode table: entries 8*r* to 8*r* + 7 at 0x1000 + 16*r*, entry 8*r* + *j* in bits [16*j* + 15 : 16*j*] | 4 KiB |
| 0x00_2000–0x00_3FFF | 0 | the ROM: word *j* at 0x2000 + 16*j* | 8 KiB |
| 0x10_0000–0x1F_FFFF | 1 | the code queue's pushes | |
| 0x20_0000–0x2F_FFFF | 2 | the read-log queue's pushes | |
| 0x30_0000–0x3F_FFFF | 3 | the write-log queue's pushes | |
| 0x40_0000–0x4F_FFFF | 4 | the expansion queue's pushes | |
| the rest of part 0 | 0 | unmapped | |
| 0x50_0000–0xFF_FFFF | 5–7, or bit 23 1 | unmapped | |

- **Writes** are 128-bit `WRITE`s, 16-byte aligned, one a cycle, each landing
  within `PERIPHERAL_LATENCY` (17.3). Another size, or an unmapped offset, is UB
  (17.3).
- **A queue's part** is one address in effect: its range lets a transfer's
  advancing address keep reaching it, and the rest of the address is ignored.
- **A register image packet** writes its two registers in one cycle, and a
  decode table packet its 8 entries.
- **Ownership** follows `STATUS.busy` (17.2):

  | | The record's words and the decode table | The ROM | The queues' parts |
  | --- | --- | --- | --- |
  | idle | written; the state word's write resets the pass (17.8.8) | written | pushed |
  | busy | UB (1.3): the pass changes `pc`, `instr_idx`, the row offset and the register file in place | written; a word a row reads in that cycle UB (1.3) | pushed |

  `RUN_CONFIG` follows the record's rule (17.8.1).

#### 17.8.10 A pass

- **`START`** (17.2) sets `STATUS.busy`, clears `ROWS_RUN`, and runs the pass
  from the record as it is, with `RUN_CONFIG`'s rows and flags. It does nothing
  else: the state write reset the pass (17.8.8). When its first row starts is the
  implementation's (17.8.7).
  - A `START` with no state write since the reset, or since the previous pass's
    `START`, is UB (1.3).
  - With `rows` 0 the pass runs no row: `STATUS.busy` is 1 for the cycle after
    the start takes effect alone, and `EVENTS.done` is posted at its end.
- **The end.** The pass ends when its `rows`th row has been pushed into the
  output queue, inside an expansion or not: `STATUS.busy` falls and `EVENTS.done`
  is posted, in that cycle.
  - **Nothing is reset:** expansion words left in their queue stay until the next
    state write, the record's words hold the window's end state, and the output
    queue's rows drain.
  - **Leftover.** At the end, whatever `rows`, 0 included, each stream, the write
    log only with `RUN_CONFIG.store_log`, has had exactly its words taken from
    its queue and its values decoded, and no more entries have entered its queue
    than its words (17.8.12); otherwise `ERRORS.leftover` is set at the end or
    the cycle after it.
- **An invalid expansion** is a decode entry of class 3; an expansion word with an
  operand code from 146 to 255, a `format` of 6 or 7, or an opcode the ALU does
  not execute; or a template past ROM word 511 (17.8.6). It sets
  `ERRORS.invalid_expansion`.
- **An error stop.** An invalid expansion or an underflow (17.8.12) ends the pass
  at once, as its end does, with no leftover check, at the row it belongs to:
  that row is not pushed, and every row before it is. An overrun leaves the pass
  running.
- **After a trap** the pass runs on, unserviced, stalling when a queue runs dry,
  until the reset (17.5).

#### 17.8.11 The row port and the row

**The row port**, `replay_row` (3), sends each row from the output queue, one a
cycle, in pass order: `replay_row_valid` is 1 while the queue holds a row, which
the port offers until its handshake. The output queue holds
`REPLAY_OUTPUT_ROWS` = 32 rows, and while it is full no row starts. *Open:* its
consumer, the materializer, so the chip ties `replay_row_ready` to 1 until it is
specified.

**The row** is 640 bits, five 128-bit words: what Jolt's 80-byte trace row,
`RiscCycle`, carries, with `instr_idx` and without a store's memory value after,
which its consumer derives. As bytes, it is words 0–4 in order, each
little-endian.

| Word | Bits | Field | Value |
| --- | --- | --- | --- |
| 0 | [31:0] | `instr_idx` | the row's bytecode index |
| 0 | [39:32] | `opcode` | the row opcode |
| 0 | [47:40] | `rd_reg` | rd's register number; 0 for an immediate slot |
| 0 | [55:48] | `rs1_reg` | rs1's, likewise |
| 0 | [63:56] | `rs2_reg` | rs2's, likewise |
| 0 | [79:64] | `inline_sequence_remaining` | `seq_remaining` |
| 0 | [80] | `is_compressed` | 17.8.4 |
| 0 | [81] | `is_virtual` | 17.8.4 |
| 0 | [82] | `is_first_in_sequence` | `is_first`, 17.8.4 |
| 0 | [85:83] | `rd_is_imm`, `rs1_is_imm`, `rs2_is_imm` | each slot's kind: 1 for an immediate (17.8.4) |
| 0 | [86] | `imm_signed` | `sign_imm` |
| 0 | [127:87] | spare | 0 |
| 1 | [63:0] | `pc` | the instruction's pc |
| 1 | [127:64] | `imm` | the row's immediate (17.8.4), a `shift_offset` sign-extended to 64 bits; 0 for an advice row |
| 2 | [63:0] | `rs1` | rs1's value before the row; 0 for an immediate slot |
| 2 | [127:64] | `rs2` | rs2's, likewise |
| 3 | [63:0] | `rd_prev` | rd's value before the row; 0 for an immediate slot, and 0 in every row while `RUN_CONFIG.rd_prev` is 0 |
| 3 | [127:64] | `rd` | rd's value after the row; 0 for an immediate slot and for x0 |
| 4 | [63:0] | `mem_addr` | for a load or store row (17.8.3), *a* = rs1 + the immediate, and for a `…B/H/W` row *a* − (*a* & (8 − *width*)), *width* 1, 2 or 4 bytes; otherwise 0 |
| 4 | [127:64] | `mem_value` | for a load row, rd's value after the row, so 0 for an LD to x0; for a store row, its log word, or 0 without the store log; otherwise 0 |

A padding row is all 0.

#### 17.8.12 Stream decoding

Each stream has a **decoder** between its queue and the pass, which reads it raw
or LRU-coded, by the stream word's flag (17.8.2).
- **Bit order.** A stream is read least significant bit first within each entry,
  entries in queue order, and each field and value least significant bit first.
- **Raw.** Code is four 32-bit values an entry, a log two 64-bit values, the low
  first.
- **LRU-coded**, against a table E[0]–E[15] of 64-bit values, E[0] the latest:
  - a **hit** is a 1, then a 4-bit index *i*: the value is E[*i*];
  - a **miss** is a 0, a size bit, 0 for 32 bits and 1 for 64, then the value, a
    32-bit one zero-extended. Code values are 32-bit: a code miss with size bit
    1 is UB (1.3).

**The table**, for code and the read log, is true LRU over its 16 entries:
- a hit *i* moves E[0..*i* − 1] to E[1..*i*], and the old E[*i*] to E[0];
- a miss *v* moves E[0..14] to E[1..15], dropping E[15], and puts *v* in E[0]. A
  value already in the table may miss: it is inserted again.

**The write log's table** is 15 LRU entries, E[0]–E[14], and E[15], the
**last load**:
- every load row's read-log value, an LD to x0's included, is copied into
  E[15];
- a hit 15 moves nothing; a hit *i* < 15 moves as above, within E[0]–E[14]; a
  miss *v* moves E[0..13] to E[1..14], dropping E[14], and puts *v* in E[0],
  leaving E[15];
- a load row's copy precedes any later row's write-log value.

**Sizes.** Each decoder counts the entries it has taken from its queue, 16 bits,
and the values it has decoded, 17 bits, and its queue the entries that have
entered it, 17 bits, an overrun's dropped push not among them: the stream's
**size counters**, from 0 at the state write, against the stream word's sizes.
An implementation may derive the third from the queue's occupancy, which gives
the same check once the entries taken equal the words.
- **The last value.** The decoder stops after the stream's values: the bits after
  its last value, to the end of its last entry, are padding, of any value.
- **Underflow.** A row that needs a value past the stream's values, or a value
  whose bits run past the stream's words, sets the stream's underflow error and
  stops the pass (17.8.10). Both fire on the row that needs the value, never on
  a decoder's look-ahead: a code value's row is the first its instruction runs.
- **Leftover** is the pass's end's check (17.8.10).
- **Without the store log**, the write log is in neither check. The expansion
  queue has no sizes: an expansion ends at `seq_remaining` 0.
- **An empty queue** with entries to come stalls the pass, and is no error.

#### 17.8.13 What software keeps

The core runs the passes. Breaking one of these rules is UB (1.3), unless it
names an error:
- **Before every pass** it writes `RUN_CONFIG` if the pass's rows or flags
  change, and reloads the record's words 0–65 verbatim, the state word first,
  while `STATUS.busy` is 0. It starts the pass once a counter says they have
  landed (12.3).
- **Before the state write** every push into a queue has landed, whatever its
  source.
- **Refills.** A refill pushes no more than the credits taken for it allow, and
  every refill of a stream but its last is exactly half a queue. Pushing more
  overruns only when the queue is full.
- **One source at a time** feeds a queue's part (17.3, 14.4), and no other tile
  writes the region during a pass.
- **A core-fed expansion.** On `new_expansion` the core finds the template of
  `EXPANSION_OPCODE` in the core-fed set, by its key (17.8.19), and
  pushes its words from `EXPANSION_OFFSET`, the template's `rd0` in the first
  word it sends. Under a NOP policy with the instruction's rd field 0 it sends
  the shared NOP row alone, carrying the template's `rd0`.
- **Credits with nothing behind them**, a stream's after its last refill, the
  write log's without the store log, the expansion queue's once the expansion is
  sent: the core takes their bits out of `EVENTS_MASK`, so its `WFI` waits.
- **No memory tile answer targets a queue**: LPDDR's data lands in the
  scratchpad (11.5), and is refilled from there.

**The rest is software's:** the core's loop, the core-fed set and its lookup,
the template bitmap's use, the tables' and the records' delivery, and the
resident replay region with its LPDDR staging (17.8.19, 17.8.21 and 17.8.22; the
firmware: meshsim's replay controller, `fw/replay/replay_ctl.c`; the RTL's is its `replay.rs`). Every refill, record and expansion reaches the region
as a `WRITE` packet, on this tile from its DMA unit's stream transfers out of the
scratchpad (8.3) and its write unit (9): the replayer has no other path to
the core. The core-fed set's delivery is decided and not built (item 115): the tracer
tile broadcasts a segment's used set, the 60 CSR templates and the inlines its
windows' bitmaps name, and each tile keeps the CSR templates and the inlines its
own bitmap names. Until it is, every tile keeps the whole set, 13,821 words,
216 KiB, in its scratchpad.

#### 17.8.14 Reset

The reset ends any pass and empties the five queues, so `replay_row_valid` is 0.
It zeroes `STATUS`, `EVENTS`, the credits among them, `ERRORS`, `EVENTS_MASK`,
`RUN_CONFIG`, `ROWS_RUN`, `EXPANSION_OFFSET` and `EXPANSION_OPCODE`. The state
word, the stream word, the register file, the decode table and the ROM keep
their contents, as a peripheral's memories (18). The streams' size counters and
the decoders' tables are undefined until the next state write, which a pass
needs (17.8.10).

#### 17.8.15 Ground truth, the reference and the status of the design

*Kept from v1.35's 4.19 and 4.19.1, in v1.36's terms. "The tracer" in 17.8.15–17.8.20, in lower case, is jolt-cpp's standalone tracer, the reference's source; the Tracer (17.9) is the tracer tile's peripheral. (Not stated by the RTL's specification; kept from v1.35.)*

**Ground truth** (DECISIONS-v1.3 section 17; owner, 2026-11-04, replacing 2026-10-26's parts G and H; item 113). The trace the replayer reproduces, and the Tracer writes (17.9), is **a16z's Jolt's**: the 80-byte trace row and the sequence of pc and `instr_idx` that a16z's verifier checks a proof against. **a16z decides:** jolt-cpp has no verifier of its own; it proves `jolt_verifier::JoltProof` against a16z's `jolt_program` preprocessing, from its submodule `third-party/jolt` at **`a16z/jolt` `7189216`**, the verifier's source, so where jolt-cpp builds a different bytecode or image its proofs fail verification, and following a16z is forced, not a preference. **Gold is the Java emulator in `pipeline/gold/`** (WORKER18): self-contained, it writes the segment file byte-identical to WORKER16's tool, and its rows match jolt-cpp's on both workloads at 2²⁹ and a16z's (at `17a9459`, a descendant of `7189216` with every file the findings rest on unchanged) wherever jolt-cpp's do; where the two differ at the edges (`v1.3/planning/upstream/UPSTREAM-DIFFS.md`) the owner rules case by case, by default for a16z, and gold follows. **jolt-cpp `88dca85` is the pin, checked** (DECISIONS-v1.3 section 14 (c); item 154), its submodule being the verifier's. Both checks passed: `src/tracer/isa` is unchanged since `821e1bd`, and every window's rows, the segment files and `capture_writes`' output identical to `821e1bd`'s on both workloads at 2²⁹ (WORKER16, merged at fa59d9a). So the tables built at `821e1bd` (`data/tables/`) and this section's citations stand. jolt-cpp is checked against **as data, not code** (its rows, its segment files). The formats that are ours (the streams, the LRU coder, the record, how template words are delivered) are ours to define, provided the rows that come out match. `spec/jolt/replayer-opcodes.md` is normative for the replayer as built. The rules tagged *(pending the upstream check, item 113)* were found by reading source before gold existed; stage 3 found no difference in any row it exercised, and each tag goes as gold's guests settle its case against a16z (item 113). *(v1.26.1: `LayerZero-Research/jolt` `735355e` was the ground truth and jolt-cpp the gold standard.)*

**The reference** (v1.25; DECISIONS-v1.25 sections 1 and 2). The rows must match Jolt's bit for bit. **v1.3's pin is jolt-cpp `88dca85`, checked** (ground truth, above); its tracer's `isa/` is unchanged from `821e1bd`, so what follows, built and checked at `821e1bd`, stands. The reference is **jolt-cpp `821e1bd`** (`snarkify/jolt-cpp@821e1bd2f433d6ca62300afbbbba031db50e7dc9`, `main` of 2026-10-30), as copied into the standalone tracer on the `tracer-standalone-main` branch of `jolt-asic-planning` (PR #2). v1.2's reference was the earlier copy on `tracer-standalone` (PR #1, commit `4f36b3d`, jolt-cpp of 2026-10-19). The reference workloads are jolt-cpp's own: **EVM Zone, release `evm-zone-4e3451c1d8c5`**, input `trace-29-4599adc50712` (it replaces the EVM Zone program v1.2's measurements used), and **Cyclone**, input/29, unchanged; both pad to 2²⁹ rows. Three functions are normative: `expand_virt_seq` (`isa/virtual.cpp:132`) for what an instruction expands to, `execute_instruction` (`trace_owner.cpp:145`) for what a row computes, and `expand_compressed_chunk` (`trace_owner.cpp:855`, the row at 880–913) for what a row records. Citations in this section are to files under the tracer's `tracer/src/tracer/`. **Those marked 821e1bd, and the three just given, are checked at `821e1bd`; the others were taken at `4f36b3d` and name the same code, whose line numbers may have moved** *(item 137)*. Where this section departs from the reference (log-fed values, unchecked assertions), it says so.

**Status.** The design notes (the owner's *Replayer and mesh v1.1*, updated 2026-10-23, with their shared-resource allocation table) record most of this design as candidate and some of it as decided (the streams, v1.35's run log, 2026-10-22 and 2026-10-23; the fixed row schedules, every instruction a template, the queues as true FIFOs and the resident region, all 2026-10-23; the allocation table, confirmed 2026-10-23). This section states the notes' design unmarked, as the design to build; its own additions are marked *(Proposed)* and gathered in chapter 26 (items 33–59), and so are the details the owner accepted as proposals on 2026-10-23. Facts about Jolt's expansions are cited to the tracer; the template enumeration that would confirm them across real programs is open (item 34).

#### 17.8.16 Rates and measurements

*Kept from v1.35's 4.19.1, 4.19.2, 4.19.7 and 4.19.8, in v1.36's terms, with the RTL's figures. Every figure marked [meas: v1.25 …], [meas: v1.26 …] or [meas: v1.26.1] is of that version's simulator, and is to be measured again on v1.36's. (Not stated by the RTL's specification; kept from v1.35.)*

**The RTL's figures** [meas: RTL]. The replayer of the RTL (Snarky One), simulated alone under an ideal controller on the recorded windows of both workloads (`evm-zone-23`, 128 windows, 8,388,608 rows; `cyclone-22`, 64 windows, 4,194,304 rows), the streams raw, the row port always ready. The controller loads `EVENTS` in each cycle the level is up, releases 128 entries a credit, answers a new expansion by loads of `EXPANSION_OPCODE` and `EXPANSION_OFFSET` in the next two cycles, and sends one packet a cycle. **v1.36's simulator reproduces these figures**, cycle for cycle (`tests/replayer/`, `tests/acceptance/`); v1.35's model differs from them only in its stalls.

| | EVM Zone, 128 windows | Cyclone, 64 windows |
|---|---|---|
| the RTL: cycles; cycles a row | 25,818,054; **3.0778** | 13,010,346; **3.1019** |
| – the rows' schedules (3 cycles, 5 for a class M row; 17.8.7), the same in the RTL and the model | 25,814,298 | 13,006,362 |
| – stalls | 3,756 | 3,984 |
| –– a core-fed expansion's first row, 4 cycles each | 668 rows: 2,672 | 860 rows: 3,440 |
| –– a window's first row, the queues empty at `START` | 128 rows: 1,066 | 64 rows: 540 |
| –– a load or advice row whose read-log word its decoder does not announce, 2 cycles each | 9 rows: 18 | 2 rows: 4 |
| the model (v1.26.1's simulator, Stage A, the same windows): cycles; cycles a row; stalls | 25,832,928; **3.0795**; 18,630 | 13,015,958; **3.1032**; 9,596 |
| the RTL against the model | −0.06 % | −0.04 % |
| the RTL's chip, each tile's core running the replay firmware: cycles a row | 3.0875 | 3.1249 |

- **What a stall costs in the RTL** (17.8.7 leaves it to the implementation):
  - the first row of a core-fed expansion waits for the core's answer: 4 cycles under the ideal controller; on the chip, with the firmware, 76.1 cycles on average on EVM Zone and 94.4 on Cyclone;
  - the shared NOP row of a ROM template (17.8.5): 2 cycles, the fetch reading the template's first ROM word for its `rd0`, then ROM word 1;
  - a load or advice row whose read-log word its decoder has not decoded ahead: 2 cycles;
  - `VirtualPext` or `VirtualPextSigned` with a mask of two or more runs: one cycle a mask bit up to its highest set bit, and one more, at most 65 cycles past the schedule. A contiguous mask keeps the schedule (item 140);
  - LRU-coded streams: 14 cycles more over EVM Zone's 128 windows, 8 over Cyclone's 64.
- **What v1.35's model charges and the RTL does not:** its waits on its queues at a window's start (135 cycles a window on EVM Zone, 123 on Cyclone), 2 cycles a new expansion with a controller that answers in the event's cycle, and a look at the read log before every row with `next_from_value`. It times a `VirtualPext` of any mask as 3 cycles.
- **The model's figures above are v1.26.1's** [meas: v1.26.1], run once on these windows; v1.35's simulator is cycle-equal to it by its own notes, and was not run again.

**Rate.** A row takes a fixed 3 cycles, or 5 for a multiply (17.8.7), plus stalls. **Measured at v1.25** [meas: v1.25's acceptance runs, Quest 083 and 084: meshsim's Stage B on every window of both workloads at 2²⁹, `v1.3/planning/sessions/WORKER11-REPORT.md` section 5]: on the windows holding real rows, **3.32 cycles per row on EVM Zone** (per window 3.00–3.59, median 3.32) and **3.16 on Cyclone** (3.00–3.50, median 3.10); over all 8,192 windows, the padding ones included (3.00), 3.21 and 3.11. A pass is so about 218K cycles on average and at most 235K, **about 0.22 ms at 1 GHz with everything local, 0.24 ms at worst**. Stalls are 0.24 % of a pass on EVM Zone and 0.62 % on Cyclone, most of it the fixed image and code loads (190 cycles a pass), the write log's waits and, on Cyclone, core-fed expansions (17.8.19). The consuming kernels likely need 3–6 cycles per row anyway [estimate].

**The streams' sizes**, per tile:

| stream | granularity | contents | per tile |
|---|---|---|---|
| **code** | one 32-bit value per native instruction, held while its expansion runs | the RISC-V instruction; a compressed one is stored decompressed with bit 0 cleared as its flag | about 43K words (175 KiB) at 1.5 rows per native instruction; 256 KiB at most [estimate] |
| **read log** | one 64-bit word per row that needs one from it, in row order | load values, advice, and the next `instr_idx` of every row that doesn't continue sequentially | 0.192 words per real row on EVM Zone, 0.206 on Cyclone [meas: v1.25, Quest 085, below] |
| **write log** | one 64-bit word per row that needs one from it, in row order | store rows' pre-values | 0.156 words per real row on EVM Zone, 0.100 on Cyclone [meas: v1.25, Quest 085] |

Together the two logs are 0.35 words per real row on EVM Zone and 0.31 on Cyclone, and up to 0.833 in Cyclone's densest windows (0.418 in EVM Zone's): raw, about 175–190 KiB for a median window and 437 KiB at most [meas: v1.25, Quest 085]. A pass that needs only one log fetches only that one.

**The densities at v1.25** (words per real row, load / store / advice / non-sequential) [meas: v1.25, Quest 085, every window of both v1.25 deliveries at 2²⁹, counted from the reference rows and checked word for word against each record's raw stream lengths]: **0.181 / 0.156 / 0.00001 / 0.011 on EVM Zone (total 0.348)** and **0.163 / 0.100 / 0.0003 / 0.043 on Cyclone (total 0.306)**. Per full window of real rows the total's median is 0.366 on EVM Zone (0.172–0.418) and 0.330 on Cyclone (0.107–0.833). **Cyclone's dense stretches are sustained, not bursts:** its worst 512-, 1,024- and 4,096-row spans hold 0.838, 0.836 and 0.834 words per row, most of it write log (up to 0.754); EVM Zone's are 0.781, 0.683 and 0.600. A 64-window replay pass reads on average **7.7 MiB of log raw on EVM Zone, 6.3 MiB with each stream in its smaller form**, and 7.1 MiB and 2.9 MiB on Cyclone [same]. The totals match the derived expectation from v1.2's figures, about a ninth above `4f36b3d`'s 0.315 and 0.277, with v1.25's shorter expansions removing rows but no log words; the mix moved, since the workload changed: EVM Zone's stores rose and its non-sequential words fell. What the logs cost the replayer **at v1.25** is small: read-log waits average 59 cycles per window on EVM Zone and 63 on Cyclone, write-log waits 267 and 222, together about 0.15 % of a pass [meas: v1.25's acceptance runs, Quest 083 and 084: meshsim's Stage B on every window of both workloads at 2²⁹, `v1.3/planning/sessions/WORKER11-REPORT.md` section 5]. **At v1.26, with 4 KiB queues** (17.8.8), they cost less: read-log waits average **39.2** cycles per window on EVM Zone and **43.5** on Cyclone, write-log waits **100.0** and **84.5**, together **0.066 %** and **0.063 %** of a pass. The 8 KiB control (the same v1.26 model and firmware with v1.25's 512-entry logs) waits the same on the read log and **176.2** and **153.6** on the write log, 0.102 % and 0.097 % [meas: v1.26, Quest 098 and 099 (4 KiB) and 100 and 101 (8 KiB): meshsim's Stage B on every window of both workloads at 2²⁹, `v1.3/planning/sessions/WORKER20-REPORT.md` section 5; item 143]. The control, not v1.25's figures, is the comparison for the queue size, since v1.26 changed the pass start (17.8.10). The waits are all at a pass's start (below).

**Rate.** A row is 3 cycles, or 5 for class M. **The multiply density, measured at v1.25** [meas: v1.25's acceptance runs, Quest 083 and 084: meshsim's Stage B on every window of both workloads at 2²⁹, `v1.3/planning/sessions/WORKER11-REPORT.md` section 5] [derived exactly: a pass's cycles less 3 per row and its stalls are 2 per class M row, and a padding window leaves 0]: **15.7 % of EVM Zone's rows** (per window median 15.7 %, maximum 28.6 %) and **7.1 % of Cyclone's** (median 4.1 %, maximum 24.7 %). So a 64K-row pass averages 3.32 cycles per row on EVM Zone and 3.16 on Cyclone, at most 3.59, with stalls: about 218K cycles, 0.22 ms at 1 GHz with everything local, or about 10 ms for the 46 passes of an Akita proof [estimate from the measured rate].

**The queues' size.** All four input queues (17.8.8) are dual-ported, so pushes land while the replayer runs. Every refill is local (17.8.22), and a half covers the local refill latency (a few hundred cycles) with room to spare. **Every queue is 4 KiB** (DECISIONS-v1.26 item 3, from DECISIONS-v1.3 section 12; v1.25's two logs were 8 KiB each): by the measured densities (above), the logs average 0.31–0.35 words per real row together and at most about 0.83 in Cyclone's densest windows, so a log's half-queue refill, 128 entries or 256 words, lasts at least about 300 rows even if one log carried both densities, some 900 cycles at 3 cycles a row, against a local refill of a few hundred [derived]. **Measured** (item 143; meshsim's Stage B, `replay_ctl.c` on tile 0, every window of both workloads at 2²⁹; cycles per window, average with the largest in brackets) [meas: v1.26, Quest 098–101, `v1.3/planning/sessions/WORKER20-REPORT.md` section 5]:

| per window | EVM Zone, 4 KiB | EVM Zone, 8 KiB control | Cyclone, 4 KiB | Cyclone, 8 KiB control |
|---|---|---|---|---|
| read-log wait | **39.2** (127) | 39.2 (127) | **43.5** (127) | 43.5 (127) |
| write-log wait | **100.0** (255) | 176.2 (383) | **84.5** (255) | 153.6 (383) |
| log waits, share of a pass | **0.066 %** | 0.102 % | **0.063 %** | 0.097 % |
| Stage B cycles per row | 3.2116 | 3.2128 | 3.1105 | 3.1120 |

**The 4 KiB queues wait less than 8 KiB, because the waits are all at a pass's start, not from the logs' density.** The first refill transfer sends the code queue's half, then the read log's, then the write log's, a packet a cycle, so a smaller half gets each stream's first entry in sooner: no read-log wait exceeds 127 cycles in any window, and no write-log wait 255 (383 at 8 KiB). Window by window the 8 KiB write-log wait exceeds the 4 KiB one by 0–128 cycles (exactly 128 in 4,753 EVM Zone and 4,259 Cyclone windows), and the read-log waits are identical. **After the start, a 128-entry refill always lands in time**, Cyclone's densest stretches (0.83 log words per row) included. With 4 KiB queues every window of both workloads matched in stages A and B, and every 64th in F (Quest 098, 099).

#### 17.8.17 The streams and the record: the producer's side

*Kept from v1.35's 4.19.2, in v1.36's terms: what 17.8.2 and 17.8.3 do not state, the requirements on the streams' producer and the reasons for the record's shape. (Not stated by the RTL's specification; kept from v1.35.)*

**A requirement on our tracer engine.** The two logs (17.8.3; v1.35's run log) differ from jolt-cpp's memory log on purpose. jolt-cpp logs only its load and store rows and `VirtualAdvice` (`trace_owner.cpp:822–855`, 4f36b3d; at `821e1bd` the load and store rows include the six narrow ones): it computes the DIV/REM advice in its interpreter (`trace_owner.cpp:545–630`) and looks a jump's next index up in a pc-to-index table (`finish_instruction`, `trace_owner.cpp:111–126`). The replayer has no divider and no such table, so **the tracer engine must write the DIV/REM advice and the next `instr_idx` of every control transfer into the read log**, in row order with the other log words (item 33). **From v1.3 that engine is the Tracer** (17.9), which writes them by 17.9.4's table, the advice from the host (DECISIONS-v1.3 section 13). The validation harness (item 58) proves the format, the tracer engine's extra words and the replayer together.

**The advice field is the Tracer's:** a segment's advice size at load, counting down live as the Tracer pops it (DECISIONS-v1.3 sections 23 and 24). A replayer neither checks nor changes it, and a record passes it through unchanged.

A stream is at most 65,536 values (one per instruction or per row) and 33,792 words (65,536 LRU-coded 64-bit misses of 66 bits; raw, 32,768 for a log and 16,384 for code), so 16 and 17 bits suffice. **Words** is the fewest 128-bit entries holding the stream: ⌈values / 4⌉ for raw code, ⌈values / 2⌉ for a raw log. v1.25's stream word gave three exact bit lengths instead (code [23:0], read log [55:32], write log [87:64]); v1.26's placed the word counts at [47:0], the flags at [50:48] and the value counts at [114:64], with no advice field.

**The replayer needs only words 0–65**, v1.25's part-0 image: writes to words 66 and 67 are accepted and ignored, and a core may simply not send them. **The record loads from byte 0**, so it streams straight into part 0. The tile's core reads the bitmap from the record itself (in the scratchpad, 17.8.22) to trim its templates. v1.25's record was 67 words, the bitmap first, and was sent from byte 16 onward.

**What the record is** (owner, 2026-11-04; DECISIONS-v1.26 item 17; v1.26.1). **The record is the superset of both units' state, plus the window's descriptor:** one 68-word format, each unit using what it needs and ignoring the rest, so the host, gold, the segment files, the Tracer's DMA and the tiles all write one layout. **The test for state is resuming:** what a unit needs to continue execution elsewhere, as RISC-V's `satp` is saved with a hart's context though running never changes it. The row count and the two per-pass flags fail it, so they are in `RUN_CONFIG` (17.8.1); the files word passes, being the Tracer's context.

| words | the Tracer | the replayer | what it is |
|---|---|---|---|
| 0, 2–65 | the program's state | the program's state | state |
| 1, the advice field | the live input count | ignored | state (the Tracer's) |
| 1, the three stream fields | outputs, written at done | inputs, the pass's size checks | the window's descriptor |
| 66, the files word | its context: the files, K, `lowest_address` | ignored | state (the Tracer's) |
| 67, the template bitmap | an output, written at done | ignored (the tile's core trims its templates by it) | the window's descriptor |

**The window's descriptor is in the record for pragmatism, not principle:** word 1's stream fields and word 67 describe one window rather than state (reset at each run, produced at done, and delivered to tile *n* while the state goes to tile *n* + 1, DECISIONS-v1.3 section 24). They travel with the window because the Tracer's DMA delivers them with it for free; the alternative for the bitmap, four read-only status registers, would have the tracer tile read them at every done and deliver them itself, a second path. The stream fields have the stronger case, since the replayer's checks read them.

Sending all 128 registers avoids special-casing a mid-sequence start, at 1 KiB per tile. **A tile that starts inside an expansion** has that instruction as its first code-stream entry, and the row offset (17.8.2) says where in the expansion to begin. jolt-cpp's chunked replay instead rewinds to the start of the expansion when inline scratch registers are live (`trace_owner.cpp:722–739`); the full image makes that unnecessary. **Register 0** of the image is ignored: the working file's register 0 always reads 0 *(Proposed, item 41)*.

**Inline scratch.** Registers 48–127 hold an expansion's scratch values and are 0 between expansions (`isa/virtual.hpp:25–27`). Inline templates end with explicit `ADDI vN, x0, 0` rows that zero each scratch register they used (for example the end of `inlines/blake3.inc`), so the replayer needs no state for them. jolt-cpp also zeroes them defensively at chunk starts (`normalize_inline_vregs`, `trace_owner.cpp:128`); with the full register image the replayer doesn't need to.

**End of trace** *(pending the upstream check, item 113)*. The trace is padded to 2²⁹ rows after the program ends. The program's last real row jumps to pc 0: a JAL with immediate 0 sends pc to 0 (`trace_owner.cpp:281`), and its log word sets `instr_idx` to 0. The tracer writes the padding as real code entries: one code value 0x00000000 per padding row, with no log words. The padding word maps to the ROM's padding template (17.8.6), an all-zero NOP row with `hold` set, so pc and `instr_idx` stay 0, as in the tracer's padding, where every padded row is zero and decodes as the zero-filled NOP at bytecode index 0 (`trace_owner.hpp:160–163`; real instructions start at index 1, `isa/virtual.cpp:1359`). **From v1.3** (DECISIONS-v1.3 section 25) the end is **any JAL with immediate 0, whatever its rd**, EBREAK and `JAL ra, 0` included, which a16z treats as the pc stalling; the Tracer emits this encoding unchanged (17.9.4).

**Why the write log has a last-load entry** (kept from v1.35's 4.19.15; owner, 2026-10-26). The pre-values of sub-word stores, AMOs and SC, and of many plain read-modify-write SDs, equal the value of the load just before them, which the read log has already delivered: at `821e1bd` every sub-word store, SC.W and 32-bit AMO loads its doubleword with a `VirtualLoad…` row just before its `VirtualStore…` row. So the write log's table gives one entry, E[15], to the last load (17.8.12). **The encoder's side is the Tracer's** (17.9.6): it applies the same table rules, and codes a value that matches both E[15] and an LRU entry as index 15, so that the LRU order isn't disturbed. The decoder doesn't depend on that choice; the rule makes the encoding deterministic. An LD to x0 *(pending the upstream check, item 113)* still copies its read-log value into E[15], though its row reports a memory value of 0 (17.8.11): the decoder sees the log value, the row what rd holds.

#### 17.8.18 The expansion word and the rd = x0 policy: Jolt's rules behind them

*Kept from v1.35's 4.19.3 and 4.19.4, in v1.36's terms: where each field of 17.8.4 and each rule of 17.8.5 comes from in Jolt, and what the tables hold for it. The table builder's rules here are a software contract. (Not stated by the RTL's specification; kept from v1.35.)*

Every row is described by one **128-bit expansion word**, in place of the tracer's 48-byte `VirtualInstruction` (`isa/virtual.hpp:89–126`). The same format serves the ROM and the words the core supplies.

**`virt`** *(Proposed, item 35)*. Jolt marks a row virtual when its expansion has more than one row **or its opcode differs from the source instruction's** (`isa/virtual.cpp:183`). The second half is not derivable in hardware, since the replayer never sees the source instruction as a row opcode: SLLI becomes one `VirtualMULI` row and is virtual, while ADD stays one ADD row and is not. The template builder therefore sets `virt` on every word whose opcode isn't the source's, and the replayer derives the rest (17.8.4).

**`shift_offset`** (DECISIONS-v1.25 sections 1 and 4). At `821e1bd` jolt-cpp's `VirtualInstruction` has a signed 32-bit `shift_offset` (its byte 12, formerly padding), used only by `VirtualShiftDataB/H/W`: the row's rs2 is the access's **base register**, the byte lane is taken from rs2 + `shift_offset` (17.8.20), and the row's immediate is `shift_offset` (`imm_raw`, `trace_owner.hpp:168–177`, 821e1bd). In every expansion `shift_offset` is either the source instruction's immediate (SB, SH and SW) or 0 (SC.W and the 32-bit AMOs), so the expansion word carries it as one bit, `offset_imm`, rather than a field.

**`sign_imm`** (DECISIONS-v1.25 sections 4 and 5). Jolt reports a row's immediate as a 128-bit value, the 64-bit immediate read as signed or as unsigned by opcode: signed for jolt-cpp's `signed_immediate` set (`isa/riscv.hpp:209`, 821e1bd), which is the load and store rows (the six narrow ones included), the conditional branches and, new at `821e1bd`, `VirtualShiftDataB/H/W`. v1.2 left that set to the row's consumer, which normalized the immediate by opcode; v1.25 makes it **table data**: the table builder sets `sign_imm` from jolt-cpp's own `signed_immediate`, so a change to Jolt's set means rerunning the builder, not changing the hardware. A bit rather than a new operand code, because codes 146–255 are kept reserved (an invalid expansion). The 64-bit immediate itself is unchanged: code 132 already sign-extends the source immediate to 64 bits.

`seq_remaining` = 0 marks the end of an expansion; there is no separate end bit. Its largest value, 3,120 (17.8.4), is `KECCAK256_ABSORB_PERMUTE`'s, which expands to 3,121 rows (`isa/virtual.cpp:1310`). The per-instruction fields (`rd0`, `format`, `next_from_value`) live in the ROM words, not the decode table, because every instruction is a template (17.8.6).

**The operand codes' sources in Jolt.** Code 132 is the source instruction's immediate as the tracer decodes it (`isa/decoder.cpp:74–212`). Code 133 is `mask_trailing_zeros(imm & 0x3f)` (SRLI, SRAI; `isa/virtual.cpp:565–573`, `../utils/math.hpp:134`); 134 is SLLI's multiplier, for `VirtualMULI` (`isa/virtual.cpp:596–600`); 135 is SLLIW's, for `VirtualMULIW` (`isa/virtual.cpp:616–620`); 136 is the 32-bit window bitmask of SRLIW and SRAIW (`isa/virtual.cpp:575–579`, `629–633`). Code 145 is an immediate slot whose value is 0, independent of `const` and of the source instruction (owner, 2026-10-27, part L). In codes 133–136, `imm` is the source instruction's immediate, the same value as code 132; the masks make the shift-immediate formats' funct bits irrelevant (the tracer decodes shift immediates with their own shamt format, `isa/decoder.cpp:98`, and only these codes ever use them). Templates never contain literal registers 40–47; they use the temporary codes.

**Two immediate slots** (part L). A word holds only one constant, so a row with two immediate slots, a non-zero constant and a zero, writes the zero slot as **code 145**, in ROM and core-fed words alike. Such rows occur in the core-fed secp256k1 templates (`LUI rd, #0x1000003d1, #0`) and in the ROM, as the first row of the LR/SC and AMO templates (`LUI v40, #0x80000000, #0`): **17 rows in EVM Zone's tables and 28 in Cyclone's** (15 and 26 core-fed, plus 2 ROM each) [meas: tracer enumeration]. A slot that is 0 in every instance, with no other constant in the row, is written as code 128 with `const` = 0, as the tracer now does. Before part L the zero slot borrowed code 132, which reads 0 only because every inline is R-format; tables built that way still replay correctly, but new tables use 145.

**The slot kind is committed** (17.8.4): Jolt records which slots are immediates (`rd_is_imm`, `rs1_is_imm`, `rs2_is_imm`, `isa/virtual.hpp:100–102`), so a zero placeholder must not replay as x0.

**The row's immediate** is the sum of its immediate slots' values, as Jolt computes it (`imm_raw`, `trace_owner.hpp:168–177`, 821e1bd), **except on a `VirtualShiftData…` row, whose immediate is its `shift_offset`** (its three slots are registers, 17.8.20); in every template the tracer emits, at most one immediate slot can be non-zero [derived: every word of the v1.25 tables, ROM and core-fed].

**The derived fields in Jolt** (17.8.4). `is_first` is `isa/virtual.cpp:186`. Only an expansion's last row carries `pc_increment` (`isa/virtual.cpp:1416`). Jolt reports `is_compressed` only where `pc_increment` = 2 (`trace_owner.hpp:184`, `trace_owner.cpp:899`), that is on an expansion's last row, so **the template builder sets `report_c` on last words only**. The row's pc, Jolt's `risc_pc`, is the same for every row of an expansion (`isa/virtual.cpp:1413–1415`).

**Jolt's conventions are data where they can be** *(Proposed, item 56)*. Each place where the replayer must copy a Jolt convention rather than plain RISC-V is a flag in the expansion word or a code in it, set by whoever builds the tables, so a change in Jolt means new tables rather than new silicon: `virt` (which rows Jolt calls virtual), `rd0` 1 and 3 (whether the rd = x0 NOP row is virtual), `imm0_pc0` (JAL to immediate 0 ends the program), `report_c` (which rows carry `is_compressed`), `from_log` (which rows take a log word), `sign_imm` (which rows' immediates are signed; v1.25), `offset_imm` (which rows take the source immediate as `shift_offset`; v1.25), and the advice rows' operands (17.8.20). What stays in logic is listed in 17.8.7 and item 57.

**The rd = x0 rewrite in Jolt.** Jolt rewrites an instruction whose rd is x0 before expanding it (`isa/virtual.cpp:307–319`):
- with **no side effect beyond writing rd**, it becomes one NOP row, `ADDI x0, x0, 0` (`isa/virtual.cpp:307–311`);
- with **side effects**, rd is remapped to a throwaway virtual register, v40, and the template's own temporaries then start at v41 (`isa/virtual.cpp:315–319`; temporaries are allocated upward from 40, `isa/virtual.cpp:140`, `148–153`);
- **a few instructions handle rd = x0 themselves** and are expanded unchanged (`rd0_is_handled_internally`, `isa/virtual.cpp:57–102`).

The replayer reproduces this with the template's `rd0` policy and the one-bit **temporary offset** (17.8.5), "offset" below.

Under a **NOP** policy with rd = x0, the instruction becomes the one shared NOP row (17.8.6) in place of its template. The tracer marks that row virtual unless the source instruction is ADDI itself (`isa/virtual.cpp:183`), so the policy says which: `rd0` = 1 makes the NOP row virtual (and so `is_first`) *(pending the upstream check, item 113)*, and `rd0` = 3, used only by ADDI's template, makes it not virtual *(Proposed, item 35)*. The row's virtual-ness is data, not an opcode compare.

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

LR/SC *(pending the upstream check, item 113)*, CSRRW, CSRRS, ECALL, EBREAK, MRET and the inlines are exempt (`rd0` = 0), as is every instruction whose rd field is an immediate (stores, branches, `VirtualAssertEQ`, FENCE; `has_rd0_register`, `isa/virtual.cpp:104–106`, 821e1bd). **`VirtualHostIO` is one of these at `821e1bd`:** it has no operands (the decoder makes all three slots immediates and accepts it only with rd = 0, `isa/decoder.cpp:1264`, 821e1bd), so it is never remapped, and its one-word template has `rd0` = 0; v1.2 remapped it. JAL and JALR also allocate their own throwaway register when rd is x0 (`isa/virtual.cpp:1165–1185`, 821e1bd); after the generic remap that case never arises, and both paths give rd = 40, so one-word remap templates reproduce them. With the decode table's AMO index (17.8.6), every atomic reaches its own template and the offset handles the remap in hardware.

**v1.3: a16z decides two cases differently** (owner, 2026-11-05; DECISIONS-v1.3 section 17; `v1.3/planning/upstream/UPSTREAM-DIFFS.md` D13, D14), and gold follows a16z:
- **D13, the rewrite row isn't virtual:** a16z emits the `ADDI x0, x0, 0` an rd = x0 instruction is rewritten to with no virtual sequence, whatever the source instruction, where jolt-cpp marks it virtual (and first in sequence) unless the source is ADDI. Under a16z's rule `rd0` = 1 and 3 give the same row.
- **D14, LR and SC are remapped:** a16z exempts only ECALL, MRET, EBREAK, CSRRW and CSRRS from the remap, so an LR or SC with rd = x0 has rd remapped to v40 and its temporaries moved up one, like the other side-effecting instructions; jolt-cpp exempts LR/SC (and the inlines, which changes no row of a provable program).

**The hardware is unchanged** (the policy and the offset are data); **the universal tables are not yet**: `data/tables/replayer/` is still built from jolt-cpp `821e1bd`, with `rd0` = 0 on the LR/SC templates and `rd0` = 1 on every NOP-policy template but ADDI's. Neither workload has such an instruction. **Decided (owner, 2026-11-06; item 158): the tables are rebuilt to a16z's rule right after v1.3's freeze, not in v1.3**; it changes no hardware. v1.35 and v1.36 keep v1.3's tables unchanged (item 158). The rest of this subsection describes the tables as they stand.

**What the remap and the exemptions produce** (owner, 2026-10-26; `spec/jolt/replayer-opcodes.md` section 5) *(pending the upstream check, item 113; v1.3: D13, D14 above)*:
- **The remapped link is really written:** a JAL or JALR with rd = x0 leaves its link (pc + `pc_increment`) in v40, which is not an inline temporary and keeps that value across instructions (it is below 48, so the tracer never zeroes it); the row shows rd = 40 and the link.
- **The exempt instructions emit rows that name x0:** `LD x0` (LR.D), `VirtualPextSigned x0` (LR.W) and `XORI x0` (SC.W, SC.D). Register 0 drops the write (17.8.7), and an LD to x0 reports a memory value of 0 (17.8.7, 17.8.11).
- **EBREAK's single row is `JAL v40, 0`** (17.8.19): it allocates v40 itself, and ends the program.

**The temporaries stay in 40–47.** The tracer asserts that no expansion, remap included, allocates beyond register 47 (`isa/virtual.cpp:148–153`), so every correct template keeps 40 + *k* + offset ≤ 47. The replayer doesn't check it *(Proposed, item 36)*.

**Every expansion word, ROM or core-fed** (owner, 2026-10-26, part G, ruling N3). The rd = x0 handling applies to every expansion the same way: the ROM is a cache of templates (17.8.6), so a word means the same whichever path delivers it. Today's core-fed expansions (the inlines and the CSR instructions) happen to be exempt, so their offset is 0, but that describes the current templates, not a rule. The core-fed path follows the same rule with two mechanisms:
- **`rd0` is read from the first word delivered** for an expansion (17.8.4): on a mid-sequence start the core sends words from `EXPANSION_OFFSET` (17.8.13), so it **copies the template's `rd0` into the first word it sends**;
- **under a NOP policy with rd = x0,** the core supplies **the single NOP word** (the shared NOP row, 17.8.6) instead of the template. It knows the instruction word (`EXPANSION_OPCODE`) and the template's policy. **That word carries the template's `rd0` (1 or 3) in bits [47:46]**; otherwise the word is ROM word 1's. So the NOP row's virtual flag comes out as on the ROM path: `rd0` is read from the first word delivered (17.8.4), and the NOP word is the only word delivered (owner, 2026-10-26, part I) *(pending the upstream check, item 113)*.

#### 17.8.19 The tables: the decode index, the builder's rules, the ROM's fill and the core-fed set

*Kept from v1.35's 4.19.5, 4.19.6 and 4.19.9, in v1.36's terms: the contents of the tables 17.8.6 calls data, and the key the core finds a core-fed template by (17.8.13). (Not stated by the RTL's specification; kept from v1.35.)*

**The decode index** (17.8.6). *(Proposed, item 38: the special groups' bit positions; the notes fix only which instruction bits each group uses.)* Index[9:8] = 11 with index[10] = 1 is never formed. The general group's entries for the three special opcodes are never read.

The jolt instruction encodings it must separate (`isa/decoder.cpp`): the inlines are custom-0 with funct7 0x00–0x05 and funct3 0–7 (`isa/decoder.cpp:1210–1249`); custom-2 (0x5B) carries `VirtualRev8W` (funct3 0, funct7 0x05), `VirtualAssertEQ` (funct3 1) and `VirtualHostIO` (funct3 2) (`isa/decoder.cpp:1251–1264`), which the general group separates by funct3; MRET is recognised by its exact encoding (`isa/decoder.cpp:603–606`).

**The tables are universal** (DECISIONS-v1.25 section 5): one decode table, one ROM and one core-fed set, covering **every RV64IMAC instruction and every inline jolt-cpp supports**, built once per jolt-cpp version by the table builder (an offline tool, `build_tables` in jolt-asic-planning PR #2) and good for any guest program. They are in `data/tables/` (`replayer/decode_table.bin`, `rom.bin`, `templates.bin`, `template_index.bin`), with the opcode table and a manifest. v1.2's tables were per program; trimming them to what one program uses was rejected, because a new guest would need the builder rerun before it could run.

**The table builder's rules** (software contract): every entry is written, because the table isn't reset; **an unused entry is class 3, invalid** (DECISIONS-v1.25 section 5, revising v1.2's class for the words the core feeds): with universal tables an unused index is an encoding that is no valid instruction, which Jolt itself rejects, so reaching one is an error, the invalid expansion v1.2 already raises for class 3 (17.8.10), with no new error bit (owner, 2026-11-01). An instruction whose index bits include immediate bits (the U, J and most I formats) has its entry **replicated** across every index those bits can form. **At `821e1bd`** the table has **306 ROM entries, 17 core-fed entries** (the 15 inlines, class 1; CSRRW and CSRRS, class 2) **and 1,725 invalid** [built: `data/tables/replayer/listing.txt`]. Program-level choices are still the tracer's: **the code stream carries whichever instruction word the tracer's bytecode decodes,** and the tables cover that word (owner, 2026-10-27). jolt-cpp builds the bytecode from the raw bytes but rewrites them at decode time (`apply_host_io_encoding`, `host_io_encoding.cpp:79–92`): under the legacy raw-ECALL host-I/O encoding a host-I/O ECALL becomes `VirtualHostIO x0, x0, 0`, and the tracer puts its **canonical word 0x0000205B** in the code stream, not ECALL's 0x00000073. So that word's entry is the custom-2 funct3-2 entry, a one-word `VirtualHostIO` template **with no operands** (all three slots immediate 0, `rd0` = 0; 17.8.18; v1.2's template was under the rd = x0 remap), and ECALL's own entry is unaffected (a ROM template, below).

**The ROM is 8 KiB, 512 × 128-bit words, two-port (17.8.6),** loaded by `WRITE` packets to the replayer's region (17.8.9); one port serves the replayer's reads and the other the writes, which it accepts even while a pass runs. It is a RAM; "ROM" says that replay never writes it. **It is a cache of templates,** filled by the table builder (owner, 2026-10-26): **any instruction may be core-fed** through its decode class (17.8.6), so what the ROM holds is a choice, not a requirement. **The goal** (DECISIONS-v1.25 section 5): every expansion except the inlines and the advice generators is in the ROM, so the core feeds only what is inherently special, and everything executed routinely, the atomics and the system instructions included, comes from the ROM. **The fill at `821e1bd`**, the universal tables (17.8.6) [built: `data/tables/replayer/listing.txt`]:

| content | words |
|---|---|
| ROM words 0 and 1: the **padding** row (word 0: opcode NOP, every operand code 0, `seq_remaining` 0, `hold` 1) and the **shared NOP** row (word 1: `ADDI x0, x0, 0`, i.e. opcode ADDI, `rd` 0, `rs1` 0, `rs2` 128 with `const` 0, `report_c` 1, since it is its instruction's only row) *(Proposed, item 39: their fixed places)* | 2 |
| emulation sequences: the register shifts (12), the sub-word loads (18) and stores (18), the M extension's divisions, remainders, MULH and MULHSU (99) | 147 |
| pass-through instructions, one word each | 42 |
| system instructions: ECALL 7, EBREAK 1, MRET 1 | 9 |
| atomics: LR.D 5, LR.W 7, SC.D 12, SC.W 21, the 18 AMOs 143 (the .W forms 96, the .D forms 47) | 188 |
| **total** | **388 of 512** (92 templates over 306 decode entries) |

v1.2's fill was about 508 of 512, with the atomics partly core-fed; `821e1bd`'s shorter sub-word and 32-bit atomic expansions (17.8.20) leave **124 words free**. **The CSR instructions are the gap against the goal** (owner, 2026-11-01: the spec follows the tables as delivered): CSRRW and CSRRS, 60 templates and 102 words, would fit, but the decode index (17.8.6) gives each of them one entry, while jolt-cpp's expansion depends on the **CSR number** (each CSR is its own virtual register, v34–v39, named literally in the rows) and on the **register pattern** (rd = x0, rs1 = x0, rd = rs1), which changes the row count from 1 to 3. So they stay core-fed, keyed as below, the one deliberate exception to the goal (owner, 2026-11-01; DECISIONS-v1.25 section 6; item 139): CSR instructions are rare in guest code, so feeding them costs almost nothing. rd = x0 remaps need no variant words: the offset rule applies in hardware. **If a later jolt-cpp version's templates don't fit,** the builder reports how many words short it is and which expansions are largest, and the owner decides between a larger ROM and what to feed (DECISIONS-v1.25 section 5).

**Loading.** The decode table and the ROM load by `WRITE` packets to the replayer's region (17.8.9), while the replayer is idle. Their delivery is software's (17.8.13): v1.35 had the master tile broadcast them at boot; with v1.36's boot (6.5) the master tile's firmware broadcasts them once the compute tiles' loads are done, before the first pass.

**Core-fed templates and their numbers** (owner, 2026-10-26, part G; item 114; **the key, v1.25**: DECISIONS-v1.25 section 5 and `data/tables/README.md`). The core-fed set is universal like the rest of the tables (17.8.6): its **template words** (`templates.bin`) and an **index** (`template_index.bin`) of 16-byte entries, each the entry's **key** (bytes 0–3), the template's word offset (4–7) and length in words (8–11), its `rd0` (12) and its decode class (13), with 14–15 spare. **The entries are sorted by key, ascending, and a template's number is its entry's position**, 0–127; numbers may share template words, so the limit of **128** counts index entries, not template storage. A universal set can't list every instruction word, so **the key is the instruction word with its register fields normalized**, and the core computes **key(*w*)** from the presented word *w* (`EXPANSION_OPCODE`) before the lookup. key(*w*) keeps every bit of *w* except:
- **an inline** (class 1, *w*[6:0] = 0001011): rd [11:7], rs1 [19:15] and rs2 [24:20] are **cleared**, one entry per inline. Its template resolves any registers through codes 129–131, x0 and aliasing included;
- **CSRRW or CSRRS** (class 2, *w*[6:0] = 1110011, funct3 1 or 2): rd and rs1 are **renamed by first appearance**: x0 stays 0, a non-zero rd becomes 1, and rs1 becomes 0 if x0, 1 if it equals a non-zero rd or if rd is x0, else 2. The CSR field [31:20] is kept. These five patterns per CSR and instruction are exactly the cases jolt-cpp's expansion distinguishes (rd = x0, rs1 = x0, rd = rs1), which change its row count from 1 to 3 (above).
Operand codes 129–131 still resolve against the presented word, not the key. **A word whose key has no entry is an error:** jolt-cpp rejects it (a CSR it doesn't support), so a traced program has none. **At `821e1bd`** the set has **75 numbers**, the 15 inlines and 60 CSR keys (CSRRW and CSRRS for jolt-cpp's six CSRs, mstatus, mtvec, mscratch, mepc, mcause and mtval, five patterns each), in 13,821 words [built: `data/tables/replayer/listing.txt`]. v1.2 keyed each program's set by the exact instruction word. A tile that keeps only the templates its window uses keeps every word that any of its numbers references (item 115).

**EBREAK's expansion** is one word (owner, 2026-10-26): **JAL, rd = temporary 0 (v40), immediate 0, with `imm0_pc0` and `next_from_value` set**, as the tracer emits it (`JAL v40, 0`, `isa/virtual.cpp:1164–1192`). EBREAK is exempt from the rd = x0 remap, so the offset is 0 and temporary 0 is v40. It ends the program. **v1.25 puts it in the ROM** with ECALL and MRET (DECISIONS-v1.25 section 6; v1.2 fed it from the core) *(pending the upstream check, item 113)*.

**Core-fed expansions** are only **the inlines (custom-0) and the CSR instructions, CSRRW and CSRRS** (DECISIONS-v1.25 section 6: "the controller feeds the inlines, the advice generators and the CSR instructions, and nothing else", the controller being v1.35's name for the core; the advice generators are the Tracer's, v1.3). The CSR instructions are the one deliberate exception to the ROM goal (17.8.6): their expansion depends on which CSR and on the register pattern, which a single decode entry can't see, and they are rare in guest code. **At v1.25** [meas: v1.25's acceptance runs, Quest 083 and 084: meshsim's Stage B on every window of both workloads at 2²⁹, `v1.3/planning/sessions/WORKER11-REPORT.md` section 5]: **Cyclone** averages 6.6 core-fed expansions per window of real rows (1,154 of its 5,670 such windows have any, at most 176), and their stalls average 812 cycles a window, 0.26 % of a pass; **EVM Zone** has 665 in all, in 4 windows (at most 230), 11 stall cycles a window on average. v1.1 measured about 13 per window on Cyclone, about 0.4 % of a pass, with ECALL, EBREAK and MRET still among them [meas: v1.1, the design notes]. **The core-fed set is to reach the tiles by broadcast** (DECISIONS-v1.3 section 25; v1.3; **decided, not built**: no firmware implements it, and until one does every tile keeps the whole set, 17.8.13). The tracer tile holds all of it, since the Tracer can meet any template, and ORs each window's template bitmap (the record's word 67, set by the Tracer for the inlines only, 17.9.4) into the segment's used set. **When the proving stages start**, once the segment is traced, it broadcasts **all 60 CSR templates** (102 words, always) **and the inlines the segment used**, with their index entries, to every tile once; **each tile keeps the CSR templates and the inlines its own record's bitmap names**, with every word any of those numbers references, in the scratchpad (17.8.22). So every tile has its templates before its replayer runs. *(Open, item 115: the broadcast's mechanics. v1.35 proposed a size-first command so that each tile arms its counter; commands are gone (6.5), and nothing replaces it yet.)*

#### 17.8.20 A row: the opcodes and Jolt's behaviour behind them

*Kept from v1.35's 4.19.7, in v1.36's terms: the opcodes of each class, and where 17.8.7's rules come from in Jolt. (Not stated by the RTL's specification; kept from v1.35.)*

**What a row computes is defined in `spec/jolt/replayer-opcodes.md`** (owner, 2026-10-26, part F; item 112), which is **normative** for it (its sections 1–5): for each of the 94 opcodes that occur in Jolt's rows, its slots, the exact rd formula (widths, extension, wraparound, shift masks), its control transfer, its log word and, for the load and store rows, its memory fields, all derived from `execute_instruction` (`trace_owner.cpp:145`) and checked against it with 252 test vectors; **v1.25 regenerates it at `821e1bd`** from the harness outputs of WORKER9 (DECISIONS-v1.25 section 1). 17.8.7 keeps the classes, the schedules and the pc and `instr_idx` rules. A row reads each slot's value as `read_reg_vals` does (a register slot reads its register, an immediate slot carries its value, `trace_owner.cpp:78–83`), with the three departures from the tracer that 17.8.7 lists. Leaving the assertions unevaluated is safe: the tracer aborts on a failed assertion while tracing (`release_assert`, e.g. `trace_owner.cpp:334–351`), so a traced program never contains one that fails. A row whose rs1 or rs2 is also its rd reads the old value (17.8.7), as the tracer does (`trace_owner.cpp:903–908`).

**`VirtualPext` and `VirtualPextSigned` are correct for any mask** (DECISIONS-v1.25 section 1, from v1.3's section 9; revises v1.2's narrowing to contiguous masks, item 43). Jolt's prover proves the general bit-extract the appendix defines (jolt-cpp's PEXT lookup table handles any mask), so a future expansion could use a scattered mask, and the replayer must still compute it: `VirtualPext` gathers the bits of rs1 at rs2's 1-bits into rd's low bits, and `VirtualPextSigned` then sign-extends from the last bit placed; a zero mask gives 0. **How is the implementation's choice**, not the spec's: a single-pass extract network, or an iterative unit taking one run of ones per cycle (the mask's lowest run: two trailing-zero counts, a shifter and a mask generator). **Full speed is promised only for contiguous masks**, which is every mask `821e1bd` produces (each comes from a `VirtualWindowMask…` row, `expand_load`, `isa/virtual.cpp:239–247`, 821e1bd); a row with any other mask may take an implementation-defined number of extra cycles beyond its schedule (17.8.7). **The RTL** extracts a mask of two or more runs a bit a cycle, at most 65 cycles past the schedule (17.8.16), which settles the method (item 140). **The simulator** reports a non-contiguous mask as a statistic, not an error (owner, 2026-11-01); v1.35's timed every mask like any other row, and v1.36's charges the RTL's cycles.

**The memory rows** (DECISIONS-v1.25 section 1; WORKER9's diff of `821e1bd` against `4f36b3d`). jolt-cpp now expands the sub-word accesses and the 32-bit atomics around "load the aligned doubleword, then work on the byte lane":
- **`VirtualLoadB/H/W rd, rs1, imm`** load the doubleword holding the access, like LD: rd = the log word (class L). **`VirtualStoreB/H/W rs1, rs2, imm`** store rs2's doubleword, like SD: the log word is the pre-value and rd is an immediate slot holding the access's immediate. Their address is rs1 + imm with its low bits cleared for the access's width (`memory_address`, `isa/riscv.hpp:241–253`, 821e1bd): the doubleword's address for every access the tracer lets through.
- **A sub-word load** is three rows: the narrow load into a temporary, `VirtualWindowMask…` of (rs1, imm), and `VirtualPext` or `VirtualPextSigned`. **A sub-word store** is six: the narrow load, the window mask, `ANDN` (clear the lane), `VirtualShiftData…` (the new value moved into the lane), `ADD`, and the narrow store. LR.W, SC.W and the 32-bit AMOs use the same rows on a word lane.
- **`VirtualShiftDataB/H/W rd, rs1, rs2`**: rs1 is the value and **rs2 the access's base register**, the lane coming from rs2 + `shift_offset` (17.8.4); v1.2's rs2 was the computed address.
- **A misaligned access is no longer a trace row:** jolt-cpp aborts on one (`checked_memory_address`, `isa/riscv.hpp:257`, 821e1bd), so `VirtualAlignAddr` and the alignment assertions are gone and no trace contains a misaligned memory row, LD and SD included. The replayer doesn't check; meshsim flags one (invariant 16; DECISIONS-v1.25 section 7, Q4), and the row still replays with the address the formula gives.
- **`VirtualHostIO`** has no operands: it reads no register and writes none.

**The opcodes of each class.** Of the tracer's 171 opcodes at `821e1bd`, 94 occur in Jolt's rows (NOP among them, in padding); 74 occur only as source instructions, which their templates replace, and 3 never occur (`VirtualMove` 130, `VirtualAdviceRemU` 164, `VirtualAdviceRemUW` 168) [derived: the appendix's harness at `821e1bd`; `data/tables/opcode_table.txt`]. Codes are the tracer's at `821e1bd` (`isa/riscv.inc`). **They are renumbered from v1.2's**, and the ALU decodes them: six opcodes are inserted at 110–115 and three removed, so v1.2's codes 0–109 are unchanged, 110–111 move up by 6, 114–153 by 4 and 155–167 by 3, and 112, 113 and 154 are gone. **61 row opcodes change** (52 renumbered, 3 removed, 6 new); `spec/jolt/replayer-opcodes.md` section 2.0 lists every one:

| class | cycles | opcodes (code) |
|---|---|---|
| **L**, log | 3 | the load rows LD 54 and `VirtualLoadB/H/W` 110–112 (rd = the log word); the store rows SD 53 and `VirtualStoreB/H/W` 113–115 (the log word is the memory pre-value; rd is an immediate slot); `VirtualAdvice` 126 (rd = the log word), and the DIV/REM advice opcodes `VirtualAdviceDivU` 163, `VirtualAdviceDiv` 165, `VirtualAdviceRem` 166, `VirtualAdviceDivUW` 167, `VirtualAdviceDivW` 169, `VirtualAdviceRemW` 170, which the templates write as `VirtualAdvice` (below) |
| **M**, multiply | 5 | MUL 43 and `VirtualMULI` 118 (low 64 bits of the product), MULHU 45 (high 64 bits, unsigned), MULW 65 and `VirtualMULIW` 119 (low 32 bits, sign-extended) |
| **S**, simple | 3 | every other row opcode: NOP 0, ADD 1, ADDI 2, SUB 3, XOR 4, XORI 5, OR 6, ORI 7, AND 8, ANDI 9, SLT 16, SLTI 17, SLTU 18, SLTIU 19, BEQ 28, BNE 29, BLT 30, BGE 31, BLTU 32, BGEU 33, JAL 34, JALR 35, AUIPC 36, LUI 37, FENCE 52, SUBW 62, ADDW 63, ADDIW 64, and the virtual opcodes 107–109, 116, 117, 120–125, 127–129, 131–162 |
| source only | — | the shifts SLL–SRAI 10–15 and SLLW–SRAIW 56–61, the sub-word loads and stores 20–27 and LWU 55, ECALL–MRET 38–42, MULH 44, MULHSU 46, DIV–REMU 47–50, UNIMP 51, DIVW–REMUW 66–69, LR/SC and the AMOs 70–91, the inlines 92–106 |

*(Proposed, item 37: the class of every opcode; the notes settle only the principle.)* The class L rows are exactly those whose word has `from_log` set (17.8.4): the flag, not the opcode, decides that a row takes a log word. The replayer **executes 88 opcodes** (v1.2: 85): the S and M classes and the nine executed class L opcodes, the eight memory opcodes and `VirtualAdvice` (`data/tables/opcode_table.txt` marks them S, M and L). A word with any other opcode is an invalid expansion *(Proposed, item 36)*.

**Advice rows are data** *(Proposed, item 56)*. Jolt reports every advice row as `VirtualAdvice` with rs1, rs2 and the immediate all 0 (`trace_owner.cpp:912–917`), though the tracer's DIV/REM advice rows name real source registers (`isa/virtual.cpp:331`, for example) so that its interpreter can compute them. The replayer takes the value from the log, so the template builder writes those rows as `VirtualAdvice rd, x0, x0` with `from_log` set: the row then comes out exactly as Jolt reports it, with no masking logic. (The inlines' own advice rows are already `VirtualAdvice` with two zero immediates, `isa/virtual.cpp:216–218`, and are recorded the same way.)

**The multiplier needs no signed high half.** MULH and MULHSU never reach a row: the tracer expands them through MULHU and MUL (`isa/virtual.cpp:485–503`). So the multiplier produces the low product, the unsigned high product and the low 32 bits sign-extended; the sign corrections the notes provide for MULH are not needed *(Proposed, item 37)*. `VirtualAssertMulUNoOverflow` multiplies only to check, so it is class S.

The multiplier is four 16 × 64 multipliers in parallel, a full 64 × 64 array, which is cheap in area. There is no divider: Jolt's DIV and REM use advice plus checks, and the advice comes from the log.

**The edge cases** (AUIPC's use of the source instruction's pc, JAL and JALR linking pc + `pc_increment`, `VirtualHostIO` reading and writing nothing, `VirtualShiftData…`'s lane, `VirtualROTRIW`'s min(32, tz(imm)), the shift masks, MULHU, the advice values) are in `spec/jolt/replayer-opcodes.md`. **Extension is per opcode:** several word forms zero-extend rather than sign-extend (`VirtualPow2W`, `VirtualShiftRightBitmaskW`, `VirtualROTRIW` and the `VirtualXORROTW…` forms among them; the appendix's flag F5 lists them), so the appendix's rd formula, not the opcode's name, decides.

**Zero shift masks** (owner, 2026-10-26). The masks reaching `VirtualSRL`, `VirtualSRLI`, `VirtualSRA` and `VirtualSRAI` are **never 0**: each comes from `VirtualShiftRightBitmask` or from the expansion's 2⁶⁴ − 2^shamt, and the tracer guarantees it. The shift is tz(mask), and **the result for a zero mask (tz = 64) is undefined**, as it is in the tracer's C++; meshsim flags one (invariant 16).

**A load row's memory fields** (owner, 2026-10-26) *(pending the upstream check, item 113)*. A load row (LD or `VirtualLoad…`) forms its memory value (pre = post) from **the value written to rd**, not from the log word, as the tracer does (`trace_owner.cpp:909–912`, 821e1bd). So an LD to x0, which only LR.D with rd = x0 produces (17.8.18), consumes its read-log word but reports a memory value of 0 (17.8.11).

**pc and `instr_idx`: Jolt's behaviour behind 17.8.7's rules.** pc is computed, never stored per row; `instr_idx` increments by 1 on every row that continues sequentially, because the committed bytecode lays each expansion out contiguously in program order (`isa/virtual.cpp:1405–1420`).
- **JALR**'s target is (rs1 + imm) & ~1 (`trace_owner.cpp:287`); MRET and traps are JALR rows through mepc (v36) or mtvec (v34) (`isa/virtual.hpp:36–38`; `isa/virtual.cpp:1098–1162`).
- **A JAL with immediate 0** goes to pc 0, Jolt's program-end convention (`trace_owner.cpp:281`). The template builder sets `imm0_pc0` in **every JAL word, ROM or core-fed** (owner, 2026-10-26, item 56), so the replayer ends the program wherever the tracer does.
- **A new pc of 0 ends the program, from any row,** checked before the other rules (`trace_owner.cpp:117–119`). Besides a JAL with imm 0, a JALR whose target is 0 or a branch whose pc + imm wraps to 0 also ends it (owner, 2026-10-26, item 57) *(pending the upstream check, item 113)*. **From v1.3 no trace holds such a JALR or branch:** a16z fails there, and the Tracer stops with an error (17.9.4, 17.9.9; DECISIONS-v1.3 section 25), so the rule stays and is never exercised.
- **A jump to its own pc is sequential** *(pending the upstream check, item 113)*: Jolt's `finish_instruction` transfers control only when the new pc differs from pc (`trace_owner.cpp:117–124`), so a taken branch or a jump whose target is its own pc advances past it, with no log word.
- **Only an expansion's last row sets a new pc** (17.8.7's table). v1.35's simulator did not hold a row that is not its expansion's last to this, in the replayer or in the Tracer; no shipped table has such a row. v1.36's simulator follows 17.8.7.

**Hardwired Jolt behaviour** *(Proposed, item 57)* is 17.8.7's "Jolt's conventions in the hardware": four conventions, which can't be table data because they depend on values known only while the row runs. In Jolt: the self-jump compare is `finish_instruction`'s (`trace_owner.cpp:117–124`) *(pending the upstream check, item 113)*; register 0 reads 0 and drops writes as the tracer's `save_rd` does (`trace_owner.cpp:85–93`); the memory fields are formed for jolt-cpp's `is_load` and `is_store` rows only, LD, SD and the six narrow opcodes (`trace_owner.cpp:904–913`, 821e1bd), and JALR clears bit 0 of its target (`trace_owner.cpp:287`); a new pc of 0 ends the program from any row *(pending the upstream check, item 113)*.

**The row's fields in Jolt** (17.8.11). The row carries what Jolt's `RiscCycle` does (jolt-cpp's port: `trace_owner.hpp:19–94`), plus `instr_idx`, which Jolt's provers take from a separate column (`trace_columns.inc:53`), and less the memory value after, which the consumer derives (an LD's is rd's value after the row, an SD's is rs2's value; `trace_owner.hpp:230–235`). The values are those jolt-cpp's `expand_compressed_chunk` records (`trace_owner.cpp:893–927`), checked against gold and a16z as the ground truth says (17.8.15, item 113). The layout is ours, not `RiscCycle`'s field order, and its byte order is the owner's ruling N11 (2026-10-26). `imm` is Jolt's raw `_imm`, which a consumer extends to 128 bits by `imm_signed` (DECISIONS-v1.25 section 7, Q1; v1.2's consumers normalized it by opcode); `mem_addr` is `memory_address` (`isa/riscv.hpp:241–253`, 821e1bd), the doubleword's address for every access the tracer lets through; a padding row is all zeros, as Jolt's padded rows are.

The ALU's per-opcode arithmetic is hardwired too, by nature: a new Jolt opcode needs new logic. Which rows can transfer control is explicit, not inferred from the row opcode: `next_from_value` is set on branches, JAL and JALR, ECALL's and MRET's JALR rows and EBREAK's JAL, all in the ROM at `821e1bd` (17.8.6), and on any core-fed row that can transfer control. No pc-to-index table is needed.

#### 17.8.21 The core's loop

*Kept from v1.35's 4.19.8, 4.19.9 and 4.19.10, rewritten for v1.36's registers and events: software's, for a replay controller's firmware to follow (meshsim's replay controller is `fw/replay/replay_ctl.c`; the RTL's, its `replay.rs`). v1.35's loop waited for two events at once with `EVENTS_REQUIRED`, which is gone (7.2): this one loops until it has both. (Not stated by the RTL's specification; kept from v1.35.)*

**The loop.** Every `WFI` has exactly the sources its mask allows. `rp` is the replayer's window, peripheral 1's; `QUEUES` its credit bits that have something left to send; counter 4, say, the counter the loop counts its refills on; and counter *n* the one that counts the record's reload. A pass runs inside a phase, so no release can come during it (7.3), and `release` is left out of the tile's mask while the loop runs; the program puts it back before it arrives (22.10). v1.35's loop kept GO in the mask throughout.

```
    rp->RUN_CONFIG    = 65536 | store_log | rd_prev    ; the pass's rows and flags: held, so once for passes alike
    tile->COUNTER_n   = 66; send the record's words 0-65, state word first   ; verbatim: every pass
    wait for counter_n_done                            ; landed (12.3); the state write reset the pass (17.8.8)
    rp->EVENTS_MASK   = done | new_expansion | QUEUES  ; write_log_credit only with the store log
    tile->EVENTS_MASK = peripheral_1                   ; only the replayer can wake the core
    rp->START         = 1                              ; starts the pass; the credits are already 2
loop:
    WFI                                                ; the tile's live bit for the replayer
    ev = rp->EVENTS & rp->EVENTS_MASK                  ; consumes: one credit from each set queue bit
    if ev == 0: goto loop                              ; the tile's bit may lag a consuming load: re-check (17.2)
    if ev.done: exit
    if ev.new_expansion: find key(rp->EXPANSION_OPCODE) (17.8.19), core-fed templates only,
                         its words from rp->EXPANSION_OFFSET; add expansion_credit to rp->EVENTS_MASK
    for each credit taken: queue a refill of min(half, what that stream has left)   ; its words, from the stream word
    for each stream with nothing left: take its bit out of rp->EVENTS_MASK
    tile->COUNTER_4 = packets; store the STREAM entries; tile->DMA_START = 1
    tile->EVENTS_MASK = counter_4_done | dma_done      ; squelch: only the refill's counter and its transfer wake it
    got = 0
    repeat: WFI; got |= tile->EVENTS & tile->EVENTS_MASK   ; a load consumes what it returns
    until got == (counter_4_done | dma_done)           ; both: its packets have landed, and its tables are read (8.6)
    tile->EVENTS_MASK = peripheral_1                   ; credits returned meanwhile fall through
    goto loop
```

- **The squelch** is a mask setting: while the transfer streams, the replayer's credits accumulate, and only the refill counter's `counter_4_done` and the transfer's `dma_done` can wake the core. Once it has both, the next iteration may rewrite the `STREAM` entries (8.6; as v1.35's recipe 26 did). The counter counts each packet `LAND_LATENCY` cycles after the director accepts it, by which time it is in its queue (12.3, 17.8.9), so the inner loop ends when every word has landed.
- **Waiting on the refill counter's own event, not `all_counters_done`** (owner, 2026-10-26), means a fetch from LPDDR outstanding on another counter (17.8.22) doesn't hold the refill up.
- **Credits returned during the transfer** stay counted and, the tile's `peripheral_1` being live, make the next `WFI` fall through once the mask is restored.
- **No other tile writes the replayer's region during a pass** (17.8.13), so the refill counter counts only the loop's own refills.
- **A credit is permission to send up to half a queue** (owner, 2026-10-28), not one transfer: the program may split the refill into smaller moves (a 2 KiB credit as two 1 KiB moves), as long as their total stays within the credit. Up to two refills can be outstanding, which keeps a draining queue fed, and the tile's live bit stays high while any masked credit remains, so `WFI` can't miss a refill. A queue that empties simply stalls the replayer until data lands. Every refill of a code or log queue except its stream's last is exactly half a queue (17.8.13), so their credits balance; only the expansion queue sees short refills, and its credit's fill at each new expansion (17.8.8) keeps a short one from stranding a credit.

**Data from LPDDR** (17.8.22) never goes straight into a queue (owner, 2026-10-26; 17.8.13): the overflow is fetched into the scratchpad and refilled locally like resident data, so every refill is a local transfer and the loop is the same for all of them.

**What a stream has left** is counted in 128-bit entries, from the stream's `…_words` in the stream word (17.8.2): every refill but the last is a half, 128 entries, and the last is what remains. The write log's count is used only with the store log.

**Between passes.** The loop exits only at the top, after its last refill transfer has landed (its counter's event consumed), so every queue push has landed when the next pass's reload writes the state word and resets the pass (17.8.8, 17.8.13). The loop as written starts the pass before its first refills; a loop may instead take the two credits and push the first refills between the reload and `START`: a push after the state write is kept (17.8.8; preloading, DECISIONS-v1.26 item 11).

**Short transfers, not interruption.** The address generator is not interruptible. Instead every transfer is short: a 1 KiB piece is 64 packets, about 64 cycles, so nothing waits long for the DMA unit, and the core re-checks the queues after each. Streaming occupies about 11–20 % of a pass [estimate]. Group transfers (the sort's all-to-alls) stay exclusive and don't coincide with replay.

**How a core reloads the record** (owner, 2026-10-26, recommended practice; before every pass, 17.8.13). The record is the working state (17.8.2), so before every pass the core reloads its words 0–65, state word first, with the replayer idle (`STATUS.busy` 0), counted on a counter *n*, and stores to `START` once `counter_n_done` says every word has landed:
- **From the scratchpad** (the usual way): a stream transfer (8.3) from the tile's copy of the record (17.8.22), starting at the record's byte 0, to region 0x04 offset 0: 66 packets (or all 68: words 66 and 67 are accepted and ignored, 17.8.2), about 66 cycles. The words go verbatim: the pass's row count and flags are in `RUN_CONFIG` (17.8.1), so nothing in the record changes for a pass.
- **From DMEM**, through the write unit (9): `WRITE_HDR` once (`dst` its own tile id, mode `WRITE`, counter *n*) and `WRITE_DEST_ADDR` = 0x0400_0000; a store of the number of 128-bit words to `COUNTER_n`; per word `WRITE_W0`–`W2` then `WRITE_SEND_128`, the address advancing by 16 each send. The 66 words are about 264 stores (worked example in `fw/REGISTERS.md`).

**The stalls this loop costs on the RTL's chip** [meas: RTL]: a core-fed expansion's first row waits 76.1 cycles on average on EVM Zone and 94.4 on Cyclone for the firmware's answer, against 4 under an ideal controller. A pass on the chip is 3.0875 cycles a row on EVM Zone and 3.1249 on Cyclone (17.8.16).

#### 17.8.22 The resident replay region (the software contract)

*Kept from v1.35's 4.19.13, in v1.36's terms. (Not stated by the RTL's specification; kept from v1.35.)*

The trace stays in the scratchpad for the whole proof, in a **replay region** per tile, a software-managed cache. **Residency is best effort** (owner, 2026-10-26): each stage keeps as much of a window's streams resident as its free scratchpad allows, and the rest streams from LPDDR through a **staging buffer in the scratchpad** and is refilled locally. No fixed size is guaranteed; normal programs fit, and adversarial ones (dense windows) stream, which the host prices. The hardware knows nothing of it: it sees only the packets the core's streams and the memory system send to its region. This subsection is the contract software keeps so that the hardware works.

**Layout.**
1. **This window's templates first:** only the core-fed templates its 64K rows actually use (often none; a window of BLAKE3 calls needs about 20 KiB [estimate]). The Tracer lists the inlines in the record's word 67 (17.9.4); the tile keeps those and the 60 CSR templates from the tracer tile's broadcast (17.8.19; decided, not built: until it is, the tile keeps the whole set, 17.8.13).
2. **Then code and the two logs** as 4 KiB blocks in consumption order, code, read-log and write-log blocks interleaved, so "resident" is a single boundary. A pass that needs only one log keeps and fetches only that one.
3. **The rest,** beyond what the stage's free scratchpad holds (with a 512 KiB region, at most about 190 KiB in the densest windows [meas: v1.1, the design notes]), stays in LPDDR and is **fetched into the scratchpad by `READ` requests to the memory tile** (11.5; 128-bit `WRITE` packets into region 0x03, 4 KiB a request, counted on a counter of their own) ahead of the replayer, then **refilled locally like resident data** (owner, 2026-10-26). The direct read from LPDDR into a queue's part is no longer part of the replayer's contract. The loop's refill waits on its refill counter's done alone (17.8.21), so a fetch in flight on another counter never holds a refill up (owner, 2026-10-26, item 94). Before refilling from a fetched block, the core waits for that fetch's counter.

The median window's trace is about 335 KiB and the densest about 700 KiB [meas: v1.1, the design notes]. At the stages whose scratchpad margin is too small, the core shrinks or evicts the region and reloads it afterwards (at 512 KiB per tile, 32 MiB per chip, about 2 ms [estimate]). The same mechanism can spill compact, expensive-to-rebuild intermediates; anything the trace can regenerate is better replayed (about 0.2 ms per pass) than spilled. In sumcheck tails the core can prefetch the next pass's streams from LPDDR (about 21 MiB per chip, about 1.3 ms at 16–17 GB/s [estimate]), into banks the remaining rounds don't use, with the master tile pacing the fill so it never fans in.

**The rules software keeps:**
- **Stream to a queue's part from its base** (17.8.9), in 128-bit packets, with transfers of at most a half; a refill is min(half, entries remaining) (17.8.8). Local refills may have two outstanding.
- **No LPDDR read targets a queue** (part C): fetches go into the staging buffer, and the loop refills from there.
- **No other tile writes the replayer's region during a pass,** and a queue's part takes one source at a time (invariant 18; 17.3, 14.4): the region may be written from any source (17.3), but the core's loop counts only its own refills (17.8.21).
- **Load the decode table and the ROM while the replayer is idle** (17.8.9), and **reload the record's words 0–65 before every pass** (17.8.9): part 0 is the working state, and the state word's write resets the pass (17.8.8). The tile keeps each window's record in the scratchpad for this. Pushes after the state write are kept, so the first refills may precede `START` (preloading).
- **Before the state write, every queue push has landed** (owner, 2026-10-23; v1.26: was before a start): the refill counter's done for the core's refill transfers (17.8.21). The rule is software's (invariant 16), kept by waiting on those counters before writing part 0: the replayer can't see a push in flight, since every packet reaches it through one in-order landing path (meshsim doesn't check it).
- **Every refill except a stream's last is exactly half a queue,** and a credit's refill never exceeds half (17.8.8).

**Code is not in the scratchpad.** Each tile's core has its own IMEM and DMEM, and each stage's image is loaded into them by the boot ROM's load (6.5), so the earlier plan's 256 KiB code reservation in the scratchpad is reclaimed for data.

#### 17.8.23 Cost and implementation notes

*Kept from v1.35's 4.19.14, 4.19.15 and 4.19.10, in v1.36's terms. Estimates, not contract; the RTL's measured areas are its own (Appendix A of `spec/machine-spec.md`). (Not stated by the RTL's specification; kept from v1.35.)*

**Memory per tile** (v1.26): 4 + 4 + 4 + 4 KiB of queues (code, expansion, read log, write log; v1.25's logs were 8 KiB each), an 8 KiB ROM, a 4 KiB decode table, 1 KiB of register file, which is the register image (17.8.7; v1.25 had a separate 1 KiB image beside it), 32 B of state and stream words (words 66–67 aren't stored), about 2.5 KiB of output queue (32 × 80 B) and 320 B of decoder tables (17.8.12): **about 32 KiB** [estimate] (v1.25: about 41 KiB). Across 64 compute tiles that is about 2.0 MiB (v1.25: 2.6 MiB), roughly 6–8 mm² per chip with the logic [estimate, v1.25's 7–10 mm² scaled by the memory], against 48 MiB for the 768 KiB-per-tile design planned earlier.

**Logic per tile:** the ALU (with `VirtualPext` for any mask, 17.8.7) and comparator, the 128 × 64-bit true dual-port register file, two adders (memory address, pc + imm), the operand resolver, the decode index logic, four queue readers and the credit counters, three decoders with their size counters (17.8.12): about 0.09–0.14 mm² with its memories [estimate; v1.25's 0.1–0.15 with 8 KiB more of queues]. Chapter 24 adds it to the tile's budget; its placement is in chapter 25.

**The decoders' cost:** three tables, 16 × 64 bits for each log and 16 × 32 for code, whose values are 32-bit (about 320 B of registers; v1.35 counted 16 × 64 for code too, 384 B; the write log's E[15] is its last-load register, with a mux from the read-log path), three bit unpackers and three sets of size counters, the entries taken, the values decoded and the entries in (17.8.12).

**Implementation note: the decode table's organization** (v1.25; a note, not a requirement). The decode table is logically 2,048 × 16 bits (17.8.6), but it is naturally **built as 256 × 128 bits, one row per 128-bit packet**: entry *e* lives in row *e* >> 3, bits [16·(*e* & 7) + 15 : 16·(*e* & 7)], exactly as 17.8.9's map places it. A write is then **one row in one cycle**, which keeps the write port taking a packet per cycle and landing it within `PERIPHERAL_LATENCY`, and a lookup reads row *e* >> 3 and selects 16 bits by *e* & 7. A 16-bit-wide build would need eight banks, one per *e* & 7, to take a packet per cycle. The implementation may organize it differently, as long as it takes a packet per cycle.

#### 17.8.24 The simulator's checks and the parameter summary

*Kept from v1.35's sections 9 and 10, in v1.36's terms: the replayer's part of invariant 16 (chapter 20) and its row of the parameter summary (19.1). (Not stated by the RTL's specification; kept from v1.35.)*

**The replayer's part of invariant 16.** UB that the simulator stops on, unless marked:
- no write to its region at an unmapped offset or of a size other than 128 bits (17.8.9, 17.3), and none to the record's words or the decode table while a pass runs, words 66–67 included (17.8.9);
- no store to `RUN_CONFIG` while a pass runs (17.8.1);
- no `START` while a pass runs (17.2), and none with no state write since the reset or since the previous pass's `START` (17.8.10): the record's words 0–65 are reloaded before every pass (17.8.13);
- no expansion word in the expansion queue at a new expansion (17.8.8);
- no reserved bit set in an expansion word, no `from_log` with `next_from_value`, and `offset_imm` only on `VirtualShiftData…` rows (17.8.4);
- no code miss with size bit 1 (17.8.12);
- no row without `next_from_value` whose new pc differs from its pc (17.8.7);
- no zero mask reaching `VirtualSRL`, `VirtualSRLI`, `VirtualSRA` or `VirtualSRAI` (17.8.7);
- no ROM word written in the cycle a row reads it (17.8.9);
- every queue push, whatever its source, has landed before the state write (17.8.13; the simulator can't see a push in flight and doesn't check it);
- no other tile writes its region during a pass, and one source at a time feeds a queue's part (17.8.13; invariant 18);
- every refill except a stream's last is exactly half a queue, and no refill exceeds the credits taken for it (17.8.13);
- no memory tile answer targets a replayer queue: LPDDR's data goes through the scratchpad (17.8.13, 17.8.22);
- **checked by the hardware,** its `ERRORS` (17.8.1): no `queue_overrun`, no `invalid_expansion`, no stream's underflow and no `leftover`. Every code value and log word a pass is given is consumed in order, and a pass consumes exactly its window's streams. The sizes are checked in hardware because a record and its streams that disagree, or a refill that sent the wrong words, depend on the run's data, which simulation of the firmware may not reach: a starved pass becomes a named error at once, not a hang left to the master tile's watchdog (DECISIONS-v1.3 section 24). The owner expects an invalid expansion never to happen, since the Tracer catches bad expansions;
- **counted, and no UB:** no memory row is misaligned (17.8.7, 17.8.20; jolt-cpp aborts on one, so no trace has it; the row still replays);
- **reported, as a statistic, and no error:** a non-contiguous mask reaching `VirtualPext` or `VirtualPextSigned` (17.8.7), which is correct;
- **the rows:** the replayer's rows equal gold's and a16z's (the ground truth, 17.8.15; jolt-cpp's at the pin wherever they agree; the validation harness, item 58; the vectors of `spec/jolt/replayer-opcodes.md` section 9; item 113).

**Its row of the parameter summary:**

| parameter | value |
|---|---|
| the replayer (17.8), peripheral type 4 | peripheral 1 on every compute tile; tables built at jolt-cpp `821e1bd`, the pin `88dca85`, checked identical (17.8.15); universal tables (ROM 388 of 512 words used; decode table 306 ROM, 17 core-fed and 1,725 invalid entries; 75 core-fed template numbers); 88 executed row opcodes; a pass of `RUN_CONFIG.rows` rows, typically 65,536 (words 32–35 are the Tracer's too); code, expansion, read-log and write-log queues of `REPLAY_QUEUE_ENTRIES` = 256 × 128 bits each, two credits of half a queue each, reset with the size counters and the decoders by the state write; a 68-word record, whose words 0–65 are the working state itself; per stream, 16-bit word and 17-bit value counts (the stream word's advice field ignored); three LRU decoders, 16 × 64 bits for each log and 16 × 32 for code; ROM 512 × 128 bits; decode table 2,048 × 16 bits; register file 128 × 64 bits; output queue `REPLAY_OUTPUT_ROWS` = 32 rows of 640 bits; rows of 3 or 5 cycles; no start copy (the record is reloaded before each pass, 66 packets); about 32 KiB of memory per tile |

## Open items (chapter 26's numbering)

Chapter 26 of `spec/machine-spec.md` indexes these with the platform's; the settled items' record is `spec/history/machine-spec-v1.3-section-13.md`. An item the RTL (Snarky One) builds as proposed says so: the owner confirms it.

| item | what is to be settled | section | status |
|---|---|---|---|
| 137 | Re-cite the `4f36b3d` file:line references at the pinned commit, with item 158, when the appendix is regenerated | 17.8.15 | open (owner, 2026-11-12) |
| 59 | Decided: the row's layout, and the replayer's side of the row port, `replay_row`: valid/ready, a row a cycle (3, 17.8.11). Open: the consumer's side, its width and rate, the placement against the materializer | 17.8.11 | decided, part open |
| 113 | Decided: the ground truth (a16z, `7189216`); open: each case tagged "pending the upstream check", until gold settles it | 17.8.15, 17.8.17, 17.8.18, 17.8.20 | decided, part open |
| 140 | The PEXT method: closed by the RTL. A contiguous mask keeps the schedule; a mask of two or more runs takes a cycle a mask bit, at most 65 past the schedule (17.8.16). The contract stays "as many as the implementation's" (17.8.7) | 17.8.7, 17.8.20 | closed (v1.36) |
| 123 | The replayer's event and error bits: closed. `ERRORS` is packed from bit 0, v1.35's bits one lower, its reserved [0] dropped; the events are v1.35's (17.8.1) | 17.8.1 | closed (v1.36) |
| 35 | The `virt` bit stored per word; the NOP row's virtual flag (pending the upstream check, 113) | 17.8.4, 17.8.5, 17.8.18 | proposed; built as proposed in the RTL |
| 38 | The decode index's special groups (AMO, custom-0, SYSTEM) | 17.8.6 | proposed; built as proposed in the RTL |
| 39 | The padding and shared NOP rows at ROM words 0 and 1 | 17.8.6 | proposed; built as proposed in the RTL |
| 42 | Fetch and decode overlap the previous row | 17.8.7 | proposed; built as proposed in the RTL |
| 56 | Jolt's conventions kept as table data | 17.8.4, 17.8.7, 17.8.18 | proposed; built as proposed in the RTL |
| 57 | The replayer's hardwired Jolt behaviour, four conventions (two rules pending the upstream check, 113) | 17.8.7, 17.8.20 | proposed; built as proposed in the RTL |
| 36 | Decided: what the replayer executes; still proposed: it doesn't check that temporaries stay in 40–47 | 17.8.18, 17.8.20 | decided, part proposed; built so in the RTL |
| 115 | Decided: the tracer tile broadcasts the used core-fed set. Not built, in the RTL's firmware or v1.35's: every tile keeps the whole set, 216 KiB (17.8.13). Open: the broadcast's mechanics, v1.35's size-first command having gone with the commands | 17.8.13, 17.8.19, 17.8.22 | decided, not built; part open |
| 158 | Rebuild the universal tables to a16z's rule for LR/SC with rd = x0 and the NOP rows' virtual flag; v1.36 keeps v1.3's tables unchanged | 17.8.18 | decided, not yet done (owner, 2026-11-12) |
| 139 | Possible later addition: a SYSTEM-group decode putting the CSR templates in the ROM | 17.8.19 | decided, part later |
