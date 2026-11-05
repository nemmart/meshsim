# WORKER26 report: the Tracer's host-side inputs (the jump-target map, the memory images, word 66)

**Status (2026-11-05): done.** The tool is `wip/jumpmap/` on `v1.3-jumpmap` (README there). Both workloads at 2²⁹ pass
every check, and their outputs are in the runner's persistent data (Quest 109, DONE). The format is agreed in `NOTES.md`.
The fixtures are in `wip/jumpmap/out/`.

## 1. What was built

One run of gold per proof does everything:
- the static analysis of the ELF;
- the run's control transfers, with gold's next `instr_idx` after each;
- **one table per proof**, the union of the two, sent whole in frame 6 with its constants (DECISIONS-v1.3 section 18);
- the acceptance: every jump through the Tracer's exact probe over `frame6.bin` as written to disk;
- **the initial image** and **each segment's list of pages written**;
- the memory check: at every segment's first row, the initial image with segments 0 to k−1's updates (decoded from the
  segment file's frames 4 and 5) must equal gold's memory word for word, and after the last segment too;
- **log₂ K and `lowest_address`**: K follows jolt-cpp, the run's highest load or store raised to the program-image floor.

Gold is unchanged; jumpmap reads gold's memory pages by reflection.

**The hash constants.**
- *k* is the smallest with the table at most half full.
- *p*₁ is the golden ratio (0x9E3779B1) if every key lands in its home line; otherwise the best of it and 31 fixed odd
  alternatives.
- *p*₃ = 10,368,899, the first prime at or above 0x9E3779.

## 2. Results at 2²⁹

| | EVM Zone `trace-29-4599adc50712` | Cyclone `input/29/input-15961266` |
|---|---|---|
| rows, jumps (lookups) | 354,861,942; 4,055,167 | 371,526,406; 16,039,067 |
| static entries | 49,770 | 42,388 |
| run's distinct targets / not in the static table | 6,195 / **2** | 4,240 / **0** |
| table | **49,772 entries, k = 13 (1 MB), load 0.380** | **42,388 entries, k = 13 (1 MB), load 0.323** |
| *p*₁ | 0x6c442cb7 (the golden ratio leaves a line full) | 0x9e3779b1 |
| fullest line; keys in their home line | 15; all | 15; all |
| **acceptance** | **0 map errors, 0 wrong bases; every probe one line** | **the same** |
| **memory check** | **ok at all 128 segments and at the end** | **the same** |
| log₂ K (run, image floor) | 23 (23, 23) | 24 (24, 23) |

Both log₂ K values match WORKER17's.

**What each kind of analysis adds** (cumulative; run targets missed, of 6,195 and 4,240):

| kinds | EVM Zone: entries, missed, jumps missed | Cyclone: entries, missed, jumps missed |
|---|---|---|
| direct (branch, JAL) | 32,559, 2,893, 1,882,703 | 25,680, 1,961, 2,267,575 |
| + return (after JAL/JALR with rd x1, x5) | 48,190, 312, 973,780 | 39,435, 400, 540,983 |
| + AUIPC+JALR far calls | 48,800, 107, 3,929 | 39,440, 395, 540,977 |
| + AUIPC+ADDI code addresses | 48,825, 106, 3,928 | 39,452, 390, 539,587 |
| + data scan | 49,770, 2, 1,746 | 42,348, 8, 875 |
| + after ECALL | 49,770, 2, 1,746 | 42,388, **0**, 0 |

- **The data scan** adds 945 entries to EVM Zone and 2,896 to Cyclone, and catches 104 and 382 of the run's targets.
- **EVM Zone's 2 remaining targets** are function pointers that the program loads with AUIPC+LD from writable memory set at
  run time. No static scan sees them; the run supplies them.
- Branch or JAL targets that aren't instruction starts (174 and 108) are dropped and counted. I haven't looked into them.

**Run-only targets, per proof, across inputs.**
- EVM Zone: 2 at every input from 2²³ to 2²⁹ (7 inputs).
- Cyclone: 0 at every input (2²², two at 2²⁵, 2²⁶, two at 2²⁹).
- Acceptance and log₂ K are as above on all of them.

**Per segment** (segments with rows: 85 of EVM Zone's, 89 of Cyclone's; median / max):

| | EVM Zone | Cyclone |
|---|---|---|
| distinct targets | 892 / 3,077 | 1,235 / 2,489 |
| 64-entry LRU misses (cold per segment) | 1,525 / 8,666 (3.8% of jumps) | 32,742 / 87,700 (19.0%) |
| **cache footprint: distinct table lines touched** | 841 / 2,583 (105 / 323 KB) | 1,155 / 2,160 (144 / 270 KB) |
| the same against a table of every instruction start (k = 17, 16 MB) | 892 / 3,047 (112 / 381 KB) | 1,228 / 2,468 (154 / 309 KB) |
| pages written | 19 / 81 | 45 / 1,048 |

The LRU figures agree with section 18's (8,665 and 87,261 at the worst segment).

**The cache footprint is about one line per distinct target, whatever the table.** The 1 MB table touches only 12–15% fewer
lines than the 16 MB one, because the hash scatters a segment's targets. Every first touch is an LRU miss, so the footprint
doesn't depend on the LRU's size. Cost at the worst segment:
- the cold lines: about 2,600 × 80 cycles;
- the warm misses: about 85,000 × 6 cycles (Cyclone);
- together under 1 % of the 100 ms budget, as section 18 found.

If the footprint ever matters, a key that keeps a code line's targets together (hashing *o* >> 7, say) would shrink it. That
is a software change only, not proposed.

**The initial images:** EVM Zone 496 pages (2.0 MB), Cyclone 1,519 pages (6.2 MB).

## 3. The 2²⁹ outputs (Quest 109; `/home/questrunner/data/w26-jumpmap/`)

Each run's directory holds `proof.json`, `map.lines`, `frame6.bin`, `initial.pages`, `seg/NNN.{pages,json}`,
`segments.seg` and the measurements, with `manifest.sha256` and `<run>.COMPLETE`. The SHA-256s below match between my local
run, the local smoke test and the runner:

| file | EVM Zone | Cyclone |
|---|---|---|
| `segments.seg` (gold) | `208520ece5475cafff6e89caffe716be3346eb45f3189b0383e6f1668aa17d18` | `f45f71b6e5359960829a901aacf08569e690fc3d8139dc17858fb0df8aa64070` |
| `map.lines` | `10c146d0380f690b24959e4c0bc8d94f45439258c6facf2f5d0b3e170fe09a6c` | `ef286e0c9d0c96dcab628bf2a3e6c3975f4ab5f680ee737dabbec2025df1a68e` |
| `frame6.bin` | `d3e646b2329ed07b2cc45abb003b265f28154b39c08a3ed87b030add0225eac6` | `1a0c646e58a994b697f860f0fe4aedda36834e63a0ae91e00d24565460962464` |
| `initial.pages` | `51aaea2f2be6f892a75fa509a2e1a67aca4ce02c9fb5a4e1d695e9efaaa29401` | `6f7438cd73cae839a80173d20cc80e353cf490301441e9a385cd252ff164932a` |
| `seg/*.pages`, concatenated in order | `56b63dd683f960e21e05ad29d916765baf7feddc83267f548b6ce5bb48b44b94` | `d7679f6f63a4ed734e966b8c72e22e5b46126a82baf3403c3e85d3b4d826dc84` |

- The manifests (every file, `report.json` included, whose run time varies) are in Quest `results/109-w26-jumpmap-29/`.
- The task's kit is gold 7ef9039 and jumpmap at 7a91438, compiled with Java 21, because the runner has no javac. Later
  commits change only comments and docs.

## 4. For others

- **WORKER22:** the format is in `NOTES.md`. `seg/NNN.pages` lists the pages to create and own at segment NNN's start.
  `initial.pages` lists nothing owned.
- **WORKER18 (gold writing frame 6):** frame 6's data is u32 k, u32 p1, u32 p3, then the lines. jumpmap's `HashTable` is
  the rule: placement in ascending key order, the constant choice, and the probe. Gold could call it or copy it. The table
  needs the whole run's targets, so gold writes frame 6 after the run, though it goes before the segments in the file.
- **Not done:** the 2³⁰ inputs; the unexplained non-start branch targets.
