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

Agreed (O). Answers T1. *(Section 13 removes the untraced advice rows and their bank-1 registers; the rest stands.)*

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

*(Superseded by section 13: advice comes from the host, so the Tracer needs no divider.)*

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

- **What changed in the row operations** *(corrected 2026-10-31 by WORKER9's diff of `821e1bd` against `4f36b3d`; the
  first version of this list, read at `52fbbab`, wrongly called `VirtualPext`, `WindowMask` and `ShiftData` new: they
  already existed at `4f36b3d` and are in v1.2's replayer opcode table, spec 4.19.7)*:
  - **New opcodes, only:** `VirtualLoadB/H/W` and `VirtualStoreB/H/W`, which load or store the doubleword containing
    the access (address rounded down); memory rows like LD/SD.
  - **`ShiftData*` changed meaning:** its second operand is now the base register, with the access's offset in a new
    field, `shift_offset` (signed), so **the expansion word must carry it**.
  - **Narrow memory rows** now carry the access's immediate, and the address is rounded down to the doubleword.
  - **A misaligned access** aborts the tracer instead of producing an assertion row; `VirtualAlignAddr` and the
    alignment assertions are gone.
  - **`VirtualHostIO`** has no operands any more (still one row, different contents).
  - **The AMO\*.W bodies** changed beyond their length (e.g. `AMOMAXU/MINU.W` now sign-extend).
  - **The opcode numbering** shifted: 6 opcodes inserted, 3 removed.
  - **PEXT** (owner, 2026-10-31; revises the 2026-10-30 entry; answers T6). **The spec requires `VirtualPext` and
    `VirtualPextSigned` to be correct for any mask, contiguous or not**, replacing v1.1's narrowing to contiguous masks
    (spec invariant 16). Jolt's prover proves the general case (jolt-cpp's PEXT lookup table and its prefix/suffix
    split handle any mask), so a future expansion could use a scattered mask, and the hardware must still run it.
    **How is the implementation's choice**, not the spec's: a single-pass extract network, or an iterative unit taking
    one run of ones per cycle (the mask's lowest run `x…x 1ⁿ 0ᵐ` each cycle: two trailing-zero counts, a shifter and a
    mask generator), as Tony first sketched. **The spec promises full speed only for contiguous masks**, every mask
    current Jolt produces; any other mask takes an implementation-defined number of cycles.
  - Gone: `VirtualAlignAddr` and the alignment assertions.
- **Timing** (owner, 2026-10-31; *revised: the move is milestone v1.25, between v1.2 and v1.3*): **v1.2 stays on `4f36b3d`**, so the simulator port, the sort and the replayer run against
  what was built and measured; **the move is v1.3 work**, together with the Tracer, since both the replayer's and the
  Tracer's tables are rebuilt anyway. Pin a specific `main` commit when the work starts, not a moving branch.
- **The work:** diff the two versions' expansions in full; rebuild the replayer's opcode table, ROM templates and
  decode table (spec 4.19.6's template count and ROM fill); add `shift_offset` to the expansion word; regenerate the
  replayer's test vectors; and the same for the Tracer's tables (7, 8). WORKER7's advice work is unaffected (the
  division-family advice code is byte-identical).

## 10. The Tracer computes all its advice; C&C 3 generates long expansions (2026-11-01)

*(Superseded by section 13 for advice: the host supplies it, and C&C 3 generates no inverses. Q10 stays moot.)*

Decided (O). Answers T5; makes Q10 moot.

- **The Tracer computes every advice value itself, through expansions** (untraced rows, 7-8). **C&C 3 never computes
  advice**; it only feeds template words. "Every advice" in jolt-cpp is the division family (six advice opcodes,
  WORKER7's templates) and `VirtualAdvice` in the seven secp256k1 inlines: the field and scalar multiply and square
  (200-381 untraced rows, generated and checked by WORKER7), the two division inlines and GLV. Upstream Rust Jolt's
  advice tape doesn't apply (jolt-cpp doesn't implement it; WORKER7's F2).
- **Q10 is moot:** C&C 3 never needs the inline's operands, so it never reads the Tracer's dirty cache lines.
- **Long expansions are generated, not stored.** The division inlines' advice needs a modular inverse, about 53,000-
  110,000 untraced rows as one straight-line template (about 1.7 MB of `templates.bin`, more than C&C 3 can hold). But
  **C&C 3 produces the template words, so it can loop**: a Fermat inverse repeats one short block (square, and multiply
  where the exponent's bit is 1), so C&C 3 holds the block once, a few hundred words, and emits it the required number
  of times.
  - **The sequence must not depend on the data**, since C&C 3 never sees the operands. Fermat's method qualifies: the
    exponent is the fixed p − 2, so the order of rows is the same every time. A data-dependent inverse (binary GCD)
    would not.
  - **The feed is paced by the credits** (owner): a credit is permission to send up to half a queue, filled in as many
    pieces as C&C 3 likes (v1.2 section 10), so C&C 3 generates the words on the fly and sends them in batches as credits
    arrive. No buffer holds the whole expansion, and no streaming rate is required: if C&C 3 falls behind, the Tracer
    waits, which is acceptable at this rarity.
  - **The expansion's length is known up front** and sent in the first word, as for any controller-fed template.
    *(Revised by v1.25 section 5, Q6: it counts **traced** words only; untraced words are marked and uncounted.)*
- **For the table builder:** a long data-independent expansion is emitted as **a loop body plus a repeat count** (and
  any prologue and epilogue), not as its full word sequence.

## 11. The Tracer's time budget, and what the workloads cost it (2026-11-01)

*(Section 13 removes the untraced rows this budget counts; its host-supplied backup for T7 is now the plan.)*

From WORKER12's counts (`WORKER12/REPORT.md`; both workloads at 2²⁹, traced totals matching WORKER9's). *Corrected the same
day: the first version assumed one row per cycle and counted only window 34's untraced rows; the Tracer runs at **about 4 cycles
per row** (owner), and windows 33-36 all fall in the same proof.*

- **The Tracer's time budget: under 100 ms** (owner) **per proof**, in which each Tracer traces its own 4M-row segment (a
  proof is the whole run over 128 ASICs: section 14).
- **What the workloads cost**, at about 4 cycles per row and 1 GHz:
  - **Cyclone:** 96,687 untraced rows against 371.5 M traced (0.026 %), all from the division family; at most 129 in any window.
    No secp256k1 inlines. A proof is about 17 ms.
  - **EVM Zone:** 32.0 M untraced rows (9.0 % of traced), 98 % from 254 DIVQ and 2 DIVR, **all in windows 33-36, so all in its
    first proof** (window 34 alone: 11.07 M untraced against 65,536 traced).

| EVM Zone's first proof | traced | untraced | at 4 cycles per row |
|---|---|---|---|
| single Fermat loop, as built | 4.19 M | about 32.0 M | **about 145 ms: over the budget** |
| addition chain (C4) | 4.19 M | about 16.8 M | **about 84 ms: under, with about 16 % margin** |

- **Q8 and T7: keep the single Fermat loop, with host-supplied advice as the backup** (owner, 2026-11-01). The loop puts EVM
  Zone's first proof at about 145 ms, over the budget, but the system has a fallback that needs no Tracer hardware:
  - **The host already knows every advice value**, in order, because it emulates the program itself.
  - **C&C 3 already generates the inverse's expansion on the fly** (10). Instead of the 124,000-row loop it can send **a short
    expansion that puts the known results into the right bank-1 registers** (a few untraced rows, the values as constants); the
    traced rows read them through `advice_reg` exactly as before, so the trace and the replayer are unchanged.
  - **What it needs, if used:** the host delivers each advice result to C&C 3 through Ingress **before the Tracer reaches the
    call**, keyed by its position in the run (`instr_idx`), and C&C 3 matches them; the values equal jolt-cpp's, as they will if
    the host computes them with jolt-cpp.
  - It is a fallback for any heavy advice, not only the inverse. Used only if a workload's advice threatens the budget.
  - The addition chain stays candidate C4; an inverse unit in the Tracer is not needed.
- **The CSR decision holds** (v1.25 section 6): Cyclone runs 8,751 CSR instructions (10 per trap, 875 traps, at most 160 in any
  window), EVM Zone none.

## 12. The Tracer's region: parts 0 and 1 (2026-11-01)

*(Section 13: no untraced rows, so bank 1 holds no untraced scratch; the starting state is unchanged.)*

Decided (O). The Tracer is C&C 3's peripheral; everything C&C 3 or the memory system gives it arrives as 128-bit packet writes into
its region, with **offset bits [22:20] picking the part**, as in the replayer's (spec 4.19.10, CL1).

- **Part 0, 0x000000-0x003FFF, is the replayer's layout, unchanged:** the state word (pc, `instr_idx`, next offset; the four flags
  ignored), the stream word (ignored), the register image (registers 0-127, bank 0, at 0x20 + 16*i*), the decode table at 0x1000
  and the ROM at 0x2000 (the Tracer's own contents, same format and builder, section 7). The same loader code serves both units.
  Bank 1 (untraced scratch) needs no initial state. The next offset is normally 0: the Tracer starts at an instruction boundary.
  - **T2, answered (WORKER16, 2026-11-01; `WORKER16/REPORT.md`):** at instruction boundaries, pc, `instr_idx`, registers 0-127
    and RAM are the **whole** state: restarts matched at 1,096 of 1,096 points on Cyclone and 673 of 673 on EVM Zone at 2²⁹, once
    mid-secp points carry jolt-cpp's advice buffer (`trace_owner.cpp:49-50`; section 13 covers it). The CSRs and the LR/SC
    reservation are live, in registers 32-39. **Registers 40-47** are never read across an instruction but set the next write's
    `rd_prev`, and jolt-cpp's JIT doesn't track them (section 13, open (c)); registers 48-127 were 0 at every instruction start.
- **The page table is the guest's memory as a file** (owner, 2026-11-01; restated with section 20): **`.pages` is a sparse file**
  whose byte offset is the guest address minus `lowest_address`, and **its tree is the page table**, in 4 KB pages throughout
  (replacing a fixed three-level table with 8 top registers, before that 16 KB pages, 11 + 10 + 11 bits, a 4 GiB table and a flat
  512 MiB one). Future programs are unknown, so the hardware takes the general case; storage scales with the pages that exist.
  - **a data page:** 4 KB, 512 cells of 8 bytes;
  - **index pages of 1,024 entries** above it, each level covering 1,024 times the one below (4 MB, 4 GB, 4 TB);
  - **the depth follows the file's length** (section 20): **2 for K up to 2²⁹ cells** (a 4 GB, 32-bit offset; both workloads), 3
    above it, up to section 14's 2³² cells.
  - **One chunk size everywhere** (data pages, index pages, files, logs), so the memory system has a single 4 KB pool and
    nothing fragments.
  - **Translation belongs to the memory system** (section 20): the Tracer asks for (root, depth, line) of `.pages`; the memory
    system's TLB and tree walker do the rest, through the shared cache, at most one index read per level.
  - **Measured clustering** (WORKER17, 2²⁹, in 16 KB pages): EVM Zone 69 pages in 3 16 MiB regions; Cyclone 693 pages in 4
    (the 2.9 MB untrusted advice, and one 7.75 MiB run of stack plus heap). At 4 KB both still fit a handful of leaf tables.
  - **Entries are 32 bits; bits 30:0 address the file system** (LPDDR) **in 128 B units** (owner; 2³¹ x 128 B = 256 GiB), the
    format in LPDDR: **an address names a 128 B unit**, and **the memory system reads a page (4 KB, the most efficient), a half
    page (2 KB), or 128 B (poor)** (owner). Tables and data pages are whole 4 KB pages, so a page-table entry's low 5 bits are 0 and
    fetching a table or a data page is one page read; other pointers (a half page, a 128 B unit) use the finer addressing.
    - **every entry, at every level:** bits 30:0, the page's address (an index page or a data page); **bit 31, the owned bit**
      (owner, 2026-11-01): 1 means this file created the page; 0 means the page is shared from an ancestor and must be copied
      before any change (section 20's `cow_open`). It is never cleared; whether a file may be written at all is a file-level
      read-only flag. (Replaces a write bit, which freezing cleared and so lost ownership; and before that a read-only bit on
      bottom entries only.)
    - **no entry: `0xFFFFFFFF`** (owner), at every level; no valid entry has its low bits set, and address 0 stays usable. A table
      must be filled with `0xFFFFFFFF`, not zeroed: a zeroed entry is a valid pointer to address 0.
  - **A 512 KB cache of the file system, shared by the whole chip** (owner, 2026-11-01): **4,096 lines of 128 B**, caching
    file-system (LPDDR) addresses, **after** translation, not any unit's address space; **a miss fills 128 B** (owner). One cache
    in front of LPDDR keeps every unit coherent (the Tracer's tables and pages, C&C 2's directories and files, the Updater's
    images, the cores): nothing sees stale data, and no private caches need invalidating. A copied page has a new address, so
    copies can't alias, and pages shared between proofs are cached once.
    - **Bulk streams bypass it** (integrator's proposal, with the sharing): the Updater's image building and the logs move whole
      pages or half pages directly, invalidating any cached lines they overwrite, so they don't push others' working sets out.
    - **Priority: the file system above the Tracer** (owner); the Updater, which must never stall (the broadcast stream), above
      both for LPDDR bandwidth.
    - **Line accesses only** (owner): word accesses happen only in the Tracer's local caches (section 21); every client of the
      shared cache reads or writes whole 128 B lines (a client changing part of a line holds the line).
    - **Organization: 8-way set-associative, 512 sets** (owner, 2026-11-01): an address is
      already in 128 B units, so its low 9 bits pick the set and the other 22 bits are the tag; replacement LRU within a set.
    - **Banking:** 8 banks of 64 KB, interleaved on the line address's low bits (a page's 32 lines spread over all of them);
      each bank serves one 1,024-bit line per cycle; arbitration per bank, the file system ahead of the Tracer.
    - **Misses: non-blocking**, about 8 to 16 outstanding (so a Tracer miss to LPDDR doesn't hold up a file-system hit); a miss
      fills 128 B.
    - **Write-back, write-allocate;** optionally, when all 32 lines of a page are dirty, they go back as one page write.
    - **Coherence with bulk streams through allocation:** streams write freshly allocated pages, and **a page has no lines in
      the cache when it is allocated**: freeing a page invalidates its 32 lines (one tag lookup in each of the 32 sets it spans),
      so bulk writes never snoop.
    - **Open:** the ways, the banks and the outstanding misses, from gold replaying both workloads' address streams (section 21
      (d)).
    - **The file system moves data pages directly** (owner), whole 4 KB pages like the Updater (section 20's buffers), and
      reads and updates **index pages through it** (section 20). So the shared cache serves the Tracer and the controllers' line accesses. One coherence rule covers every page
      client: **writing a page directly invalidates its 32 lines in the cache**, and **a page read directly has no dirty lines
      there**, by ownership (the only pages dirtied through the cache are the Tracer's private copies, which no page client
      reads during a segment).
  - *(A page-table stream format, a walk of the 2,048 top-level entries and their leaves, was proposed here and is superseded:
    the tables live as files and are read in place, section 19.)*
- **Findings from upstream Jolt** (a16z/jolt `main` at `17a9459`, read 2026-11-01; `common/src/jolt_device.rs`, `constants.rs`):
  addresses are 64-bit, with **no 4 GiB limit in Jolt** (jolt-cpp's 4 GiB is its emulator's own reservation). The used space is
  **one contiguous range**: the I/O region (trusted and untrusted advice, inputs, outputs, termination and panic words) **below
  `RAM_START_ADDRESS` (0x8000_0000)**, then the program, a stack canary, the stack and the heap (configurable, default 32 MiB)
  **above it**. Every address is remapped to a cell index `(address − lowest_address) / 8`, `lowest_address` being the bottom of
  the advice area; below it is an error; K bounds the number of cells. The range moves with configuration (a Jolt test with 512
  MiB of advice puts `lowest_address` at 0x4000_0000). **Programs use both ends of the range and leave the middle empty** (owner).
  So: a directory indexed from a **base register set to `lowest_address`** suits it. Not yet checked against jolt-cpp's layout.
- **Next research area for v1.3** (owner, 2026-11-01): **the Tracer's memory: the page table and how it maps Jolt's layout** (its
  two levels and sizes, the base, what an unmapped page means, the entry format, how C&C 3 builds and loads it per proof, the
  throwaway layer, how jolt-cpp's layout compares with upstream's). "Needs to be dug into a lot more"; section 12's sizes are
  proposals only.

## 13. Advice comes from the host, in the read log (2026-11-01)

Decided (O). Supersedes section 7's untraced advice rows and their bank-1 registers, section 8, and section 10's first and
third bullets; answers T7 the way section 11 kept as the backup; makes C4 moot.

- **The host supplies every advice value.** For each slice the host sends the Tracer an **input read log** of that slice's
  advice words in row order. An advice row (`VirtualAdvice`, or the division family's advice) **pops the next read-log word into
  `rd`, in the Tracer exactly as in the replayer** (spec 4.19.7); the Tracer copies each word into the read log it writes for the
  replayers.
- **Slices: 128 advice streams for a 2²⁹ run, one per 2²² (4M) rows**, aligned with the replayer windows. Rows are Jolt's,
  virtual rows included. A sequence that crosses a boundary puts its pending words at the head of the next stream.
- **The host builds them with jolt-cpp's JIT engine**, extended to emit the streams as it runs: the quotient or remainder for the
  division family, `evaluate(operation, a, b)`'s 8 words for the secp256k1 inlines. The JIT already runs to each slice's start
  (`execute_upto_cycle`, `trace_owner.cpp`), handing over to the interpreter inside virtual sequences, where `prepare_advice`
  runs today. Work item W2.
- **No untraced rows.** Every untraced row existed to compute advice (WORKER12 at 2²⁹: 32.0M on EVM Zone, nearly all from 254
  `SECP256K1_DIVQ` Fermat inverses at about 124K rows each, up to 11.07M in one window; 97K on Cyclone). Gone with them: their use
  of bank 1, `advice_reg`, section 8's divider, and C&C 3's generated inverses. **The Tracer's templates become the replayer's.**
- **The starting state stays T2's:** pc, `instr_idx`, next offset and registers 0-127. jolt-cpp's secp advice buffer
  (`secp_advice`, `next_secp_word`, `trace_owner.cpp:49-50`) is the one piece of state WORKER16 found outside them, live mid-sequence;
  under this section it becomes the head of the next slice's stream.
- **Size, measured (WORKER16, 2²⁹):** EVM Zone 3,782 advice words in all (per slice: min 1, mean 44.5, max 3,676; its secp work
  all in slice 0); Cyclone 96,687 (min 0, mean 1,086, max 3,094). No 2²² boundary fell inside a secp sequence on either run.
- **Words per operation:** a secp inline takes `shape(op).advice_words` (`inlines/secp256k1_shapes.hpp:10-16`), not always 8:
  4 for MULQ, SQUAREQ and MULR, 6 for GLVR, 8 for DIVQ and DIVR; the division family takes 1. The streams must carry exactly
  what the rows consume, or they fall out of step (W2).
- **New for the Tracer:** a read-log input queue with its credits, fed by its controller.
- **Open:**
  - (a) ~~v1.25: the expansion word's tracer-only fields~~ **done:** reserved in v1.25 (DECISIONS-v1.25 section 9, spec `b7c90fb`).
  - (b) ~~Whether bank 1 has any use left~~ **decided: probably not** (owner, 2026-11-01): with no untraced rows and a
    128-register processor state (section 15), **the Tracer is a 128-register machine**; bank 1 (registers 128-255) goes.
  - (c) **Registers 40-47 at a slice's start.** They set the next write's `rd_prev` and come from trace history, not RISC-V
    state; jolt-cpp's JIT doesn't track them (WORKER16). jolt-cpp's tracer and GPU compressed-trace expander do handle them
    (owner): find how, and what that means for slice starting states built on the host (W2).
    **Scoped** (WORKER16, W3 report): the JIT tracks registers 0-47; 14 families (53 opcodes) write 40-47, each final value a
    formula of what the JIT already has (operands, effective address, aligned memory word, quotient, reservation registers, pc);
    note `rd` = x0 remaps `rd` to 40 and shifts the temporaries up by one. Plan: the four frequent families inline (about 99.5%),
    a generic helper for the rest; the formulas are verified only once built, by the W3 checker's restarts.
    **Deferred** with W2 (owner): needed in the long run, not soon.

## 14. RAM K: up to 2³², one K per run (2026-11-01)

Decided (O); also a reasonable size for the chip (O).

- **RAM K ranges up to 2³² cells** (32 GiB of guest range). Smaller runs keep the smaller sizes: K is the next power of two above
  the highest touched cell, as jolt-cpp chooses it (`node_context.cpp:318-371`, `trace_owner.cpp:1134-1140` at the pin; a
  program-image floor, 2²³ for both workloads). Measured (WORKER17, 2²⁹): EVM Zone 2²³, Cyclone 2²⁴. K follows the run, not the
  layout, so it can change between inputs of the same program.
- **A proof is the whole run** (owner, 2026-11-01: a 4M segment is never its own proof): T = 2²⁹ for our workloads, spread over the
  128 ASICs, one segment each. So **jolt-cpp's chunks are 8 bits** (log T ≥ 25, `config.hpp:121-127`; we match jolt-cpp, open (b)).
- **The RAM one-hot chunks follow K:** *d* = ⌈log K / 8⌉ (`onehot_params.cpp:11-12, 48`), and the RAM `ra` virtualization's degree
  is *d* + 1 (upstream `relations/ram/ra_virtualization.rs:60-62`). **At K = 2³², *d* = 4 and the degree is 5**; our workloads
  (2²³, 2²⁴) have *d* = 3, degree 4. jolt-cpp's whole-run proofs use the same (WORKER17).
- **jolt-cpp's K limits** (WORKER17): the `ra` virtualization accepts 2-6 chunks at the pin (`MAX_D` = 6), 2-8 at HEAD, so at
  8-bit chunks it is no limit. But **the GPU read-write checking refuses K above 2²⁴ at the pin, whatever the chunk width**
  (`RW_MAX_ADDRESS_SPACE = 1u << 24`, `ram/gpu/rw_sparse.hpp:33`, checked at `rw_sparse.tpp:93-97`), **2³⁰ at HEAD**; and the GPU
  output check holds `ram_k` in a `uint32_t` (`ram/gpu/output_check.cu:89`). Both workloads fit, Cyclone exactly at 2²⁴. *(Earlier
  text here assumed 4M-row proofs, and then said the pin was no limit at 8-bit chunks; both superseded.)*
- **What jolt-cpp sizes densely by K** (WORKER17, at the pin; upstream not yet read):

| sumcheck | dense in K | sized by T or by region |
|---|---|---|
| read-write checking | CPU: arrays and loops over K; GPU: about 7 K field elements | the accesses, sparse over T |
| raf evaluation | 2 K field elements | |
| output check | K / cards field elements per card, plus initial and final RAM snapshots of K x 8 bytes on every card | |
| `ra` virtualization, Val check, Hamming booleanity | | T |
| program image, untrusted advice | | their own words |

  At K = 2³² a snapshot is 32 GiB and a K-sized array of field elements 128 GiB, so **jolt-cpp as written can't reach 2³²**.
  Against that, our workloads touch 69 and 693 pages (1.1 and 11 MB). Open (d).
- **The hardware's sumcheck engines** handle the RAM `ra` virtualization at degrees 3 to 5 (*d* = 2 to 4). The other RAM
  sumchecks' degrees don't depend on K; they gain address rounds, up to 32.
- **Open:**
  - (a) ~~per slice or whole run~~ **decided: one K for the whole run** (owner, 2026-11-01).
  - (b) ~~the chunk width~~ **decided: match jolt-cpp** (owner, 2026-11-01): it is part of the proof's shape, which the
    verifier shares. With a proof being the whole run, that is 8 bits (above).
  - (c) ~~Moving the jolt-cpp pin~~ **decided** (owner, 2026-11-01): **v1.25 stays at 821e1bd** (frozen, accepted there);
    **v1.3 moves to a deliberate new pin**, HEAD (88dca85) or later, after two checks: `src/tracer/isa` unchanged (true at
    88dca85: 13 commits, the tracer's only change is image loading and reset, #1544), and row hashes per window identical on both
    2²⁹ workloads. PR #2's tracer copy (7 files) is re-synced between WORKER16's tasks, not during one.
  - (d) **The K-dense steps** (above): our hardware computes them over the touched pages instead (an absent page is zero, so
    untouched cells contribute nothing or a closed form), or K is capped where dense structures fit. Upstream's Val_init and
    K-dense parts are WORKER17's next reading.

## 15. What the host sends per segment: images and transport (2026-11-01)

Decided (O), from the planning discussion of 2026-10-19 recorded in jolt-asic-planning PR #1 (branch `tracer-standalone`,
`docs/write-stream-experiments.md`, with measurements on both workloads) and meshsim's planning notes of 2026-10-27. A proof is
128 **segments** of 4M (2²²) rows, one per ASIC.

- **Each segment comes from the host with three chunks** (owner):
  1. **the processor state** at the segment's start: pc, `instr_idx`, the next offset, and registers 0-127. **A segment can start
     inside a sequence, just like a replayer window** (owner): the next offset is then nonzero, registers 48-127 hold the
     sequence's live scratch, and the sequence's pending advice words head the advice log (section 13). The host takes this
     state from its interpreter, which steps the last rows to the exact cut (the JIT stops at the instruction boundary before);
     at an instruction boundary registers 48-127 are 0;
  2. **the advice log**, the segment's read-log input (section 13);
  3. **the memory updates list**, the segment's delta: each word written during the segment with its last value (the dirty set,
     "R-dirty" in PR #1; the host reads the values from guest memory at the segment's end, so stores are never logged).
- **A proof's stream starts with the page counts** (owner; frame 1, section 16), before any chunk: **a per-segment table** (the
  4 KB pages each segment writes) and **a per-ASIC table** (each ASIC's exact page needs). Every ASIC
  reads all 128 counts, so C&C 3 can reserve exactly the pages the proof needs before any data arrives: its own segment's
  private copies (below) and the pages the updates add to the start and final images. That is the planning notes' "the host
  computes each ASIC's exact page count, so memory never runs out mid-proof". The host's JIT has the counts from its page
  bitmaps (PR #1 section 4).
- **Who needs what.** A segment's ASIC needs its own processor state and advice log. Memory is shared: the start image of segment
  *k* is the initial image plus the updates of segments 0 to *k* - 1, and the final image is the initial image plus all of them.
  So every ASIC receives **every segment's updates list, applied in segment order** (section 16; owner, 2026-11-01, replacing
  PR #1's single stream sorted by address and merged across segments: per-segment lists are simpler on both sides and are what
  one pass over a run produces).
- **Three images per ASIC:** the initial image (cached, permanent), its segment's start image, and the final image, built by
  applying the segments' lists in order (the planning notes' Updater, unit 73).
- **Two copies of memory at the segment's start** (owner): the proof needs the start image intact, and the Tracer runs on its own
  copy and changes it. With the page table (section 12) the second copy costs only the written pages:
  - pages the segment only reads are **shared**, marked read-only;
  - pages the segment writes are **duplicated before the Tracer starts**, the Tracer's table pointing at the private copy. The
    host's JIT knows exactly which pages those are (PR #1 measured 182 and 499 pages written in a whole 2²⁹ run, EVM Zone and Cyclone).
  - So **the Tracer never copies on write** (owner): a store to a read-only page means the table is wrong, which is UB (8a: the
    tables come from our software). The Tracer's copy is the planning notes' "throwaway layer".
- **An absent page reads as zero** (PR #1 section 4). Answers T3's question for absent pages.
- **The encoding: an LRU coder with bounded expansion, not LZ4** (owner): an LZ4 match can expand into a long copy while consuming
  almost no input, so output outruns input and data must queue as it flows in from the host. A bounded-expansion code (each code
  word at most one 64-bit value) keeps the decoder at the link's pace with only the 8 KB input queue (planning notes: no ring in
  LPDDR). Measured, R-dirty with an LRU of 512: about 2.6 MB per proof for EVM Zone and 12.5 MB for Cyclone, against about 25.8 MB
  raw for Cyclone; a raw mode per segment covers incompressible data.
  **A small LRU: 32 entries** (owner): a hit names its entry in 5 bits, 6 with the hit flag. PR #1 measured 512 entries (49-83%
  hits); 32 entries hit less, which the links' slack absorbs; to measure.
- **The transport is one byte stream, identical for every ASIC** (owner). So the per-segment chunks (processor state, advice log)
  travel in the same stream as the broadcast updates, and **each ASIC picks out its own segment's chunks**, by their position:
  frames come in segment order (section 16). Below the byte stream: LVDS in, an 8 KB byte queue with flow control, credit-based flow to the Updater,
  which must never stall (planning notes). How the stream reaches each ASIC (PR #1's chain per card, route 2') doesn't change
  what an ASIC receives.
- **Open:**
  - (b) ~~the framing~~ **decided: section 16.** (The segment tag isn't needed: frames come in segment order.)
  - (c) ~~FIFO against true LRU~~ **decided: true LRU** (owner): at 32 entries it is a move-to-front list, the same on both
    sides (a hit sends the entry's rank and moves it to the front; a miss sends the value and pushes it at the front, dropping
    the last). **Packing: copy the replayer's stream coding** (owner; spec 4.19.15): bit-packed, least significant bit first;
    **hit:** flag 1, then a 5-bit index (6 bits); **miss:** flag 0, a size bit (0 = 32, 1 = 64), then the value, a 32-bit miss
    zero-extended. Here the bits run least significant first within each byte of the byte stream. When the table resets (each
    segment's updates, or each chunk) is open with (b)'s framing.
  - (d) the physical topology (the chain, route 2'), which doesn't change the stream;
  - (e) ~~the read-only pages~~ **measured** (WORKER17, 2²⁹): EVM Zone 11 read-only and 58 written pages; Cyclone 194 and 499.
    Per segment, written pages min / median / max: 10 / 10 / 39 and 3 / 19 / 266; read-only: 6 / 8 / 12 and 8 / 69 / 180. All of
    Cyclone's untrusted advice is read (178 pages); EVM Zone reads all 64 KiB of its trusted advice. Only 85 and 89 of the 128
    segments have rows; the rest are padding.

## 16. The segment file: framing (2026-11-01)

Decided (O). **A tool on the host runs the trace and writes the file** (a slow one first is fine), which later runs load instead of
tracing again (W3). It is also the byte stream every ASIC receives (section 15). "We might opt for something else later, but it's
a really easy format to work with, from multiple languages" (O).

- **Framing, as jolt-cpp's RPC messages** (`src/jolt-infra/rpc/message.{h,cpp}`: a response is [count][code][data], `header_limbs`
  = 2): every frame is
  - **u32 length, in 32-bit words, the header included** (so 2 + the data words, at least 2), least significant byte first;
  - **u32 type**;
  - **the data, zero-padded to a whole number of 32-bit words.** Every multi-byte value is least significant byte first.
  - jolt-cpp rejects a zero length and caps a message at 2²⁶ words (256 MiB); a segment's raw updates are at most about 34 MB
    (one new word per cycle), so the cap is comfortable.
- **Frame types:**

| type | frame | data |
|---|---|---|
| 1 | page counts | **a per-segment table**, 128 x u32: the 4 KB data pages each segment writes; then **a per-ASIC table**, 128 x 3 u32: for the ASIC running segment *k*, the exact pages, data and index together, of its start image (the union of segments 0 to *k* - 1's writes, with their paths), its final image (the union of *k* to 127's) and its Tracer's copy (segment *k*'s own) (section 20's arenas) |
| 2 | processor state | pc, `instr_idx`, the next offset, registers 0-127: 131 x u64, 1,048 bytes |
| 3 | advice log | the segment's advice words, u64 each, in row order (section 13) |
| 4 | updates, raw | the segment's update words, u64 each |
| 5 | updates, LRU-compressed | a u32 count of the 64-bit words encoded, then the codes (below) |
| 6 | jump-target supplement | section 18: the run's jump targets the program's static table lacks, as (pc offset u32, base `instr_idx` u32) pairs | a u64 start address (64 B aligned), then two u32 per 64 B block of code from there, the base and the mask (section 18); part of the program's load, with the initial image |

- **Order:** frame 1 once, first; then frame 6, the proof's jump-target supplement; then for each segment in order, frames 2, 3, and
  one of 4 or 5 (the host's choice per segment).
- **The update words:** the segment's dirty runs in address order (PR #1's "R-dirty"): each run is **a header word** (the start
  cell in the low 32 bits, the word count in the high 32) **followed by that many values**, each the word's last value in the
  segment. Headers and values form one sequence of 64-bit words, so frames 4 and 5 carry exactly the same words.
- **Frame 5's codes:** the replayer's coding (spec 4.19.15) with a 32-entry table: bit-packed, least significant bit first within
  each byte; **hit:** flag 1, a 5-bit index; **miss:** flag 0, a size bit (0 = 32, 1 = 64), the value (a 32-bit miss
  zero-extended); true LRU, move to front. **The table is all zeros at the start of each frame 5**, so each decodes on its own;
  the leading word count tells the decoder where the codes end (the last byte may hold spare bits).

## 17. A Java emulator of Jolt's tracer, to become our gold (2026-11-01)

Decided (O).

- **A standalone Java program** in `wip/gold/` that reads the guest's ELF (parsed by hand: no ELF library), sets up Jolt's memory,
  and runs the emulation, producing Jolt's trace rows and, at the gold stage, section 16's segment file.
- **Self-contained and clear** (owner): its own folder and build, no imports from meshsim's classes (the replayer model, the
  universal tables), no native code, no use of the C++ tools. Each part is written to be read: the ELF loader; the memory layout;
  an RV64 interpreter; **Jolt's expansions written out per instruction family as plain code** (following jolt-cpp's
  `isa/virtual.cpp`, not the 128-bit expansion words); the CSRs, traps, LR/SC and ECALL; the inlines; the advice (secp256k1 in
  `BigInteger`).
- **Validation is central** (owner). jolt-cpp is checked against as **data, not code**: its rows (per-window hashes, as WORKER16
  and WORKER17 do), WORKER16's segment files, WORKER17's layout and page tables.
- **Stage 1, a Tracer model:** the loader, the layout, the interpreter and the expansions, with advice taken from the data
  (jolt-cpp's rows, or frame 3); rows identical to jolt-cpp's on the small inputs, then on both workloads at 2²⁹.
  **Stage 1 done** (WORKER18, 2026-11-01): rows identical to jolt-cpp's on both small inputs (every record) and both workloads
  at 2²⁹ (every window hash; EVM Zone's 3,674 advice words all consumed); all 15 inlines match jolt-cpp's emit lists row for
  row, checked on every build. About 16.5 M rows/s on one CPU: a 2²⁹ run in about 22 s (jolt-cpp's `row_dump`: 27-29 s).
- **Stage 2, gold:** it computes everything itself (the advice, the processor state at each cut, the dirty runs, the page counts)
  and writes the segment file. **It earns gold** by writing files byte-identical to WORKER16's tool (W3) on both workloads at 2²⁹
  and on the hard-cut tests; the C++ tool then becomes the cross-check.
  **Stage 2 done: gold earned** (WORKER18, 2026-11-01; `WORKER18/REPORT.md`): the emulator writes section 16's file
  **byte-identical** to WORKER16's tool (ca30b5e) on both workloads at 2²⁹ (13.5 and 15.5 s) and on both hard-cut files, with the
  secp256k1 advice computed in `BigInteger`; `wip/gold/check.sh` reruns all four. Seven of jolt-cpp's example guests also match
  on every record (MULH and MULHSU, the division family, SRA and SRAW, LR.D and SC.D, every AMO, all seven secp operations,
  SHA-256 and the other inlines). Never executed yet: EBREAK, FENCE, KECCAK256 without absorb, `VirtualAssertEQ`,
  LegacyRawEcall mode, the abort paths. **The Java emulator is now our gold;** WORKER16's tool is the cross-check.
- **The gold loader loads only instructions we can prove** (owner, 2026-11-01; a ruling on `WORKER18/UPSTREAM-DIFFS.md` D4):
  - **an instruction Jolt can't prove** in the ELF's load stream (CSRRC and the immediate CSR forms, a CSR outside the six,
    FENCE.I, WFI, SRET and other privileged instructions) **throws an exception at load**, as upstream rejects it at decode
    (`instruction/mod.rs:1230-1323`). jolt-cpp instead decodes these as UNIMP and drops them from the bytecode, shifting later pcs
    (`isa/decoder.cpp:597-640, 1200-1207`; D4); gold follows upstream here, by design;
  - **the exception: the standard UNIMP encodings** (`c.unimp` 0x0000, `unimp` 0xC0001073), which compilers place after code
    that can't return and as padding: **loaded as an unreachable marker** (upstream's `UNIMPL`), **throwing only if executed**;
  - everything else loads and runs as now. For every valid program (both workloads included) nothing changes, so the
      byte-identical acceptance holds.
  - **Gold has one loading mode** (owner, 2026-11-01): no permissive mode. ACT4 is run by **rebuilding its ELFs under a target of
    our own** (its linker script putting code and data in separate segments, one executable `PT_LOAD`; the ACT4 sources
    unmodified, as upstream requires), so each test that then loads strictly is a provable program; a test whose data is
    inherently inside its code is reported as such. Only if some can't be relinked, an ACT4-only runner in `test/` decodes at run
    time like `jolt-emu`, executing through gold's ordinary machine; gold's loader stays single-mode.
- **Stage 3, validation against upstream Jolt** (owner, 2026-11-01; recorded now, added to WORKER18's work at a stopping point):
  upstream's own Rust tracer, pinned (17a9459, the commit section 12 read; HEAD has since been restructured into `crates/`),
  produces rows on the same ELFs and inputs, as data. Its `Cycle` rows need a mapping to jolt-cpp's format first. Where jolt-cpp
  and upstream differ, each difference is recorded and the owner decides which the gold follows (by default upstream: the spec's
  ground truth is upstream Jolt's trace, item 113). This is ST1's work, done through the emulator.
  **Stage 3's result** (WORKER18, 2026-11-01): **upstream at 17a9459 produces the same rows as jolt-cpp** on every run (both small
  inputs and seven example guests record by record; both workloads at 2²⁹ by every window and the run hash), and **gold matches
  upstream everywhere jolt-cpp does**. The inlines are unchanged since f8fc214 (all 15 upstream dumps byte-identical to
  jolt-cpp's). Mapping: upstream's kinds are jolt-cpp's opcodes (NoOp is NOP); four upstream-only virtual kinds never occur. The
  differences are at the edges, recorded in `WORKER18/UPSTREAM-DIFFS.md` for the owner's rulings: the program image (PT_LOAD
  segments against sections), upstream's stack-canary check, jolt-cpp's LegacyRawEcall mode, UNIMP (dropped from jolt-cpp's
  bytecode, rejected upstream), a jump into the middle of an instruction, the misaligned-access checks, the bytecode count's
  convention, and the padding rule (to confirm). Eleven hand-assembled guests cover the paths no workload reached.

## 18. The Tracer's pc to `instr_idx` map (2026-11-01)

Decided (O). The replayer takes the next `instr_idx` of a control transfer from the read log (spec 4.19.7); the Tracer writes
those words, so it needs pc to `instr_idx` for every jump target. `instr_idx` is the row's index in the committed bytecode: it
advances by 1 on every row (each virtual row is its own bytecode entry), so a target's value is its instruction's **base**, the
index of its expansion's first row (1 + the expansion lengths of every instruction below it); a row inside an expansion is the
base plus its offset. Sequential flow needs no lookup (the Tracer knows each expansion's length as it runs it), and a segment's
start comes in frame 2. **Only jumps need the map.**

- **A 64-entry pc to `instr_idx` LRU in the Tracer** (owner): a hit costs nothing.
- **On a miss, the Tracer probes the hash table itself** (owner, 2026-11-01; replacing a lookup by the controller through a
  `TARGET_PC` register and event): a multiply, a shift, one line read and 16 comparators.
- **The table: one hash table of target pc to base `instr_idx`, static per program, with a per-proof supplement inserted into
  it** (owner, 2026-11-01; robust to programs whose runs jump to many targets, where a per-proof table could swell to megabytes
  in the broadcast stream):
  - **the static table, a program file**, built once by the host from the ELF and cached on chip like the initial image, **at most
    half full**: every **direct target** (branch and JAL offsets), every **return address** (the instruction after each call), and
    every **code pointer found in data** (an aligned 64-bit word in the data or read-only sections equal to an instruction start).
    Measured (integrator, both workloads at 2²⁹; the ELFs are stripped, so no symbols): direct and return targets alone, under 70K
    entries (about 1 MB at half full), cover **6,183 of EVM Zone's 6,189** distinct targets and **4,205 of Cyclone's 4,228**; the
    rest are indirect (0.00% and 0.17% of jumps). The data scan isn't measured yet.
  - **the supplement, frame 6:** the run's targets the static table lacks, a list of (pc, base) pairs (here 6 and 23); the host
    knows them exactly, having run the program.
  - **inserted at proof start by the lookup's own rule** (home line, else step by *p*₃), into **private copies of the table pages
    they land in** (section 15's mechanism for written pages), freed at the proof's end; so each proof sees one table, the static
    table stays intact, and **the Tracer has one lookup path and one format**.
  - **The robustness is in software** (owner): a program that jumps to many targets a simple analysis misses makes the supplement
    big, and the fix is a better analysis on the host, with no hardware change.
  - Open: who inserts (C&C 3 at proof start, or the host sending patched lines); the host checks that no inserted key lands more
    than two lines from home.
- **The hash** (owner): the line is the top *k* bits of pc x *p*₁ (multiplicative hashing; *p*₁ odd), so the table has 2*ᵏ* lines;
  the 16 entries of a line are compared at once; on a full line the probe steps by an odd *p*₃ (mod 2*ᵏ*). An entry is 8 bytes:
  the pc as an offset into the code (u32) and the base (u32). The host chooses *k* and *p*₁ so every key sits in its home line
  (measured on both workloads with every instruction start at load 0.26-0.29: the fullest line held 9 of 16, so a smaller table of
  actual targets has even more room).
- **Where the Tracer keeps it: open** (integrator's proposal: a dedicated SRAM next to the Tracer, 128 KB, so a lookup is a few
  cycles and never touches the shared cache or LPDDR; the 64-entry LRU then becomes optional).
- **What it replaces** (all superseded, recorded here for the history): a code map of one (base, mask) pair per 64 B of code,
  walked by decoding forward and summing expansion lengths from a 2,048-entry length table (with an rd = x0 bit, the CSR rules by
  rd and rs1, and inlines and CSRs sent to the controller through `LENGTH_OPCODE` with a 16-entry LRU); and before that, one word
  per 128 B of code with a decode walk of about 30 cycles. The length and CSR findings stay true of jolt-cpp (`isa/virtual.cpp`:
  `num_cycles` 1210-1276, `should_nop_for_rd0` 108-112, CSRRW and CSRRS 1287-1302).
- **Open:**
  - (a) ~~the LRU's size~~ **decided: 64 entries** (owner, 2026-11-01), on the measurement below: even Cyclone's worst segment
    costs under 1% of the budget at 64, so a bigger LRU buys little. **Measured** (integrator, 2026-11-01, gold replaying both workloads at 2²⁹; each segment's caches start
    cold, a segment running on its own ASIC; multiplicative hash with the golden-ratio constant, 2^k lines at most half full):

| | EVM Zone | Cyclone |
|---|---|---|
| instruction starts (table keys) | 616,231 | 545,760 |
| table | 2¹⁷ lines, 16 MB, load 0.29 | 2¹⁷ lines, 16 MB, load 0.26 |
| fullest line (of 16) | 9: every key in its home line | 8: every key in its home line |
| jumps | 4.06 M (1.1% of rows) | 15.9 M (4.3% of rows) |
| LRU misses, 64 / 256 / 1,024 entries | 3.8% / 3.1% / 2.0% | 19.0% / 7.4% / 2.3% |
| worst segment, LRU 64: misses, of which a line's first touch | 8,665, 3,048 | 87,261, 2,467 |

    **So a lookup is always one line read**, and the cost is small: the worst segment (Cyclone, LRU 64) is about 2,500 cold line
    reads from LPDDR at **80 cycles** (owner: an idle LPDDR4X miss, about 50 ns at the DRAM plus the controller, at 1 GHz) and
    85,000 warm ones at about 6, roughly 0.7 M cycles, under 1% of the Tracer's 100 ms budget per segment [estimate]. The warm
    ones assume the segment's hash lines stay in the 512 KB cache: they are 316 KB (Cyclone) and 390 KB (EVM Zone), so they compete
    with the Tracer's data and code; a bigger LRU, or hashing by 32 B code block (pc >> 5), would shrink that. Cold lines (one per distinct target, about 2,500-3,000 per segment) dominate
    and don't depend on the LRU's size; a 256-entry LRU halves Cyclone's warm misses.
  - (b) the hash function and the table's exact layout (software, free to change);
  - (c) building the table on the host (from the bytecode's instruction starts).

## 19. Launching a program (2026-11-01)

Decided (O). **A file is its root's 32-bit address and its length** (section 20), so launching is handing the Tracer two files'
roots and depths:

- **`<program>.pages`, the guest's memory** (section 12): the Tracer is given **its root and depth**, and then just starts, asking
  for (root, depth, line). (This replaces 8 middle-table addresses loaded into Tracer registers, and then a slot.)
- **The jump-target table** (section 18): the Tracer is given the proof's map's root and depth too; the proof's supplement (frame 6)
  goes into private copies of the pages it lands in.
- **No load step:** everything is read in place, through the shared 512 KB cache.
- **Fragmentation: none.** With one 4 KB chunk size, any free page fits any request, so the memory system needs no buddy
  allocator and no compaction (this replaces a 4, 8 and 16 KB scheme with lifetime-separated allocation). A growing file, such
  as a log, takes the next free page wherever it is (section 20's tree); a log is read sequentially, so scattering costs nothing
  (each page is one efficient 4 KB read).
- **Open:**
  - (c) whether a log lives only for its proof or can outlive it (a re-run).

## 20. The file system (2026-11-01)

Decided (O). Run by C&C 2 (the planning notes of 2026-10-27), over section 12's single pool of 4 KB pages; addresses in 128 B units.

- **A file is a tree whose depth follows its length** (owner, 2026-11-01; replaces a chain of index pages, each 1,023 pointers
  plus a next pointer): index pages of **1,024 pointers**, no next pointer:

| depth | structure | maximum size |
|---|---|---|
| 0 | the data page itself | 4 KB |
| 1 | an index page of 1,024 data pages | 4 MB |
| 2 | an index page of 1,024 index pages | 4 GB |
| 3 | three levels | 4 TB |

  - **Growing** past a depth allocates one index page whose entry 0 points to the old root, which becomes the new root: the old
    tree moves down a level untouched (one page, one pointer, the directory entry's location updated).
  - **The depth follows from the length**, so it needs no field. **Random access** is one read per level, at most three.
  - **Holes:** an entry of `0xFFFFFFFF` means no page there, so a sparse file costs only the pages it has.
  - **The page table is such a file** (integrator's observation, owner's direction): a depth-3 file's root entries each cover
    4 GB, a middle table of 1,024 leaf tables of 1,024 data pages, the layout section 12 had as 9 + 10 + 10 bits. So **`.pages` is a sparse depth-3
    file whose contents are the guest's memory**, using the root's first 8 entries (32 GiB); a guest address is a file offset.
    **Its depth follows the run's K like any file's** (owner): Jolt's used range is a low part (I/O and advice, just below
    0x8000_0000) and a high part (program, stack, heap), both inside a 32-bit window from `lowest_address`, so for K up to 2²⁹ cells
    the offset is 32 bits and **`.pages` is a depth-2 file (4 GB)**; both workloads are (EVM Zone 2²³ cells, 64 MiB; Cyclone 2²⁴,
    128 MiB). Only K above 2²⁹ cells, up to section 14's 2³², needs depth 3.
- **The memory system reads files directly** (owner): **a request names the file itself: its root's address (31 bits), its
  depth (0 to 3) and a line** (owner, 2026-11-01; replacing a table of file slots), so the memory system holds no per-file state
  and any client can read any file. It translates through a TLB tagged by (root, page), a miss walking the tree (at most three
  index reads) through the shared cache, then reads the line from the cache. **One translator for everything:** the Tracer's
  guest memory is just a file (its `.pages`), and so is any table or log read by offset. **A hole reads as zero; writing to one
  is UB:** only software allocates. Bulk page transfers (the Updater, the file system's own moves) still bypass the cache, and
  writing a page directly still invalidates its lines. A request's line number is up to 35 bits (a depth-3 file of 4 TB); with the
  root and depth, about 68 bits.
- **How the file system reaches a file** (owner, 2026-11-01): **index pages through the shared cache**, a line at a time, both
  reading and updating entries; **data pages directly**, as whole 4 KB pages into its buffers. **It finds a page through the
  memory system's translator**: it asks which page (root, depth, offset) is, the hardware walker and TLB
  doing the work, so there is **one walker in the chip**; software walks a tree only to change it (fill, copy, free, grow), writing
  the index entry through the cache so the walker sees it, and invalidating any translation of the old entry. (This replaces the
  file system bypassing the cache entirely.)
  **So the file system has fast `read_page` and `write_page` primitives anywhere in it** (owner): (root, depth, offset) through
  the translator to a page, then one direct 4 KB transfer, whatever the file's size or depth. A `write_page` to a hole in a sparse file
  fills the hole first; in a dense file it overwrites within the length, or appends at the end.
- **Copy-on-write files** (owner, 2026-11-01):
  - **`freeze(path)`** sets the file's **read-only flag** in its directory entry: O(1), no walk (the flag, not the entries,
    makes a file unwritable).
  - **`cow_open(source_path, dest_path)`** makes a new file at the destination **sharing everything with the source but its
    root page**, which is copied with its entries' owned bits cleared, so the copy owns nothing yet. **The source must be
    frozen.**
  - **a write** to a file that isn't frozen walks down: an entry with owned bit 1 is written through in place; one with owned bit
    0 has its page copied into a new page (whose own entries keep owned 0, since its children are still shared), the entry
    pointing to the copy with owned 1 (path copying). Only the paths to written pages diverge, once each.
  - **deleting a file** frees exactly what its owned bits reach, stopping at any entry with owned 0 (an ancestor's page). **A
    source must outlive its copies** (proposed, integrator: a count of copies in its directory entry's reserved bytes, a delete
    allowed only at 0); a frozen source can have many.
  - **per-proof arenas** (owner, 2026-11-01): everything made during a proof (section 22: the cycle-0 image, the start and final
    images, the Tracer's copy, the proof's map) dies with the proof, so the proof's pages come from **its own pool, tracked by a
    per-proof bitmap**, and cleanup is one bitmap merge (the planning notes' per-proof bitmaps), no tree walks. **Frame 1's 128
    page counts size the pool** before any data arrives: the updates the start and final images take (every segment's written
    pages), the Tracer's private copies (the ASIC's own segment's), the cycle-0 image's input and advice pages, and **the index
    pages the copies add along their paths, from exact totals the host sends** (owner, 2026-11-01). Neither the trees nor
    per-segment counts give them: writes can be scattered (each write into a fresh 4 MB region needs a new index page), and counts
    don't add (a page written in two segments is one private page in an image that takes both, and an owned path is copied only
    once), so an image's need is the size of the **union** of the writes it takes, which differs on every ASIC. So frame 1 carries
    **a per-ASIC table**: for the ASIC running segment *k*, the exact pages (data and index) of its start image, its final image
    and its Tracer's copy; the arena is that row's sum plus the cycle-0 image's input and advice pages. **The host computes them**
    from each segment's set of written pages: prefix unions for the start images, suffix unions for the final images, each
    union's index pages being its distinct 4 MB and 4 GB regions. Frame 1 also keeps **the per-segment table** of pages written. Only the permanent files (the program image, the program's map) live outside it.
  - **Uses:** section 22.
- **File-system calls** (owner) for what hardware never does, served by C&C 2: **fill a hole** (allocate a page at an offset,
  growing the tree or adding index pages as needed), used for example by the Updater building the memory image for the next
  trace; and, by the same token, **copy a page** (replacing a shared page with a private copy, the Updater's streaming
  copy-on-write) and **free a page** (making a hole). Each invalidates any translation of that page cached for the file.
- **A directory holds each file's name and length** (owner), with its first pointer (the data page, or the first index page). The
  length also says which form a file has (at most 4 KB: a single page).
- **A directory entry is a fixed 128 bytes** (owner), exactly one 128 B unit, so it can be addressed and read on its own, and a 4 KB
  directory page holds 32 entries. Least significant byte first:

| bytes | field |
|---|---|
| 0-95 | **the name**, at most 95 bytes, zero-padded, so always followed by at least one zero byte (owner) |
| 96-103 | **the file's length** in bytes, u64 |
| 104-107 | **its location**, u32: the data page, or the first index page (128 B units) |
| 108-111 | **its type**, u32 |
| 112-127 | reserved, 0 (the 32 bytes of important information are bytes 96-127) |

- **Open:**
- **Sparse and dense files** (owner, 2026-11-01), the same tree, different rules:
  - **dense:** no holes; it grows only by **appending** (pages allocated in order) and is read anywhere within its length. Logs,
    segment files, directories, the static jump-target table. A hole in a dense file means it is corrupt.
  - **sparse:** holes allowed and read as zero; it can **grow** (its length set beyond its pages, without allocating) and take
    **writes of arbitrary pages** (filling a hole at any offset), copies and frees of single pages. The guest's memory (`.pages`)
    and the images the Updater builds.
  - The file-system calls follow: append for both; fill a hole, copy a page and free a page for sparse files only.
  - Proposed (integrator, to confirm): a flag in the type word (bit 31 set for sparse), so any kind can be either.
- **The type is the kind of file** (owner), **0 marking a free entry**, and **a directory is a type** (owner): a directory is a file
  of 128-byte entries, single-page or chained like any file, so directories nest. Proposed values (integrator, to confirm):

| type | kind |
|---|---|
| 0 | free entry |
| 1 | directory |
| 2 | data (any other file) |
| 3 | page table (`.pages`, section 19) |
| 4 | (unassigned; was the code map) |
| 5 | log |
| 6 | segment file (section 16) |

- **Open:**
- **At most 10 directory levels** (owner), so a full path fits **1,024 bytes of the file system's memory**: 10 names of at most
  95 bytes, plus 10 separators, is at most 960 bytes.
- **The path isn't kept once a file is open** (owner): opening walks the path through the directories in a temporary 1,024-byte
  buffer, freed once the entry is found. **An open file is a 64-byte record plus a 4 KB page buffer** in the file system's memory:
  - **the record** (proposed layout, integrator, to confirm): the **file descriptor number** (u32, owner; naturally the record's
    index in the table), **the directory entry's address** (u32: an entry is one 128 B unit, so one address names it), location
    (u32, the tree's root), type (u32), length (u64), current position (u64), the current position's index page (u32), flags (the buffer's page and whether it is
    dirty), the rest reserved. Updating the file (its length after an append) writes back to its entry through the address.
  - **the buffer: 4 KB of an open file cached in memory** (owner): the data page at the current position, so reads and writes
    within a page touch no memory, and an append fills the page before it is written out.
  - **The buffers are a pool managed by LRU** (owner): a file that needs a buffer takes a free one, or **flushes the least recently
    used** (writing it back if dirty) and takes that. So the number of open files (64-byte records) and the number of buffers are
    separate choices; a file whose buffer was taken simply reloads its page when next used.
  - **Sizes: 512 open files; 128 buffers for files being read and written; 32 more buffers just for directory pages** (owner),
    each pool managed by LRU: the records take 32 KB (512 x 64 bytes), the file buffers 512 KB, the directory buffers 128 KB, so
    about 672 KB of the file system's memory. **The file system reads and writes whole 4 KB pages directly**, bypassing the shared
    cache (section 12), so these buffers are its only cache.
- **A superblock at address 0** (owner), **cached by the file system** (C&C 2), which reads it at boot: it identifies the file
  system and says where the root directory is. Address 0 is otherwise an ordinary address (`0xFFFFFFFF` is "none"), but a zeroed
  pointer is a valid pointer to it, so the superblock's magic catches a stray write or a foreign memory at the next check.
  Proposed contents (integrator, to confirm), least significant byte first, in the first 128 B of page 0 (the rest of the page
  reserved): a magic (8 bytes), a version (u32), the root directory's location (u32, 128 B units), later the free-page map's
  location and the memory's size; the rest reserved, 0.
- **Open:**
  - (a) the superblock's exact contents; the type values above;
  - (b) ~~whether `.pages` is a file~~ decided: a sparse depth-3 file (above). Formerly: whether `.pages`, the page table's tree, is stored as a file's pages
    or is a structure outside the file system, reached only by the Tracer's pointers.

## 21. The Tracer's local caches (2026-11-01)

Decided (O). In front of section 12's shared 512 KB cache, which acts as an L2 for the Tracer; a local miss is a (root, depth,
line) request to the memory system, which translates it (section 20). **The 512 KB cache sits close to the memory** (owner,
2026-11-01): with the translator, its TLB and walker, and the file system's index accesses all going through it, it is the
memory system's centre, not the Tracer's. The Tracer's local 16 KB caches absorb the longer trip over the 1,024-bit fill bus.
(This reverses an earlier placement next to the Tracer, made while the file system bypassed the cache.)

- **16 KB of data and 16 KB of instructions, local to the Tracer: 128 lines of 128 B each** (owner).
- **Each line is tagged by its line in the guest's memory file only** (2026-11-01; replacing two tags, guest address and
  memory-system address). With translation in the memory system the Tracer has **no TLB**, and no use for a memory-system tag: a
  dirty victim is written back by its line (with the file's root and depth), translated again by the memory system (usually a TLB hit there), and nothing
  else writes the Tracer's pages during a segment, so nothing needs to invalidate a line by its memory address; the caches are
  invalidated at the segment's boundary.
- **Access to them is single-cycle** (owner).
- **The Tracer's whole interface to the memory system: reads and writes of one 128 B line of a file** (owner, 2026-11-01), as
  (root, depth, line), each request knowing exactly which line it wants. There are exactly three reasons (owner): **a program miss** and
  **a data miss** (the guest's memory file, where code and data both live; a data miss may also write back its dirty victim),
  and **a mapping miss** (a pc to `instr_idx` LRU miss: one line of the jump-target table, its own file, the line chosen by the
  hash). Translation is the memory system's; the Tracer's streams (advice in, logs out) move as whole pages and never use the
  cache.
- **A miss fills from the shared cache over a 128 B (1,024-bit) bus** (owner): a line per transfer.
- **Coherence is nearly free for the Tracer** (integrator's note): during a segment nothing else writes what it sees (its written
  pages are private copies, its shared pages read-only, its tables unchanged), so the local caches need no protocol. Dirty data
  lines are written back to the shared cache on eviction, and both caches are invalidated at a segment's boundary; dirty lines left
  at a segment's end can be discarded, the Tracer's copy being throwaway (the next images come from the updates). Aliasing is
  harmless: two guest addresses can share a memory-system page only if it is read-only.
- **Open:**
  - (a) ~~the associativity~~ **decided: fully associative** (owner): 128 lines each, one Tracer, so the comparators' power is
    acceptable;
  - (b) ~~a small page TLB~~ **decided: TLBs, now the memory system's** (owner, 2026-11-01): on a local miss the Tracer sends
    (root, depth, line), and the memory system's TLB, tagged by (root, page), (a page to its bottom index entry: the page's address and its
    owned bit; proposed 64 entries, fully associative) and a cache of upper index entries (each covering 4 MB; 16 to 64) mean a
    miss usually costs one index read, through the shared cache; a 4 KB page is 32 lines, so a missing line's neighbours share its
    translation. (Restated from the Tracer's own two TLBs under 8 top registers; sizes to confirm by measurement, (d).)
  - (c) ~~an instruction line bringing its code map words~~ moot: the code map is gone (section 18).
  - (d) sizes to confirm by measurement: the gold emulator can replay both 2²⁹ workloads' address streams through these caches,
    the TLBs and the shared one and report hit rates (WORKER18, after ACT4).

## 22. The images and maps as copy-on-write files (2026-11-01)

Decided (O), with section 20's `freeze` and `cow_open`. Each file shares every unchanged page with the one it came from and costs
only the pages its changes touch; a source outlives its copies.

**The jump-target map** (section 18):

| file | made by | lifetime |
|---|---|---|
| `<program>.map`, the original hash table | built by the host from the ELF, then frozen | permanent |
| the proof's map | `cow_open` of it, then frame 6's supplement inserted (path-copying only the pages it lands in) | one proof |

**The memory images**, on the ASIC running segment *k*:

| file | made by | lifetime |
|---|---|---|
| the program image | the ELF's memory, then frozen | permanent |
| **the cycle-0 image** | `cow_open` of the program image, the proof's inputs and advice written in, then **frozen** (owner: one more step) | one proof |
| **the start image** of segment *k* | `cow_open` of the cycle-0 image, segments 0 to *k* - 1's updates written (the Updater), then frozen; kept intact for the prover's RAM checking | one proof |
| **the Tracer's copy** | `cow_open` of the start image, the pages the segment writes pre-copied (frame 1's counts), given to the Tracer | one segment, thrown away |
| **the final image** | `cow_open` of the start image, segments *k* to 127's updates written, then frozen | one proof |

The "two copies at the segment's start" (section 15) are the frozen start image and the Tracer's copy of it. **Cleanup:** all of
the proof's files come from its arena (section 20), so the proof's end frees them in one bitmap merge; the program image and
the program's map, permanent, stay.

**When the updates are applied** (owner, 2026-11-01): **the whole stream, all 128 segments, is received and applied before tracing
starts**, the images being **built as the data arrives** (owner), never buffered first: the ASIC running segment *k* writes segments 0 to *k* - 1's updates into its start image (then
freezes it and `cow_open`s the final image) and segments *k* to 127's into its final image (then freezes it), keeping its own
segment's frames 2 and 3. When the stream ends, every ASIC hands its Tracer its copy's root and depth and **all 128 Tracers start
together.** (So the stream's transfer time is part of a proof's latency, unless the next proof's stream arrives during the
current proof.)

## 23. The Tracer's startup state (2026-11-01)

Decided (O). **Everything the Tracer needs to start a segment, and nothing else:**

| part | from |
|---|---|
| pc, `instr_idx`, the expansion offset, registers 0-127 | frame 2 (section 16), the processor state at the segment's first row |
| **the guest's memory file:** its root's address (31 bits) and its depth (0 to 3) | C&C 3: the Tracer's copy (section 22) |
| **the jump-target map file:** its root's address and its depth | C&C 3: the proof's map (section 22) |

**Loading it invalidates the Tracer's local caches**, the 16 KB instruction and data caches and the pc to `instr_idx` LRU,
**without flushing dirty lines** (owner): the previous segment's copy of memory is throwaway (section 21), so its dirty data is
simply dropped. The advice log (frame 3) is fed separately, through its input queue (section 13).

## 24. The Tracer's peripheral window (2026-11-01)

Decided (O). The Tracer is C&C 3's peripheral, built like the others (spec 4.17):

- **Starting:** C&C 3 **writes the startup state** (section 23) into the window, then **pulses start**.
- **A sender**, as on the other peripherals, for the Tracer's output.
- **Wake flags** (events), each waking the controller:
  - **expand:** an instruction needs its expansion from the controller (`EXPANSION_OPCODE` holds its word, as in the replayer's
    controller-fed path, spec 4.19.9);
  - **segment done:** the segment's 2²² rows are traced;
  - **code log**, **read log** and **write log:** each stream has something ready (sections 2 and 6).
- **Open:** the register offsets; whether the advice input queue (section 13) needs a wake flag of its own (room for more advice)
  or is fed by credits alone; statistics registers.

---

## v1.3 work list

Work agreed for v1.3, each with the decisions it carries out. Studies (below) are separate.

| # | task | from | status |
|---|---|---|---|
| W1 | ~~Bring the replayer and the Tracer up to date with jolt-cpp's latest~~ | 9 | **moved to v1.25** (owner, 2026-10-31): `DECISIONS-v1.25.md` |
| W2 | **jolt-cpp's JIT emits the advice streams**: 128 per 2²⁹ run, one per 2²² rows, words in row order (`shape(op).advice_words` per secp op), a crossing sequence's pending words at the head of the next; and **how jolt-cpp handles registers 40-47** at a chunk's start (13 (c)) | 13 | **deferred** (owner, 2026-11-01): the fast JIT version isn't needed any time soon with the Java emulator (W4); the slow tool (W3) produces the advice logs and the full state, registers 40-47 included. The scoping is in `WORKER16/W3-REPORT.md` for later |
| W3 | **The segment file tool**: runs the trace on the host and writes section 16's file (page counts, then per segment the processor state, advice log and updates, raw or LRU-coded); slow first | 16 | **done** (WORKER16, jolt-asic-planning `worker16-w3` ca30b5e; `WORKER16/W3-REPORT.md`): both workloads at 2²⁹ and hard-cut tests pass every check; whole file 1.75 MB (EVM Zone), 10.33 MB (Cyclone) |
| W4 | **The Java emulator** (`wip/gold/`): self-contained; stage 1 a Tracer model matching jolt-cpp's rows, stage 2 gold, writing section 16's file | 17 | WORKER18: **stages 1 and 2 done, gold earned**; stage 3 next |
| W5 | **Upstream's tests through the gold emulator** (owner, 2026-11-01): (a) the **ACT4 architectural tests** (RISC-V International's `riscv-arch-test`, `tests/arch-tests/` upstream): self-checking ELFs for every RV64IMAC instruction, unprivileged, expected signatures baked in at build time by the Sail reference model; upstream skips the CSR tests its tracer doesn't fully support (`tests/arch-tests/skip.txt`). Building them needs riscv-none-elf-gcc 15+ and Sail, once; the ELFs then run as data through gold, jolt-cpp and upstream. (b) **The inline tests** (`jolt-inlines/*/src/tests.rs`): each inline's sequence run on the emulator against an independent library (arkworks for secp256k1, and so on); port their vectors to gold. Check both exist at the pin 17a9459 (seen at HEAD c670357) | 17 | to do (WORKER18, after stage 3's write-up) |

---

## Candidates (not decided)

Options recorded for later, each with the evidence that would justify it.

| # | candidate | why | cost |
|---|---|---|---|
| C1 | **Rejected** (owner, 2026-10-31; stated in spec 4.7): not worth fixing at a cost per step; it would be if the cost were per query. **A delay per destination:** decrement a counter **2 cycles** after acceptance for writes to controller memory and peripherals (their landing time is fixed, `cm_latency`, `rp_latency`), and K = 8 only for big SRAM | WORKER4's measurement: a read-port search step costs about **6 cycles more** than in v1.1 (35.9 + 37.42 per active search against 29.8 + 37.43), because v1.2 decrements at K = 8 where v1.1 counted controller-memory writes at landing. The extra is in the **fixed** term: about 6 cycles **per search step**, shared by all the searches active in it, not per probe (the per-search slope is unchanged). So roughly 6 × the steps per split, about a hundred cycles per split and a few hundred per sort: **negligible today**. Streaming work doesn't notice | two delay taps; a counter may then take two decrements in one cycle (one from each tap), a small adder change. Adopt if the sort's measurements show the split slowing |
| C2 | **Signed counters** (v1.2 section 36): an arrival before arming just goes negative, so the order of arming and arrival stops mattering, and strays are caught as done early | removes the GO-to-arming window | a signed counter; the immediate stray report becomes meshsim's only |
| C4 | **Moot (section 13).** **An addition chain for the modular inverse** (11): libsecp256k1's for DIVQ, a hand-built one for DIVR | halves the inverse's untraced rows: EVM Zone's first proof about 145 ms → about 84 ms against a 100 ms budget | the segment format already supports it (WORKER9); see T7 |
| C3 | **Trim the 256-bit word's storage** to the bits in use, with the links staying 256 wide (v1.2 section 2) | power: constant-zero flops are still clocked | revisit when the design has settled |

---

## Studies for other sessions

| # | study | why |
|---|---|---|
| ST1 | **Compare the two Jolt tracers' traces:** jolt-cpp (https://github.com/snarkify/jolt-cpp, ours) and the Rust Jolt tracer (https://github.com/a16z/jolt). They are written differently and may work slightly differently but are believed to produce the same traces. Run the same guest programs through both and find every place the traces mismatch (rows, `instr_idx`, advice, memory values, padding) | **answered by section 17's stage 3** (WORKER18): the same rows on every run; edge differences in `WORKER18/UPSTREAM-DIFFS.md`, for the owner |

---

## Pending questions

| # | question | from | status |
|---|---|---|---|
| T1 | What the 8 KB and 4 KB expansion memories hold | 1 | **answered: section 7** (same format and builder as the replayer's ROM and decode table; different contents) |
| T4 | The Tracer's template coverage | 7 | **answered: section 7** (a list in the ROM; the rest fed by C&C 3, replayer-style) |
| T2 | The RV64 initial state: what it contains and where it comes from | 1 | **mostly answered: section 12** (part 0 as the replayer's); to verify it's complete |
| T3 | The page table: its entry format and semantics, and how C&C 3 maintains it | 1 | open (its size and place: section 12) |
| T8 | ~~Does 512 MiB cover both workloads?~~ | 12 | **withdrawn**: the table covers 4 GiB, sparse (section 12) |
| T6 | `VirtualPext`/`VirtualPextSigned` in hardware | 9 | **answered: section 9** (any mask must work; the method is the implementation's) |
| T7 | The modular inverse within the Tracer's 100 ms budget | 11 | **answered: section 13** (host-supplied advice; no inverse on the Tracer) |
| T5 | The inlines' advice | 8 | **answered: section 13** (the host supplies all advice in the read log; was section 10) |
