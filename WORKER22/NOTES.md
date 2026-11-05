# WORKER22 notes

## The host-side inputs: the format agreed with WORKER26 (2026-11-05; revised for the one-memory acceptance, 9cf1b52)

What WORKER26 hands over, and what `TracerInputs` (on `v1.3-tracer`) loads. Every value is little-endian. Offsets are
offsets in `.pages`, that is, guest address − `lowest_address`. WORKER26 proposed frame 6's data and took the rest as
below, without `static.lines`.

One directory per run (a program and its input, e.g. `evm-zone-29/`):

| file | contents |
|---|---|
| `proof.json` | the run's constants. **Word 66's values:** `log2_k` (int) and `lowest_address` (hex string; page-aligned). **The hash constants:** `hash_k`, `hash_p1` (odd, u32), `hash_p3` (24-bit prime). **Identity:** the program's ELF SHA-256, the input's name, the gold commit, the segment file's SHA-256. The `sha256` of every file below. |
| `initial.pages` | **the initial image**, the cycle-0 image (the ELF's sections, the inputs and advice as gold's loader writes them), as a sequence of page records sorted by offset, ascending. Each record is **u64 offset** (a multiple of 4,096), **u32 flags** (0), **u32 reserved** (0), then **4,096 bytes** of content. A page not listed is a **hole**. Every page the image has is listed, even when all zero. |
| `frame6.bin` | frame 6 in section 16's framing (u32 length, u32 type 6, then the data). **The data (WORKER26's proposal, agreed): u32 *k*, u32 *p*₁, u32 *p*₃, then the 2^*k* merged lines** of 128 B. Line *h* holds 16 entries of 8 bytes: the key *o* (u32), then the base `instr_idx` (u32); a free entry is key 0xFFFFFFFF, base 0xFFFFFFFF. |
| `map.lines` | the same 2^*k* lines alone, line *h* at byte 128*h* (a convenience; the loader checks it equals frame 6's). |
| `seg/NNN.pages` | **the pages segment NNN writes** (three decimal digits, 000–127): ascending u64 offsets, multiples of 4,096, one per page, from its own update list. |
| `seg/NNN.json` | `segment`, `first_row`, `pages_written`, the `.pages` file's `sha256`, `gold_check`. |

**What I do with them** (the one-memory acceptance, PLAN.md section 5):
- `FileStore` builds `initial.pages` once, as one depth-2 tree (holes for absent pages, 0xFFFFFFFF in unused entries).
- **At segment NNN's start** it clears every owned bit, then creates each page in `seg/NNN.pages` (a zero page, if
  absent) and marks its path owned. Every other page reads "not owned", so a stray store sets `ERRORS` [8].
- The map becomes a dense file at the depth its length needs.
- Word 66 takes both roots and depths, and `log2_k` and `lowest_address`.
- `HASH_MUL`, `HASH_STEP` and `HASH_LINES` take *p*₁, *p*₃ and *k*.
- The record comes from gold's segment file: frame 2, converted from its flat 131 × u64 to words 0 and 2–65, and frame 3's
  count as the advice field.

## T1 on small runs (2026-11-05, local, v1.3-tracer at the commit after 1ccf487)

The references were built locally from jolt-asic-planning `tracer-standalone-main` (fa59d9a, jolt-cpp 88dca85):
- the tracer, with `capture_writes` (giving `ram_K` and `lowest_address`, which stand in for frame 7);
- WORKER9's `delivery_writer` with meshsim's universal tables, `--windows=all --keep=all`; every window self-proved;
- gold's segment files (`gold.Main segments`, frames 1-5).

The bring-up inputs were gold's cycle-0 image and an every-instruction-start map. **No timing is taken from these runs**
(owner: not until WORKER26's map is in).

| run | rows | windows | raw streams | compressed | boundaries |
|---|---|---|---|---|---|
| Cyclone, empty input (input/22 dir) | 20,033 | 1 | OK | (not run) | segment 0 → 1 OK |
| Cyclone input/22/input-default | 2,705,819 | 64 of 64 | OK | OK | segment 0 → 1 OK; 77,564 updated cells |
| EVM Zone trace-23 | 5,833,069 | 128 of 128 | OK | OK | segments 0 → 1 → 2 OK, segment 1 on segment 0's carried memory |

"OK" means, for every window:
- the start state equals WORKER9's `state.bin`;
- each raw stream's values equal WORKER9's;
- with compression on, each stream's coded words and pick equal those of WORKER9's encoder;
- the template bitmap equals the manifest's inline templates;
- Stage A, the replayer model fed the Tracer's own record and streams, gives every row of `rows.bin` (jolt-cpp's) and
  consumes each stream exactly.

At each segment boundary, the state equals the next segment's frame 2, and every cell of the update list equals the
memory.

Command (from `wip/meshsim`, gold built into G):
`java -cp build:G TracerT1 PROGRAM INPUT SEGFILE DELIVERY NAME LOG2K LOWEST [--segments a-b] [--compress]`

## T1 at 2²⁹ (Quest 110b, 110c; meshsim v1.3-tracer 37fb37e and aeab52e)

**110b: PASS.**
- Every window of both workloads (8,192 each) and every segment boundary (128 each) pass, in all three runs: raw streams value
  for value; LRU-coded, with Stage A on the Tracer's own streams against jolt-cpp's rows; and the separate-map control.
- The inputs are WORKER26's map and initial image (Quest 109), on one memory carried through the segments.
- 110 failed on Cyclone from segment 16. The cause was a bug in the ideal controller, which fed stale expansion words at a
  segment's start (SPEC-QUESTIONS Q14), not in the Tracer.

| | EVM Zone | Cyclone |
|---|---|---|
| real rows | 354,861,942 | 371,526,406 |
| **core cycles per real row** (target 4) | **4.650** | **4.480** |
| rows alone (no stalls; class M rows take 6) | 4.313 | 4.141 |
| stalls per real row: instruction fetch / data miss / map probe | 0.306 / 0.008 / 0.022 | 0.072 / 0.120 / 0.146 |
| per segment with real rows: min / median / max | 4.23 / 4.66 / 4.71 | 4.07 / 4.52 / 5.68 (segment 10, data misses) |
| all-in per segment, rows only (the drain comes with milestone 2): median / max at 1 GHz, against 100 ms | 19.5 / 19.7 ms | 18.4 / 23.8 ms |
| shared cache hits: `.pages` index / data; map index / data | 99.99 / 98.15%; 99.56 / 50.40% | 99.94 / 77.79%; 99.98 / 95.05% |
| the walk: share of the memory system's cycles; of the real rows' cycles | 58.9%; 4.27% (0.198 per row) | 35.9%; 2.72% (0.122 per row) |
| write buffer: early evictions, read waits (cycles) | 1,502, 556 (25,850) | 154,343, 28,107 (1,336,090) |
| jumps: LRU hits, misses (one map line each) | 3,902,035, 153,132 | 12,994,216, 3,044,851 |
| the map's effect on the data (its own cache in the control): data hits; cycles | 98.15% → 98.16%; +0.006% | 77.79% → 80.93%; +0.61% |

**110c: PASS.** The instruction-fetch stalls broken down, at 4.21.5's 128 lines (16 KB). Each miss is classed as follows:
- **cold**: the line's first fetch since the segment's word-0 write;
- **capacity**: the line was fetched before in the segment and evicted since;
- **straddling second half**: a 32-bit instruction's second half, in the next line.

| | EVM Zone | Cyclone |
|---|---|---|
| instruction-fetch stalls per real row | 0.306 | 0.072 |
| cold, first half | 52,200 misses, 0.014 per row (4.5%) | 52,599, 0.013 (18.6%) |
| cold, straddling second half | 25,717, 0.007 (2.2%) | 15,408, 0.004 (5.4%) |
| **capacity, first half** | **3,560,965, 0.181 (59.0%)** | 774,127, 0.045 (62.6%) |
| **capacity, straddling second half** | **2,070,709, 0.105 (34.3%)** | 148,658, 0.010 (13.5%) |
| cycles per miss: cold, capacity | 93, 18 (cold lines from LPDDR; capacity ones from the shared cache) | 94, 22 |
| straddling instructions | 0.53% of instructions | 0.29% |
| core cycles per real row at 256 lines (32 KB) | **4.368** (fetch stalls 0.024) | 4.463 (0.055) |
| at 1,024 lines (128 KB) | 4.364 (0.021) | 4.434 (0.031) |

**Reading:**
- **EVM Zone's fetch stalls are capacity misses** (93%): its code's working set is larger than 16 KB. At 32 KB they almost
  vanish, and the core comes to 4.37 cycles per row. What remains above 4 is the multiply rows' 6 cycles (rows alone 4.31).
- The straddling second halves are a third of EVM Zone's capacity misses, but they aren't an extra cost of straddling as
  such. In sequential code the next line is the one the next instruction fetches anyway, so a straddle moves that miss
  earlier.
- Cyclone's stalls are mostly data misses and map probes (0.12 and 0.15 per row), with its slowest segments (10, 14)
  dominated by data misses.
- The map's lines cost Cyclone 0.6% (data hits 77.8% against 80.9% when the map has its own cache), and EVM Zone nothing.

## Milestone 2 at 2²⁹ (Quest 110d; meshsim v1.3-tracer 271e4dc): PASS

T1 again, with the real output path in place of the stand-in drain (4.21.6):
- set 2, the packers, output queues 1–3 with their credits and live service flags, and queue 0 read in place;
- done last, and the DMA in 128-bit words (4.17.9);
- an ideal controller serving the service requests: queue 0 in its four transfers, then the streams' credits, one packet a
  cycle.

Every window of both workloads and every boundary pass, in all three runs. The core's cycles per row equal 110b's (4.6497,
4.4800), so the drain doesn't disturb the rows.

| | EVM Zone | Cyclone |
|---|---|---|
| the drain per segment (ideal controller): median / max | 4.12 M / 4.20 M cycles | 3.15 M / 4.20 M cycles |
| **all-in per segment, rows and drain: median / max at 1 GHz**, against 100 ms | **23.6 / 23.7 ms** | **21.2 / 27.2 ms** (segment 10) |
| scan stalls (a queue full) | 0 | 0 |

The drain is the code scan's one entry a cycle: about 65K cycles per window (a padding window has 65,536 code values), so
about 4 M per segment, near 4.21.6's estimate of 0.05 ms per window.
