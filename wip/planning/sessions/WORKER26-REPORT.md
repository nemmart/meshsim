# WORKER26 report: the Tracer's host-side inputs (the jump-target map, the memory images, word 66)

**Status (2026-11-05): done; merged into main at 5e1f97c.** The tool is `wip/jumpmap/` on `v1.3-jumpmap` (README there). Both workloads at 2²⁹ pass
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
- Branch or JAL targets that aren't instruction starts (174 and 108) are dropped and counted; section 2a classifies them.

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

## 2a. The dropped targets: all are traps (`c.unimp`)

Every static target that isn't an instruction start lands on a **`c.unimp` (halfword 0x0000)**. That is the trap LLVM emits
for `unreachable`: after a call that never returns, and as the target of a failed check's branch. Gold, like a16z, removes
it from the bytecode, and reaching it aborts the run. So the 2²⁹ runs never jump to any of them, and an entry in the table
would be dead.

The other candidate causes don't occur in either program: data decoded as branches inside a text section, jumps into the
middle of an instruction, and targets outside the text. Each program's text is one Text section decoded with no desync, and
every dropped target is a halfword where a decoded instruction starts. **No tool change.**

Counts per reference, the source checked against the 2²⁹ run:

| | EVM Zone | Cyclone |
|---|---|---|
| branch or JAL to a `c.unimp`, source never executed | 162 | 94 |
| branch to a `c.unimp`, source executed (never taken) | 12 | 14 |
| **direct, total** (the report's `bad_direct`) | **174** | **108** |
| return address that is a `c.unimp` (a call that never returns), source never executed | 25 | 20 |

These are the report's `bad_return`, in the same class.

Examples (target in brackets):

- **A failed check's branch over the function's end.**
  - EVM Zone `0x800b79b6: blt a1, x0, +36` [`0x800b79da`], where `0x800b79d6` is `bge a1, x0, −28` and the next function's
    prologue (`addi sp, sp, −368`) follows the `c.unimp`.
  - EVM Zone `0x80001420: bltu s0, s10, +1852` [`0x80001b5c`], just after the function's `ret`.
  - Cyclone `0x80163b1a: bgeu a1, s4, +262` [`0x80163c20`], after a `jr a5`.
- **A trap after a call that never returns.**
  - EVM Zone `0x8013ee80: blt a0, x0, +192` [`0x8013ef40`] and `0x80183eb2: blt a1, x0, +3360` [`0x80184bd2`]: each target
    follows a `jalr ra` (a panic call).
  - Cyclone `0x80063186: blt a1, x0, +328` [`0x800632ce`] and `0x80063052` to the same trap, after `jalr ra, a3`.
- **Return addresses after a call that never returns.**
  - EVM Zone `0x80014e24`, `0x800cb7a0`, `0x8015f712` (`jalr ra, ra, …`, each followed by `c.unimp`).
  - Cyclone `0x800632cc` and `0x800c2ee6` (`c.jalr`).

The classifier was a throwaway read of gold's decoder and a 2²⁹ run, not committed.

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
- **Not done:** the 2³⁰ inputs (EVM Zone's four, Cyclone's one), which only add input variety to section 2's per-input table.
