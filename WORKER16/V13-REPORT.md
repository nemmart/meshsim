# WORKER16: the segment tools to v1.3's formats (DECISIONS-v1.3 section 16 at 7ab240d)

Branch **`worker16-v13`** of jolt-asic-planning, from `worker16-w3-88dca85`, now at `7991321`. New in `tracer/segments/`:
`v13.hpp`, the C++ ports and layouts. Changed: `segment_writer.cpp`, `segment_check.cpp`, `segment_file.py`.
`worker16-w3-88dca85` (`f2ae129`) stays as the v1.2 format's reference.

## What the files hold now

**File order:** frame 7, frame 1, frame 6, then per segment frames 2, 3 and 4 or 5.

- **Frame 7, the proof parameters** (184 bytes of data): u32 version 1, u32 n = 22, then 22 u64. These are log₂ K,
  `lowest_address`, then **a16z's `MemoryLayout` in declaration order**: program_size, max_trusted_advice_size,
  trusted_advice_start, trusted_advice_end, max_untrusted_advice_size, untrusted_advice_start, untrusted_advice_end,
  max_input_size, max_output_size, input_start, input_end, output_start, output_end, stack_size, stack_end, heap_size,
  heap_end, panic, termination, io_end.
  - **Source:** a16z/jolt `7189216`, jolt-cpp 88dca85's `third-party/jolt` submodule: `common/src/jolt_device.rs:302-325`.
    These 20 fields are also identical at `bd9aef4`.
  - **Values:** computed by a port of `MemoryLayout::try_new` (`:377-534`), with heap_size = `mem_conf.memory_size` and
    program_size = program_end − 0x8000_0000. That is a16z's own convention (e.g. `crates/jolt-tracer-x86/src/bin/jolt_emu_x86.rs:92`),
    with program_end the highest end of any section at or above 0x8000_0000 (`image/elf.rs:46-56`).
  - **Against jolt-cpp:** every field agrees with jolt-cpp's `ProgramMemoryLayout` on both workloads and all fixtures.
    a16z's stack_end is jolt-cpp's program_end.
- **log₂ K, by a16z's prover rule** (`crates/jolt-prover/src/config.rs:53-99`):
  - **K** = next_pow2(max(touched, image_end)), where touched = (the highest RAM row address − `lowest_address`) / 8, with
    no +1, and image_end = (min_bytecode_address − `lowest_address`) / 8 + num_words + 1.
  - **num_words** = ceil((max init address + 3) / 8) − floor(min init address / 8) + 1 (`preprocess/ram.rs:40-58`), over
    the initialized bytes of the sections at or above 0x8000_0000.
  - **Bounds:** the writer refuses a K outside `compute_min_ram_k`..`compute_max_ram_k` (`ram.rs:96-131`).
  - **For comparison:** jolt-cpp's rule and jumpmap's formula are computed beside it and recorded in `<out>.json`.
- **Frame 1:** the per-segment table, 128 × u32, in **4 KB pages** ((address − `lowest_address`) >> 12). The per-ASIC table
  waits for F1's index layout.
- **Frame 6:** u32 k, u32 p1, u32 p3, then the 2ᵏ lines. They are built by C++ ports of WORKER26's `StaticTargets` and
  `HashTable` (`wip/jumpmap/src/jumpmap/`).
  - **Keys:** the union of the static targets and the run's jump targets. A run target is a taken branch, a JAL other
    than the end, or a JALR, on any row, virtual ones included; self-jumps are skipped, and the base is the next `instr_idx`.
  - **Order:** keys sorted unsigned, then `choose` (golden ratio or 31 splitmix64 candidates; p3 = 10,368,899).
  - **Mapping to jolt-cpp:** gold's `isStart` is jolt-cpp's `get_instr_idx(pc) != 0`, since a removed UNIMP gets no index
    (`isa/virtual.cpp:1358`). The decoder is jolt-cpp's, with the same operand order as gold's.
- **Frame 2:** v1.26.1's 68-word record (1,088 bytes):

  | word | contents |
  |---|---|
  | 0 | pc [63:0], `instr_idx` [95:64] (the row's own index, as before), next offset [111:96], the rest 0 |
  | 1 | the advice field [124:102] = the segment's advice value count; output fields 0 |
  | 2-65 | registers two per word, low register in the low half; register 0 is 0 |
  | 66 | log₂ K [71:66] and `lowest_address` / 4 KB [123:72] from frame 7; roots and depths 0 |
  | 67 | 0 |

  Padding segments: word 0 is 0, the registers are the final ones, and the advice count is 0.

## Checks

`segment_check` restarts every segment from the record alone and compares each row with jolt-cpp's reference rows, as
before. It also checks the advice, the updates, the start and final images, and the page counts. Added for v1.3:
- **Frame 7:** recomputed, with K from the reference rows' own highest access, and checked against jolt-cpp's layout.
- **Frame 6:**
  - every jump of the reference rows goes through the Tracer's exact probe, and the base must equal the next row's
    `instr_idx` within 3 lines;
  - every entry must be an instruction start whose base is jolt-cpp's `instr_idx`, findable by the probe.
- **The record:** reserved and spare bits 0, the advice count equal to frame 3's length, word 66 equal to frame 7's
  values with roots and depths 0, word 67 0.

**Results:**

| run | segments | result |
|---|---|---|
| EVM Zone 2²⁹ | 128 | **every check passes**: 354,861,942 rows restarted; 4,055,167 jumps probed, all on their home line; 49,772 entries valid |
| Cyclone 2²⁹ | 128 | **every check passes**: 371,526,406 rows restarted; 16,039,067 jumps probed, all on their home line; 42,388 entries valid |
| EVM Zone trace-23, hard cuts (auto, `--force=lru`, `--force=raw`) | 93 (56 mid-sequence, **19 with pending advice at the head**) | all pass (the cuts are W3's) |
| Cyclone input/22, hard cuts (the same three) | 111 (59 mid-sequence) | all pass |
| both small inputs, 2¹⁶-row segments | 128 each | all pass |

**Cross-checks against the Java tools** (gold and jumpmap at meshsim `7ab240d`, built here with JDK 21):
- **Frame 6 is byte-identical to jumpmap's `frame6.bin`** on both workloads at 2²⁹, on both small inputs, and on 7 of
  WORKER26's 8 fixtures that jolt-cpp can load. EVM Zone's table needs the non-golden multiplier `0x6c442cb7`, and it
  matches too.
- **log₂ K** equals jumpmap's on both workloads (23, 24) and both small inputs.
- **Frame 1** equals jumpmap's `segments.csv` `pages_written` on all 128 segments of both workloads (1,761 and 6,093 pages).
- **Every segment's state, advice and decoded updates** equal gold's (v1.2) files on all 128 segments of both workloads,
  and on both small inputs (`segment_file.py --compare`).
- **The Python reader**'s per-frame hashes equal the C++ checker's on every file, and it still reads v1.2 files.

**Negative tests** (Cyclone input/22), one bit flipped each:

| corrupted | caught as |
|---|---|
| frame 7's log₂ K | frame 7, and word 66 in every record |
| a `MemoryLayout` field | frame 7 |
| frame 6's p1 | all 116,957 probes |
| an entry's base | the entry check |
| record bit 127 | reserved bits |
| the advice count | advice count |
| word 66's `lowest_address` | word 66 |
| a root bit | roots and depths |
| word 67 | word 67 |
| register 40 | a row differs |
| a page count | page count |

The base flip was missed before the entry check existed, since no run jump reaches that entry, so the entry check was added.

**Sizes at 2²⁹:**

| | EVM Zone | Cyclone |
|---|---|---|
| file | 2,801,736 B (`e9145e04…`) | 11,388,488 B (`fb1f4b6f…`) |
| frame 6 | 1,048,588 B (k = 13) | 1,048,588 B (k = 13) |
| frame 2 (128 records) | 139,264 B, instead of 134,144 at v1.2 | same |

The jump table is most of the growth on EVM Zone.

## Findings (reported, not chosen)

1. **K: a16z's rule and jumpmap's formula differ on 6 of WORKER26's fixtures, by one power of two** (`data/v13/jumpmap-fixtures.json`).
   - **Where:** `assert_eq_pass`, `ebreak`, `fence`, `rd0_add` and `stack_canary_store` give a16z log₂ K 4 against
     jumpmap's 3. They agree on `keccak256`, `unimp_unreached` and `d1_decodable_gap`, and on both workloads.
   - **The cause:** the image's word count. a16z's is ceil((max init + 3) / 8) − min / 8 + 1, for example 4 words for a
     16-byte image. jumpmap's is (program_end − program_start + 7) / 8, which gives 2. With the +1 in image_end, a tiny
     program crosses a power of two.
   - **A second difference:** jumpmap adds 1 to the highest access and a16z doesn't. Neither workload is near a boundary
     (touched = 5,651,595 and 13,977,089 words, against K = 2²³ = 8,388,608 and 2²⁴ = 16,777,216).
   - **jolt-cpp's rule** agrees with a16z's on every case here, though its image words come from program_end, including
     NOBITS (EVM Zone: 251,303 against a16z's 251,211).
   - jumpmap's formula moves into gold, so WORKER18 fixes it there; I follow a16z.
2. **`d1_decodable_gap`: one frame-6 base differs** (key `0x38`: 7 here, 3 in jumpmap's). jolt-cpp decodes the bytes in
   the gap between the text sections into its bytecode, which moves later `instr_idx` values (gold's D1). a16z and gold
   decode only the text sections. My tool follows jolt-cpp's rows, so its bases and rows carry D1.
3. **jolt-cpp cannot load 3 fixtures** (`d12_c_slli_x0`, `d1_rodata_in_text`, `jumps`): its JIT fails to compile them
   ("failed to compile riscv for fast execution"). My tool runs jolt-cpp, so these stay gold-only.

## For WORKER18 (byte identity)

My files make these choices; gold's must make the same:
- **The record and frame 1:** the record's bit positions above; `instr_idx` is the row's own index; padding as above;
  frame 1 holds only the per-segment table.
- **K:** a16z's prover rule exactly as written above, with touched taken from rows' `mem_access_addr`, which are
  doubleword-aligned for narrow accesses.
- **Frame 6:** StaticTargets and HashTable exactly as jumpmap's. Where jolt-cpp and a16z decode differently (D1), the files
  will differ by construction.

Since my tool follows jolt-cpp, finding 2 means byte identity with gold holds only where jolt-cpp and a16z agree on the
bytecode. That's true on both workloads.
