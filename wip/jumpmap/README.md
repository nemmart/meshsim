# jumpmap: the Tracer's host-side inputs (WORKER26)

The host tool behind the Tracer's bring-up (B1): **one jump-target table per proof** (DECISIONS-v1.3 section 18; spec
4.21.4), **frame 6**, the **initial image** and **each segment's pages written** (B1's acceptance on one memory), and
**word 66's values** (log₂ K, `lowest_address`) standing in for frame 7. Plain Java 21 on gold's classes (`../gold`);
no dependencies. `HashTable` and `StaticTargets` now live in gold, which builds frame 6 itself (owner, DECISIONS-v1.3
section 16, 7ab240d); this tool uses gold's, and takes K (`proof.json`'s `log2_k`) from gold's `ProofParams`, a16z's
prover rule, as frame 7 carries it (its own formula differed from a16z's on six tiny fixtures, by one power of two:
WORKER16, `wip/planning/sessions/WORKER16-V13-REPORT.md`).

```sh
./build.sh                                              # builds gold if needed, then build/
java -cp ../gold/build gold.Main segments <program dir> <input dir> run.seg       # gold's segment file
java -cp ../gold/build:build jumpmap.Main <program dir> <input dir> run.seg <out dir> [--images=write|check|none]
     [--segment-rows=4194304] [--name=NAME] [--gold-commit=HASH]
test/fixtures.sh                                        # regenerate out/ (the fixtures) and diff against git
```

## What it does, in one run of gold

1. **The static targets** (`StaticTargets`), from the ELF's Text sections decoded as gold does (D1): every branch and JAL
   target; the instruction after every call (JAL or JALR with rd x1 or x5); AUIPC+JALR far calls; AUIPC+ADDI code
   addresses; the instruction after every ECALL (where MRET returns); every 8-byte-aligned u64 in a non-executable section
   with data equal to an instruction start. Targets that aren't instruction starts are counted and dropped.
2. **The run's targets**: every control transfer's new pc (a taken branch, a JAL other than the end, a JALR; a transfer to
   its own pc is sequential) and gold's next `instr_idx` after it.
3. **The table** (`HashTable`): the union, keys o = pc − `lowest_address`, in ascending order, each placed by the
   lookup's rule (home line, then steps of p₃, at most 3 lines). k is the smallest with the table at most half full; p₁ the
   golden ratio (0x9E3779B1) if every key lands in its home line, else the best of it and 31 fixed odd alternatives; p₃ the
   first prime at or above 0x9E3779 (10,368,899).
4. **The acceptance**: every jump of the run through the Tracer's exact probe over `frame6.bin` as written to disk: the
   base must equal gold's next `instr_idx`, with no map error.
5. **The memory**: the initial image (gold's cycle-0 memory; every section's pages and every input file's pages listed,
   zero where nothing is written); at each segment's first row, the initial image with segments 0 to k−1's updates (frames
   4 or 5 of the segment file) must equal gold's memory word for word, and after the last segment too.

## Outputs (one directory per run; WORKER22's layout, as agreed in WORKER26/NOTES.md)

| file | contents |
|---|---|
| `proof.json` | `log2_k`, `lowest_address`, `hash_k`, `hash_p1`, `hash_p3`, `map_entries`; the ELF's, segment file's and outputs' SHA-256s; `initial_pages`; `seg_written_sha256` per segment |
| `map.lines` | the table: 2^k lines of 128 B, 16 entries of (key u32, base u32), free = all ones |
| `frame6.bin` | frame 6: u32 length in words, u32 6, then **u32 k, u32 p1, u32 p3, then the 2^k lines** |
| `initial.pages` | the initial image: per page, ascending, u64 offset in `.pages` (a multiple of 4,096), u32 flags (0), u32 0, 4,096 bytes |
| `seg/NNN.pages` | the pages segment NNN writes: u64 offsets in `.pages`, ascending (the file's length / 8 of them) |
| `seg/NNN.json` | `segment`, `first_row`, `pages_written`, `sha256`, `gold_check` ("ok": the memory check at its first row) |
| `report.json`, `segments.csv`, `run_only.csv` | the measurements (WORKER26/REPORT.md) |

`out/` holds the fixtures: gold's guests that run to their end and `jumps` (`test/make_jump_guest.py`: one target of each
kind, and one only the run sees), each in 4 segments.
