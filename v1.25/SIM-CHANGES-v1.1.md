# meshsim v1.0 → v1.1: the simulator session

This session brought meshsim, its generated files (`sw/mesh.h`, the generated blocks of `sw/REGISTERS.md`) and meshsim's
own tile test programs to the v1.1 specification. The authority is `spec/machine-spec.md` v1.1, with proposed items
25–32 accepted. The replayer is the stub of spec 4.17's "Until the replayer is specified" paragraph: its "Design
direction" is not implemented. `spec/`, `sort/`, `kit/`, `verify-v1.0/`, `diagrams/` and the PDFs are untouched. The
recipes (`meshsim/sw/tests/rcp_*`, `sw/RECIPES.md`), the sort, the split and the planner are the software session's
(section e).

Toolchain used: OpenJDK 21; `gcc-riscv64-unknown-elf` 13.2 and picolibc 1.8.6 from Ubuntu 24.04. Before any change, every
suite was run on v1.0 with images built by this toolchain: all matched the shipped v1.0 logs exactly.

## a. Classes changed

**The link word and the network**

| class | change |
|---|---|
| `LinkWord` | The 168-bit word: a 2-bit size in header [39:38] (00 32, 01 64, 10 128; 11 undefined), a 128-bit payload carried as the bus's first and third longs, `M_REPLAY` = 8, `badPacket()` (mode 0 or 9–15, or mode 4 at 128). The read-port tag moved from the third bus word to header bit 63, which is simulator-only. |
| `Router` | Comment only: the three longs it already carried are now payload [63:0], header and payload [127:64]. |
| `InjectionPath` | Invariant 5 at the sender uses `badPacket()` and also flags size 11. The read-port tag is read from the header. |
| `Endpoint` | `send()` takes size and payload [127:64], clears bits above the size, and flags bad packets and size 11. |
| `CnC` | Send register with size and 128-bit payload; receive-FIFO entries keep payload [127:64] and the size (item 29). Mode-5 boot loads are 128-bit packets, padded with zero words to whole packets (spec 6.2). Commands go out as 32-bit mode-6 packets. |

**The receive side**

| class | change |
|---|---|
| `TrafficDirector` | Bad packets are dropped uncounted and set condition 1. Mode 8 goes to the replayer stub. Size 11 is flagged and dropped (see d). Mode 5 carries 1, 2 or 4 words, and invariant 11 is checked per word. |
| `ReplayerStub` (new) | Mode 8's port (4.17): drops the packet, uncounted, no condition, no wake effect, and records it (a count and the first 1,000 packets). Status bits read 0, conditions read 0, the wake is 0, and HALT resets it (it has no state). |
| `WriteEngine` | Modes 2 and 3 write at the packet's size with `PTR` += 1, 2 or 4. Mode 4 writes the 64-bit record from a 32- or 64-bit packet and steps 2. Any write with o + n > 4 spans two rows: part 0, then part 1 the next cycle ahead of other writes. |
| `BigSRAM` | Comments: writes of 1, 2 or 4 chunks; part 1 of a spanning write arrives already shifted. |
| `ControllerMemories` | Port B writes 1, 2 or 4 consecutive words, each decoded by its own low 12 bits, through the banks. |
| `BankedMemory` (new) | One controller memory as four 512 × 32 banks interleaved by word, each an `SRAMMacro`. Port A uses one bank per access; port B writes one word per bank per cycle, so the banking is checked rather than assumed. |
| `GatherEngine` | The request holds the 20-bit ordinal from payload [51:32] (it read [63:32] before; the result is the same mod 2^20). |

**The send side and the tables**

| class | change |
|---|---|
| `SRAMReadQueue` | One packet per return, stamped with the request's size. 128-bit reads no longer return as two packets. Size 11 is a zero-width entry: no read, payload 0. |
| `AddressGenerator` | Every visit takes one cycle; the group period is *t*. Group streams may mix read sizes (item 27): the 2*t* period and the one-size-per-group report are gone. Element *e* is read at source + *e* × (1 << size). Modes 5 and 8 step the destination word by 1, 2 or 4, wrapping mod 2^20. On every entry taken it checks reserved modes (0, 6, 7, 9–15), size 11, and mode 4 at 128. |
| `TableStore` | `STREAM` is three 256 × 32 macros read in parallel at one index. The return carries src \| dest << 32 and count. |
| `Fields` | Encoders `streamSrc`, `streamDest(word, d)` and `streamCount(count, mode, size, gap)`, plus decoders for the two-long entry. `SIZE_*` = 0/1/2 (`SIZE_UNDEF` 3). Constants for `TILE_PULSE` [2] and [3]. |
| `Tile` | `setStream(i, src, dest, count)`, the replayer stub's wiring and its HALT reset. |

**Control and the controller**

| class | change |
|---|---|
| `Conditions` | Renumbered by unit (8.3). `BAD_MODE` is renamed `BAD_MODE_OR_SIZE`. Bits [13:11] are reserved for the replayer. Each condition's kind (P or H) is now listed here. |
| `Registers` | The 17-register map at 0x800–0x840 (8.1), with offsets 0x844–0xFFC unmapped. Field layouts for `TILE_PULSE` (8.6), `TILE_STATUS` (8.2) and `STREAM_SRC`/`_DEST`/`_COUNT`. The window base addresses (8.9). It generates `mesh.h` 1.1. |
| `TileControl` | `TILE_PULSE` with [2] and [3] flagged; `ROUTER_SET_W1`–`W3`; the three send registers, with the size taken from the register stored to, words beyond the size sent as 0, and `W1`–`W3` cleared at every send. addr steps +1/+2/+4 for modes 5 and 8 and +1 for modes 6 and 7. Wake = GO \| local latch \| replayer wake, the last 0 and blocked by the done gate (item 30). The replayer's status and condition bits are merged in as 0. |
| `Controller` | Send stores of 32, 64 and 128 bits, with the stall-and-retry path for all three. The table window decodes `STREAM_SRC` 0x2800, `STREAM_DEST` 0x2C00, `STREAM_COUNT` 0x3000 and `GROUP` 0x3400; 0x3600–0x3FFF is a bus error. |
| `Chip` | Counts the address generator's mode-4-at-128 reports among its violations. |
| `Config` | Comments only: C&C's receive-FIFO width; write combining for 32- and 64-bit writes. |
| `RegDoc` | Every v1.1 block: `TILE_PULSE`, `ROUTER_SET_W1`–`W3`, the three send registers (named together), the three `STREAM` arrays, the replayer's reserved bits in `TILE_STATUS` and `TILE_CONDITIONS`, the modes table (8 and 9–15), the 168-bit link word, the controller-memory producers and the window. |

`WordBuses`, `Bus`, `Buses`, `PhaseLog`, `Hops`, `Drain` and `SRAMMacro` are unchanged. Every bus already carried three
longs, and a 128-bit value is now simply one packet in the accounting.

**Tile programs and scripts**
- `meshsim/sw/tests/`: `a2a`, `blockcopy`, `cmd`, `cond`, `faults`, `gather`, `inv`, `rdport`, `regs`, `search` and
  `wfi` are ported to `mesh.h` 1.1. `loader.S` needed no change: it reads only `TILE_STATUS`.
- `regs.c` tests the v1.1 send port at all three sizes.
- `cond.c`'s case numbers are its conditions' v1.1 bit numbers. It no longer includes `rcp.h` and carries verbatim
  copies of `spin()`, `delay4()` and the REPORT recipe.
- `blockcopy.c` copies at 128 bits, as spec 6.5 says.
- `sw/build.sh`: with `RECIPES_PENDING=1`, a recipe build failure is reported PENDING instead of failing the build.
- `run_tests.sh`: adds HooksTest; reports the recipes and the sort as PENDING.
- `meshsim/README.md`: v1.1.

**Generated files**
- `sw/mesh.h` and `meshsim/sw/mesh.h` are `mesh.h` 1.1 (`MESH_H_VERSION` 0x0101), with the agreed names:
  - the arrays `STREAM_SRC`, `STREAM_DEST`, `STREAM_COUNT`;
  - the fields `STREAM_SRC_ADDR`, `STREAM_DEST_WORD`, `STREAM_DEST_D`, `STREAM_COUNT_N`, `_MODE`, `_SIZE`, `_GAP`;
  - `set_stream(i, src, dest, count)`;
  - `TILE_COND_BAD_MODE_OR_SIZE`;
  - no macro for `TILE_PULSE` [2].
- I added, as my own choices, the composers `STREAM_DEST_ENTRY(word, d)` and `STREAM_COUNT_ENTRY(count, mode, size, gap)`,
  and the helpers `send32(w0)`, `send64(w1, w0)` and `send128(w3, w2, w1, w0)`.
- `sw/REGISTERS.md`: every generated block is regenerated, and §0's "v1.1 status of the generated blocks" paragraph is
  removed.

## b. Suites

Logs are in `meshsim/results/`; the v1.0 logs are replaced. Every suite here passes with 0 failures.

**Changed and passed**

| suite | change (pass count) |
|---|---|
| NetTest (and its shuffled runs) | The 168-bit header round trip. The director test is rewritten: bad packets including mode 4 at 128; mode 8 to the stub; mode 5 at 32, 64 and 128, including a 128-bit packet straddling 0x7FF/0x800; per-word invariant 11; size 11. (30,524) |
| MemTest (and its shuffled runs) | One packet per 128-bit read; each packet stamped with its size. The token-window case changed: v1.0 filled the window with 128-bit reads that returned as two packets, which no longer exists, so it now fills it with an array latency of 80. (163) |
| EngineTest (and its shuffled run, and the full run) | 128-bit and 32-bit local copies; 1K-word block copies at 128, 64 and 32 bits. (356 quick, 383 full) |
| AgTest (and its shuffled run) | The group-read-size test is replaced by a mixed-size group (item 27). Computed fields for modes 5 and 8 at every size, in stream and group runs, including the 20-bit wrap. (222) |
| AgBench | One packet per element; re-measured (section c). |
| ReadPortGatherTest (and its shuffled run, and the full run) | Reserved streams add modes 9 and 15 and mode 4 at 128; size 11 is undefined. The C parts run with the rebuilt images. (368 quick, 373 full) |
| CtrlTest (and its shuffled run, and the full run) | Field table, 17 registers, `TILE_PULSE` [3]. `regs.c` checks for the v1.1 send port. (6,704 quick, 6,713 full) |
| CondTest (and its shuffled run) | The v1.1 bit numbers; cases renumbered. (390) |
| RegDocTest | Updated for v1.1. Its `split_regs.h` check is reported PENDING: 8 of that header's 9 registers are at v1.0 offsets. (197) |

**New**
- **HooksTest, with a shuffled run (95 checks).** The mode-8 stub at every size, from a tile, from loopback and from C&C:
  dropped, recorded, uncounted, no condition, no wake. The local wake still fires only on a counted packet.
  `TILE_STATUS` [4:3] and `TILE_CONDITIONS` [13:11] read 0. `TILE_PULSE` [2] and [3] are flagged and do nothing. The
  register offsets 0x844–0xFFC and the window's 0x3600–0x3FFF are bus errors, and the window decodes as in 8.9. The send
  port: sizes, `W1`–`W3` clearing, and addr steps for modes 5, 7, 8 and 2.

**Passed with no expectation changes:** BusTest, RiscvTest (prebuilt riscv-tests, no `mesh.h`), AccountingTest (quick and
full), SearchBench, Sweep quick.

**Pending for the software session**

| suite | why it can't run yet |
|---|---|
| RecipeTest | The recipes (`rcp_*.c`, `rcp.h`) must be rebuilt against `mesh.h` 1.1 together with `sw/RECIPES.md`, whose snippets RecipeTest compares. |
| SortTest p1, sort, sort-shuf, sort-bt | The sort program, split and planner (`sort/`, mirrored in `meshsim/sw/ext/`) don't build against `mesh.h` 1.1. |
| SortSummary, SortCompare, PhaseNeutral, Sweep full's sort rows | They need the sort images. |
| RegDocTest's `split_regs.h` check | `sw/ext/split-bisect/split_regs.h` still has v1.0's offsets. |

## c. Re-measured numbers (old → new)

**The measurements the spec marks for re-measurement**
- **Spec 3.2, fan-in under 0/+1 skew (NetTest skewtable 40):** identical to v1.0, row for row. The safe W at
  63/48/32/24/16/12/8/6/4/3/2/1 senders is 0/1/1/2/2/3/5/7/15/15/30/≥64. The table counts packets and link timing doesn't
  depend on the word's width, so v1.0's numbers stand for v1.1.
- **Spec 4.3, read-port tolerance (ReadPortGatherTest full):** unchanged. At a full-rate mode-5 stream the largest R with
  no overflow is 14 (first overflow at 15); at half rate it is 31 (first at 32).
- **Spec 4.2, the nine-group comparison:** cannot be re-measured here. It comes from simulator v3.10's
  `AllToAllTest measure`, which is not in this tree. Spec 4.2's TH/BT peaks need the sort and are pending.

**AgBench, 16 groups, t = 4, R = 1, 128-bit, no stalls**

| L | cycles (v1.0 → v1.1) | smallest `ag_lookahead` with no loss (v1.0 → v1.1; 2L + 2) |
|---|---|---|
| 1 | 134 → 71 | 2 → 4 (4) |
| 2 | 138 → 75 | 3 → 6 (6) |
| 3 | 142 → 79 | 4 → 8 (8) |
| 4 | 146 → 83 | 5 → 10 (10) |

A 128-bit group now runs at period *t*, exactly like a 64-bit one, so it needs the full 2L + 2. v1.0's two-cycle visits
hid the read-ahead. With stalls (the same shape): v1.0 177/197/190/202 cycles → v1.1 119/124/119/125. Every other AgBench
row is unchanged.

**Other measurements that moved**

| measurement | v1.0 | v1.1 | why |
|---|---|---|---|
| EngineTest 128-bit local copy | 4,096 packets, 4,114 cycles | 2,048 packets, 2,066 cycles | one packet per element |
| 1K-word block copy into controller memory (EngineTest) | 512 × 64-bit packets, 526 cycles | 256 × 128-bit, 270 cycles | 128-bit reads (64-bit: 526, 32-bit: 1,038) |
| 1K-word block copy from C (CtrlTest) | 527 cycles | 271 cycles | 128-bit reads |
| CtrlTest boot | 31 mode-5 packets; all READY at cycle 71; ebreak by 150 | 16 128-bit packets; READY at 56; ebreak by 135 | 128-bit boot loads (6.2) |
| CtrlTest commands | GO at 265, 308, … | GO 27 cycles earlier each | follows the earlier boot |
| MemTest, 128-bit reads to one area | 4,000 cycles for 4,000 packets | 2,000 cycles for 2,000 packets | one packet per element |
| MemTest token window | 177 cycles of issue wait (two-packet returns) | 76 cycles (array latency 80) | a different case |
| AccountingTest configure phase (quick and full) | 3,059 cycles | 3,188 cycles | `a2a.c` stores three `STREAM` words per stream instead of two |
| AccountingTest a2a phase | 3,132 cycles (full: 71,190) | the same | — |
| gather from C (quick; full) | 12,331; 196,649 | 12,332; 196,650 | `gather.c`'s third `STREAM` store |
| Sweep quick | — | the gather column +1, otherwise identical; a2a 5,690, searches 29,639, round trip 41 | as above |
| NetTest (quick), the all-to-all data sets (EngineTest full, CtrlTest full), SearchBench, the read port under an all-to-all | — | unchanged | — |

## d. Spec issues found, and resolutions

1. **Size 11 is undefined (3.1), so meshsim had to pick a behaviour.** It flags a size-11 packet at its sender (invariant
   5's last sentence) and, for a `STREAM` entry, at the address generator, which makes no memory read and sends payload 0
   stamped 11. The receiving director flags it again and drops it, uncounted, with no condition, since the hardware has
   no check. This is a simulator choice, not a hardware claim; nothing in the spec needs to change.
2. **Spec 3.1 wording: C&C can also produce size 11.** 3.1 says the tile's senders can't make size 11 and that only a
   `STREAM` entry with read size 11 can. But C&C's send register (6.6) sets "any dst, mode, address and size", so it can
   produce one too. meshsim flags it from either source (`Endpoint`, `InjectionPath`). Proposed wording: "Only a
   `STREAM` entry with read size 11 (4.2), or C&C's send register (6.6), can produce one; the tile's send port has no
   size-11 register." The owner will fold this into the spec; `spec/` was not edited.
3. **CHANGES-v1.1 (c) lists CondTest among the Java-driven suites, but most of it runs the `cond` tile image.** Resolved
   by rebuilding meshsim's test programs (step 7). `cond.c` included the recipe header `rcp.h` for three helpers; it now
   carries copies, so CondTest doesn't wait on the recipes.
4. **RiscvTest needs no rebuild.** It runs the prebuilt riscv-tests binaries, which don't use `mesh.h`, so it was run
   rather than listed as pending.
5. **Spec 6.2's padding writes zero words past the end of an image.** A C&C load of a word count that isn't a multiple of
   four writes up to three zero words after it. This is harmless for the loader and the images here. A note in 6.2 that
   anything placed right after an image may be overwritten would make it explicit.
6. **Invariant 14 applies to mode 8 (interpretation).** The read-port overlap rule covers every outside packet except
   C&C's mode 6, so a mode-8 packet from outside counts while read-port results are outstanding. meshsim applies it that
   way. The spec reads unambiguously, so this is a note, not an issue.

## e. Handoff for the software session

**Rebuild against `mesh.h` 1.1**
- `sort/` and its mirror `meshsim/sw/ext/sort/`: the sort program and its overlays.
- The split: `split-bisect`, including `split_regs.h`, whose eight v1.0 offsets RegDocTest lists.
- The planner: `skew-planner`, including `skew.h`'s `STREAM` packing.
- The recipes: `meshsim/sw/tests/rcp_*.c`, `rcp.h`, `sw/RECIPES.md`.
- `sw/CONVENTIONS.md`: section 8's block copy is still v1.0's 64-bit form.
- `kit/`: its images and hashes.
- After changing `sort/`, re-sync `meshsim/sw/ext` (`tools/check_layout.sh`).
- `cond.c` no longer includes `rcp.h`; rejoin it if the recipe header is restored.

**Changes that silently alter behaviour**

| change | what to check |
|---|---|
| Size codes are 0 = 32, 1 = 64, 2 = 128 (v1.0: 1/2/3, 0 reserved). | Any hand-packed `STREAM` word or `8 << size` arithmetic is now wrong without a compile error. |
| `send32()` sends a true 32-bit packet. In v1.0 it sent 64 bits with a zero high word. | Mode 2 now writes one chunk and steps `PTR` by 1, not a 64-bit record and +2. Mode 5 writes one word and steps addr by 1, not two words and +2. Use `send64(0, x)` where the old shape was meant. |
| A 128-bit element is one packet. | Counts (`DMA_*_REMAINING`) for 128-bit streams halve. Mode-5 streams at 128 bits step the destination by 4 words. |
| The size is stamped on every packet. | Gather values are 64-bit and read-port results 32-bit; writes land at the packet's size. |
| A 32-bit mode-2 or mode-3 element lands packed (`PTR` += 1); v1.0 padded it to 64 bits. | Mode 4 still writes a 64-bit record from a 32- or 64-bit packet. |
| `STREAM` is three arrays. | `set_stream()` takes three words. A new round usually rewrites only `STREAM_SRC` and `STREAM_COUNT`. |
| Register offsets after `TILE_CONDITIONS` all moved; `DMA_PULSE` is now `TILE_PULSE` at 0x808. | Rebuild anything that hard-codes an address. |
| Condition bits are renumbered by unit (8.3). | Check anything that decodes `TILE_CONDITIONS` by number, including REPORT readers and expected masks. |
| 128-bit groups now need `ag_lookahead` = 2L + 2 exactly, like 64-bit ones (AgBench, section c). | In v1.0 their two-cycle visits gave slack; the default 2L + 2 is now the exact minimum for them, with no margin. |

**Suites to run** (`./run_tests.sh all`)
- RecipeTest and its shuffled run.
- SortTest p1, sort, sort-shuf, sort-bt.
- SortSummary, SortCompare and PhaseNeutral where used, and Sweep full.
- RegDocTest: its `split_regs.h` check becomes a real check once the header says "mesh.h 1.1".
- Re-measure the spec's remaining "v1.0 measurement; to be re-measured in v1.1" items that need the sort: spec 4.2's TH/BT
  peaks and section 14.
