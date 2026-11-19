# The Tracer: machine specification, v1.37

*Section 17.9 of the machine specification (`spec/machine-spec.md`), which keeps a stub there. Section numbers are
the machine specification's. 17.9 and 17.9.1–17.9.12 are the hardware, as the RTL (Snarky One) builds it: its
specification's text, under its numbers, corrected in the three places where the RTL's sources differ from it
(`spec/CHANGES-v1.35-to-v1.36.md`, section 9.1). 17.9.13–17.9.18 are what v1.35's 4.21 has beyond that, kept in v1.36's
terms: the overview, the simulator's model of the memory system, the core's loop, the reasons, the timing and cost,
and the simulator's checks. v1.35's own text is `spec/history/tracer-v1.35.md`. **v1.37** changes no hardware of
the Tracer's: the other side of its `fill` port is now the cache (23.6), so 17.9.14 is a stub, and the master tile
delivers its segment in its load (17.9.10, 17.9.15; 30.5).*

### 17.9 The Tracer, the tracer tile's peripheral

The Tracer is the tracer tile's peripheral 1, type 3, version 0. It executes the
**guest**, an RV64IMAC program, as the rows of Jolt's expanded trace, the
replayer's rows (17.8.7), and writes for each trace window the record (17.8.2)
and the three streams (17.8.3) from which one compute tile's replayer
regenerates the window. Its work is a **segment**: from a state write (17.9.3),
the **runs** that `START` starts, each tracing `RUN_CONFIG.rows` rows, 65,536
for a window, and draining their output through its DMA (17.9.6). The guest's
memory is a file, which it reads and writes by lines over its fill port
(17.9.5).

This section replaces v1.35's 4.21 and v1.35's 8.16 (`spec/history/tracer-v1.35.md`).
- **`PARAMS`:** type 3, version 0, and no constants, [31:16] 0.
- **Its DMA** (17.4) sends its output queues 0–3 (17.9.6). Its packets enter the
  tracer tile's router on a link input of its own (16.4).
- **The reference.** Its records and streams equal, bit for bit, those of gold,
  the Java emulator of Jolt's tracer (`pipeline/gold/`), whose ground truth is
  a16z's Jolt at `7189216` (items 113 and 154), and those of v1.35's Tracer.
  Its rows are the replayer's, so a window's streams replay on a compute tile
  to the rows the Tracer ran.
- **Normative beside this section**: 17.8's files
  (`spec/jolt/replayer-opcodes.md` sections 1–5, its section 2 H the assertions,
  and `data/tables/opcode_table.txt`), and, for the RVC expansion, a16z's
  `crates/jolt-riscv/src/uncompress.rs` at `7189216`, which gold's
  `pipeline/gold/src/gold/Decoder.java` ports (17.9.4).

| Part | Size | Holds | Section |
| --- | --- | --- | --- |
| record words | 4 × 128 bits | the state word, the stream word, the files word, the template bitmap | 17.9.2 |
| register file | 128 × 64 bits, 1 KiB, true dual-port | the 128 registers, the record's register image | 17.9.4 |
| decode table | 2,048 × 16 bits, 4 KiB | as the replayer's (17.8.6) | 17.9.4 |
| ROM | 512 × 128 bits, 8 KiB, single-port | as the replayer's, written only while idle | 17.9.4 |
| read queue | 256 × 128 bits, 4 KiB | the segment's advice, two values an entry | 17.9.3 |
| expansion queue | 256 × 128 bits, 4 KiB | the expansion words the core feeds | 17.9.4 |
| instruction cache, data cache | 128 lines of 128 bytes, 16 KiB each, fully associative | the guest's memory, by lines | 17.9.5 |
| jump LRU | 64 × (32 + 32 bits) | jump targets' keys and bases | 17.9.4 |
| code FIFO | 65,536 × 32 bits, 256 KiB | a run's code values | 17.9.6 |
| log FIFO | 65,536 × 66 bits, 528 KiB | a run's log words, each with its tag | 17.9.6 |
| encoders | two sets of three, each a table of 16 × 64 bits, 16 × 32 for code; a stream's two may share one | set 1 sizes the streams, set 2 codes them | 17.9.6 |
| output queues 1–3 | 256 × 128 bits, 4 KiB each | the code stream, the read log, the write log, packed | 17.9.6 |
| ALU | the replayer's, with the assertions' checks | each row's values | 17.9.4 |
| address checks | a 64-bit subtracter and comparator | the range and alignment of every memory row | 17.9.5 |
| map probe | a 32 × 32 multiplier and 16 comparators of 32 bits | a jump target's base, from the map | 17.9.4 |
| statistics counters | 8 × 32 bits | bring-up's counts | 17.9.8 |

Its SRAMs total about 849 KiB, 784 KiB of them the FIFOs.

#### 17.9.1 Its words and bits

Its words, at offsets from its window's base, 0xFFFF_FA00: words 32–35 the
replayer's (17.8.1), and the others v1.35's 8.16, at its word numbers.

| Word | Offset | Register | Access | Fields |
| --- | --- | --- | --- | --- |
| 32 | 0x080 | `RUN_CONFIG` | R/W | [23:0] `rows`, the rows the next run traces, 0–65,536; [24] `store_log` and [25] `rd_prev`, the replayer's, held and read back, with no effect here; [28:26] `tracer_compression`: [26] the code stream, [27] the read log, [28] the write log may go LRU-coded (17.9.6) |
| 33 | 0x084 | `ROWS_RUN` | R | [23:0] the rows of this run traced |
| 34 | 0x088 | `EXPANSION_OFFSET` | R | [15:0] the row offset at which the last posted core-fed expansion starts, held until the next is posted |
| 35 | 0x08C | `EXPANSION_OPCODE` | R | [31:0] the code value of the instruction awaiting a core-fed expansion |
| 36 | 0x090 | `STATE_LENGTH` | R | [15:0] output queue 0's length in entries: 68, a constant |
| 37 | 0x094 | `CODE_LENGTH` | R | [15:0] the code stream's 128-bit words (17.9.6) |
| 38 | 0x098 | `READ_LOG_LENGTH` | R | [15:0] the read log's |
| 39 | 0x09C | `WRITE_LOG_LENGTH` | R | [15:0] the write log's |
| 40 | 0x0A0 | `HASH_MUL` | R/W | [31:0] *p*₁, the map's multiplier, odd (17.9.4) |
| 41 | 0x0A4 | `HASH_STEP` | R/W | [23:0] *p*₃, the probe's step, odd |
| 42 | 0x0A8 | `HASH_LINES` | R/W | [4:0] *k*: the map has 2*ᵏ* lines |
| 43 | 0x0AC | `EXPANSION_TEMPLATE` | W | [6:0] `template`, a core-fed template's number; [31] `valid` (17.9.4) |
| 44–51 | 0x0B0–0x0CC | `STAT_CYCLES`, `STAT_STALL_FETCH`, `STAT_STALL_DATA`, `STAT_STALL_MAP`, `STAT_STALL_OUTPUT`, `STAT_MISS_I`, `STAT_MISS_D`, `STAT_PROBES` | R | [31:0] each, the statistics counters (17.9.8) |

Every other word of blocks 2–7 is unmapped (5.3).

- **`RUN_CONFIG`** follows 17.8.1's rules: a store while `STATUS.busy` is 1 is
  UB (1.3), it holds the last value stored, and the reset zeroes it. A `rows`
  above 65,536 is UB: the FIFOs hold one window.
- **`ROWS_RUN`.** `START` clears it, and each row traced adds 1.
- **`CODE_LENGTH`, `READ_LOG_LENGTH`, `WRITE_LOG_LENGTH`**, the **length
  words**, words 37–39, read 0 from `START`
  until the run's rows end, then the stream word's word counts (17.9.6) until
  the next `START`.
- **`HASH_MUL`, `HASH_STEP`, `HASH_LINES`** are a proof's map constants: a store
  while `STATUS.busy` is 1 is UB, they hold the last value stored, the state
  write leaves them, and the reset zeroes them.
- **`STATUS`** has the standard bits alone (17.1). `busy` holds through the
  run's drain (17.9.6), and after an error that stops the Tracer (17.9.9).

**`EVENTS`**, packed from bit 0 (5.4):

| Bit | Event | Kind | Set by |
| --- | --- | --- | --- |
| 0 | `done` | counted, 1 | the run's end: its rows traced and its output drained (17.9.6) |
| 1 | `dma_done` | counted, 1 | the end of the DMA's transfer (17.4) |
| 2 | `advice_credit` | counted, 2 | the read queue's credit (17.9.3) |
| 3 | `expansion_credit` | counted, 2 | the expansion queue's, as the replayer's (17.8.8) |
| 4 | `new_expansion` | counted, 1 | an instruction of decode class 1 or 2, whose expansion the Tracer waits for (17.9.4) |
| 5 | `state_ready` | live | output queue 0 holds a credit (17.9.6) |
| 6 | `code_ready` | live | output queue 1 holds a credit |
| 7 | `read_log_ready` | live | output queue 2 holds a credit |
| 8 | `write_log_ready` | live | output queue 3 holds a credit |

**A fill** sets a credit to 2, as on the replayer (17.8.1): the state write fills
`advice_credit` and `expansion_credit`, and a new expansion `expansion_credit`.

**`ERRORS`**, sticky until the reset, packed from bit 0:

| Bit | Error | Kind | Set by |
| --- | --- | --- | --- |
| 0 | `queue_overrun` | P | a push into a full read or expansion queue (17.9.3) |
| 1 | `invalid_expansion` | P (H on a corrupted table, ROM or guest) | 17.9.4 |
| 2 | `advice_underflow` | P (H on corrupted advice) | an advice row with the advice field at 0 (17.9.4) |
| 3 | `map_miss` | H (P on a bad map) | a jump target not in the map (17.9.4) |
| 4 | `store_not_writable` | H (P on a bad file) | a store to a line that is not writable (17.9.5) |
| 5 | `page_missing` | H (P on a bad file) | a fill beyond its file's depth, or a hole in the map (17.9.5) |
| 6 | `out_of_range` | H (P on bad advice or a bad guest) | a memory row outside the provable range (17.9.5) |
| 7 | `failed_assertion` | H (P on bad advice) | a `VirtualAssert…` row whose condition is false (17.9.4) |
| 8 | `misaligned` | H (P on a bad guest) | a memory row whose address is misaligned (17.9.5) |
| 9 | `write_back_error` | H | a write-back's response `error` (17.9.5) |

#### 17.9.2 The record and its region

The record is the replayer's, 17.8.2: 68 words of 128 bits, in the Tracer's
region at consecutive offsets from 0, word *w* at 16·*w*. Its words 0–66 are
the Tracer's working state, with no copy: the state write and the words after it
load them, the rows change them in place, and output queue 0 reads them in place
(17.9.6).

| Word | Loaded | While tracing | At a run's end |
| --- | --- | --- | --- |
| 0, the state word | the segment's start | live | the next window's start |
| 1, the stream word: [101:0], the streams' fields | 0, ignored | held | the run's totals (17.9.6) |
| 1, the stream word: [124:102], the **advice field** | the segment's advice values, 0 to 2²² | less one for each advice value taken | the advice left: state |
| 2–65, the register image | the segment's start | live: the register file | the next window's start |
| 66, the files word | the guest's files and bounds (below) | constant | unchanged |
| 67, the template bitmap | ignored: a write is accepted and dropped | built as the run goes (17.9.4) | the run's totals |

So at a run's end, words 0 and 2–66 are the next window's start, and words 1 and
67 the run's totals, the advice field apart.

**The files word**, word 66:

| Bits | Field |
| --- | --- |
| [30:0] | `memory_root`: the root of the guest's memory file, in 128-byte units |
| [32:31] | `memory_depth`, 0–3 |
| [63:33] | `map_root`: the root of the jump-target map's file |
| [65:64] | `map_depth`, 0–3 |
| [71:66] | `log_k`: log₂ K, K the proof's RAM in 8-byte cells, 0–32 |
| [102:72] | `io_size`: 0x8000_0000 − `lowest_address`, in bytes |
| [127:103] | spare, 0 |

`lowest_address` is 0x8000_0000 − `io_size`, a 64-bit address. A `log_k` above 32
is UB.

**The spare bits are not held.** The state word's [127:112], the stream word's
[127:125] and the files word's [127:103] are dropped as their words land, and
output queue 0 reads them as 0, whatever was written. Register 0 reads 0
(17.9.4), so image word 0's [63:0] is read as 0 likewise.

**Its region**, 0x04 on the tracer tile, is the replayer's map (17.8.9), its
parts picked by offset bits [22:20], with bit 23 0:

| Offset | Bits [22:20] | Contents | Size |
| --- | --- | --- | --- |
| 0x00_0000–0x00_043F | 0 | the record, word *w* at 16·*w* | 1,088 bytes |
| 0x00_1000–0x00_1FFF | 0 | the decode table, laid out as the replayer's | 4 KiB |
| 0x00_2000–0x00_3FFF | 0 | the ROM: word *j* at 0x2000 + 16*j* | 8 KiB |
| 0x20_0000–0x2F_FFFF | 2 | the read queue's pushes | |
| 0x40_0000–0x4F_FFFF | 4 | the expansion queue's pushes | |
| the rest | 0–7, or bit 23 1 | unmapped, parts 1 and 3 included | |

- **Writes** are 128-bit `WRITE`s, 16-byte aligned, one a cycle, each landing
  within `PERIPHERAL_LATENCY` (17.3). Another size, or an unmapped offset, is UB
  (17.3).
- **A queue's part** is one address in effect, as on the replayer (17.8.9).
- **Ownership** follows `STATUS.busy` (17.2): part 0 is written only while idle,
  and a write to it while busy is UB. The queues' parts are pushed at any time.

#### 17.9.3 The state write, `START` and the read queue

**The state write**, the write of the state word, offset 0, as it lands, starts a
segment. It:
- empties the read queue, the expansion queue, both FIFOs and output queues 1–3,
  and resets output queue 0's pointer;
- fills `advice_credit` and `expansion_credit`, and clears the output queues'
  credits;
- clears the read queue's half-entry pointer, the length words and the
  statistics counters;
- invalidates both caches and the jump LRU, and drops the write-back buffer's
  line, writing no dirty line back; a write already requested still holds the
  buffer's place until its response (17.9.5);
- clears the template latch and the template bitmap (17.9.4).

Nothing else does these but the reset. The words after it, 1–67, land as any
part-0 write. A push the director accepts after it is kept, and one before it
lost, as on the replayer (17.8.8).

**`START`** (17.2) sets `STATUS.busy`, clears `ROWS_RUN`, and starts a run: it
zeroes both encoder sets' tables (17.9.6), empties the packers and output queues
1–3 and clears their credits, clears the length words and the template bitmap,
then sets the latched template's bit if the run starts inside a core-fed
expansion with a template latched (17.9.4). It leaves the record, the read queue
and its credit, the expansion queue and its credit, the caches, the jump LRU and
the template latch: a run carries on from the last, and only the state write
starts a segment.
- A `START` with no state write since the reset is UB (1.3).
- With `rows` 0 the run traces no row and writes no code value, even inside an
  expansion: its output is output queue 0 alone (17.9.6).
- **The rows' end** is the cycle after the one in which the run's last row
  completes, or, with `rows` 0, the cycle after `START`'s. `ROWS_RUN` counts
  each row in the cycle after it completes, and no row starts in the rows'
  end's cycle.

**The read queue**, part 2, holds the segment's advice: two 64-bit values an
entry, the low first.
- **Its credit**, `advice_credit`, is half the queue: the state write fills it,
  and each 128 entries popped since its last return, or since the state write,
  return 1.
- **The advice field** of the stream word counts the values still to come: each
  advice row takes a value and decrements it (17.9.4).
- **The advice is the segment's:** the queue, its credit and the field carry
  across runs.

**The expansion queue**, part 4, is the replayer's (17.8.8): 256 entries, its
credit filled by the state write and by each new expansion.

**Overrun.** A push into a full read or expansion queue is dropped, and sets
`ERRORS.queue_overrun`. It is still counted (12.3). "Full" is the count before
the cycle's pop (7.4).

#### 17.9.4 Execution

**The machine** has the replayer's 128 registers (17.8.7), in the register file,
the record's image: register 0 reads 0, and a write to it is dropped. Rows are
sequential: each row's reads see the previous row's writes.

**Each instruction:**
1. **Fetch** 16 bits at pc through the instruction cache (17.9.5). If their bits
   [1:0] are 11, the instruction is 32-bit, and its next 16 bits follow, from the
   next line if pc is a line's last 2 bytes.
2. **Its code value** (17.8.3) is a 32-bit instruction as it is, or a 16-bit one
   expanded to its 32-bit form as a16z's `uncompress.rs` expands it, with bit 0
   cleared. A 16-bit encoding with no 32-bit form, `c.unimp` (0x0000) among them,
   is an invalid expansion. The code value enters the code FIFO (17.9.6).
3. **Its template** is the decode table's, by 17.8.6, with the rd = x0 policy and
   the temporary offset of 17.8.5.
4. **Its rows** are its expansion's, one expansion word each (17.8.4), through
   the word with `seq_remaining` 0. Then pc and `instr_idx` advance as 17.8.7
   says.

An invalid expansion is 17.8.10's, with `c.unimp`, and sets
`ERRORS.invalid_expansion`.

**A run that starts inside an expansion**, its row offset non-zero, runs that
instruction's remaining rows, the ROM's from `start` + the row offset or the
expansion queue's as it stands, and its code value is the run's first again. At a
segment's start the Tracer fetches the instruction at pc to form it; at a later
run it holds it.

**Core-fed expansions.** A template of class 1 or 2 runs as on the replayer
(17.8.6): the Tracer presents the code value in `EXPANSION_OPCODE` and the row
offset in `EXPANSION_OFFSET`, posts `new_expansion`, fills `expansion_credit`,
and takes the expansion's words from the expansion queue, `rd0` from the first
(17.8.5). A run's end inside a core-fed expansion does not present it again: the
queue, its credit and the latch carry to the next run.

**The template bitmap and the latch.** A store to `EXPANSION_TEMPLATE` with
`valid` 1 sets bit `template` of the record's word 67, and latches `template` as
the current expansion's; with `valid` 0 it clears the latch alone. A new
expansion and the state write clear the latch. `START` clears the bitmap and sets
the latched template's bit again (17.9.3), so a window that starts inside an
inline lists it. Causes in one cycle take effect in this order: the state write,
`START`, a new expansion, the store.

**What a row does.** A row computes what 17.8.7 and `spec/jolt/replayer-opcodes.md`
sections 1–5 define, with these departures, the replayer's log words turned
around:

| Row (17.8.3) | The Tracer | Its log word (17.9.6) |
| --- | --- | --- |
| a load row | its address *a* (17.8.11's `mem_addr`) and its checks (17.9.5); rd = the doubleword at *a*, from the data cache, dropped for x0 | tag `load`: the doubleword, an LD to x0's included |
| a store row | its address *a* and its checks; reads the doubleword at *a*, then writes rs2's value there, the whole doubleword | tag `write`: the doubleword read |
| an advice row | rd = the next advice value from the read queue; the advice field less 1 | tag `other`: the value |
| a control transfer: `next_from_value`, its new pc ≠ pc | the next `instr_idx` is the target's base, from the jump LRU or the map (below) | tag `other`: the base, zero-extended |
| the program's end: a JAL row with immediate 0 and `imm0_pc0`, its expansion's last (17.8.7) | new pc 0, `instr_idx` 0, no look-up | tag `other`: 0 |
| a `VirtualAssert…` row | evaluated, by `spec/jolt/replayer-opcodes.md` section 2 H: a false one sets `ERRORS.failed_assertion` | none |
| a padding row, while pc is 0 | fetches nothing and executes nothing: pc, `instr_idx` and the registers stay | none; its code value is 0x0000_0000 |
| any other row | as 17.8.7 | none |

- **One log word a row at most**, as 17.8.3 says.
- **pc** follows 17.8.7: `hold`, a new pc equal to pc as sequential, a row that
  is not its expansion's last keeping its pc, `JALR`'s cleared bit 0,
  `VirtualPext` for any mask. So a branch not taken, whose new pc is pc, looks
  nothing up and writes no log word. `VirtualHostIO` does nothing.
- **Advice.** An advice row with the advice field at 0 sets
  `ERRORS.advice_underflow`. With the field above 0 and the read queue empty, the
  row waits.
- **The end and padding.** Only the end row ends the program, whatever its rd:
  its link is written as any JAL's. While pc is 0 every row is a padding row, so
  a run past the end, or a segment that starts at pc 0, fills its rows.

**Jump targets: the LRU and the map.** A jump target's value is its
instruction's **base**, the `instr_idx` of its expansion's first row. Only a
control transfer looks one up, by its new pc.
- **The key** is the target's offset in the memory file, *o* = new pc −
  `lowest_address`, in 64 bits. A target with *o* ≥ 2³² sets `ERRORS.map_miss`
  at once, with no look-up: the LRU is untouched and no probe starts, so
  `STAT_PROBES` does not count it. A new pc of 0 from any row but the program's
  end, a `JALR`'s, a branch's or a `JAL`'s whose target wraps to 0, is such a
  target: `lowest_address` is at least 1, `io_size` being below 2³¹ (17.9.2),
  so its *o* is at least 2⁶³.
- **A target at *o* = 0xFFFF_FFFF** is UB (1.3): that key is a free entry's, so
  the host can place no such target in the map.
- **The jump LRU:** 64 entries of (key, base), fully associative, true LRU: a
  hit moves its entry to the front, and an insert replaces the oldest. A hit
  costs no cycle.
- **On a miss, the probe.** The home line is *h* = ((*o* × *p*₁) mod 2³²) >>
  (32 − *k*), 0 when *k* is 0, with *p*₁ = `HASH_MUL` and *k* = `HASH_LINES`.
  The Tracer reads line *h* of the map's file over the fill port (17.9.5) and
  compares its 16 entries at once; on a full line without a match it reads line
  (*h* + *p*₃) mod 2*ᵏ*, *p*₃ = `HASH_STEP`, and so on, for at most
  `MAP_PROBE_LINES` = 3 lines.
- **A map line** is 16 entries of 8 bytes, entry *j* at bytes 8*j* to 8*j* + 7,
  least significant byte first: the key, 32 bits, then the base, 32 bits. A
  **free** entry has key 0xFFFF_FFFF.
- **The outcome.** A matching entry gives the base, the lowest-numbered entry's
  if several in its line match, and the LRU inserts it. A free entry in a probed
  line without a match, or three full lines without one, sets
  `ERRORS.map_miss`.
- **The map is software's**, built by the host at most half full, so that no key
  lies more than two steps from its home line.

#### 17.9.5 Memory: the caches, the fill port and the address checks

**The guest's memory** is a file, `.pages`: the byte at guest address *a* is at
its offset *a* − `lowest_address`. Its root and depth are the files word's
(17.9.2), and an absent page reads as 0.

**The caches.** An instruction cache and a data cache, each 128 lines of 128
bytes, fully associative, true LRU, a hit taking no extra cycle.
- **A line's tag** is its line in `.pages`, (*a* − `lowest_address`) >> 7.
- **The data cache** is write-back and write-allocate: a store that misses fills
  its line first. Each data line has a **writable** bit, from its fill's status
  (below), and a dirty bit.
- **Need, not speculation.** A cache looks up, and fills, only what the
  instructions and rows the Tracer runs need: each instruction's fetch, in order,
  a straddling instruction's two halves each a look-up of its own, and each
  memory row's line.
- The state write invalidates both, and drops their dirty lines (17.9.3).

**The fill port**, `fill` (3), is the Tracer's whole interface to the memory
system.

> **One handshake, one request. Each cycle in which a request channel's `valid`
> and `ready` are both 1 is one request. The Tracer drops `valid` in the cycle
> after its handshake, unless it has a new request to make:** a `valid` held a
> cycle longer is a second request, which the cache serves (23.6.4), and its
> response reaches nobody. It is the client's rule; the cache cannot tell. **It is
> verified in the RTL, by an assertion on the port.**
 A request reads or writes one 128-byte line of a file, named by (root,
depth, line), for one of three reasons: an instruction miss, a data miss with its
dirty victim's write-back, and a map probe's line. A read's response carries a
status:

| Status | Meaning | The data | The Tracer |
| --- | --- | --- | --- |
| 0, `ok` | the page exists, and every entry on its path from the root is owned | the line | a data line is writable |
| 1, `hole` | no page there: an index entry of 0xFFFF_FFFF on its path | zeros | not writable; a map line sets `ERRORS.page_missing` |
| 2, `not_owned` | an entry on its path is not owned: shared from another file | the line | not writable |
| 3, `beyond` | the line is past its depth's capacity, line ≥ 2^(5 + 10·depth) | undefined | sets `ERRORS.page_missing` |

- **A fetch beyond the fill port's line**, whose line the 35-bit `line` cannot
  name, *a* − `lowest_address` ≥ 2⁴² or *a* below `lowest_address`, is beyond
  every depth's capacity: its look-up misses, counted in `STAT_MISS_I`, no read
  is requested, and it sets `ERRORS.page_missing`. A data line inside the range
  is always named.
- **A store** to a line that is not writable sets `ERRORS.store_not_writable`,
  and is not made.
- **A write-back's status.** Only a writable line is dirty, so a correct memory
  system answers every write-back `ok`. An `error` sets
  `ERRORS.write_back_error`, a hardware fault: the line is lost.
- **An instruction** fetched from a hole reads 0, `c.unimp`: an invalid
  expansion.
- **Order.** The Tracer runs in order and waits on each read: at most one read is
  outstanding, the fetch's halves, the data line and the map's lines in the order
  it needs them. A dirty victim goes into a **write-back buffer** of one line,
  whose write is requested when no read is outstanding, and whose place is free
  again when its write response arrives: at most one write is outstanding. A
  write is **requested** once its request is raised on `write_request`, which
  holds it until its handshake (3), so a requested write is made. A second victim
  waits for the place, and its row with it. A miss on the line in the buffer
  takes the line back from it, dirty, with no read: a write not yet requested is
  then not made, and one requested still holds the place until its response.
- **After the state write** a write still requested is no longer the buffer's
  line to take back, and no read of its line is requested until the write is
  accepted: the memory system orders only a read after a write already accepted
  (3).

**The address checks**, on every load and store row, beside the data cache's
look-up and at no cost in cycles, on the row's address *a* (17.8.11's
`mem_addr`):
- **The range:** (*a* − `lowest_address`) mod 2⁶⁴ < 8·K, K = 2^`log_k`, in 64
  bits. Otherwise `ERRORS.out_of_range`.
- **The alignment:** *a* is a multiple of 8: the row's effective address, rs1 +
  the immediate, is aligned to its width, 8, 4, 2 or 1 bytes. Otherwise
  `ERRORS.misaligned`.
- **Both** are set if both fail, and either stops the row before its access, so
  `store_not_writable` and `page_missing` are set only by a row that passed
  both.

**The files are software's:** the memory file is the Tracer's copy of the
guest's memory for the segment, which owns only the pages the segment writes, so
a store into code finds its line not writable; the map is a file of its own.

#### 17.9.6 Output: the FIFOs, the encoders and the drain

A run's output is the record, read in place as **output queue 0**, and the three
streams, each raw or LRU-coded (17.8.12), as **output queues 1–3**: output queue
*n* feeds a replayer's part *n* (17.8.9).

**The FIFOs**, each of `TRACER_WINDOW_ROWS` = 65,536 entries, one window:
- **the code FIFO**, 32 bits: one code value for each instruction of the run,
  the run's first again if it starts inside one, and one for each padding row;
- **the log FIFO**, 66 bits: each log word in row order, and its 2-bit **tag**:
  0 `load`, 1 `other`, the read log's others, and 2 `write`, the write log; 3 is
  never written. The tags never leave the Tracer.

A row writes at most one entry of each, so the FIFOs never fill while the rows
run. The rows write them and the drain reads them after, never both in a cycle,
so each may be a single-port SRAM.

**The encoders.** Two sets of three, one for each stream, each with a table
E[0]–E[15] of 64-bit values: 17.8.12's decoders inverted.
- The code and read-log tables are true LRU over 16 entries; the write log's has
  15 entries and E[15], the **last load**, which every `load` word replaces, in
  row order.
- A value in the table is a hit at its **lowest** index, and in the write log at
  index 15 first.
- A miss is coded 32-bit exactly when the value's top 32 bits are 0, and 64-bit
  otherwise. A code value is always a hit or a 32-bit miss.

**Two passes.** As the rows run, each word entering a FIFO passes set 1, which
counts its stream's coded bits. After the rows, set 2 codes the streams from the
same start, the tables `START` zeroed, so its output is exactly what set 1
counted. Set 1 runs as the rows run and set 2 in the drain, never both in a
cycle, so a stream's two sets may share one table, zeroed again when the rows
end: set 2 then starts from the zeroed table as set 1 did.

**The choice**, when the rows end, for each stream: raw if its
`tracer_compression` bit is 0; otherwise LRU-coded if its coded form is fewer
128-bit words than its raw form, and raw on a tie. Raw words are ⌈values / 4⌉
for code and ⌈values / 2⌉ for a log; coded words, ⌈bits / 128⌉. The choice, the
words and the values are the stream word's fields [101:0] (17.8.2), written when
the rows end, and the length words read the same words. The advice field is not
touched.

**The drain** starts no earlier than the cycle after the rows end:
- **The scans.** One scan of each FIFO, in order, through set 2: the code FIFO's
  into output queue 1, and the log FIFO's `load` and `other` words into output
  queue 2 and its `write` words into output queue 3, every log word passing the
  write log's table so that E[15] sees the loads. The two scans run
  independently.
- **The packers.** Each stream's packer packs its bits least significant first
  into 128-bit entries (17.8.12), and pushes each whole entry into its queue; at
  the stream's end, its last part, the rest of that entry padding, of any value.
  A scan waits while the queue its next entry enters is full.
- **The credits.** Each of output queues 1–3 holds a credit for each 128 entries
  its packer has pushed, and, once its stream has ended, one for its last entries
  if they are not a whole 128: 0–2. A stream ends once its last value is packed:
  a log's once the log scan has given it its last value, whatever the scan still
  holds of the other log's. Its `_ready` event is up while it holds one,
  and a credit is taken when the DMA has read the last entry it covers.

**Output queue 0** is part 0, the record's 68 words, read in place by a pointer,
which the rows' end sets to word 0 and each transfer from it advances by its
entries. The rows' end posts `state_ready`, its one credit taken when the pointer
passes word 67. A transfer past word 67 is UB (1.3).

**A transfer** (17.4) from output queue *q* sends its entries as 128-bit
`WRITE`s, an entry a packet, its `count` the entries. One that asks for more
entries than its queue's credits cover is UB.

**The end.** The run ends, `EVENTS.done` posted and `STATUS.busy` falling in one
cycle, once its rows are traced, both scans have ended, every output queue is
empty and the DMA's last transfer has ended. So `done` means the Tracer is ready
for the next `START`.

#### 17.9.7 Timing

- **Exact rows.** A run's code values, log words, record and streams are
  exact, as the replayer's rows are (17.8.7).
- **A row's schedule.** A row of class S or L (17.8.7) takes 4 cycles, and one of
  class M 6, when nothing stalls: in sequential flow the next instruction's fetch
  and decode overlapped, and every cache hit and LRU look-up included.
- **A row that sets its successor's pc**, a control transfer, a branch taken or
  not and the program's end, has its successor fetched and decoded only once it
  completes: those cycles are stall cycles, counted in `STAT_STALL_FETCH`
  (17.9.8).
- **Stalls** delay a row and never change its schedule: an instruction miss (one
  fill, or two for a straddling instruction), a data miss (its fill, and a wait
  for the write-back buffer's place), a probe (its lines), an advice row's empty
  read queue, and a core-fed expansion's empty expansion queue.
- **A probe** takes 2 cycles a line after its line's read response: its 16
  comparisons and its step.
- **The drain.** Each scan moves one FIFO entry a cycle while its queue has
  room, and the DMA sends a packet a cycle (17.4).
- **The memory system's latencies** are its own (`fill`, 3), so a run's cycles
  depend on it: the simulator's are those of its model of the memory system
  (17.9.14).
- **Exact counts.** Each cache looks up only what is needed (17.9.5), so the
  misses and the probes are exact, whatever the latencies: `STAT_MISS_I`,
  `STAT_MISS_D` and `STAT_PROBES` are the same on the simulator and on the RTL
  (Snarky One).

#### 17.9.8 The statistics counters

Words 44–51, for bring-up: where the Tracer's cycles go, by the attribution
below, the RTL's (Snarky One's), which the simulator follows (item 157). Each
is 32 bits, wrapping, read-only, zeroed by
the reset and the state write, so that it counts across a segment's runs, and
counts only while `STATUS.busy` is 1, the drain and the cycles after a stopping
error included.

**A stall cycle** is a cycle, from `START`'s effect until the run's rows end, in
which no row is in its schedule (17.9.7): the next row cannot start. Each is
counted in one stall counter at most, by what the next row waits for.

| Register | Counts |
| --- | --- |
| `STAT_CYCLES` | every cycle with `STATUS.busy` 1 |
| `STAT_STALL_FETCH` | stall cycles in which the next row waits for its instruction's fetch and decode: an instruction line's fill, each half of a straddling instruction in turn, a run's first instruction, and an instruction whose pc a row's completion gives (17.9.7) |
| `STAT_STALL_DATA` | stall cycles of a memory row waiting for its data: from the cycle after its schedule would have completed it until the data is in hand, its line's fill and its dirty victim's wait for the write-back buffer's place included |
| `STAT_STALL_MAP` | stall cycles of a control transfer waiting for its look-up's outcome: from the cycle after an LRU hit would have completed it until the outcome, the probe's lines' reads and comparisons included |
| `STAT_STALL_OUTPUT` | cycles, after the rows end, in which a scan waits on a full output queue, its stream's last entry's push included: one a cycle, whether one scan waits or both |
| `STAT_MISS_I` | instruction-cache look-ups that miss, a straddling instruction's halves each its own |
| `STAT_MISS_D` | data-cache look-ups that miss |
| `STAT_PROBES` | jump-LRU misses that start a probe |

- **The waits on the core**, an advice row's on an empty read queue and a
  core-fed expansion's on its words, are stall cycles in no counter, and so are
  a non-contiguous `VirtualPext`'s extra cycles (17.8.7).
- **With `rows` 0** no cycle is a stall cycle: the rows end in the cycle after
  `START`'s (17.9.3).
- **So the cycles add up.** From `START`'s effect to the rows' end, the cycles
  are the rows' schedules (17.9.7), plus `STAT_STALL_FETCH`, `STAT_STALL_DATA`
  and `STAT_STALL_MAP`, plus the cycles in no counter: the stall counters never
  overlap, and never count a fill that delays no row. The cycles after the rows'
  end are in `STAT_CYCLES` alone, but for `STAT_STALL_OUTPUT`'s.
- **Across implementations.** The stall counters depend on the memory system's
  latencies, which are its own, so the simulator's equal the RTL's only where
  its model's latencies are the memory system's. The miss and probe counts are
  exact (17.9.7).

#### 17.9.9 Errors

- **Queue overrun** drops the push, and the Tracer runs on.
- **Every other error stops the Tracer** where it is: it traces no further row,
  its rows end no more and its run never ends, it posts no event, and
  `STATUS.busy` stays 1 until the reset. The drain is not stopped: a scan that
  is running goes on, and so does a transfer of the DMA's in progress, whose
  packets are sent; only their events are withheld. Every error is terminal
  (6.2), so this decides only what the handler reports.
  - **No event** from the cycle after the error: no counted event is set,
    `dma_done` among them, and the live events are down. Counts already posted
    stay until consumed, and later errors are still set.
  - **While idle**, an error that stops the Tracer, a write-back's `error` after
    the run's end, stops the next run at its `START`: `STATUS.busy` rises and
    stays 1, and the run traces nothing.
- **A row with several errors.** The address checks are made together (17.9.5),
  and an access they stop sets no other error.
- **After a trap** the Tracer runs on, unserviced, and waits when a queue runs
  dry or a credit goes unanswered, until the reset (17.5).

#### 17.9.10 The segment on the chip

The segment around the Tracer is software's, with no hardware of its own: the
tracer tile's firmware, the master tile's and the compute tiles'. It replaces
v1.35's 4.21.10, and relies on these rules:
- **Every packet is counted**, on a counter its receiver armed before it arrived
  (12.2), the windows' included.
- **Compute tile *n* receives** its window's record start, words 0 and 2–66,
  from run *n* − 1's output queue 0, or, for tile 0, from the tracer tile's own
  copy of the record; its totals, words 1 and 67, from run *n*'s; and its three
  streams, whose lengths exist only once run *n*'s rows end.
- **Output queue 0's four transfers:** word 0 to tile *n* + 1, word 1 to tile
  *n*, words 2–66 to tile *n* + 1 and word 67 to tile *n*, so a tile's totals
  land before its streams. At the last run all 68 words go to the tracer tile
  itself, its own id as dst (16.2), which forwards words 1 and 67 to tile
  `TILE_COUNT` − 1.
- **A compute tile arms only after the release** (7.3): an `ARRIVE` with a
  counter non-zero is an early arrival. The tracer tile takes no part in the
  barrier.
- **The replay** starts at the barrier after every window has landed.

**In v1.37** the master tile answers the tracer tile's `LOAD` only once the
Updater has built the segment's files, and the load carries the segment: its
record, the end state it is checked against, its advice and the map's
constants (30.5). **Its last record is pulled, not pushed** (30.6): when its
trace ends the tracer tile prepares one block of 69 128-bit words in its own
memory, its figures and the record at the segment's end, and sends its result,
which says the trace is done; once the replay is done the master tile asks for
the block by one request, `SEND_END`, and the tracer tile sends it by one DMA
transfer. The master tile answers the result only once the block has landed,
since the answer ends the tracer tile's image: until then the tracer tile
serves requests.

**The handshakes** are requests to the tracer tile (target 2, 11.5), so no
timing is assumed, as the load waits for its `LOAD` (6.5):
- **`ARMED`.** A compute tile, woken by the release, arms its counters for its
  record's start and totals, then sends `ARMED`.
- **`STREAMS_ARMED`.** At its window, once its totals have landed, it reads its
  streams' lengths from them, arms its stream counter, and sends
  `STREAMS_ARMED`.
- **`SEND_END`.** The master tile, once the replay is done, asks for the block
  (30.6): word 0 [20:18] `data_counter`, the master tile's counter armed for the
  block's 69 packets; word 1, the byte address in the master tile's memory where
  the block's word 0 goes; words 2 and 3, 0. The tracer tile answers with the
  data alone, no reply (11.4): one DMA transfer of 69 128-bit `WRITE`s, from that
  address up, counted on `data_counter`, along the line between the special
  tiles (16.1).
- **The tracer tile sends a tile nothing its request has not allowed:** a
  tile's record pieces only after its `ARMED`, its streams only after its
  `STREAMS_ARMED`. The requester field names the tile, and the tracer tile pops
  each request before the tile can send its next, within its limit of 1 (11.3).
  Their `op` codes are the tracer tile's firmware's.

#### 17.9.11 What software keeps

Breaking one of these rules is UB (1.3), unless it names an error:
- **A segment** starts with the state write, then words 1–67 in order, while
  `STATUS.busy` is 0, and `RUN_CONFIG` and the hash's constants are written
  before its first `START`. The decode table and the ROM are written once, while
  idle.
- **A new segment** waits for the last run's `done`, and its core drops any
  core-fed expansion it was feeding: the state write empties the expansion queue,
  and the Tracer presents the expansion again if the segment starts inside it.
- **The advice field** holds the segment's advice values; every refill of the
  read queue but the last is exactly half a queue, pushed on a credit, as the
  replayer's refills are (17.8.13).
- **A core-fed expansion** is fed as on the replayer (17.8.13), from
  `EXPANSION_OFFSET`; for an inline, the `EXPANSION_TEMPLATE` store precedes its
  first pushed word.
- **One source at a time** feeds a queue's part (17.3, 14.4).
- **A transfer** from output queue *q* takes no more than its credits cover: 128
  entries for a whole credit, and exactly the rest, from the length words, for a
  stream's last part.
- **At a segment's end** the core checks, from the last run's record, that the
  advice field is 0 and the end state is the next segment's start.

**The rest is software's:** the core's loop, the core-fed set and its key,
the files and the map, the advice's delivery, and the records' and streams'
destinations (17.9.15; the firmware is meshsim's tracer tile's program,
`fw/tracer/tracer_ctl.c`; the RTL's is its `tracer.rs`). Every
refill and expansion reaches the region as a `WRITE` packet, from the tracer
tile's DMA unit and write unit, as on the replayer.

#### 17.9.12 Reset

The reset ends any run; empties every queue, both FIFOs and the write-back
buffer, so the DMA has nothing to send; invalidates both caches and the jump LRU,
writing no dirty line back; and zeroes `STATUS`, `EVENTS`, the credits among
them, `ERRORS`, `EVENTS_MASK`, `RUN_CONFIG`, `ROWS_RUN`, `EXPANSION_OFFSET`,
`EXPANSION_OPCODE`, the length words, the hash's constants, the statistics
counters, the template latch and the template bitmap. The state word, the stream
word, the files word, the register file, the decode table and the ROM keep their
contents, as a peripheral's memories (18). The encoders' tables are undefined
until the next `START`, which zeroes them, and the read queue's pointer until the
next state write, which a run needs (17.9.3).

#### 17.9.13 Overview: the Tracer in a proof

*Kept from v1.35's 4.21.1 and v1.35's 4.21.7, with "where the output lands" of v1.35's 4.21.6, in v1.36's terms. It adds no rule to 17.9.1–17.9.12. (Not stated by the RTL's specification; kept from v1.35.)*

**What it shares with the replayer, and what it doesn't.** The Tracer is its own machine, not the replayer with a
memory path added. The two share formats and tables, not the design: the 68-word record (17.8.2), the region's map
(17.8.9), the decode table, the ROM and the core-fed templates (17.8.6), the core-fed expansion path with its event
and words (17.8.6), the role of part 2's queue (both pop advice from it), and the window's words 32–35 (17.8.1). Its rows are the replayer's rows:
it expands each instruction by the same templates under the same rules (17.8.4–17.8.7), so it knows which log
words the replayer will pop, in which order, and where every window boundary falls, even inside an expansion. What
it adds is everything the replayer takes from the streams: it fetches the program from memory, loads and stores the
guest's memory, takes advice from the host, and finds each jump target's `instr_idx` in a map.

**Its interfaces.** The tracer tile reaches it as every peripheral (17.1–17.4): `WRITE` packets into its region and
loads and stores to its window. It has no host channel: its inputs are the record, the advice and the core-fed
expansion words, all from the tracer tile's core. It reaches the guest's memory and the jump-target map as files,
by 128-byte lines, over the fill port (17.9.5). Its output leaves by its DMA (17.4, 16.4).

**What a segment is.** A proof is the whole run, 2²⁹ rows for our workloads, spread over the 128 chips; each
chip's Tracer traces one segment of 2²² rows from the processor state the host sends for it, in `TILE_COUNT` = 64
runs of 65,536 rows, one for each compute tile's window. Between runs nothing is loaded and nothing is flushed
(17.9.3): a run boundary is only a cut in the output. A run stops exactly at its row count, even inside an
expansion. Each segment starts from its own record: the processor's state does not carry from one segment to the
next, and the tracer tile's core checks that it agrees (17.9.11).

**The budget** is 100 ms a segment at 1 GHz, the misses and the drain included. The rows' schedules alone (17.9.7)
make a 2²²-row segment about 17 M cycles; 17.9.17 has the measured figures. The jump map's worst segment costs
under 1 % of it [estimate: v1.3].

**Where the output lands, and what is still to come.** In the first implementation the DMA writes each window
straight into its compute tile's scratchpad, the record's pieces and the streams into the tile's copy of them
(17.8.22), every write counted on a counter the tile armed for it (17.9.10), so no replayer credits pace the
drain. *(Deferred, with the file system: item 119.)* Later the windows are to land in the
tracer tile's memory and go to LPDDR as files, for a later replay, which removes the per-window handshake of
17.9.10. Signed counters (item 125) would have removed the wait between a release and a tile's arming; the requests
of 17.9.10 remove it.

#### 17.9.14 The memory system behind the fill port

**Specified in v1.37:** the memory system, the memory tile's peripheral 1 (17.11), whose cache serves `fill`
(23.6.4) and walks the files (23.5). 17.9.5 and `fill` (3) stay the whole contract between it and the Tracer; the
cache keeps it, and its timing is 23.6.7's: 2 cycles a level on hits, a line from LPDDR about 80.

**v1.36's simulator's model,** v1.35's (a shared cache of 8 ways × 512 sets, LRU, write-back, cold at each segment's
start; a fill of a depth-*d* file *d* + 1 accesses of `ms_line_hit` = 6 cycles a hit and `ms_line_miss` = 80 a
miss; the next victim kept clean through a 4-line write buffer), stays in v1.37's simulator as a setting, for
comparison: it is no specification. On the proof runs the cache is −2.7 % to +4.4 % against it `[meas: v1.37's
simulator]` (23.6.7).

#### 17.9.15 The tracer tile's loop and the segment's sequence (software)

*Kept from v1.35's 4.21.3 (the controller's loops) and v1.35's 4.21.10 (the sequence), rewritten for v1.36: there are no
commands, the tracer tile is loaded by its `LOAD` (6.5), and the handshakes are requests (17.9.10). Software only,
for orientation: the rules are 17.9.3, 17.9.6, 17.9.10 and 17.9.11. The working version is meshsim's firmware,
`fw/tracer/tracer_ctl.c`, `fw/cc/cc1.c` and `fw/replay/replay_ctl.c` (the RTL's is its `tracer.rs`, `master.rs` and
`replay.rs`), which runs one segment a run on the chip (`tests/test_segment.cpp`). (Not
stated by the RTL's specification; kept from v1.35.)*

**What each compute tile receives, and on which counter.** Window *n* goes to compute tile *n*, which keeps it in
its scratchpad as its replay region (17.8.22), every packet counted:
- its record's start, 66 words (the record's words 0 and 2–66), on its **start counter**;
- its record's totals, 2 words (words 1 and 67), on its **totals counter**;
- its three streams, whose lengths exist only once run *n*'s rows end, on its **stream counter**.

**The loop**, once the tracer tile's image has started:

```
    (once) load the decode table and the ROM into part 0 (0x1000, 0x2000)
    tr->HASH_MUL = p1; tr->HASH_STEP = p3; tr->HASH_LINES = k      ; a proof's, from the program's map
    tr->RUN_CONFIG = 65536 | tracer_compression
    drop any expansion being fed (its template, position, pending words)     ; 17.9.11
    write the record's words 0-67, word 0 first: the segment's state; word 1's advice field = the
          segment's advice values, its other fields 0; the registers; word 66; word 67 0
    tr->EVENTS_MASK = done | dma_done | advice_credit | expansion_credit | new_expansion
                    | state_ready | code_ready | read_log_ready | write_log_ready
    preload advice: up to two refills of half a queue (the state write filled advice_credit)
    once tile 0's ARMED has been popped: send window 0's start, the record's words 0 and 2-66 from
          the tile's own copy, to tile 0's record
    for window n in 0 .. TILE_COUNT - 1:
        tr->START = 1
        loop: WFI; ev = tr->EVENTS               ; consumes the counted events the mask selects
            new_expansion:    key(EXPANSION_OPCODE) -> template t; if t is an inline:
                              tr->EXPANSION_TEMPLATE = valid | t; feed t's words from EXPANSION_OFFSET
                              on its credits
            expansion_credit, advice_credit: send up to half a queue of what remains
            state_ready:      four transfers from output queue 0, in its order (17.9.10): word 0 to
                              tile n+1, word 1 to tile n, words 2-66 to tile n+1, word 67 to tile n,
                              each tile's pieces only after its ARMED
                              (the last run: all 68 to the tracer tile itself, then words 1 and 67
                              forwarded to tile TILE_COUNT - 1)
            code_ready, read_log_ready, write_log_ready, once tile n's STREAMS_ARMED has been
                              popped: one transfer of what the credit covers, to tile n's streams
            done:             the next window
    check that the advice left (word 1's field, from the last run's record) is 0, and that the end
          state (words 0, 2-65) is the next segment's start; else report
```

- The core waits for `dma_done` between transfers, one at a time (17.4), and reads `EVENTS` again after each, since
  the `_ready` events are live and the tile's `peripheral_1` may lag a consuming load by a cycle (17.2).
- The requests wake it through the tile's `EVENTS.request_pending` (7.2, 11.1).
- Until tile *n*'s `STREAMS_ARMED` has come, its stream queues wait full, their scans stalled (17.9.6). Nothing
  deadlocks: output queue 0's transfers never depend on them.
- The tracer tile's core holds every core-fed template, the 75 of the universal set (`templates.bin`), computes
  key(*w*) as a compute tile's core does (17.8.19), and feeds the words from `EXPANSION_OFFSET` on its credits.
  A key with no template is an error in the tracer tile's software, not the Tracer's.
- **A mid-segment state write is UB.** The state write is made only at a segment's start: one between a segment's
  runs would empty the read queue and the caches and lose the segment's progress. The simulator cannot tell it
  from a new segment's record, so it flags a state write that finds the advice field above 0. *(Not stated by the
  RTL's specification, whose 17.9.11 says only that a new segment waits for the last run's `done`; kept from
  v1.35.)*

**The sequence.** Three releases (7.3), and no timing assumed between a release and a tile's arming:
1. **The load.** After the reset every compute tile and the tracer tile ask for their images (`LOAD`, 6.5). The
   compute tiles arrive, nothing armed; the tracer tile takes no part in the barrier and starts its image. In
   v1.37 the tracer tile's load waits for its segment's files, and carries the segment (30.5).
2. **The first release.** Each compute tile, woken, arms its start counter for 66 and its totals counter for 2,
   then sends `ARMED`.
3. **The deliveries**, window by window, as the loop runs. Tile *n*'s totals land before any of its streams; the
   tile reads the three streams' lengths from word 1 ([15:0], [31:16], [47:32], in 128-bit words), arms its stream
   counter for their sum, and sends `STREAMS_ARMED`; only then does the tracer tile send its streams. One request's
   round trip a window, small against the drain.
4. **Each compute tile arrives** when its counters have reached 0: its window has landed.
5. **The second release.** The master tile sees every arrival and releases, and every compute tile starts its
   pass together (17.8.10): the replay is about 1 % of the trace (17.9.17), so starting tiles early would gain
   nothing worth the coupling.
6. **The third release** ends the passes.

v1.35 closed the wait between the release and the arming by timing, the tracer tile sending nothing sooner than
`stage_delay` after its start, and had tile *n* answer with a `COUNT` packet. v1.36 has neither: `stage_delay` is
removed, and the answers are the two requests.

#### 17.9.16 Reasons, and the host's side

*Kept from v1.35's 4.21.2, v1.35's 4.21.4, v1.35's 4.21.5, v1.35's 4.21.6, v1.35's 4.21.7 and v1.35's 4.21.8: why rules of 17.9.2–17.9.6 and 17.9.9 are as they are, and what the
host does around them. No rule is added. (Not stated by the RTL's specification; kept from v1.35.)*

- **The tables.** The decode table and the ROM hold the replayer's tables, unchanged (`data/tables/replayer/`),
  loaded over the same path that later fills the queues. The Tracer's ROM is single-port, written only while idle,
  since nothing reloads it while tracing (item 156).
- **The files word.** `memory_depth` is 2 for K up to 2²⁹ cells and 3 above. `io_size` is exact, whatever the
  region's alignment: a16z puts the I/O region just below `RAM_START_ADDRESS`, 0x8000_0000, and the host rejects a
  layout whose I/O region is 2 GiB or more (owner, 2026-11-05).
- **The advice field** is the Tracer's input size and its live count in one: advice is never coded and always 64
  bits, so its size is a count of values, and its entries ⌈values / 2⌉. The replayer ignores the field, and a
  record carries it through.
- **Where a16z decides.** The RVC expansion is a16z's (item 113): `C.SLLI` with rd = x0, a HINT, is `SLLI x0, x0,
  shamt`. `ECALL` and `MRET` are ordinary rows through the virtual CSRs. There is no second register bank, no
  untraced row and no advice register: the host supplies every advice value. `VirtualHostIO` does nothing: its
  effects happened in the host's run.
- **The program's end** is any `JAL` row with immediate 0, whatever its rd: the templates set `imm0_pc0` in every
  `JAL` word (17.8.4), so `JAL x0, 0`, `C.J 0`, `EBREAK` (whose template is one `JAL` to a virtual register) and
  `JAL ra, 0` all end the program, as a16z treats each as the pc stalling. A `JALR` or a branch to 0 is a map miss
  instead (17.9.4; item 146).
- **Stores into code.** Jolt executes its fixed bytecode, but the Tracer fetches through memory, so a store into
  the text would make its trace differ silently from a16z's. The host rejects such a program before it sends
  anything, by scanning each segment's writes against the text sections. The Tracer's backstop is free: its copy
  owns only the pages the segment writes, never a text page, so a store into code finds its line not writable and
  sets `ERRORS.store_not_writable` (17.9.5). This is a deliberate divergence: a16z would prove such a program.
- **The store's check is at the store,** not at the write-back: dirty lines are dropped at a segment's end, so a
  store to a hole might otherwise never be reported.
- **Dropping the dirty lines at the state write is safe,** because the Tracer's copy of the guest's memory is
  throwaway: the next proof's files are built separately.
- **The caches need no coherence protocol:** during a segment nothing else writes what the Tracer sees, and the
  two caches can hold the same line, since a store never reaches a code line.
- **The map is the host's.** It builds the table from the ELF (every direct target, every return address, every
  code pointer found in data), at most half full, and inserts a proof's supplement by the same rule into private
  copies of the pages it lands in. The host and gold compute the key as 17.9.4 does, the target's offset in the
  memory file. The hash's constants come with the program's map, a proof at a time; they are registers and not the
  record's because word 66 has no room. `HASH_STEP` is a 24-bit prime, so odd.
- **The range** is 8·K because the RAM's checks cover exactly K cells (item 149). It is not the file's length:
  `.pages` is larger than the provable range, so an access past K inside the file would read a hole as 0. A fetch
  needs no range check: a pc outside the code misses the map.
- **The alignment rule is a16z's own,** on every memory row, a native instruction's and an inline's alike (item
  150): the row's floor address, ea − (ea & *m*) with ea = rs1 + the immediate and *m* = 0, 4, 6 and 7 for the
  doubleword, word, halfword and byte rows, must be a multiple of 8. That is 17.9.5's rule, natural alignment to the
  row's width: the floor address keeps exactly the bits a misaligned access of that width sets. A check of the
  row's doubleword address, ea & ~7, would miss a sub-word access. It is what both tracers abort on: a16z at
  `7189216` in `tracer/src/instruction/floor_access.rs:66–75` (LD and SD through `tracer/src/emulator/mmu.rs:323,
  431`), and jolt-cpp in `checked_memory_address` (`isa/riscv.hpp:257–268`, 821e1bd), as gold ports it
  (`pipeline/gold/src/gold/Isa.java:60–68`).
- **The errors** are kept for what plausibly comes from corruption somewhere, a host table or a page damaged on
  the way or a hardware fault, where hanging or running on would hide it; the firmware's own mistakes with the
  Tracer are UB. A failed assertion is a run the R1CS cannot prove, such as a wrong quotient in the host's advice.
  Leftover advice at a segment's end is checked in software (17.9.11), since it depends on the run's data.
- **One log FIFO in row order,** not three, since the write log's E[15] depends on the order of loads and stores
  (17.8.12): set 2 must update it on exactly the load rows the replayer will, which only the tag tells it once the
  rows are gone.
- **The miss rule** of the encoders, 32-bit exactly when the top 32 bits are 0, is gold's (item 151): any fixed rule
  decodes, but set 2 must reproduce set 1, and gold the Tracer.
- **A request only goes up for data already in its queue** (17.9.6's credits), so a transfer that answers a
  `_ready` event never waits on the scan, and a full queue stalls its scan only while its own event is up: no
  deadlock, whatever order the core serves them in.
- **The template bitmap** tracks the inlines only, so that a large inline's template need reach only the tiles
  whose windows use it (17.8.13; item 115). The simulator flags an inline expansion that completes a row with no
  `EXPANSION_TEMPLATE` store since its `new_expansion` (item 145).

#### 17.9.17 Timing and cost

*Kept from v1.35's 4.21.9, at the RTL's figures. 17.9.7 is the contract; this subsection gives what was measured,
and on what. v1.35's own figures are marked so: they are of v1.35's hardware, and v1.36's simulator gives the RTL's. (Not stated by
the RTL's specification; kept from v1.35.)*

**What v1.36's simulator models.** v1.35's model charged a row its schedule and its waits on the memory system,
and nothing else. The RTL (Snarky One) has a pipeline, whose costs beyond the schedule are below; 17.9.7 makes the
first of them the contract's ("a row that sets its successor's pc"), and 17.9.8 says where each is counted. v1.36's
simulator reproduces them, cycle for cycle: its Tracer is the RTL's `tracer_unit` at the block level, and what it
takes from the RTL's sources beyond this table is listed in `OPEN-v1.36.md`. *(Items 153 and 175: whether that
exactness is the requirement is the owner's to confirm.)*

| Path | v1.36: the RTL [meas: RTL] | v1.35's model |
|---|---|---|
| a row of class S or L; of class M | 4; 6 (17.9.7) | `tr_row_cycles` 4; `tr_mul_cycles` 6 |
| a row that sets its successor's pc: a control transfer, a branch taken or not, the program's end | the next row starts 5 cycles after its schedule would have it, every access a hit; in `STAT_STALL_FETCH` | 0 |
| a run's first instruction, but one the last run left inside an expansion | starts 6 cycles after `START`'s effect | the cycle after `START` |
| a row under the rd = x0 NOP policy (17.8.5) | + 2: the template's first ROM word is read for its `rd0`, then ROM word 1, as on the replayer (17.8.16) | 0 |
| the successor of a core-fed instruction | + 1 | 0 |
| an instruction miss | its fills, and about 2 cycles | its fills |
| a data miss | its fill, and 5 cycles | its fill |
| a probe | 2 cycles a line after its response (17.9.7), and 4 cycles around its reads | `tr_probe_compare` 2 a line |
| a cache hit, a jump-LRU hit | no cycle (17.9.4, 17.9.5) | the same |
| the rows' end to the stream word, the length words and `state_ready` | the stream word written 2 cycles after the rows' end; the length words and `state_ready` the cycle after that | `state_ready` the cycle after the last row; the scans from the cycle after that |
| the drain | one FIFO entry a cycle a scan; one packet a cycle (17.9.7) | the same |
| `done` | at most 2 cycles after the DMA's last pop | the cycle after |
| a write into the region | within `PERIPHERAL_LATENCY`; the RTL's lands at 2 | `rp_latency` 2 |
| a fill | the cache's (23.6.7): 2 cycles a level on hits, a line from LPDDR about 80 | the model's, 17.9.14: 6 a hit, 80 a miss, depth + 1 accesses |

**The real workloads,** the Tracer alone under an ideal controller, 128 windows of EVM Zone and 64 of Cyclone
(8,388,608 and 4,194,304 rows; 2,555,539 and 1,488,485 of them padding rows after the program's end):

| | EVM Zone | Cyclone |
|---|---|---|
| the rows' schedules, cycles a row (4, and 6 for an M row) | 4.0773 | 4.1010 |
| the RTL, each fill answered the cycle after it is taken: the rows' cycles; a row [meas: RTL] | 37,439,531; 4.4631 | 18,525,430; 4.4168 |
| – `STAT_STALL_FETCH` | 3,038,589 | 1,138,611 |
| – of it, the rows that set their successor's pc, 5 cycles each | 592,198 rows | 222,571 rows |
| – `STAT_STALL_DATA`; `STAT_STALL_MAP` | 48,720; 146,237 | 66,300; 116,634 |
| the drain, cycles a window | about 54,650 | about 53,240 |
| v1.3's model on its memory system (17.9.14): the rows' cycles; a row [meas: v1.3] | 35,816,438; 4.2697 | 18,593,591; 4.4331 |
| – its stalls: the fetch's, the data's, the probes' | 450,338; 430,470; 727,084 | 259,754; 653,758; 473,858 |
| the RTL at that model's mean waits (a miss 55 and a probe 35 cycles; 62 and 28): a row [meas: RTL] | 4.6506, + 8.9 % | 4.7396, + 6.9 % |
| instruction misses; data misses; write-backs; probes, each one line (exact: equal on both) | 7,865; 8,120; 4,337; 20,891 | 3,775; 11,050; 7,917; 16,662 |

At like waits the rows that set their successor's pc are 93 % and 87 % of the difference. Real code has one every
7.5 and 8.6 instructions.

**On the chip** [meas: RTL], with the RTL's firmware and its tests' memory system (reads answered in 2 to 40
cycles), from the Tracer's first `START` to its last `done`: 5.51, 5.26 and 5.38 cycles a row for the three
segments (EVM Zone's windows 0–63 and 64–127, Cyclone's), 23.1 M, 22.1 M and 22.6 M cycles, the drains 0.74 to 0.94
of a cycle a row. v1.35 gave a segment as about 21 M cycles [meas: v1.3] and the replay after it as about 236 K.
So a segment is about 23 ms at 1 GHz, inside the budget (17.9.13), before the memory system's real latencies.

**The drain's bound** is both FIFOs full, about 784 KiB a window at a packet a cycle, about 0.05 ms at 1 GHz
[estimate], against about 1.6 ms of budget a window.

**Memory:** about 849 KiB of SRAM, 784 KiB of it the two FIFOs (17.9's table), on the tracer tile only. **Logic:**
an RV64 fetch and RVC expansion, the replayer's decode, operand resolution, ALU, multiplier and comparator, the
assertions' checks, the address checks, the probe's multiplier and 16 comparators, the encoders with their size
counters, and three packers. Chapter 24 budgets the compute tiles, not the tracer tile.

#### 17.9.18 The simulator's checks and parameters

*Kept from v1.35's sections 9 and 10, the Tracer's parts: its part of invariant 16 (chapter 20) and its row of the
parameter summary (19.1), in v1.36's terms. (Not stated by the RTL's specification; kept from v1.35.)*

**Invariant 16, the Tracer's part.** The simulator checks:
- no write to an unmapped offset of its region or of another size than 128 bits, and no part-0 write while
  `STATUS.busy` is 1, the drain included (UB: 17.9.2);
- the state write only at a segment's start: it flags a state write that finds the advice field above 0 (17.9.15);
- no `START` with no state write since the reset (UB: 17.9.3);
- no store to `RUN_CONFIG`, `HASH_MUL`, `HASH_STEP` or `HASH_LINES` while busy, no run of more than 65,536 rows,
  and no `log_k` above 32 (UB: 17.9.1, 17.9.2);
- every advice refill but the segment's last exactly half a queue, and none beyond the credit taken (17.9.11);
- an inline's `EXPANSION_TEMPLATE` stored before its expansion's first word (17.9.11);
- no transfer that asks for more than its queue's credits cover, and none that reads output queue 0 past word 67
  (UB: 17.9.6);
- no jump target at offset 0xFFFF_FFFF (UB: 17.9.4);
- none of the Tracer's errors (hardware: `ERRORS` [0]–[9], 17.9.1);
- no expansion word sent on a new segment's fresh credits for an expansion the core was feeding before its record
  (17.9.11; caught as wrong rows or a queue overrun);
- every delivery into a compute tile counted on a counter it armed, its record's pieces only after its `ARMED`
  and its streams only after its `STREAMS_ARMED` (17.9.10; invariants 9 and 22);
- no advice left, and the end state equal to the next segment's start, at a segment's end (the tracer tile's
  software, 17.9.11);
- and its streams decode, on the replayer, to the rows of the same window traced by gold (17.9).

**Parameters.** The contract's are `TRACER_WINDOW_ROWS` and `MAP_PROBE_LINES` (19), and the sizes of 17.9's table.
The simulator's own:

| v1.35's parameter | v1.36 |
|---|---|
| `tr_row_cycles` 4, `tr_mul_cycles` 6 | the contract's schedules (17.9.7); no longer parameters |
| `tr_probe_compare` 2 | the contract's (17.9.7) |
| `tr_probe_lines` 3 | `MAP_PROBE_LINES` (19) |
| `ms_line_hit` 6, `ms_line_miss` 80 | kept: v1.36's model of the memory system, a setting of v1.37's simulator for comparison (17.9.14); the cache's figures are 23.6.7's |
| `rp_latency` 2 | `PERIPHERAL_LATENCY` (19) |
| `stage_delay` 64 | removed (17.9.15) |

## Open items (chapter 26's numbering)

| Item | What is to be settled | Section | Status |
|---|---|---|---|
| 144 | The Tracer's `EVENTS` and `ERRORS` positions: v1.35 kept the replayer's positions, with gaps (accepted, owner, 2026-11-05); v1.36 packs both from bit 0, as the RTL does (item 161, in chapter 26) | 17.9.1 | closed, and reversed in v1.36 (161); for the owner to rule on |
| 147 | To measure: the caches' sizes, by gold replaying both workloads | 17.9.5 | decided, part open |
| 148 | The fill port's statuses. v1.36 adds the write's status and `write_back_error`; v1.37's cache answers an LPDDR error on a read `beyond`, with its own error set, since the read's four statuses are used (23.6.4; RCR comment E) | 17.9.5, 17.9.9 | decided |
| 153 | The Tracer's timing. The RTL's values are in 17.9.7 and 17.9.17, and v1.36's simulator reproduces the RTL's Tracer cycle for cycle (item 175); what stays open is the memory system's latencies | 17.9.7, 17.9.17 | part closed by the RTL; part open |
| 157 | The statistics counters' attribution: the RTL's, 17.9.8, which the simulator follows | 17.9.8 | closed by the RTL |
| 174 | The memory system with a read and a write outstanding together, and its write status. Closed in v1.37 by the cache: its fill read and fill write have a register each, and a write is answered `ok` or `error` by the walk (23.6.4, 23.6.5) | 17.9.14, 23.6 | closed (v1.37) |
| 175 | Whether the simulator reproduces the RTL's Tracer cycle for cycle, or its rows, counts and stall classes with the table's costs as constants. v1.36's simulator does the first: its rows, counters and cycles equal the RTL's at the block level and on the chip (`OPEN-v1.36.md`) | 17.9.17 | built cycle for cycle; for the owner to confirm |
| 176 | The `op` codes of `ARMED` and `STREAMS_ARMED`: the tracer tile's firmware's (17.9.10); the RTL's firmware uses 1 and 2 | 17.9.10, 11.5 | open |
| 177 | The cycle of the rows' end's effects: 17.9.1 and 17.9.6 give the length words, the stream word and `state_ready` "when the rows end"; the RTL writes the stream word 2 cycles after and raises the others the cycle after that | 17.9.6, 17.9.17 | open |
