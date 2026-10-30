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
  - **Registers for untraced work** (owner, 2026-10-29): an advice expansion runs with tracing off in **bank 1**, the
    register file's second 128 registers; the result stays in a bank-1 register, which the traced row reads through
    `advice_reg`. Bank 0 (0-127: x0-x31 and the virtual registers) is all the register image carries and all the
    replayer sees. Operand codes 128-145 are taken by the special operands (spec 4.19.3), so **untraced work uses
    registers 146-255**: code *c* names register *c*, and registers 128-145 are left unused (no remapping). Placing the
    special codes at the top instead would have given the same 110 registers, only starting at 128; renumbering now
    would ripple through the replayer, its model, the table builder and the test vectors for no capacity, so it stays.
  - **Coverage** (owner, 2026-10-29; T4): a defined list of templates goes in the Tracer's 8 KB ROM, and **the few
    that don't fit are fed by its controller, C&C 3, in the replayer's style** (spec 4.19.5): the Tracer reaches
    such an instruction, presents it and waits, and C&C 3 supplies the expansion words (the Tracer's variant, with
    any untraced advice rows).
  - **Consequences:** the Tracer has an **expansion queue** that C&C 3 writes, a **"new expansion" event** and an
    **`EXPANSION_OPCODE`** word in its peripheral window (as the replayer's, v1.2 section 39), and **stalls** while
    C&C 3 responds; rare, as for the replayer (about 13 per window, around 0.4 % of a pass, spec 4.19.5).

## 8. A divider for advice (2026-10-29)

Decided (O, T).

- **The Tracer's row ALU includes the RV64 M extension's division as base operations:** DIV, DIVU, REM, REMU and
  the W forms (DIVW, DIVUW, REMW, REMUW). They are used **only by untraced rows**; the replayer never executes
  them (an untraced word reaching a replayer is already an invalid expansion, spec 4.19.11), so it needs no divider.
- **The DIV/REM advice** (Jolt's `VirtualAdviceDivU`, `Div`, `Rem` and their W forms, spec 4.19.7) is then **one
  untraced row** with the native operation, writing a bank-1 register (7), which the traced advice row reads through
  `advice_reg`.
- **To verify, not assume:** that Jolt's advice values equal RISC-V M's results in the edge cases (division by zero:
  quotient all ones, remainder the dividend; signed overflow, −2⁶³ ÷ −1; the W forms' 32-bit sign extension). Where
  Jolt differs, the untraced expansion adds a fix-up row.
- **Still open:** the inlines' `VirtualAdvice` (BLAKE3, secp256k1 and others, which C&C 3 feeds, 7): computed by
  untraced rows, or by C&C 3's software when it supplies their expansion words (T5).

## 9. Moving to jolt-cpp's current expansions (2026-10-30)

Decided (O). From WORKER7's F1.

- **The hardware moves to jolt-cpp `main`**, away from the spec's reference copy (jolt-asic-planning `4f36b3d`, taken
  from jolt-cpp on 2026-10-19). The trace our hardware produces has to match what the Jolt prover we pair with
  expects, so following Jolt is not optional, only when.
- **Why Jolt changed:** `main` re-expands the sub-word loads and stores and the 32-bit atomics around "load the aligned
  doubleword, then work on the byte lane", and the expansions are much shorter:

| instruction | `4f36b3d` rows | `main` rows |
|---|---|---|
| LB, LBU | 4 | 3 |
| LH, LHU, LW, LWU | 5 | 3 |
| SB | 8 | 6 |
| SH, SW | 9 | 6 |
| LR.W | 9 | 7 |
| SC.W | 26 | 21 |
| AMOSWAP.W | 18 | 8 |
| AMOADD/AND/XOR/OR.W | 19 | 9 |
| AMOMIN/MAX(U).W | 24 | 13 |

- **New row operations** the replayer and the Tracer must compute (jolt-cpp `main` at `52fbbab`, `src/tracer/isa/
  virtual.cpp` and `trace_owner.cpp`):
  - `VirtualLoadB/H/W`, `VirtualStoreB/H/W`: load or store the doubleword containing the access (address rounded down);
    memory rows like LD/SD.
  - `VirtualWindowMaskB/H/W`: the lane's mask from the address's low bits, e.g. `0xFF << 8 × (addr & 7)`.
  - `VirtualShiftDataB/H/W`: the store value moved into its lane, `(v & 0xFF) << 8 × ((base + offset) & 7)`; needs the
    store's offset as a new field (`shift_offset`), so the **expansion word must carry it**.
  - `VirtualPext`, `VirtualPextSigned`: **parallel bit extract** by the mask, optionally sign-extended. Every use in
    `main` feeds a single contiguous lane (from `WindowMask`), where it is a right shift plus zero- or sign-extension;
    Jolt defines general PEXT (T6).
  - **PEXT in hardware** (owner, 2026-10-30; answers T6): **one run of ones per cycle.** Each cycle the unit takes the
    mask's lowest run, *n* ones starting *m* bits up (the mask's low end has the form `x…x 1ⁿ 0ᵐ`), shifts those *n*
    bits of the value down, appends them to the result, clears the run from the mask, and repeats until the mask is
    empty. A contiguous mask, every current use, takes **one cycle**; a scattered mask takes one cycle per run, at most
    32. Per cycle: two trailing-zero counts (*m*, then *n*), a shifter and a mask generator, far smaller than a full
    compaction network. `VirtualPextSigned` sign-extends from the last bit placed. Rows run at fixed cycle counts
    otherwise, so a multi-run PEXT stalls its pipeline; no current expansion produces one.
  - Gone: `VirtualAlignAddr` and the alignment assertions.
- **Timing** (owner, 2026-10-31): **v1.2 stays on `4f36b3d`**, so the simulator port, the sort and the replayer run against
  what was built and measured; **the move is v1.3 work**, together with the Tracer, since both the replayer's and the
  Tracer's tables are rebuilt anyway. Pin a specific `main` commit when the work starts, not a moving branch.
- **The work:** diff the two versions' expansions in full; rebuild the replayer's opcode table, ROM templates and
  decode table (spec 4.19.6's template count and ROM fill); add `shift_offset` to the expansion word; regenerate the
  replayer's test vectors; and the same for the Tracer's tables (7, 8). WORKER7's advice work is unaffected (the
  division-family advice code is byte-identical).

---

## v1.3 work list

Work agreed for v1.3, each with the decisions it carries out. Studies (below) are separate.

| # | task | from | status |
|---|---|---|---|
| W1 | **Bring the replayer and the Tracer up to date with jolt-cpp's latest.** Pin a current `main` commit; diff its expansions against `4f36b3d` in full; rebuild the replayer's opcode table, ROM templates and decode table (spec 4.19.3-4.19.7, the template count and ROM fill); add `shift_offset` to the expansion word; add the new row operations (`VirtualLoad`/`Store`, `WindowMask`, `ShiftData`, PEXT one run per cycle); drop `VirtualAlignAddr` and the alignment assertions; regenerate the replayer's test vectors; rebuild the Tracer's tables (7) and re-check its advice templates (8) | 9 | agreed (owner, 2026-10-31) |

---

## Candidates (not decided)

Options recorded for later, each with the evidence that would justify it.

| # | candidate | why | cost |
|---|---|---|---|
| C1 | **A delay per destination:** decrement a counter **2 cycles** after acceptance for writes to controller memory and peripherals (their landing time is fixed, `cm_latency`, `rp_latency`), and K = 8 only for big SRAM | WORKER4's measurement: a read-port search step costs about **6 cycles more** than in v1.1 (35.9 + 37.42 per active search against 29.8 + 37.43), because v1.2 decrements at K = 8 where v1.1 counted controller-memory writes at landing. The extra is in the **fixed** term: about 6 cycles **per search step**, shared by all the searches active in it, not per probe (the per-search slope is unchanged). So roughly 6 × the steps per split, about a hundred cycles per split and a few hundred per sort: **negligible today**. Streaming work doesn't notice | two delay taps; a counter may then take two decrements in one cycle (one from each tap), a small adder change. Adopt if the sort's measurements show the split slowing |
| C2 | **Signed counters** (v1.2 section 36): an arrival before arming just goes negative, so the order of arming and arrival stops mattering, and strays are caught as done early | removes the GO-to-arming window | a signed counter; the immediate stray report becomes meshsim's only |
| C3 | **Trim the 256-bit word's storage** to the bits in use, with the links staying 256 wide (v1.2 section 2) | power: constant-zero flops are still clocked | revisit when the design has settled |

---

## Studies for other sessions

| # | study | why |
|---|---|---|
| ST1 | **Compare the two Jolt tracers' traces:** jolt-cpp (https://github.com/snarkify/jolt-cpp, ours) and the Rust Jolt tracer (https://github.com/a16z/jolt). They are written differently and may work slightly differently but are believed to produce the same traces. Run the same guest programs through both and find every place the traces mismatch (rows, `instr_idx`, advice, memory values, padding) | A mismatch is either a bug in one tracer or a difference our hardware must pick a side on. The spec's ground truth is upstream Jolt (spec 4.19, item 113), and our replayer and Tracer are validated against jolt-cpp |

---

## Pending questions

| # | question | from | status |
|---|---|---|---|
| T1 | What the 8 KB and 4 KB expansion memories hold | 1 | **answered: section 7** (same format and builder as the replayer's ROM and decode table; different contents) |
| T4 | The Tracer's template coverage | 7 | **answered: section 7** (a list in the ROM; the rest fed by C&C 3, replayer-style) |
| T2 | The RV64 initial state: what it contains and where it comes from | 1 | open |
| T3 | The page table: its size, format and how C&C 3 maintains it | 1 | open |
| T6 | `VirtualPext`/`VirtualPextSigned` in hardware | 9 | **answered: section 9** (one run of ones per cycle) |
| T5 | The inlines' advice (`VirtualAdvice`): untraced rows, or C&C 3's software when it feeds their templates | 8 | open |
