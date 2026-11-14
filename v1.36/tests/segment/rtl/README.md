# The RTL's segments on the chip, and its figures for them

*For anyone checking the simulator's chip against the RTL's on a segment: what these files are, where they came from,
and why they are never regenerated here.*

Each `<test>.seg.gz` is one segment of the RTL's chip test of the segment (Snarky One's
`rtl/chip/test/src/chip_trace_test.rs`), written by that test as it ran on the RTL's chip model:

| file | the RTL's test | the segment |
|---|---|---|
| `chip_trace_test.seg.gz` | `chip_trace_test` (every tier) | 4 windows of 3,000 rows on tiles 0–3, the small guest |
| `chip_trace_test_full.seg.gz` | `chip_trace_test_full` (`test-full`) | a window of 1,500 rows on each of the 64 compute tiles, the full guest, built to miss in the Tracer's caches and its jump LRU and to write lines back |
| `chip_trace_test_window.seg.gz` | `chip_trace_test_window` (`test-full`) | one window of 65,536 rows on tile 0, the small guest |

A file has two halves. **The segment**, as the RTL's test built it before its run, from its own generator and models
(the Tracer's tests' execution model and stream encoders, which share no code with the design and none with the
simulator): the guest's memory file and jump-target map, the loaded record, the advice, the expected end state, and each
window as it must be delivered and replayed (its record, its three streams' meaningful bits, its rows). **The RTL
chip's figures** for the run: its cycles from the reset, the master's summary (every release's mcycle), the tracer
tile's report (each run's `START`, rows' end and `done`, the Tracer's statistics counters), every request of the fill
port (its cycle, its line, its answer's cycle), and the cycle every row left its tile's `replay_row` port.

**They are the expectations of `tests/test_rtl_segment.cpp`** (`tests/segment_bench.h`; the runner's step
`rtl-segment`): the simulator, running the same firmware on the same segment under the same model of the memory
system, must give the segment's rows and deliveries and every one of the RTL chip's figures. **Never regenerate them
from the simulator.** A hardware change is first made in the RTL, and these files are then produced again by the RTL.

## Where they came from

Snarky One `main` at `076e647` (the firmware images are those of `370e3ac`, vendored in `fw/rtl/`, byte for byte:
`fw/rtl/MANIFEST`), 2026-11-15: `chip_trace_test.rs` with `chip_trace_dump.patch` (here) applied, which adds the dump
and changes no cycle (each run's printed totals are the unmodified test's: 113,152, 764,928 and 609,280 cycles from the
reset). From a scratch worktree of Snarky One (`mise run agent-new <name> shared`), with the patch applied:

```sh
mise run sort-images && mise run build
SEGMENT_DUMP=<a directory> mise exec -- cargo --locked --offline test -p chip chip_trace_test::chip_trace_test -- --nocapture
SEGMENT_DUMP=<a directory> mise exec -- cargo --locked --offline test -p chip chip_trace_test::chip_trace_test_ -- --ignored --nocapture --test-threads 1
gzip -9 <a directory>/*.seg
```

## The format

Little-endian; a 128-bit word is its low 64 bits, then its high. `tests/segment_bench.cpp`'s `read_fixture` is the
reader, `chip_trace_dump.patch`'s `dump` the writer.

| | |
|---|---|
| the header | `"SEG1"`; the windows, the rows a window, the seed (u32 each); the cycle limit (u64); whether the guest must miss (u32); the tracer tile's header and the replay header as the test placed them, 20 words each |
| the guest's file | the lowest address (u64), log2 K, the depth, the lines' number (u32 each); then each line: its number in the file (u64), its read status (u8), its 128 bytes |
| the map | k, p1, p3, the lines' number (u32 each); each line: its status (u8), its 128 bytes; a flag (u8) and, if set, the filler line |
| the records and the advice | the loaded record, 68 words; the advice values' number (u32) and the values (u64 each); the expected end state, 68 words |
| each window | its record as its tile receives it, 68 words; its run's code values and log words (u32 each); for each of the three streams its bits (u64), its entries' number (u32) and its entries, the bits past the stream's end 0; its rows' number (u32) and its rows, five words each |
| the RTL chip's figures | the cycles from the reset (u64); the summary's words and the tracer tile's report's (a u32 count, then the words); the fill port's four counts (u64 each); the fill requests (a u32 count; each: its cycle (u64), read or write (u8), root (u32), depth (u8), line (u64), its answer's cycle (u64), its status (u8)); for each of the 64 tiles its rows' number (u32) and each row's cycle out of the port (u32) |
| the end | `"SEND"` |

A cycle is the RTL test's: cycle 0 is the first after the reset's release. The tracer tile's report is its image's
`.summary` (208 words), of which the image writes words 0–4, 8–15 and three a window from 16; the others are whatever
the RTL's memory model started with, and are not compared.
