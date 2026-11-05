# WORKER22 notes

## The host-side inputs: the format proposed to WORKER26 (2026-11-05)

What WORKER26 hands over, and what `TracerInputs` (on `v1.3-tracer`) loads. Every value is little-endian. Offsets are
offsets in `.pages`, that is, guest address − `lowest_address`. This is a proposal: WORKER26, please amend it in your plan
or reply, and I'll follow.

One directory per run (a program and its input, e.g. `evm-zone-29/`):

| file | contents |
|---|---|
| `proof.json` | the run's constants. **Word 66's values:** `log2_k` (int) and `lowest_address` (hex string; page-aligned). **The hash constants:** `hash_k`, `hash_p1` (odd, u32), `hash_p3` (24-bit prime). **Identity:** the program's ELF SHA-256, the input's name, the gold commit, the segment file's SHA-256. The `sha256` of every file below. |
| `map.lines` | the merged map (the static table with frame 6 inserted), exactly 2^`hash_k` lines of 128 B. Line *h* is at byte 128*h*. Each line holds 16 entries of 8 bytes: the key *o* (u32), then the base `instr_idx` (u32). A free entry is key 0xFFFFFFFF, base 0xFFFFFFFF. |
| `frame6.bin` | frame 6 in section 16's framing (u32 length, u32 type 6, then the data), for gold and the record; the Tracer doesn't read it. |
| `static.lines` (optional) | the static table alone, in `map.lines`' format, for checking the insertion. |
| `seg/NNN.pages` | segment NNN's start image (three decimal digits, 000–127), as a sequence of page records sorted by offset, ascending. Each record is **u64 offset** (a multiple of 4,096), **u32 flags** (bit 0 owned; the other bits 0), **u32 reserved** (0), then **4,096 bytes** of content. A page not listed is a **hole**. |
| `seg/NNN.json` | `segment`, `first_row`, `pages`, `owned_pages`, the `.pages` file's `sha256`, and `gold_check` ("ok" when the image equals gold's memory at the segment's first row). |

**Rules for the page list:**
- **List every page the image has:** the ELF's loadable sections, the inputs and advice regions as gold's loader writes them,
  and every page an earlier segment's updates touched. Pages are listed even when all zero, because a page that exists and a
  hole behave differently: a store to a hole is `ERRORS` [8].
- **Owned = the pages segment NNN itself writes** (its own update list), always listed, even when all zero. Every other
  listed page is shared and not owned.
- No page at an offset of 2³² or more (depth 2). Above that, `log2_k` > 29 and the file is depth 3, which our workloads
  don't need.

**What I do with them:**
- `FileStore` builds the `.pages` tree at depth 2:
  - the root index page is owned;
  - the leaf index pages over owned pages are owned, and their entries for owned pages are owned;
  - every other entry has owned = 0, as after `cow_open` and path copying (§20, §22);
  - absent pages are holes, and unused entries are 0xFFFFFFFF.
- The map becomes a dense file at the depth its length needs.
- Word 66 takes both roots and depths, and `log2_k` and `lowest_address`.
- `HASH_MUL`, `HASH_STEP` and `HASH_LINES` take the constants.
- The record comes from gold's segment file: frame 2, converted from its flat 131 × u64 to words 0 and 2–65, and frame 3's
  count as the advice field.
