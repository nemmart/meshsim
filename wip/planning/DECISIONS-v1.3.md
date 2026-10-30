# Decisions toward v1.3

One entry per decision, in the order made; later entries may revise earlier ones, and superseded points are
marked in place. Labels: O owner decision, T confirmed by Tony. Builds on v1.2 (`DECISIONS-v1.2.md` and, once
written, the v1.2 spec). **v1.3's focus is the Tracer** (owner, 2026-10-29).

---

## 1. The Tracer: components (2026-10-29)

The Tracer is C&C 3's peripheral (v1.2, 25): the RV64 row machine that executes the guest program and writes
each shard's run log, the streams the tiles' replayers consume (spec 4.19). Around its RV64 core (O):

1. **The RV64 initial state** the core starts a window from.
2. **The page table**, held locally.
3. **Two local expansion memories, 8 KB and 4 KB.**
4. **The LRU compressors: two sets of three**, one compressor per stream (code, read log, write log) in each set.
5. **The big FIFOs** (2).

## 2. The big FIFOs (2026-10-29)

Decided (O).

- **Two FIFOs, each 72 × 1,024 = 73,728 entries**: one window of 2¹⁶ rows plus an eighth of headroom.
  - **The code FIFO, 32 bits wide:** one opcode word per native instruction, at most one per row.
  - **The log FIFO, 65 bits wide:** a 64-bit read-log or write-log word, and **a tag bit** saying which. One
    FIFO with the tag, rather than a separate 1-bit FIFO kept in lockstep. The words stay in row order, which
    is what the write log's last-load entry (E[15], spec 4.19.15) needs when the logs are compressed.
- **At most one log word per row** (spec 4.19.15: "the core takes one code value per instruction and at most
  one log value per row": an LD row pops the read log, an SD row the write log, no row both). So a window's
  log words always fit. **This bound sizes the FIFO, so it must hold**: a row producing two log words would
  overflow the FIFO mid-window, and since a window can't drain before it ends, the Tracer would deadlock.
  *To validate:* the tracer model and the delivered run logs confirm no window exceeds 65,536 log words.
- **Windows are pipelined:** window *n* drains while window *n* + 1 fills behind it. The drain is expected to
  be much faster than the fill; the eighth is margin against a slow one. If the FIFO ever fills, back-pressure
  stalls the RV64.
- *(Spec slip, passed to the v1.2 spec:* v1.1's 4.19.15 bounds a stream's length by "64K rows × at most two log
  words × 66 bits", about 8.6 Mbit, which contradicts its own one-value-per-row rule. The bound is
  64K × 66 bits, about 4.3 Mbit, within the 24-bit length field.)

## 3. Two-pass compression (2026-10-29)

Decided (O).

- **Set 1 sizes:** as each raw word enters its FIFO, the same word runs through set 1's compressor for its
  stream, which counts the compressed size and discards the output.
- **At the window's end**, each stream's compressed size is compared with its raw size.
- **Set 2 compresses on the way out:** a stream drains through set 2 if compressed is smaller, raw otherwise.
  The choice is per window and per stream, recorded in the run log's per-run flags (spec 4.19.15, item 108).
- **Set 1 sizes window *n* + 1 while set 2 compresses window *n*** (2).
- **Set 2 reproduces set 1 exactly:** both start each window from the same table state and apply the same
  rules, so set 2's output is exactly the size set 1 counted.

## 4. The LRU tables (2026-10-29)

Decided (O). The Tracer's encoders match the replayer's decoders (spec 4.19.15, item 104) exactly.

- **Reset to all zeros at every window start**, as the replayer's decoder resets. Seeding the table with
  distinct values was considered and dropped: it saves at most a few hits in a 65,536-row window.
- **The encoder's tie rule:** a value matching several entries (duplicates are legal, 4.19.15) is coded as the
  **lowest matching index**; in the write log, a match with the last-load entry E[15] is coded as index 15
  first (4.19.15's existing rule). The decoder doesn't depend on the choice; it makes the encoding
  deterministic, which set 2 reproducing set 1 requires.

## 5. Window and proof boundaries (2026-10-29)

Decided (O).

- **Between windows** (every 2¹⁶ rows): **nothing is flushed.** The caches and the page table carry straight on;
  a window boundary is only a cut point in the run log.
- **Between proofs** (every 4M cycles, i.e. 64 windows, one per tile): the next proof runs over a different memory
  image, so the Tracer
  - **invalidates its D-cache, discarding dirty lines** (no write-back), and its I-cache;
  - **loads the next proof's page table** from the pager (C&C 3).
- **Why discarding is safe:** the Tracer writes to a **throwaway layer** (planning notes section 5); the next
  proof's image is built separately, by the Updater from the update stream. Within a proof, a dirty line that is
  evicted is still written to the throwaway layer's pages in LPDDR, since the program may read it back later in
  the same proof; at the proof boundary everything the Tracer wrote is garbage, and the pager frees those pages
  with the proof's bitmap.
- *(Inferred, to confirm:)* the RV64's architectural state carries across a proof boundary; only the view of
  memory changes.

## 6. The Tracer's output per window (2026-10-29)

Agreed (O). The Tracer's output is defined from the replayer's side (spec 4.19); per window of 2¹⁶ rows it
produces:

- **The state record**, 67 × 128-bit words (1,072 bytes; spec 4.19.2):

| word | contents | goes to |
|---|---|---|
| 0 | **template bitmap**, 128 bits: bit *i* set if the window uses controller-fed template *i* (0-127, spec 4.19.5) | the controllers: the tile's, to stage only the templates its window uses, and the chip's used set (spec item 115) |
| 1 | **state word**: pc, `instr_idx`, next offset, the four flags (code, read log, write log compressed; includes store log) | replayer part 0, offset 0x000 |
| 2 | **stream word**: the three streams' exact lengths in bits | replayer part 0, offset 0x010 |
| 3-66 | **register image**: 64 words, two registers each | replayer part 0, offset 0x020 |

- **The three streams**, code, read log and write log, each raw or LRU-compressed (3), into the replayer's queues.
- The compressed flags in the state word and the lengths in the stream word are the two-pass compression's choice
  and counts (3).

## 7. The Tracer's expansion tables (2026-10-29)

Agreed (O). Answers T1.

- **The 8 KB and 4 KB memories are a ROM of expansion templates and a decode table**, the replayer's sizes and
  **format** (spec 4.19.3-4.19.6), built by the same table builder. The Tracer expands each instruction into rows
  as the replayer will, to count rows, place window boundaries inside an expansion (the next offset) and know
  which rows are traced.
- **Their contents differ from the replayer's** (as concluded in a v1.1 session, reconstructed here):
  - **Untraced advice rows.** Where Jolt's expansion takes advice (DIV and REM, for example), the Tracer's
    template adds **untraced rows** that compute the advice in the register file's second bank, and the traced rows
    read it through `advice_reg` (expansion word [62:56]). The replayer's template has the traced rows only and pops
    the advice from the run log; an `untraced` word (expansion word [55]) reaching a replayer is an invalid
    expansion (spec 4.19.11). This is why the run log differs from jolt-cpp's memory log on purpose (spec 4.19.2).
  - **Coverage** (owner, 2026-10-29; T4): a defined list of templates goes in the Tracer's 8 KB ROM, and **the few
    that don't fit are fed by its controller, C&C 3, in the replayer's style** (spec 4.19.5): the Tracer reaches
    such an instruction, presents it and waits, and C&C 3 supplies the expansion words (the Tracer's variant, with
    any untraced advice rows).
  - **Consequences:** the Tracer has an **expansion queue** that C&C 3 writes, a **"new expansion" event** and an
    **`EXPANSION_OPCODE`** word in its peripheral window (as the replayer's, v1.2 section 39), and **stalls** while
    C&C 3 responds; rare, as for the replayer (about 13 per window, around 0.4 % of a pass, spec 4.19.5).

---

## Pending questions

| # | question | from | status |
|---|---|---|---|
| T1 | What the 8 KB and 4 KB expansion memories hold | 1 | **answered: section 7** (same format and builder as the replayer's ROM and decode table; different contents) |
| T4 | The Tracer's template coverage | 7 | **answered: section 7** (a list in the ROM; the rest fed by C&C 3, replayer-style) |
| T2 | The RV64 initial state: what it contains and where it comes from | 1 | open |
| T3 | The page table: its size, format and how C&C 3 maintains it | 1 | open |
