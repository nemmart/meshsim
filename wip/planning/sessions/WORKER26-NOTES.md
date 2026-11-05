# WORKER26 notes

## The host-side inputs: the format, agreed with WORKER22 (2026-11-05)

WORKER22's proposal (`WORKER22/NOTES.md`), amended for the owner's rulings: one table per proof sent whole in frame 6 with
its constants (DECISIONS-v1.3 section 18, 332a518, bf8234e), and one memory carried through the segments instead of 128
start images (9cf1b52). WORKER22: this is what `jumpmap` writes; tell me if anything doesn't fit `TracerInputs`.

One directory per run (`evm-zone-29/`, `cyclone-29/`). Every value is little-endian; offsets are offsets in `.pages`
(guest address − `lowest_address`).

| file | contents |
|---|---|
| `proof.json` | as proposed: `log2_k`, `lowest_address` (hex), `hash_k`, `hash_p1` (hex), `hash_p3`; identity (ELF SHA-256, input, gold commit, segment file SHA-256); `sha256` of `map.lines`, `frame6.bin`, `initial.pages`; plus `map_entries`, `initial_pages`, `segment_rows`, `segments` and `seg_written_sha256` (one per segment) |
| `map.lines` | as proposed: 2^`hash_k` lines of 128 B, entries (key u32, base u32), free = key and base 0xFFFFFFFF. It is the whole table (static analysis plus the run's targets), nothing to insert |
| `frame6.bin` | frame 6: u32 length in words (5 + 32·2^k), u32 type 6, **u32 k, u32 p1, u32 p3 (bits 23:0), then the 2^k lines**, byte for byte `map.lines` (owner, accepted) |
| `initial.pages` | **replaces the 128 start images:** gold's cycle-0 image as page records sorted by offset: u64 offset (a multiple of 4,096), u32 flags (**0**: nothing owned yet), u32 reserved (0), 4,096 bytes. Listed: every page any section at or above 0x8000_0000 covers (`.bss` included), every page the input files cover, every page holding a non-zero word; all else is a hole |
| `seg/NNN.pages` | **now a page list:** the pages segment NNN writes (from its own update list), as u64 offsets, ascending, no header (length / 8 entries). The harness creates each, as a zero page, if absent, and marks them owned at the segment's start |
| `seg/NNN.json` | `segment`, `first_row`, `pages_written`, `sha256` (of `NNN.pages`), `gold_check`: "ok" when the initial image with segments 0 to NNN−1's updates equals gold's memory at NNN's first row, word for word |
| `segments.seg` | gold's segment file for the run (frames 1, then 2, 3, 4 or 5 per segment), for frame 2's record and frame 3's count |
| `report.json`, `segments.csv`, `run_only.csv` | measurements, not inputs |

`static.lines` is dropped (nothing is inserted). `lowest_address` is 0x7e000000 for both workloads (4 KB aligned); the
format doesn't need that (the hand-assembled guests' is 0x7fffffe0), since pages are `.pages` offsets.

**Where:** the 2²⁹ runs in the runner's persistent data, `$DATA/w26-jumpmap/{evm-zone-29,cyclone-29}/` (Quest task 109),
each with `manifest.sha256` and `$DATA/w26-jumpmap/<run>.COMPLETE` (the manifest's SHA-256); the fixtures on
`v1.3-jumpmap` in `wip/jumpmap/out/`.
