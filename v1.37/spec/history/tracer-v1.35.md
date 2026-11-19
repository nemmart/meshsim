# The Tracer: machine specification, v1.35

*Part of the machine specification (`spec/machine-spec.md`), moved here whole in v1.35 with its section numbers: 4.21, its window (8.16), its invariant (section 9, 16) and its parameters (section 10).*

### 4.21 The Tracer (peripheral type 3)

*Figures 9.1 (the Tracer), 9.3 (the state record); 9.2 (the output path) is to come.*

The **Tracer** is **C&C 3's peripheral** (2, 6.6): the RV64 row machine that executes the guest program and writes each window's **run log**, the state record and the three streams that the tiles' replayers consume (4.19.2). It runs one **segment** of 2²² rows per proof, as 64 runs of one window each, and stops at the end of every run until its output is drained. It is **peripheral 1, type 3**, and uses the standard window throughout (4.17). This section specifies it in full for the model and the RTL; it states `spec/history/DECISIONS-v1.3.md` sections 1–25 as decided (cited **§*n*** in this section; the owner's rulings on SP1's plan, 2026-11-05, are recorded there up to 830b8e7), **the answers to the seventeen questions that building it in meshsim raised** (B1, WORKER22; cited **B1 Q*n***, `v1.3/planning/sessions/WORKER22-SPEC-QUESTIONS.md`, distinct from SP1's questions, cited "owner, 2026-11-05, Q*n*", all answered by the owner on 2026-11-05 and 2026-11-06), and the owner's rulings of 2026-11-06. Its own additions are gathered in section 13 (items 144–158). **It states defaults** (the front matter's "How v1.3's text is meant"): what the Tracer must do observably is the rule, and where a mechanism is given it is a reasonable way to get there, which the RTL may better. 4.21.1–4.21.9 are the hardware; **4.21.10 is the software contract** around it, the segment's sequence on the chip.

#### 4.21.1 Overview

**What it shares with the replayer, and what it doesn't** (§25). The Tracer is **its own core**, not the replayer with a memory path added. What the two share is **formats and tables, not the design**: the 68-word state record (4.19.2), the region map (4.19.10), the universal decode table, ROM and controller-fed templates (4.19.5, 4.19.6, 4.19.9), the controller-fed expansion path with its event and words, the read queue's role (both pop advice from it), and the window's words 32–35 (4.17.4). **Its rows are the replayer's rows:** it expands each instruction by the same templates under the same rules (4.19.3–4.19.7), so it knows exactly which log words the replayer will pop, in which order, and where every window boundary falls, even inside an expansion. What it adds is everything the replayer takes from the log: it fetches the program from memory, loads and stores the guest's memory, takes advice from the host, and finds each jump target's `instr_idx` in a map.

**The parts** (§1):

| part | size | role | section |
|---|---|---|---|
| state record, part 0 | 68 × 128 bits: state word, stream word, register image (the working register file, 128 × 64 bits), files word, template bitmap | the working state, loaded once per segment; read in place as output queue 0 | 4.21.2 |
| decode table and ROM, part 0 | 4 KB and 8 KB, the replayer's universal tables | instruction → template; one expansion word per row | 4.21.4 |
| read queue, part 2 | 4 KB, 256 × 128 bits | the segment's advice, from the host through C&C 3 | 4.21.3 |
| expansion queue, part 4 | 4 KB, 256 × 128 bits | controller-fed expansion words (the inlines, the CSR instructions) | 4.21.4 |
| instruction and data caches | 16 KB each, 128 lines of 128 B, fully associative | the guest's memory file, line by line | 4.21.5 |
| jump LRU | 64 entries, pc → `instr_idx` | the jump-target map, in front of its file | 4.21.4 |
| range comparator | one subtract and one compare | the provable range, on every load and store | 4.21.5 |
| code FIFO and log FIFO | 65,536 × 32 bits (256 KB) and 65,536 × 66 bits (528 KB) | one window's raw streams | 4.21.6 |
| compressors | two sets of three 16-entry LRU encoders | set 1 sizes each stream as it is traced; set 2 compresses on the way out | 4.21.6 |
| packers and output queues 1–3 | three packers; 4 KB each, 256 × 128 bits | the streams in 128-bit words, for the DMA | 4.21.6 |
| DMA | 4.17.9 | sends output queues 0–3 | 4.21.6 |

**Its interfaces.** C&C 3 reaches it as every peripheral (4.17): mode-0 packets into its region, 0x04 on C&C 3 (4.21.2), and loads and stores to its window (4.21.3, 8.16). It has **no host channel** (§25): its inputs are the record, the advice and the controller-fed expansion words. It reaches the guest's memory and the jump-target map as **files**, by 128 B lines, over a **fill bus** to the memory system, which translates them (4.21.5, 6.7). Its output leaves by its DMA, on C&C 3's free router link (4.17.9).

**What a segment is** (§5, §14, §15, §24). A proof is the whole run, 2²⁹ rows for our workloads, spread over the chip's 128 ASICs; each ASIC's Tracer traces **one segment of 2²² rows** from the processor state the host sends for it (frame 2), in **64 runs of `RUN_CONFIG` [23:0] = 65,536 rows**, one per replayer window. Between runs nothing is loaded and nothing is flushed: the working state, the read queue, the caches and the jump LRU carry straight on, and a run boundary is only a cut point in the run log. A new state is written only at the next proof's segment.

**Rate** (§11; owner, 2026-11-05, the timing parameters). **A row takes 4 cycles, or 6 for a multiply**, plus stalls (4.21.9) [estimate: §11's about 4 cycles per row]. A 2²²-row segment is then about 17 M cycles, 17 ms at 1 GHz, against a budget of **100 ms per segment** (§11), the misses and the drain included; the jump map's worst segment costs under 1 % (§18).

#### 4.21.2 Its region and the state record

**The map is the replayer's** (§12; 4.19.10): mode-0 packets to region 0x04 at a 24-bit byte offset, **offset bits [22:20] picking the part**, every part taking 128-bit packets only (a 32- or 64-bit packet, or a misaligned one, is UB). The same loader code serves both units.

| offset (bytes) | part | the Tracer | sizes taken | ports |
|---|---|---|---|---|
| 0x000000–0x00000F | 0 | **state word** (word 0): pc [63:0], `instr_idx` [95:64], next offset [111:96]; [127:112] reserved and spare as on the replayer (4.19.2). **Its write resets the Tracer** (4.21.3) | 128 | single |
| 0x000010–0x00001F | 0 | **stream word** (word 1): the three output streams' fields and **the advice field, the live count** (below) | 128 | single |
| 0x000020–0x00041F | 0 | **register image** (words 2–65): **the working register file**, registers 2*i* and 2*i* + 1 in image word *i* (4.19.2) | 128 | single |
| 0x000420–0x00042F | 0 | **files word** (word 66, below) | 128 | single |
| 0x000430–0x00043F | 0 | **template bitmap** (word 67): **an output**; a write is accepted and ignored | 128 | — |
| 0x001000–0x001FFF | 0 | decode table, laid out as the replayer's (4.19.10) | 128 | single |
| 0x002000–0x003FFF | 0 | ROM, word *j* at 0x2000 + 16*j* | 128 | single (item 156) |
| 0x100000–0x1FFFFF | 1 | **unused**: the Tracer has no code input (zero length) | — | — |
| 0x200000–0x2FFFFF | 2 | **read queue port: the segment's advice** | 4 KB queue | dual |
| 0x300000–0x3FFFFF | 3 | **unused**: no write-log input (zero length) | — | — |
| 0x400000–0x4FFFFF | 4 | **expansion queue port** | 4 KB queue | dual |
| everything else | 0, 5–7 | unmapped | — | — |

A write to an unused or unmapped offset is UB (meshsim), as on the replayer (4.19.10). **The decode table and the ROM are loaded once, at startup**, over the same path from C&C 3 that later fills the queues (owner, 2026-11-04, figure 9.1), with the Tracer stopped. They hold **the replayer's universal tables, unchanged** (§13; `data/tables/replayer/`): the Tracer's templates are the replayer's. *(Item 156, accepted, owner, 2026-11-05: the Tracer's ROM is single-ported, written only while stopped, since nothing reloads it while tracing; the replayer's second port serves loads that the Tracer never has.)*

**The record** is the 68-word superset record of 4.19.2 (DECISIONS-v1.26 item 17), in part 0 at consecutive offsets from 0x000. Part 0 is **the working state** (§24): writing words 0–65 writes the live pc, `instr_idx`, offset and registers, and running changes them in place. How the Tracer uses each word:

| word | at load (segment start) | while tracing | read out at the end of run *n* (output queue 0) |
|---|---|---|---|
| 0, state word | frame 2's pc, `instr_idx` and next offset | live | **window *n* + 1's start** |
| 1, stream word: code, read-log, write-log fields ([101:0]) | written 0 by C&C 3; ignored | — | **run *n*'s totals** (4.21.6): words, values out and compression flag per stream |
| 1, stream word: advice field ([124:102]) | **the segment's advice values** (frame 3's count, 0 to 2²²) | **counts down by one per advice value popped** | the advice still unconsumed (state, part of window *n* + 1's start) |
| 2–65, register image | frame 2's registers 0–127 | live (register 0 reads 0, 4.19.7) | **window *n* + 1's start** |
| 66, files word | the Tracer's copy of memory and the proof's map (§22), log₂ K and `lowest_address` (from frame 7, the proof parameters: owner, 2026-11-05, item 155) | constant | unchanged (window *n* + 1's, which a replayer ignores) |
| 67, template bitmap | ignored | built as the run goes (4.21.4) | **run *n*'s totals**: the inlines the window used |

So at the end of run *n* the 68 words are mixed (§24): **words 0 and 2–66 are window *n* + 1's start, and words 1 and 67 run *n*'s totals** (the advice field apart, which is state). C&C 3 sends each piece where it belongs (4.21.6). Nothing writes a state between runs; window 0's start is the state C&C 3 loaded.

**The files word, word 66** (§23; owner, 2026-11-05: SP1's layout, figure 9.3's order), least significant bit first:

| bits | field | use |
|---|---|---|
| [30:0] | **memory root**: the root of the Tracer's copy of the guest's memory file (`.pages`), in 128 B units | every instruction and data fill (4.21.5) |
| [32:31] | **memory depth**, 0–3 (2 for K up to 2²⁹ cells, 3 above, §20) | the same |
| [63:33] | **map root**: the root of the proof's jump-target map file | every map probe (4.21.4) |
| [65:64] | **map depth**, 0–3 | the same |
| [71:66] | **log₂ K**, the proof's RAM size in 8-byte cells, up to 32 (§14) | the range comparator (4.21.5) |
| [102:72] | **the I/O region's size, 0x8000_0000 − `lowest_address`, in bytes**: exact, whatever its alignment (a16z puts the I/O region just below `RAM_START_ADDRESS`, 0x8000_0000; the host rejects a layout whose I/O region is 2 GiB or more; owner, 2026-11-05, replacing `lowest_address` / 4 KB at [123:72], which assumed a page-aligned region and truncates for small guests) | the `.pages` offset of every access, the range comparator and the map's keys |
| [127:103] | spare, 0 | |

`lowest_address` is 0x8000_0000 − word 66 [102:72], exact. **The stream word's advice field** (4.19.2's [124:102]) is the Tracer's input size and its live count in one: advice is never compressed and always 64-bit, so its size is a value count only, and its words are ⌈values / 2⌉. The replayer ignores it, and a record carries it through unchanged.

#### 4.21.3 Control: the window, the pulses and the queues

**Exactly two pulses** (§24; owner, 2026-11-04): **`PULSE_RUN`** runs the next `RUN_CONFIG` [23:0] rows and drains their output (4.21.6); **`PULSE_DMA`** starts a DMA transfer from one of the four output queues (4.17.9). **There is no start pulse:** the write to word 0 does all the resetting.

**The write to word 0, the state word, at part 0 offset 0x000** (§23, §24), as it lands:
- **empties every queue**, input and output: the read queue and the expansion queue, the code and log FIFOs, output queues 1–3, and output queue 0's read pointer (4.21.6);
- **sets the credits:** the advice credit and the expansion credit to 2 (below); every output queue's credits to 0;
- **clears the counters:** the read queue's half-entry pointer (an entry holds two advice values) and the queue-length registers (words 37–39); the advice field itself is the count, written with word 1 after it;
- **invalidates the instruction cache, the data cache and the jump LRU, without writing back dirty lines** (§21, §23): the previous segment's copy of memory is throwaway, so its dirty lines are simply dropped;
- **clears the template latch** (4.21.4) and the template bitmap;
- **clears the statistics counters**, words 44–51 (below).

That is safe although words 1–67 follow, because the caches fill only during execution, which begins at `PULSE_RUN`. **Word 0 is written only at a segment's start** (§24): a rewrite mid-segment would flush the read queue and the caches and lose the segment's progress, so **a mid-segment rewrite is UB** (meshsim can't tell a rewrite from a new segment's record, so it flags a word-0 write that finds advice still unconsumed, the advice field above 0). Words 1–67 are written only while the Tracer is stopped, like every part-0 write (a write while running is UB); firmware writes them only at a segment's start, in the record's order, word 0 first.

**`RUN_CONFIG`** (word 32, R/W; v1.26.1 and §3; owner, 2026-11-05):

| bits | field | the Tracer |
|---|---|---|
| [23:0] | rows to run | **0 to 65,536**: the run traces exactly that many rows, stopping exactly there, even inside an expansion. **More than 65,536 is UB** (meshsim; owner, 2026-11-05): the FIFOs hold one window (§2), so a longer trace is run in chunks of at most 65,536 rows |
| [24], [25] | the replayer's use the store log and output `rd_prev` | ignored: the Tracer always writes all three streams |
| [26] | **compress code** | 1: the window's code stream goes out LRU-coded if that is smaller (4.21.6); 0: always raw |
| [27] | **compress read log** | the same for the read log |
| [28] | **compress write log** | the same for the write log |
| [31:29] | reserved | |

The replayer ignores [28:26], as the Tracer ignores [25:24]. As on the replayer: written while stopped and used by the next `PULSE_RUN` (a write while running is UB, meshsim); **sticky**, untouched by the pulses, the word-0 write and done; **0 after the reset**. A row count of 0 traces nothing: the run's output is the state queue alone (4.21.6), and done follows its drain.

**`PULSE_RUN`** (window word 3): sets `STATUS` [0], clears `ROWS_RUN`, and **starts a window**: it resets both compressor sets' tables to all zeros (§4), the packers, output queues 1–3 and their credits, output queue 0's read pointer, the queue-length registers and the template bitmap (then sets the latched template's bit if the run starts inside a controller-fed expansion, 4.21.4). The first row starts in the next cycle. **It leaves alone everything that carries across the segment:** the working state, the read queue and its count, the expansion queue and its credit, the caches, the jump LRU and the template latch. A `PULSE_RUN` while a run is in progress is UB (4.17.2).

**`STATUS`** (word 1): [0] **run in progress**, from the end of the `PULSE_RUN` store's cycle until done, **the drain included** (4.21.6); [1] **DMA in progress** (4.17.9). Other bits read 0. While [0] is 1 the Tracer owns part 0: a part-0 write is UB (meshsim), and output queue 0 reads part 0 in place.

**Words 33–35** are the replayer's (4.19.9): **`ROWS_RUN`** counts this run's rows in [23:0]; **`EXPANSION_OFFSET`** is the row within the current expansion where the next row starts, where C&C 3 begins sending a controller-fed expansion's words (non-zero only at a segment's start inside one, since a run boundary inside an expansion doesn't present it again, 4.21.4); **`EXPANSION_OPCODE`** is the code-stream word of the instruction awaiting its expansion (4.21.4), set when new expansion is posted and held until the next.

**Words 36–43** (owner, 2026-11-05, SP1's proposal; §18, §24):

| word | register | access | bits | meaning |
|---|---|---|---|---|
| 36 | `STATE_LENGTH` | R | [15:0] | output queue 0's length in 128-bit words: **always 68** |
| 37 | `CODE_LENGTH` | R | [15:0] | output queue 1's stream, in 128-bit words: the code stream's words as it goes out (4.21.6) |
| 38 | `READ_LOG_LENGTH` | R | [15:0] | output queue 2's, the read log's |
| 39 | `WRITE_LOG_LENGTH` | R | [15:0] | output queue 3's, the write log's |
| 40 | `HASH_MUL` | R/W | [31:0] | the map's multiplier *p*₁, odd (4.21.4) |
| 41 | `HASH_STEP` | R/W | [23:0] *p*₃; [31:24] reserved | the map's probe step, a 24-bit prime (so odd) |
| 42 | `HASH_LINES` | R/W | [4:0] *k*; [31:5] reserved | the map has 2*ᵏ* lines of 128 B |
| 43 | `EXPANSION_TEMPLATE` | W | [6:0] template number, [31] valid; [30:7] reserved | the template C&C 3 is feeding (4.21.4); a load reads 0 |
| 44 | `STAT_CYCLES` | R | [31:0] | cycles with `STATUS` [0] at 1 (item 157): running, the drain included |
| 45 | `STAT_STALL_FETCH` | R | [31:0] | cycles a row waits for an instruction fill (one or two, 4.21.5) |
| 46 | `STAT_STALL_DATA` | R | [31:0] | cycles a row waits for its data line: the fill, **and the wait for the write-back buffer** (owner, 2026-11-06) |
| 47 | `STAT_STALL_MAP` | R | [31:0] | cycles a row waits for its map probe: the lines' fills and comparisons |
| 48 | `STAT_STALL_OUTPUT` | R | [31:0] | cycles in which **either drain scan is stalled on a full output queue** (owner, 2026-11-06; 4.21.6): one count per cycle, whether one scan or both |
| 49 | `STAT_MISS_I` | R | [31:0] | instruction-cache misses: lookups that missed, a straddling instruction's two halves each its own |
| 50 | `STAT_MISS_D` | R | [31:0] | data-cache misses |
| 51 | `STAT_PROBES` | R | [31:0] | jump-LRU misses, each starting a map probe (4.21.4) |

Words 37–39 are **0 from `PULSE_RUN` until the run's rows end**, then **fixed and exact** until the next `PULSE_RUN` (the stream word's word counts, 4.21.6). Words 40–42, **the hash's constants**, are context, not state (§18: word 66 has no room), so they are registers: **written once per proof** while the Tracer is stopped (a write while running is UB), sticky, untouched by the word-0 write, 0 after the reset.

**Words 44–51, the statistics counters** (owner, 2026-11-06; item 157): a minimal fixed set for bring-up, so the RTL and meshsim can be compared counter for counter. Each is **32 bits, wrapping**, read-only (a store is a bus error), **0 after the reset and cleared by the word-0 write**, so they count across a segment's runs (`PULSE_RUN` leaves them alone; a segment is about 21 M cycles, well inside 32 bits); they **count while `STATUS` [0] is 1**, which includes the drain and, after a stopping error, the cycles until the reset (4.21.8). A stall cycle is counted in the cycle it happens, so a load mid-stall reads the cycles so far. **The waits on C&C 3 are in none of them:** an advice row waiting on an empty read queue, a controller-fed expansion waiting on new expansion or on the expansion queue (4.21.9). The attributions are **defaults** (owner, 2026-11-06): the RTL may refine them (a finer split, a cause counted differently) and say so, and meshsim follows. Words 52–127 are reserved.

**Its events** (4.17.3) (the flag set §24's; the bit positions SP1's, item 144, accepted, owner, 2026-11-05). The positions shared with the replayer are kept, so the advice credit sits where the replayer's read-log credit does:

| bit | event | kind and depth |
|---|---|---|
| [0] | **run done:** the run's rows are traced **and its output is drained**: every output queue empty and the last transfer finished (4.21.6). So done means ready for the next `PULSE_RUN` | counted, depth 1 |
| [1] | **DMA done** (4.17.9) | counted, depth 1 |
| [2] | reserved (the replayer's code credit) | — |
| [3] | **advice credit:** half the read queue is free (below) | counted, depth 2 |
| [4] | reserved (the replayer's write-log credit) | — |
| [5] | **expansion credit:** as the replayer's (4.19.8) | counted, depth 2 |
| [6] | **new expansion** (§24's "expand"): the Tracer has reached an instruction whose decode class is controller (1 or 2), presents it in `EXPANSION_OPCODE` and waits | counted, depth 1 |
| [7] | **state queue needs service:** the run's rows are done; the sizes (words 37–39) are fixed and output queue 0 is ready to read (4.21.6) | live |
| [8] | **code queue needs service:** output queue 1 holds a credit | live |
| [9] | **read-log queue needs service:** output queue 2 holds a credit | live |
| [10] | **write-log queue needs service:** output queue 3 holds a credit | live |

[31:11] are reserved. The service flags are **live** (4.17.3): each is up while its queue holds a credit, and falls when the DMA has read what the credit covers (4.21.6); a load of `EVENTS` doesn't consume them. They reach C&C 3's `EVENTS` [13] while masked, like every peripheral event.

**The input queues and their credits** (§13, §24; owner, 2026-11-05: the advice credit and the expansion credit, both the replayer's mechanism). Both are true FIFOs of 128-bit entries, dual-ported, pushed through their ports (4.19.8's rules, one source at a time):
- **The read queue, part 2: the segment's advice**, 4 KB, 512 values, two per entry, low value first. **Its credit, `EVENTS` [3], is half a queue**, 128 entries (256 values): the word-0 write sets it to **2**, and the Tracer returns one each time it has popped 128 entries since the last return. **A credit is permission to send up to half a queue**, as on the replayer; every refill but the segment's last is exactly half (a software rule). **The advice is per segment, not per run:** the queue and its count carry across the 64 runs.
- **The expansion queue, part 4**, 4 KB, **identical to the replayer's** (§24; 4.19.8): depth-2 credits of half a queue, set to 2 by the word-0 write and again at each new expansion, so a template longer than the queue (KECCAK's 3,121 words against 256 entries) streams in as the Tracer consumes it.
- **Overrun:** a push into a full queue is dropped and sets `ERRORS` [1] (4.21.8), checked as on the replayer.

**The controller's loops** (C&C 3's firmware, for orientation; the rules are the text above, 4.21.6 and 4.21.10; meshsim's `fw/tracer/tracer_ctl.c` is the working version). Per proof, once the update stream is applied and the Tracer's copy exists (§22):

```
    (at startup, once) load the decode table and the ROM into part 0 (0x1000, 0x2000)
    tr->HASH_MUL = p1; tr->HASH_STEP = p3; tr->HASH_LINES = k      ; per proof, from the program's map
    tr->RUN_CONFIG = 65536 | COMPRESS_CODE | COMPRESS_READ | COMPRESS_WRITE
    drop any expansion being fed (its template, position, pending chunks)   ; B1 Q14: the word-0 write resets
          the expansion credit, and the Tracer presents the expansion again
    write record words 0-67, word 0 first: frame 2's state; word 1's advice field = frame 3's count,
          its other fields 0; frame 2's registers; word 66 from C&C 3 (section 22) and frame 7; word 67 0
    tr->EVENTS_MASK = RUN_DONE | DMA_DONE | ADVICE | EXPANSION | NEW_EXP | SVC_STATE | SVC_CODE | SVC_READ | SVC_WRITE
    preload advice: up to two half-queue refills from frame 3 (the credits are already 2)
    wait for C&C 1's start (a landed command, after the first barrier's GO), then stage_delay (4.21.10)
    send window 0's start, record words 0 and 2-66 from C&C 3's own copy, to tile 0's record   ; B1 Q3
    for window n in 0..63:
        tr->PULSE_RUN = 1
        loop: WFI; ev = tr->EVENTS & tr->EVENTS_MASK             ; consumes the counted bits only
            new expansion: key(EXPANSION_OPCODE) -> template t; if t is an inline:
                           tr->EXPANSION_TEMPLATE = VALID | t; feed t's words from EXPANSION_OFFSET on its credits
            expansion / advice credit: send up to half a queue of what remains
            state service: arm a counter for tile n's "armed"; then four transfers from queue 0, in its
                           order (4.21.6): word 0 to tile n+1, word 1 to tile n, words 2-66 to n+1, word 67 to n
                           (n = 63: all 68 to C&C 3's own memory, then words 1 and 67 forwarded to tile 63; B1 Q4)
            code / read / write service, once tile n has said "armed": one transfer of what the credit
                           covers, to tile n's stream area
            run done: next window
    check the advice left (word 1's field, from the 64th record) is 0, and the end state
          (words 0, 2-65) equals the next segment's frame 2; else report (section 24)
```

C&C 3 waits for DMA done between transfers (one at a time, 4.17.9), and re-reads `EVENTS` after each, since the service flags are live. Until tile *n* has said "armed" its stream queues simply wait full, their scans stalled (4.21.6): no deadlock, since the state service never depends on them.

#### 4.21.4 Execution

**The machine** (§13, §25). The Tracer executes the guest as **the rows of Jolt's expanded trace**, one row at a time, exactly as 4.19.7 defines them and `spec/jolt/replayer-opcodes.md` computes them, on a **128-register machine**: registers 0–127, x0–x31 and the virtual registers 32–127, held in the working register file that is the record's image; register 0 reads 0 and drops writes. There is no second bank, no untraced row and no `advice_reg` (§13: the host supplies every advice value). Rows are strictly sequential, as on the replayer, so a row's reads see the previous row's writes.

**Per native instruction:**
1. **Fetch** the instruction at pc through the instruction cache (4.21.5): 16 bits at pc; **if bits [1:0] are 11** it is a 32-bit instruction and the next 16 bits follow, possibly from the next line, even the next page (§25); otherwise it is a 16-bit compressed instruction.
2. **Form its code-stream word** (4.19.2): a 32-bit instruction as it is; **a compressed one decompressed** to its 32-bit form by the RISC-V C extension's expansion as a16z's decoder does it (`crates/jolt-riscv/src/uncompress.rs` at 7189216; gold's, `pipeline/gold/`), **with bit 0 cleared** as its flag. a16z decides where jolt-cpp differs (item 113): `C.SLLI` with rd = x0, a HINT, is `SLLI x0, x0, shamt` (UPSTREAM-DIFFS D12). A 16-bit encoding with no 32-bit form, `c.unimp` (0x0000) among them, is an invalid expansion, `ERRORS` [2] (§17: it throws only if executed). The word goes into the code FIFO as the window's next code value (4.21.6).
3. **Find its template** by the decode table (4.19.5) from the code-stream word: class 0, the ROM from `start`; class 1 or 2, **controller-fed** (below); class 3, an invalid expansion, [2]. The rd = x0 policy and the temporary offset apply exactly as on the replayer (4.19.4), the offset from the code-stream word's rd field and the template's first word.
4. **Run its rows**, one expansion word each (4.19.3), until the word with `seq_remaining` = 0, then advance pc as 4.19.7 says.

At a run's start inside an expansion (a segment whose frame 2 has a non-zero next offset, or any run after a run that stopped inside one), the instruction in progress is **the window's first code value again**, as the replayer expects (4.19.2), and its rows continue from the next offset: a ROM template from `start` + offset, a controller-fed one from the expansion queue as it stands. At a segment's start the Tracer fetches the instruction at pc to form its word; at a later run it still holds it.

**Controller-fed expansions** (4.19.9; §7, §25). When the template's class is 1 (an inline) or 2 (CSRRW, CSRRS), the Tracer **presents the code-stream word in `EXPANSION_OPCODE`** and the row within the expansion in `EXPANSION_OFFSET`, posts **new expansion**, `EVENTS` [6], sets the expansion credit to 2, and waits for words in the expansion queue; it then runs the expansion's rows from the queue as the replayer does, by `seq_remaining`, with `rd0` read from the first word delivered (4.19.4). C&C 3 holds every controller-fed template (the 75 of the universal set, `templates.bin`), computes key(*w*) as a tile's controller does (4.19.9), and feeds the words from `EXPANSION_OFFSET` on its credits. **A run boundary inside a controller-fed expansion doesn't present it again:** the expansion queue, its credit and the latch below carry across the run, and the rows continue from the queue at the next `PULSE_RUN`. **A key with no entry is an error in C&C 3's software** (4.19.9), not the Tracer's.

**The template bitmap and the template number** (§25; owner, 2026-11-05, SP1's proposal). **Word 67 tracks the inlines only** (§25: the 60 CSR templates, 102 words, are always broadcast), so that the big inline templates reach only the tiles whose windows use them (4.19.13). **C&C 3 returns the template number with its answer:** when the template is an inline, it stores **`EXPANSION_TEMPLATE` = [31] valid | [6:0] *t*** before pushing the expansion's first word. The store **sets bit *t* of the run's bitmap** and **latches *t*** as the current expansion's template; a store with [31] = 0 only clears the latch (for a CSR template a store is optional). New expansion clears the latch, and so does the word-0 write. **`PULSE_RUN` clears the bitmap and then, if the run starts inside a controller-fed expansion whose template is latched, sets the latched bit again**, so a window that starts inside an inline lists the inline its replayer will be fed (tile *n* + 1 starts inside it, with `EXPANSION_OFFSET` non-zero). *(Item 145, accepted, owner, 2026-11-05: the store must precede the expansion's first pushed word; meshsim flags an inline expansion that completes a row with no `EXPANSION_TEMPLATE` store since its new expansion.)* At the run's end the bitmap is word 67 (4.21.2).

**What each row does on the Tracer.** A row computes what 4.19.7 and the appendix define (its opcode, slots, rd formula and pc rule), with these departures, which are exactly the replayer's log words turned around:

| row (by its expansion word) | the replayer (4.19.7) | the Tracer | log word written (4.21.6) |
|---|---|---|---|
| a **load row**, LD or `VirtualLoadB/H/W`, `from_log` set | rd = the read-log word | the address *a* by appendix section 3 (the doubleword's, for a narrow row); the checks below; **rd = the doubleword at *a*** from the data cache (dropped if rd is x0) | read log, tag **load**: the doubleword (an LD to x0 included, 4.19.15's N5) |
| a **store row**, SD or `VirtualStoreB/H/W`, `from_log` set | the write-log word is the pre-value | the address *a* and the checks; **reads the doubleword at *a* (the pre-value), then writes rs2's value there** (a narrow store's rs2 is the whole new doubleword) | write log: the pre-value |
| an **advice row**, `VirtualAdvice` with `from_log` set (the DIV/REM advice rows are written so, 4.19.7) | rd = the read-log word | **rd = the next advice value popped from the read queue**; the advice field counts down by one; a pop with the field at 0 is **advice underflow**, `ERRORS` [4]; an empty queue with the field above 0 stalls | read log, tag **other**: the same value |
| a **control transfer**: `next_from_value` set and new pc ≠ pc (4.19.7) | the next `instr_idx` = the read-log word | **the next `instr_idx` = the target's base, from the jump LRU or the map** (below) | read log, tag **other**: the base, zero-extended |
| **the program's end**: a JAL row with immediate 0 and `imm0_pc0` set (below) | new pc 0, the log word 0 | new pc 0, `instr_idx` 0, no lookup | read log, tag **other**: 0 |
| a `VirtualAssert…` row | not evaluated | **evaluated**, by appendix section 2 H; a failing one is `ERRORS` [11] | none |
| a **padding row** (pc = 0, below) | the ROM's padding template | nothing executes | none (the code value 0x00000000) |
| every other row | as 4.19.7 | as 4.19.7 | none |

At most one log word per row (4.19.15, §2): no template has a row that is both a memory or advice row and a control transfer. Everything else follows 4.19.7 unchanged: the self-jump rule (a transfer to its own pc is sequential, with no log word and no lookup), JALR's cleared bit 0, `instr_idx` + 1 on every sequential row, `VirtualPext` for any mask, the zero shift masks, `VirtualHostIO` doing nothing (§25: its effects happened in the host's run). ECALL and MRET are ordinary rows through the virtual CSRs (§25).

**Jump targets: the LRU and the map** (§18; owner, 2026-11-05, SP1's proposal: the map as a file, a 3-line probe). `instr_idx` is a row's index in the committed bytecode, so a jump target's value is its instruction's **base**, the index of its expansion's first row. Sequential flow needs none; **only control transfers look up**, by their new pc:
- **The jump LRU:** 64 entries of (pc, base), fully associative, true LRU (move to front on a hit, the oldest replaced on an insert). **A hit costs nothing.** The word-0 write invalidates it.
- **On a miss, the Tracer probes the map itself.** The **key** is the target's 32-bit offset into the code, **its offset in `.pages`**: *o* = pc − `lowest_address` (64 bits), and **key = *o***, which needs *o* < 2³². The home line is **h = ((key × *p*₁) mod 2³²) >> (32 − *k*)** (*k* = 0 gives line 0), with *p*₁ = `HASH_MUL`, *k* = `HASH_LINES`. The Tracer reads line h of the map file (map root and depth, word 66) over the fill bus (4.21.5), compares its 16 entries at once, and on a full line without a match steps to **(h + *p*₃) mod 2*ᵏ***, *p*₃ = `HASH_STEP`, reading **at most `tr_probe_lines` = 3 lines** (the home line and two steps; the host guarantees no key is placed more than two lines from home, §18).
- **A line** is 16 entries of 8 bytes, entry *j* at bytes 8*j* to 8*j* + 7, least significant byte first: **the key (u32), then the base (u32)**. **A free entry has key 0xFFFFFFFF**, which no target can have (pcs are even).
- **Outcomes:** an entry whose key matches gives the base, inserted into the LRU; **a free entry in a probed line, or three full lines without a match, is "jump target not in the map", `ERRORS` [7]**. **A target with *o* ≥ 2³², or a JALR or branch whose new pc is 0, is the same error at once**, with no probe (§17, §25: a16z fails there) *(Item 146, accepted, owner, 2026-11-05: checking these two directly rather than letting the probe find them)*.
- **The table is software's:** the host builds the static table from the ELF (every direct target, every return address, every code pointer found in data), at most half full, and the proof's supplement (frame 6) is inserted by the same rule into private copies of the pages it lands in (§18, §22). The host and gold compute the same key, so frame 6's pc offsets are *o* as above (item 146). The hash's constants come from the program's map, per proof.

**The program's end and padding** (§25; owner, 2026-11-05: Q5). **The end is any JAL row with immediate 0, whatever its rd**: the templates set `imm0_pc0` in every JAL word (4.19.3), so `JAL x0, 0`, `C.J 0` (decompressed to it), **EBREAK** (whose one-word template is `JAL v40, 0`, 4.19.9) and `JAL ra, 0` (which writes ra, then ends) all end the program, as a16z treats each as the pc stalling. The row is 4.19.2's last real row: its link is written as for any JAL, the new pc is 0, `instr_idx` becomes 0 and its read-log word is 0, with no lookup. **Only that row ends the program:** a JALR or a branch to 0 is the map error above (replacing jolt-cpp's sentinel, any jump to pc 0). **While pc is 0, every row is a padding row:** it fetches nothing, executes nothing, writes the code value 0x00000000 and no log word, and leaves pc, `instr_idx` and the registers as they are, exactly the replayer's padding row (4.19.2, 4.19.6). So a run past the program's end, and a segment whose frame 2 is at pc 0, are traced like any other, every window full to its row count.

**Stores into code** (§25; owner, 2026-11-05: Q10). Jolt executes its fixed bytecode, but the Tracer fetches through memory, so a store into the text would make its trace differ silently from a16z's. **The host rejects such a program** before sending anything, by scanning each segment's write list against the text sections (§17's D1: the ELF's executable sections). **The Tracer's backstop is free:** its copy owns only the pages the segment writes (§15), never a text page once the host's scan has passed, so a store into code lands on a page not owned and sets `ERRORS` [8] (4.21.5). This is **a deliberate divergence:** a program storing into its own code is rejected, where a16z, with no region permissions, would prove it.

#### 4.21.5 Memory

**The memory system is a black box** (owner, 2026-11-05): this subsection specifies only the Tracer's side of its interface, with its latencies as parameters. Its internals (the translator and its TLBs, the 512 KB shared cache, LPDDR) are the memory system's (6.7, section 7).

**The guest's memory is a file** (§12, §19, §20): **`.pages`**, a sparse file whose byte offset is the guest address minus `lowest_address`, read through its own tree; the record's word 66 names the Tracer's copy by its root and depth. **An absent page reads as zero** (§15). Code and data both live in it.

**The local caches** (§21): **16 KB of instructions and 16 KB of data, 128 lines of 128 B each, fully associative**, single-cycle. **Each line is tagged by its line number in `.pages`**, (*a* − `lowest_address`) >> 7, and nothing else: the Tracer has no TLB and no memory-system address. *(Item 147, accepted, owner, 2026-11-05: replacement is true LRU in each cache.)* The data cache is **write-back and write-allocate**: a store that misses fills its line first. Each data line keeps **a writable bit**, set only when its fill's status was ok (below). Both caches are **invalidated by the word-0 write, dirty lines dropped** (4.21.3). Coherence needs no protocol (§21): during a segment nothing else writes what the Tracer sees, and the caches can hold the same line, since a store never reaches a code line (4.21.4).

**The fill bus: the Tracer's whole interface to the memory system** (§21): **reads and writes of one 128 B line of a file**, a request naming **(root, depth, line)**, for exactly three reasons: **a program miss** and **a data miss** (the memory file; a data miss may also write back its dirty victim) and **a mapping miss** (one line of the jump-target map). The Tracer's streams never use it: advice arrives and the logs leave through C&C 3 (4.21.3, 4.21.6).

| request | fields |
|---|---|
| **read** | op, root [30:0] (128 B units), depth [1:0], line [34:0] |
| **write** | op, root, depth, line, and the line's 1,024 bits |
| **read's response** | the line's 1,024 bits and **a status** (below) |
| **write's response** | an acknowledgement |

**The read's status** (owner, 2026-11-05, SP1's proposal; **frozen as the default interface**, owner, 2026-11-06, item 148: ok, hole, not owned, beyond the depth's capacity; to revisit with the memory system's design):

| status | meaning | the data | the Tracer |
|---|---|---|---|
| 0 | **ok**: the page exists and **every entry on the path from the root to it is owned** (B1 Q1) | the line | a data line is writable |
| 1 | **hole**: no page there (an index entry of 0xFFFFFFFF on the path) | all zeros | readable, **not writable**; on a map line, `ERRORS` [9] (item 148, accepted, owner, 2026-11-05: a hole in the map, a dense file, means it is corrupt) |
| 2 | **not owned**: some entry on the path has its owned bit at 0 (§12), the page or an index page above it being shared from an ancestor | the line | readable, **not writable** |
| 3 | **beyond the depth's capacity**: line ≥ 2^(5 + 10·depth) (B1 Q2) | undefined | **`ERRORS` [9] at once**, for any request |

**Why the whole path** (B1 Q1): after `cow_open` (§20) a shared index page's entries keep the owned bits they had in the source file, so a leaf entry can read owned inside a page this file doesn't own. **Why the depth's capacity** (B1 Q2): a request carries only (root, depth, line) and the memory system keeps no per-file state, so it can't know a file's length; a dense file's unused entries are holes anyway.

**A store to a line that isn't writable sets `ERRORS` [8]** (§25; owner, 2026-11-05; the default, item 148: a hole or a page not owned), **at the store**, which isn't performed. Checking at the fill rather than at a write-back matters: dirty lines are dropped at a segment's end, so a store to a hole might otherwise never be reported. **A write-back can't fail:** only writable lines are ever dirty. An instruction fetched from a hole reads as zero, `c.unimp`, an invalid expansion (4.21.4).

**An instruction can straddle two lines, and two pages** (§25): a 32-bit instruction whose first half is a line's last 2 bytes takes its second half from the next line, each half its own instruction-cache lookup and, on a miss, its own request; a second half beyond the depth's capacity is `ERRORS` [9].

**Ordering** (item 147, accepted, owner, 2026-11-05). The core is in order and **blocks on a read**: at most one read is outstanding, in the order the core needs them (the fetch's first half, its second, the data line, the map's lines). A dirty victim goes into **a one-entry write-back buffer**, issued after the read that displaced it, when no read is outstanding; a second victim with the buffer still full waits for it, stalling the core. A miss on the line waiting in the buffer takes it back from the buffer, with no request; once a write-back is issued, the memory system keeps a later read of that line after it.

**The range comparator** (§25; owner, 2026-11-04). On every load and store row, **alongside the data cache's lookup**, so it costs no cycle, the Tracer checks the row's address *a* (appendix section 3: the doubleword's, for a narrow row): **(*a* − `lowest_address`) mod 2⁶⁴ < 8 · K**, one subtract and one unsigned compare **on the full 64-bit address**, K = 2^(word 66 [71:66]). An address below `lowest_address` wraps and fails the same compare. **A failure is "outside the provable range", `ERRORS` [10]**, its own bit. It is not the memory system's length check: `.pages` is larger than the provable range (a 4 GB file against Cyclone's 2²⁴ cells), so an access past K but inside the file would read a hole as zero. Instruction fetch needs none: a pc outside the code misses the jump map. The bound is **8·K** (item 149, decided: the RAM sumchecks cover exactly K cells). The same subtract gives the `.pages` offset of the access.

**Misalignment** (owner, 2026-11-05; DECISIONS-v1.3 section 25; item 150). **The rule is a16z's own, on every memory row:** a load or store row's **floor address**, *a* = ea − (ea & *m*), ea = rs1 + imm, must be **a multiple of 8**, with ***m* = 0, 4, 6 and 7 for the doubleword (LD, SD), word, halfword and byte rows** (`VirtualLoad/StoreW`, `…H`, `…B`); otherwise the row is "misaligned access", `ERRORS` [12], which stops the Tracer (4.21.8). *a* is the row's address of appendix section 3, the same the range comparator checks, and the test sits beside it. **One rule for every row**, a native instruction's rows and an inline template's own LD and SD rows alike, at any row, a segment's first rows included. It is exactly what both tracers abort on: a16z at 7189216 in `tracer/src/instruction/floor_access.rs:66–75` (LD and SD through `tracer/src/emulator/mmu.rs:323, 431`), and jolt-cpp in `checked_memory_address` (`isa/riscv.hpp:257–268`, 821e1bd), as gold ports it (`pipeline/gold/src/gold/Isa.java:60–68`); UPSTREAM-DIFFS D6.

**Why it is natural alignment.** The floor address keeps exactly the bits that a misaligned access of the row's width sets: for the four row widths the test is ea mod 8, ea mod 4, ea mod 2 and nothing. Every memory row of a native load, store, LR, SC or AMO takes the instruction's own rs1, immediate and width (4.19.7), so on a native instruction the rule fails exactly when its effective address isn't naturally aligned for its access size: ea mod 8 for a doubleword (LD, SD, LR.D, SC.D, the .D AMOs), mod 4 for a word (LW, LWU, SW, LR.W, SC.W, the .W AMOs), mod 2 for a halfword (LH, LHU, SH), never for a byte. A check on the row's **doubleword** address, ea & ~7, would miss a sub-word access, which expands into rows on its aligned doubleword (4.19.7); the floor address doesn't.

#### 4.21.6 Output: the FIFOs, the compressors, the queues and the drain

**Per window the Tracer produces** (§6): **the state record**, read in place as output queue 0, and **three streams**, code, read log and write log, each raw or LRU-coded (4.19.15), as output queues 1, 2 and 3, so **output queue *n* feeds a replayer's part *n*** (§24).

**The big FIFOs** (§2; owner, 2026-11-05, Q1). Two FIFOs, **each exactly 65,536 entries, one window**:
- **the code FIFO, 32 bits wide:** one code value per native instruction of the window (the first one again if the window starts inside it, 4.21.4), and one per padding row. **A run of zero rows writes no code value**, even when it starts inside an expansion (B1 Q7): its output is the state queue alone;
- **the log FIFO, 66 bits wide:** a 64-bit log word and **a 2-bit tag: 0 load, 1 other read log (advice, a jump's next `instr_idx`), 2 write log** (3 is never written). One FIFO in row order rather than three, since the write log's last-load entry E[15] depends on the order of loads and stores (4.19.15), and set 2 must update E[15] on exactly the load rows the replayer will, which only the tag can tell it after the rows are gone. **The tags never leave the Tracer:** they route the words to queues 2 and 3 and drive E[15]; the streams carry the 64-bit words only.

**At most one log word per row** (4.19.15) and at most one code value per row, so a run of at most 65,536 rows always fits (hence `RUN_CONFIG` [23:0] ≤ 65,536, 4.21.3), and the FIFOs never fill while the rows run.

**Two-pass compression** (§3, §4). **Set 1 sizes as it traces:** each word entering a FIFO also runs through set 1's encoder for its stream, which counts the coded bits and discards them. **Set 2 compresses on the way out**, after the rows, from the same starting state (all zeros, reset by `PULSE_RUN`) by the same rules, so its output is exactly the size set 1 counted. The encoders match the replayer's decoders (4.19.15) exactly:
- the code and read-log tables are 16-entry true LRUs; the write log's has 15 LRU entries and **E[15], the last load**, updated with the doubleword of every **load** word (tag 0) in row order, as the decoder does;
- **a value matching several entries** is coded as the **lowest matching index**; in the write log, a match with E[15] is coded as **index 15** first (§4);
- **a miss is coded 32-bit exactly when the value's top 32 bits are 0**, 64-bit otherwise *(Item 151, accepted, owner, 2026-11-05: gold's frame-5 encoder's rule, `pipeline/gold/src/gold/SegmentWriter.java`; any fixed rule decodes, but set 2 must reproduce set 1 and gold the Tracer)*. A code value is always a 32-bit miss or a hit.

**The choice, at the rows' end, per stream:** with its enable in `RUN_CONFIG` [28:26] clear, the stream goes **raw**; with it set, the stream goes **LRU-coded if its coded form is fewer 128-bit words than its raw form**, raw on a tie (owner, 2026-11-05). Raw words are ⌈values / 4⌉ for code and ⌈values / 2⌉ for a log; coded words are ⌈bits / 128⌉. The choice, the words and the values out are **the stream word's three output fields** (4.19.2): words [15:0], [31:16], [47:32], values out [64:48], [81:65], [98:82], compressed [99], [100], [101], written when the rows end; **words 37–39 read the same word counts**. The advice field is not touched.

**The packers and output queues 1–3** (owner, 2026-11-05, Q2). From the cycle after the rows end, **one scan of each FIFO**, in order, drives set 2: the code FIFO feeds the code stream into output queue 1; the log FIFO feeds the read log (tags 0 and 1) into output queue 2 and the write log (tag 2) into output queue 3, every word passing set 2's write-log table so E[15] sees the loads. Each stream's **packer** packs its coded or raw bits least significant bit first into 128-bit words (4.19.15) and pushes each full word into its queue; **at the stream's end it commits its last partial word**, the rest of it padding, which can be anything (4.19.15). Each output queue is **4 KB, 256 entries**; a scan stalls while the queue its next word goes to is full, and the two FIFOs' scans run independently.

**The credits and the service requests** (§24; owner, 2026-11-05). Each of output queues 1–3 counts **credits**: **one for each 2 KB chunk (128 entries) the packer has completed**, and, when its stream has ended, **one for the last part, if it is not a whole chunk** (none if the remainder is 0: a stream of whole chunks, or an empty stream). So a queue holds 0–2 credits, and **its service flag, `EVENTS` [8]–[10], is up while it holds one** (live). **A credit is taken when the DMA has read the last entry it covers**, so a chunk may go in one transfer or several. **A request only goes up for data already in the queue,** so a transfer answering it never waits on the scan, and a full queue stalls its scan only while its own flag is up: no deadlock, whatever order C&C 3 serves the flags in. A window heavy in one log and empty in the other just raises that queue's flag repeatedly. **Sizing each transfer is C&C 3's job:** at most what the queue's credits cover, 128 words for a chunk and exactly the remainder (from words 37–39) for a stream's last part; **a transfer asking for more is UB** (the DMA would wait for data that never comes; meshsim flags it, with no hardware check).

**Output queue 0, the state queue** (§24; owner, 2026-11-05, Q4). **Output queue 0 is part 0 read in place**, the 68 words in order, through **a read pointer** that the rows' end sets to word 0 and each transfer advances by its count. **When the rows end, the Tracer posts state queue needs service, `EVENTS` [7]**, with one credit, taken when the pointer passes word 67. The sizes are then fixed and readable (words 37–39), so [7] also says "traced". The pointer lets C&C 3 send the mixed record (4.21.2) in **four transfers**: **word 0 to tile *n* + 1, word 1 to tile *n*, words 2–66 to tile *n* + 1, word 67 to tile *n***, each with its own `DMA_ADDR` into the tile's copy of its record (4.19.13). In queue order, **tile *n*'s totals reach it before any of its streams** and after nothing it needs: that is what the per-window handshake builds on (4.21.10). A transfer reading past word 67 is UB (meshsim). **At the segment's 64th run, C&C 3 sends all 68 words to its own memory** (`DMA_HDR` dst 66, its own id: routed by dst, 4.17.9), for the end-of-segment checks (4.21.7), **and then forwards words 1 and 67 to tile 63** (B1 Q4); **window 0's start**, which no run produces, C&C 3 sends tile 0 from its own copy of the record (B1 Q3).

**Done comes last** (§24; owner, 2026-11-05). **Run done, `EVENTS` [0], is posted only once the rows are done, both scans have finished, every output queue is empty and the last transfer has finished**; then `STATUS` [0] falls. So **done means ready for the next `PULSE_RUN`**, with nothing left to lose, and C&C 3 is purely reactive: it answers each service request by the queue-length registers, and pulses run at done.

**Where the output lands** (§24): **in the first implementation the DMA writes directly into the compute tiles' memory**, the state words and streams into each tile's copy of its record and streams in big SRAM (4.19.13), so no replayer credits pace the drain. **Every such write is counted** (invariant 22), on counters the tile armed for it: the record's fixed pieces at the segment's first barrier, the streams on the per-window handshake (B1 Q15; 4.21.10). **Later**, they will probably land in C&C 3's memory and go to LPDDR by file calls, for a later replay, which removes the handshake.

**The drain's cost** (§2) is at most about 784 KB per window (both FIFOs full) at one packet per cycle, about 0.05 ms at 1 GHz [estimate], against about 1.6 ms of budget per window; less at the measured densities.

#### 4.21.7 Boundaries

**Between runs, the window boundary** (§5, §24): **nothing is flushed or loaded.** The working state, the read queue and its count, the expansion queue and its credit, the template latch, the caches and the jump LRU carry straight on; `PULSE_RUN` resets only the window's output state (4.21.3). A run stops **exactly at its row count, even inside an expansion**: the next run's first code value is that instruction again, and its next offset is in the state word (4.21.4).

**Between segments, the proof boundary** (§5, §23): **a new record is written**, word 0 first, whose write empties every queue, sets the credits, and invalidates the caches and the jump LRU without writing back dirty lines (4.21.3). **C&C 3 first drops any expansion it was feeding** (B1 Q14; its template, position and pending chunks): the word-0 write empties the expansion queue and resets its credit, and the Tracer presents the expansion again if the new segment starts inside one, so an old chunk sent on the fresh credits would be executed as the new expansion's rows; the new word 66 names the next proof's copy and map. Discarding is safe because the Tracer's copy is throwaway: the next proof's images are built separately, by the Updater, from the update stream (§22). **The processor's state doesn't carry across:** each segment starts from its own frame 2. C&C 3 writes the new record only after the last run's done, so nothing is lost.

**At a segment's end** (§24), checked in software by C&C 3 from the 64th state queue (4.21.6): **the advice field must be 0** (leftover advice is the host and the trace disagreeing, which depends on the run's data), and **the end state** (words 0 and 2–65) must equal the next segment's frame 2.

#### 4.21.8 Errors

The Tracer's **`ERRORS`** (window word 2), sticky until the reset; any one raises C&C 3's `ERRORS` [11] and its error line (4.17.6). They are kept for **what plausibly comes from corruption somewhere** (a host table or a page damaged on the way, a hardware fault), where hanging or running on would hide it (§25); our firmware's own mistakes with the Tracer are UB (the principle at the top). The positions shared with the replayer are kept (owner, 2026-11-05, as SP1 proposed; [12] as ruled that day):

| bit | error | set when | kind |
|---|---|---|---|
| 0 | reserved | — | — |
| 1 | **queue overrun** | a push into the read queue or the expansion queue found it full and was dropped (4.21.3) | P |
| 2 | **invalid expansion** | a decode-table class 3; a 16-bit encoding with no 32-bit form (`c.unimp` included); an expansion word failing 4.19.11's checks (a reserved operand code, `format` 6 or 7, an opcode the replayer doesn't execute); a template past ROM word 511 | P, and H |
| 3 | reserved | (the replayer's code-stream underflow) | — |
| 4 | **advice underflow** | an advice row popped with the advice field at 0 (the replayer's read-log underflow position) | P, and H |
| 5, 6 | reserved | (the replayer's write-log underflow and leftover) | — |
| 7 | **jump target not in the map** | a free entry in a probed line, or three full lines without a match (4.21.4); a target whose offset from `lowest_address` doesn't fit 32 bits; a JALR or branch to pc 0 | H, and P on a bad table |
| 8 | **store where the copy can't write** | a store row's line is not writable: a hole or a page not owned (4.21.5), a store into code included (4.21.4) (the default, item 148) | H, and P on a bad table |
| 9 | **page doesn't exist** | a fill beyond the depth's capacity, for any request; a hole in the jump map (4.21.5) | H, and P |
| 10 | **outside the provable range** | a load or store row failed the range comparator (4.21.5) | H, and P |
| 11 | **failed assertion** | a `VirtualAssert…` row's condition is false (4.21.4; owner, 2026-11-05): a run the R1CS can't prove, such as a wrong quotient in the host's advice | H, and P |
| 12 | **misaligned access** (owner, 2026-11-05) | a load or store row's floor address, ea − (ea & *m*), *m* = 0, 4, 6, 7 for its doubleword, word, halfword or byte width, is not a multiple of 8 (4.21.5): a16z's rule, on every memory row, native or inline | H, and P |

**Leftover advice** at a segment's end is checked in software (4.21.7), not here.

**Several errors in one row** (B1 Q6). A memory row's two address checks, the range comparator ([10]) and the misalignment test ([12]), are made together, beside the cache lookup (4.21.5), and **both are set if both fail**. Either stops the row before its access, so **[8] and [9] are set only if the access goes ahead**, for a row that passed both.

**What an error does** (item 152, accepted, owner, 2026-11-05). Queue overrun drops the push and the Tracer runs on, as on the replayer. **Every other error stops the Tracer where it is:** it traces no further row and drains nothing more; `STATUS` [0] stays 1 and no event is posted. Since every error is terminal (4.13), this matters only for what meshsim and the handler's report show.

**After a trap** of C&C 3 (4.13) the Tracer carries on, unserviced, and stalls when a queue runs dry or a service request goes unanswered, until the reset (6.1), which empties its queues, invalidates its caches and LRU, and resets its run state.

#### 4.21.9 Timing and cost

**The timing parameters** (owner, 2026-11-05, SP1's proposal; to measure, item 153). They define meshsim's model; the RTL's values replace them when known.

| parameter | default | what it covers |
|---|---|---|
| `tr_row_cycles` | **4** | a row of class S or L (4.19.7), the next instruction's fetch and decode overlapped, every cache hit and the LRU's lookup included |
| `tr_mul_cycles` | **6** | a row of class M |
| `ms_line_hit` | **6** | one access that hits the memory system's shared cache, from issue to data (section 7) |
| `ms_line_miss` | **80** | one access that misses it, filled from LPDDR: an idle LPDDR miss, about 50 ns at the DRAM plus the controller (§18) |
| a fill | the sum of its accesses | **no TLB** (§21 (b)): the walk reads one index line per level of the file's tree, each an access, then the data line, so a fill of a depth-*d* file is *d* + 1 accesses, each `ms_line_hit` or `ms_line_miss` (owner, 2026-11-05) |
| a write-back | the same walk | its data line, written whole, hits in place or **allocates without an LPDDR read** (B1 Q13); the bus is busy for that long, off the core's path unless the buffer is full (4.21.5) |
| `tr_probe_compare` | **2** | per map line, after its data arrives: the 16 comparisons and the step |
| `tr_probe_lines` | **3** | the probe's bound (4.21.4) |
| the drain | 1 entry per cycle per FIFO | each scan moves one FIFO entry per cycle into its encoder and packer while its queue has room; the two scans in parallel |
| the DMA | 1 packet per cycle | 4.17.9 |
| region writes | `rp_latency` = 2 | 4.17.5 |

**The memory system behind the fill bus is a black box** (4.21.5); **these are meshsim's model of it** (owner, 2026-11-05; B1 Q12, Q13; section 7), which the latencies above assume and the RTL's memory system replaces: a 512 KB shared cache of 128 B lines, 8-way × 512 sets, LRU, write-back, addressed after translation by LPDDR address and holding index and data lines alike; one request outstanding; **cold at each segment's start**; the next victim in every set kept free or clean (a dirty LRU line evicted at once, in the background, into a 4-line write buffer draining at one line per 80 cycles), so a fill never waits for a write-back, and **a read that misses waits until the write buffer is empty**.

**Stalls** add to a row's schedule and never change it: an instruction-cache miss (one fill, or two for a straddling instruction), a data-cache miss (one fill, plus a wait if the write-back buffer is full), a map probe (per line probed, its fill plus `tr_probe_compare`), an empty read queue on an advice row, an empty expansion queue, and new expansion until its first word lands. The FIFOs never stall the rows. **The statistics counters** (4.21.3, item 157) count the first three as fetch, data and map stalls, the scans' waits on full output queues as output stalls, and the last three (the waits on C&C 3) in none. `PULSE_RUN`'s first row starts in the next cycle; the state queue's service flag is posted in the cycle after the last row, and the scans start in the cycle after that.

**Memory** [estimate]: the two FIFOs, 256 + 528 KB; the caches, 32 KB; the input queues, 8 KB; the output queues 1–3, 12 KB; the decode table and ROM, 12 KB; the working register file, 1 KB; the jump LRU, 64 × 96 bits; six 16 × 64-bit compressor tables: **about 850 KB**, dominated by the FIFOs, in C&C 3 only. **Logic:** an RV64 fetch and RVC decompressor, the replayer's decode, operand resolver, ALU (with `VirtualPext` for any mask), multiplier and comparator, the assertion checks, a 64-bit range comparator, the probe's multiplier and 16 comparators, two sets of three LRU encoders with their size counters, and three packers. Its area is not yet estimated; it is in C&C 3 only (section 11 budgets the tiles).

#### 4.21.10 The segment on the chip: barriers and deliveries (the software contract)

*Software only: what C&C 1, C&C 3 and the tiles' firmware do around the Tracer in the first implementation, where the DMA writes each window straight into its tile (4.21.6). The hardware is 4.21.1–4.21.9 and the rules it relies on are 4.12, 4.13 and 6.2–6.4; meshsim's `fw/cc/cc1.c` (`CC_SEGMENT`), `fw/tracer/tracer_ctl.c` and `fw/replay/replay_ctl.c` (layout 5) are the working version.*

**What each tile receives, and on which counter** (§24; owner, 2026-11-05, B1 Q15). Window *n* goes to tile *n*, which keeps it in big SRAM as its replay region (4.19.13), every packet counted (invariant 22):
- **its record's start, 66 words** (record words 0 and 2–66): window *n* − 1's end, from the state service of run *n* − 1 (4.21.6), or, for tile 0, **from C&C 3's own copy of the loaded record** (B1 Q3); on the tile's **state counter**;
- **its record's totals, 2 words** (words 1 and 67): from the state service of run *n*, or, for tile 63, **forwarded by C&C 3** from the record it sent itself (B1 Q4); on its **totals counter**;
- **its three streams**, whose sizes only exist once run *n*'s rows end (words 37–39, and word 1's word counts): on its **stream counter**.

The first two are fixed, so each tile arms for them at the segment's first barrier; the streams are armed on a handshake.

**The sequence** (owner, 2026-11-05 and 2026-11-06; DECISIONS-v1.3 section 24; B1 Q15, Q16 as reversed, Q17). It is **the rule of every phase** (6.2, 6.4): the window between GO and arming is kept, and closed by timing.
1. **Tiles boot and raise READY with nothing armed** (C&C 1 lands their command; `wait_for_command()`, 6.3).
2. **The first barrier:** C&C 1 sees every expected READY, **pulses GO and starts C&C 3** (a landed command; C&C 3 has no GO, 6.3). **Each tile, waking on GO, arms its state counter for 66 and its totals counter for 2.**
3. **C&C 3 sends nothing sooner than `stage_delay` after the GO** (64 cycles, 6.2 and section 10; invariant 25's announced staging). It can't see GO, so it waits `stage_delay` after its own start, which comes after the GO. The tiles' arming path is short and identical, so the counters are armed first; if the timing ever failed, the tile raises **unexpected arrival**, `ERRORS` [2] (4.12), never a silent loss. Its first packet is window 0's start, to tile 0 (B1 Q3).
4. **The deliveries**, window by window as 4.21.3's loop runs. **The per-window handshake** (B1 Q15): at run *n*'s state service C&C 3 arms a counter of its own for one "armed" packet, then sends queue 0 in its order, so **tile *n*'s totals land before any of its streams**; the tile reads the three stream sizes from word 1 ([15:0], [31:16], [47:32]: 128-bit words), arms its stream counter for their sum (nothing, if 0), and **replies "armed"**, one count-only packet to C&C 3 (the only packet a tile sends it: no "booted" packets and no request-tree barrier); **only then does C&C 3 send tile *n*'s streams.** One round trip per window, negligible against the drain; the scans wait on full queues meanwhile (4.21.6).
5. **Each tile raises READY again when its counters reach 0**: its window has landed, a genuine done (invariant 4 holds at its `done` pulse). This replaces C&C 3 reporting done.
6. **The final GO:** C&C 1 sees all 64 READY and issues GO, and **every tile starts replaying together** (B1 Q16 reversed, owner, 2026-11-06). The replay's ~236 K cycles follow the trace's ~21 M [meas: B1], so starting tiles early would gain nothing worth the coupling.

**What's still to come** (§24). The later design lands the windows in C&C 3's memory and LPDDR files, for later replays, which removes the handshake; **signed counters** (candidate C2, item 125) would remove both the handshake and the window between GO and arming.
### 8.16 The Tracer's window (C&C 3's peripheral)

*(New in v1.3; 4.21.3.)* The Tracer is peripheral 1 of C&C 3, so its window is at 0xFFFFFA00, word *n* at 0xFFFFFA00 + 4*n*, with the standard words of 8.11. Its C struct is `TracerRegs`, `PeripheralStd` followed by these words (8.1). (Item 144: the `EVENTS` bits, accepted, owner, 2026-11-05; `ERRORS` [12], item 150.)

| word | register | bits | function | reset |
|---|---|---|---|---|
| 0 | `PARAMS` | [7:0] = 3 | type 3, the Tracer | 3 |
| 1 | `STATUS` | [0] run in progress (the drain included); [1] DMA in progress | 4.21.3 | 0 |
| 2 | `ERRORS` | [1] queue overrun, [2] invalid expansion, [4] advice underflow, [7] jump target not in the map, [8] store where the copy can't write, [9] page doesn't exist, [10] outside the provable range, [11] failed assertion, [12] misaligned access; [0], [3], [5], [6] reserved | 4.21.8 | 0 |
| 3 | `PULSE_RUN` | | a store of 1 runs the next `RUN_CONFIG` [23:0] rows and drains them (4.21.3) | — |
| 4 | `PULSE_DMA` | | a store of 1 starts a transfer from output queue `DMA_HDR` [13:12] (4.17.9, 4.21.6) | — |
| 5–7 | `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK` | [0] run done, [1] DMA done, [6] new expansion (depth 1); [3] advice credit, [5] expansion credit (depth 2); [7] state, [8] code, [9] read-log, [10] write-log queue needs service (live); [2], [4] reserved | 4.21.3 | 0 |
| 8, 9 | `DMA_HDR`, `DMA_ADDR` | 4.17.9: the count in 128-bit words; queue 0 the state, 1 code, 2 read log, 3 write log | 4.21.6 | — |
| 32 | `RUN_CONFIG` (R/W) | [23:0] rows to run, at most 65,536; [25:24] ignored (the replayer's); [26] compress code, [27] compress read log, [28] compress write log; [31:29] reserved | written while stopped, used by the next `PULSE_RUN`; sticky | 0 |
| 33 | `ROWS_RUN` (R) | [23:0] | rows of this run completed | 0 |
| 34 | `EXPANSION_OFFSET` (R) | [15:0] | the row within the current expansion where the next row starts | 0 |
| 35 | `EXPANSION_OPCODE` (R) | [31:0] | the code-stream word awaiting a controller-fed expansion | 0 |
| 36 | `STATE_LENGTH` (R) | [15:0] | output queue 0's length: 68 | 68 |
| 37 | `CODE_LENGTH` (R) | [15:0] | the code stream's 128-bit words; 0 until the run's rows end, then fixed until the next `PULSE_RUN` | 0 |
| 38 | `READ_LOG_LENGTH` (R) | [15:0] | the read log's, the same way | 0 |
| 39 | `WRITE_LOG_LENGTH` (R) | [15:0] | the write log's, the same way | 0 |
| 40 | `HASH_MUL` (R/W) | [31:0] | *p*₁, odd: the map's home line is ((key × *p*₁) mod 2³²) >> (32 − *k*) (4.21.4); written once per proof, while stopped; sticky | 0 |
| 41 | `HASH_STEP` (R/W) | [23:0] *p*₃; [31:24] reserved | the probe's step, a 24-bit prime; the same rules | 0 |
| 42 | `HASH_LINES` (R/W) | [4:0] *k*; [31:5] reserved | the map has 2*ᵏ* lines; the same rules | 0 |
| 43 | `EXPANSION_TEMPLATE` (W) | [6:0] template number, [31] valid; [30:7] reserved | sets the run's template bitmap bit and latches the number (4.21.4); a load reads 0 | — |
| 44 | `STAT_CYCLES` (R) | [31:0] | cycles with `STATUS` [0] at 1, the drain included (4.21.3) | 0 |
| 45 | `STAT_STALL_FETCH` (R) | [31:0] | cycles waiting for instruction fills | 0 |
| 46 | `STAT_STALL_DATA` (R) | [31:0] | cycles waiting for data lines, the write-back buffer's wait included | 0 |
| 47 | `STAT_STALL_MAP` (R) | [31:0] | cycles waiting for map probes | 0 |
| 48 | `STAT_STALL_OUTPUT` (R) | [31:0] | cycles in which either drain scan is stalled on a full output queue | 0 |
| 49 | `STAT_MISS_I` (R) | [31:0] | instruction-cache misses | 0 |
| 50 | `STAT_MISS_D` (R) | [31:0] | data-cache misses | 0 |
| 51 | `STAT_PROBES` (R) | [31:0] | jump-LRU misses, each a map probe | 0 |

Words 33–39 and 44–51 have no read side effects, and a store to any is a bus error. **Words 44–51 are the statistics counters** (item 157; 4.21.3): 32 bits, wrapping, cleared by the word-0 write as well as the reset, counting while `STATUS` [0] is 1; the waits on C&C 3 (advice, expansions) are in none of them. Words 52–127 are reserved.

From section 9, invariant 16 (the peripherals' and the replayer's invariants):

    - **the Tracer's** (4.21; v1.3): no write to an unused or unmapped offset of its region, and no part-0 write while it runs, the drain included (UB, meshsim only, 4.21.2, 4.21.3); **word 0 written only at a segment's start** (meshsim flags a word-0 write that finds the advice field above 0, 4.21.3); no `RUN_CONFIG`, `HASH_MUL`, `HASH_STEP` or `HASH_LINES` write while it runs, and no run of more than 65,536 rows (4.21.3); every advice refill but the segment's last exactly half a queue, and none beyond the credit taken (4.21.3); an inline's `EXPANSION_TEMPLATE` stored before its expansion's first word (4.21.4); **no DMA transfer asking for more than its queue's credits cover**, and none reading output queue 0 past word 67 (4.21.6); no queue overrun, no invalid expansion, no advice underflow, no map miss, no store where the copy can't write, no page that doesn't exist, no access outside the provable range, no failed assertion and no misaligned access (hardware: its `ERRORS` [1], [2], [4], [7]–[12], 4.21.8); no expansion chunk sent on a new segment's fresh credits for an expansion C&C 3 was feeding before its record (B1 Q14, 4.21.7; caught as wrong rows or a queue overrun); every delivery into a tile counted on a counter it armed, the streams only after its "armed" (4.21.10; invariants 9 and 22); no advice left and the end state equal to the next segment's frame 2 at a segment's end (C&C 3's software, 4.21.7); and its streams decode, on the replayer, to the rows of the same window traced by gold (4.21.1).

From section 10, the parameter summary:

| parameter | default |
|---|---|
| Tracer (4.21), peripheral type 3 | C&C 3's peripheral 1; the replayer's tables and 68-word record (word 66: memory root [30:0], depth [32:31], map root [63:33], depth [65:64], log₂ K [71:66], 0x8000_0000 − `lowest_address` [102:72]); a 128-register machine; read queue (advice) and expansion queue of 256 × 128 bits, two credits of half a queue each; instruction and data caches of 128 × 128 B, fully associative; a 64-entry jump LRU; the map probed by `HASH_MUL`, `HASH_STEP` (a 24-bit prime) and `HASH_LINES`, at most 3 lines; a code FIFO of 65,536 × 32 bits and a log FIFO of 65,536 × 66 bits; two sets of three 16-entry LRU encoders; output queues 1–3 of 256 × 128 bits, a credit per 128 entries and one for a stream's tail; output queue 0, part 0 read in place by a pointer; a run of at most 65,536 rows; rows of 4 or 6 cycles; fills through the memory system's shared cache, each access 6 cycles on a hit and 80 on a miss, a fill a walk of depth + 1 accesses (4.21.9); statistics counters at words 44–51; about 850 KB of memory |

## Open items (section 13's numbering)

Settled items are listed in `spec/machine-spec.md` section 13's index; their record is `spec/history/machine-spec-v1.3-section-13.md`.

| item | what is to be settled | section | status |
|---|---|---|---|
| 153 | The Tracer's timing: measured in meshsim; the RTL's values are to replace it | 4.21.9 | open (owner, 2026-11-12) |
| 147 | To measure: the local caches' sizes, by gold replaying both workloads | 4.21.5 | decided, part open |
| 148 | The fill bus's statuses, to revisit with the memory system's design | 4.21.5, 4.21.8 | decided, part deferred |
