# WORKER22 notes

## The host-side inputs: the format agreed with WORKER26 (2026-11-05; revised for the one-memory acceptance, 9cf1b52)

What WORKER26 hands over, and what `TracerInputs` (on `v1.3-tracer`) loads. Every value is little-endian. Offsets are
offsets in `.pages`, that is, guest address − `lowest_address`. WORKER26 proposed frame 6's data and took the rest as
below, without `static.lines`.

One directory per run (a program and its input, e.g. `evm-zone-29/`):

| file | contents |
|---|---|
| `proof.json` | the run's constants. **Word 66's values:** `log2_k` (int) and `lowest_address` (hex string; page-aligned). **The hash constants:** `hash_k`, `hash_p1` (odd, u32), `hash_p3` (24-bit prime). **Identity:** the program's ELF SHA-256, the input's name, the gold commit, the segment file's SHA-256. The `sha256` of every file below. |
| `image.pages` | **the initial image**, the cycle-0 image (the ELF's sections, the inputs and advice as gold's loader writes them), as a sequence of page records sorted by offset, ascending. Each record is **u64 offset** (a multiple of 4,096), **u32 flags** (0), **u32 reserved** (0), then **4,096 bytes** of content. A page not listed is a **hole**. Every page the image has is listed, even when all zero. |
| `frame6.bin` | frame 6 in section 16's framing (u32 length, u32 type 6, then the data). **The data (WORKER26's proposal, agreed): u32 *k*, u32 *p*₁, u32 *p*₃, then the 2^*k* merged lines** of 128 B. Line *h* holds 16 entries of 8 bytes: the key *o* (u32), then the base `instr_idx` (u32); a free entry is key 0xFFFFFFFF, base 0xFFFFFFFF. |
| `map.lines` | the same 2^*k* lines alone, line *h* at byte 128*h* (a convenience; the loader checks it equals frame 6's). |
| `seg/NNN.written` | **the pages segment NNN writes** (three decimal digits, 000–127): ascending u64 offsets, multiples of 4,096, one per page, from its own update list. |
| `seg/NNN.json` | `segment`, `first_row`, `pages_written`, the `.written` file's `sha256`. |

**What I do with them** (the one-memory acceptance, PLAN.md section 5):
- `FileStore` builds `image.pages` once, as one depth-2 tree (holes for absent pages, 0xFFFFFFFF in unused entries).
- **At segment NNN's start** it clears every owned bit, then creates each page in `seg/NNN.written` (a zero page, if
  absent) and marks its path owned. Every other page reads "not owned", so a stray store sets `ERRORS` [8].
- The map becomes a dense file at the depth its length needs.
- Word 66 takes both roots and depths, and `log2_k` and `lowest_address`.
- `HASH_MUL`, `HASH_STEP` and `HASH_LINES` take *p*₁, *p*₃ and *k*.
- The record comes from gold's segment file: frame 2, converted from its flat 131 × u64 to words 0 and 2–65, and frame 3's
  count as the advice field.
