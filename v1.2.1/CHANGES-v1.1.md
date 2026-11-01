# mesh-sorter v1.1: the specification update

**Archive naming** (owner, 2026-10-23): every delivered tree is `mesh-sorter-v1.1-YYYY-MM-DD<letter>.tgz`, with letters a to z for successive archives on the same date, then aa to az, ba to bz and so on. An archive's name is the date it was produced plus the next unused letter for that date. **Packaging** (owner, 2026-10-23): a delivery is exactly one file, the archive; everything a session produces goes inside the tree before archiving, its diff and any notes or logs under `deliverables/`. This tree is `mesh-sorter-v1.1-2026-10-23d.tgz`: the wake pass's `…-23c` (part i; its input was `…-23a`, and `…-23b` its first delivery) with `DESIGN-NOTES.md` brought into line with the spec by the owner session. `…-23e` then removed every PNG (46 files, about 20 MB under `diagrams/png`, `intro/png` and `sw/regdoc/png`) to keep archives small; their SVG sources and the build scripts (`diagrams/src/build.sh`, `intro/src/build.sh`, `sw/regdoc/src/build_pdf.sh`) remain, and the PNGs are rebuilt before the PDFs are next made. `…-23f` also removed the two PDFs (`Reference.pdf`, `Intro.pdf`), stale v1.0 build outputs of `make_docs.sh`, rebuilt when the documents are next made. `…-23g` added `WORKER4_PROMPT.md`, the simulator session for everything pending. `…-26a` is the request-port and peripheral-slot pass (part j), from `…-23g`. `…-26b` removed `WORKER4_PROMPT.md` as obsolete. `…-26c` is the modes, counting and one-tile-design pass (part k). `…-26d` is part D (part l); `…-26e` adds the write log's last-load entry to it, `…-26f` the region-0 reorder, and `…-26g` the state word's flags and the controller's region-0 loading. `…-26h` is part E (part m). `…-26i` is part F (part n). `…-26j` is part G (part o); `…-26k` pins "upstream Jolt" to the baseline's version. `…-26l` is part H (part p). `…-26m` is part I (part q). `…-26n` is part J (part r). `…-26o` is fix K10 (part s). `…-26p` is the rest of part K (part t). `…-26q` is part L (part u); `…-26r` corrects part u's counts. `mesh-sorter-v1.1-2026-10-27a` is part M (part v), the first archive dated 2026-10-27. This tree is `mesh-sorter-v1.1-2026-10-27a.tgz`.

This package is the v1.0 package with the hardware specification updated to v1.1. It changes three files, `spec/machine-spec.md`, `spec/schedule.md` and `sw/REGISTERS.md` (prose only), and adds this one. Nothing else was edited: the simulator, the tile software, the kits, the results, the figures and both PDFs are still v1.0. They are listed below as stale, and part (c) is the handoff for the simulator session.

**The replayer is deferred.** v1.1 reserves only its hooks (spec 4.17), each marked *to be specified with the replayer*: mode 8, `TILE_PULSE` [2], `TILE_STATUS` [3] and [4], `TILE_CONDITIONS` [13:11], register offsets from 0x844, and one wake source.

**Its direction is recorded** (spec 4.17, *Design direction*): a small ALU with fixed 3- and 5-cycle rows, credit-managed code, expansion and value queues (4, 4 and 8 KB), an 8 KB template ROM and a 4 KB decode table, with the compressed trace resident in a 512 KB region of big SRAM and a request bus to LPDDR. That is about 32.5 KB of replayer memory per tile instead of the 768 KB planned earlier. The design notes *Replayer and mesh v1.1* (updated 2026-10-23) hold the details and the shared-resource allocation table; the hooks fit it.

**Review fixes (2026-10-22):** the stray duplicate spare row in spec 8.2 (`TILE_STATUS`) is removed. **The size encoding is now 00 = 32, 01 = 64, 10 = 128 bits** (1 << size chunks), and **11 is undefined behaviour**, not a bad packet: no hardware check, meshsim flags it (spec 3.1, 4.2, 4.13, 8.4, invariant 5, item 26; REGISTERS.md 3.4). **C&C's mode-5 boot loads are 128-bit packets** (spec 6.2).

**Send-port reorder (owner, 2026-10-23), after the simulator session.** The payload registers are now `ROUTER_SET_W0`, `W1`, `W2` (bits [31:0], [63:32], [95:64]), and each send register supplies the packet's **top** word: `ROUTER_SEND_32` bits [31:0], `ROUTER_SEND_64` bits [63:32] (with `W0`), `ROUTER_SEND_128` bits [127:96] (with `W0`–`W2`). Software stores a packet's words in ascending order and the last store sends. Offsets are unchanged in position: 0x810 `W0`, 0x814 `W1`, 0x818 `W2`, 0x81C–0x824 the three sends. Spec 4.11, 8.1, 8.4 and the front matter are updated. **Pending in meshsim** (the simulator's v1.1 tree predates this): `Controller`/`TileControl` (which word each send store supplies), `Registers` and `RegDoc` (names and descriptions), then regenerate `sw/mesh.h`, `meshsim/sw/mesh.h` and REGISTERS.md's generated blocks; update REGISTERS.md 2.2's prose and the size/payload table in 4.2, `meshsim/sw/tests/regs.c` and any test program using the send port, and HooksTest's send-port checks. The software session should port against the patched mesh.h.

**21-bit addresses (owner, 2026-10-23).** The link word's addr is 21 bits (the word is 169 bits); `ROUTER_SET_HDR` is dst [6:0], mode [10:7], addr [31:11], with no spare bit; `STREAM_SRC` is source [20:0]; `STREAM_DEST` is destination word [20:0] and *d* [26:21]; element and mode-5/8 destination addresses wrap mod 2²¹. Spec 3.1, 4.2, 4.5, 4.11, 4.16, 8.4, 8.9, 10 and 11 are updated. **Then carried through big SRAM:** 21-bit chunk addresses everywhere (1.x, 4.1): area = bits 20–17, up to 16 areas (8 MB), default still 8 areas (4 MB); addresses wrap mod 2²¹; on a device with fewer than 16 areas, including the default, chunks from the end of memory up to 0x1FFFFF read 0 and writes there are dropped. `PTR` entries, `DMA_RECORD_BASE_ADDRESS`, the gather start address and `SRAM_RD_ADDR_QUEUE` take [20:0]. **Big-SRAM size and banks (owner, 2026-10-23):** the size is any multiple of 256 KB up to 8 MB, and `TILE_STATUS` [31:27] reports it in 256 KB units − 1 (15 at the default 4 MB; no spare bits left). Areas are banks whose geometry is *(Open)* for the hardware, with one guarantee software may rely on: every area is contiguous, power-of-two sized, aligned and **at most 512 KB**, so each 512 KB-aligned block has its own port(s) and a 4 MB device has at least 8 areas. meshsim's 8 × 512 KB stays the default, and a size that isn't a whole number of areas makes the last area partial, its missing rows reading 0 like any address past the end (spec 4.1, 8.2, 10). Unchanged on purpose: mode 1's 20-bit ordinal and the 20-bit down-counters (counts, not addresses). **Pending in meshsim**: `LinkWord`, `Fields`, the send port's header, the `STREAM` encoders, the address generator's wrap, `BigSRAM`'s address decode, wrap and out-of-range rule, `PTR` width, the read port and GATHER addresses, and `TILE_STATUS`'s size field ([31:27], 256 KB units), with `mem_chunks` accepting any multiple of 65,536 chunks and a partial last area; then mesh.h and REGISTERS.md regenerated. From the simulator session's SIM-CHANGES (d): C&C's send register can also produce size 11 (spec 3.1), and a padded load writes up to three zero words past an image's end (spec 6.2).

**What v1.1 decides.** These were decided by the owner on 2026-10-22:
- the 168-bit link word with a size field;
- mode 8;
- the `STREAM` layout as three 32-bit arrays;
- the send port's `W1`–`W3` and `SEND_32`/`_64`/`_128` registers;
- the register map and the `TILE_STATUS` and `TILE_CONDITIONS` bit assignments;
- mode 4 at 32 and 64 bits;
- start only, with no resume.

## a. Changed spec sections

`spec/machine-spec.md`, v1.0 → v1.1:

| section | change |
|---|---|
| front matter | Version 1.1, `mesh.h` 1.1, the label *v1.0 measurement; to be re-measured in v1.1*, and a "Changes from v1.0" list. |
| 1 Conventions | A packet is a 32-, 64- or 128-bit payload named by its size; `STREAM` is three arrays. |
| 3.1 Link word | 168 bits (size 2, payload 128); mode 8 is the replayer write, 9–15 reserved; modes restated by size; bad packets (mode 0/9–15, mode 4 at 128) defined; size 00/01/10 = 32/64/128, 11 undefined; fixed payload bits for modes 1, 6, 7 *(Proposed)*. |
| 3.2 Latency | Link pipeline stages are 168 flops; the fan-in table and 63 × 1 result are labelled v1.0 measurements. |
| 4 Tile | The units table gains 4.17, the replayer. |
| 4.1 Big SRAM | Partial writes are 32, 64 or 128 bits. |
| 4.2 Address generator | One packet per element at every size, stamped with the read size; a 128-bit visit is 1 cycle and a group's period is *t*; mixed sizes in a group *(Proposed)*; the new `STREAM_SRC`/`DEST`/`COUNT` layout (4-bit mode, 20-bit destination word); modes 5 and 8 step the destination word by 1, 2 or 4; the schedule's 128-bit T is 4; 128-bit block copies; measured peaks and the nine-group comparison labelled v1.0. |
| 4.3 Read port | Results are 32-bit mode-7 packets; the read-port tolerance is labelled v1.0. |
| 4.4 GATHER engine | Gather values are 64-bit mode-2 packets (unchanged). |
| 4.5 Read queue, return path | Every return carries its size; a 128-bit read is one packet; injection takes 7 + 4 + 20 + 2 + 128 bits; there is no 2-cycle hold for 128-bit returns. |
| 4.7 Output queues | The FIFOs are 168 bits wide. |
| 4.8 Traffic director | Bad packets are dropped uncounted with "bad mode or size"; mode 5 is 1, 2 or 4 words; mode 8 goes to the replayer, uncounted; port B has four banks. |
| 4.9 Write engine | Modes 2 and 3 write at the packet's size and `PTR` steps 1, 2 or 4; a 128-bit packet is one write; mode 4 takes 32- or 64-bit packets and writes a 64-bit (key, ordinal) record, `PTR` += 2. |
| 4.10 Table memories | `STREAM` is three 256 × 32 macros read in parallel; the window decode moves `GROUP` to 0x3400. |
| 4.11 Controller | Each memory is four 512 × 32 banks and port B writes 1, 2 or 4 words; the mode-5 range check is per word; the send port is `ROUTER_SET_W1`–`W3` and `ROUTER_SEND_32`/`_64`/`_128`, with the size chosen by the send register and addr stepping +1/+2/+4 for modes 5 and 8. |
| 4.12 Counters, local wake | A packet counts 1 whatever its size; mode 8 is never counted; the wake term is GO, the local wake or the replayer wake; the done gate blocks the replayer wake *(Proposed)*. |
| 4.13 Status, conditions | The live bits are regrouped; the conditions are regrouped by unit, with 11–13 reserved for the replayer and kinds renumbered. |
| 4.14 Commit ordering | Mode 8 is uncounted; its landing is to be specified with the replayer. |
| 4.16 Tile 0's fifth port | The C&C FIFOs are 168 bits; cost re-estimated. |
| 4.17 Replayer (new) | Reserved hooks only; interim behaviour and the requirements on any replayer design *(Proposed)*; the design direction (ALU, small queues and a resident trace, about 32.5 KB per tile instead of 768 KB; details in the design notes) *(Open)*. |
| 5 Timing summary | `TILE_PULSE`; a replayer row. |
| 6.1 RUN/HALT | HALT holds the replayer in reset. |
| 6.2 Boot | Staging at 128 bits, with images row-aligned and padded to whole rows *(Proposed)*. |
| 6.3 done, READY, GO, WFI | `TILE_PULSE`; three wake events. |
| 6.5 Block copy | Three `STREAM` stores; 128-bit copies. |
| 6.6 C&C | The send register carries the size; the receive FIFO holds a 128-bit payload and its size *(Proposed)*. |
| 7 Memory subsystem | Mode-8 loads for the replayer. |
| 8.1 Register map | 17 registers reordered by family (0x800–0x840), 0x844 onward reserved for the replayer, a rebuild note, and a new list of configuration registers. |
| 8.2 `TILE_STATUS` | New bit assignment, with v1.0's positions. |
| 8.3 `TILE_CONDITIONS` | New bit assignment, with the v1.0 → v1.1 mapping. |
| 8.4 Send port | Seven registers. |
| 8.5 `DMA_CONFIG` | Offset 0x828; layout unchanged. |
| 8.6 `TILE_PULSE` | Renamed from `DMA_PULSE`, at 0x808; [2] is replay start (reserved), [3] reserved; start only, for every engine. |
| 8.7, 8.8 | Offsets 0x82C–0x838 and 0x83C–0x840. |
| 8.9 Table window | `STREAM_SRC` 0x2800, `STREAM_DEST` 0x2C00, `STREAM_COUNT` 0x3000, `GROUP` 0x3400; unmapped 0x3600–0x3FFF. |
| 9 Invariants | 5 is bad packets; 11 is per word; new 16, the replayer's invariants (to be specified). |
| 10 Parameters | Link word, `STREAM` arrays, controller-memory banks, replayer. |
| 11 Logic budget | Scaled for the 168-bit word, the 128-bit write path, the `STREAM` arrays and the eight banks: ≈ 0.22 mm² (0.17 in v1.0), replayer excluded. |
| 12 Physical design | Eight banks, the `STREAM` arrays; the replayer's placement is to be specified. |
| 13 Open items | 3 (write combining) and 16 (128-bit gather reads) updated; new open items 23 (the replayer), 24 (re-measurement) and 25 (`STREAM` count width); new proposed items 26–32. |
| 14 The sort | The measured results are labelled v1.0; 128-bit values are one packet. |

Not changed: 2, 2.1, 4.6, 4.15, 6.4.

The other two files:
- **`spec/schedule.md`:** the 128-bit-values bullet and the `T` comment (one packet per visit, T = 4), and its two measured tables labelled v1.0.
- **`sw/REGISTERS.md`** (prose only):
  - version 1.1, and a status note on the generated blocks (§0);
  - the access rules and reset list;
  - the live bits and condition grouping (2.1.1, 2.1.2);
  - the `TILE_PULSE` section moved from 2.3.2 to **2.1.3** (anchor `tile-pulse`), with 2.3.3 and 2.3.4 renumbered 2.3.2 and 2.3.3;
  - the send port (2.2), the counters, augmenting, the read port and controller-memory writes (2.3–2.5);
  - `PTR` (3.3) and `STREAM` (3.4, rebuilt for the three arrays), `GROUP` at 0x3400 (3.5);
  - modes and sizes (4.2), packed words (4.3);
  - the sequence table (6), and the worked examples' program lines and rules (7).

## b. What v1.1 makes stale

**Generated files, not regenerated** (the simulator session regenerates them):
- `sw/mesh.h` (still 1.0) and `meshsim/sw/mesh.h`.
- Every generated block in `sw/REGISTERS.md`. They still hold v1.0 output, under markers renamed for v1.1:
  - `addressmap` and `summary`;
  - `reg TILE_STATUS`, `reg TILE_CONDITIONS`, `reg TILE_PULSE` (was `DMA_PULSE`);
  - `reg ROUTER_SET_HDR ROUTER_SET_W1 ROUTER_SET_W2 ROUTER_SET_W3 ROUTER_SEND_32 ROUTER_SEND_64 ROUTER_SEND_128`;
  - `reg DMA_CONFIG`, the counters, `reg DMA_RECORD_BASE_ADDRESS`, `reg SRAM_RD_*`;
  - `cmwrites` and `window`;
  - `entry STREAM_SRC` (holds v1.0's `STREAM_LO`), `entry STREAM_DEST` (new, empty) and `entry STREAM_COUNT` (holds `STREAM_HI`);
  - `entry GROUP_LO` and `GROUP_HI` (the window address);
  - `linkword`, `modes` and `cmword`.
  - Only `entry HIST`, `entry PTR` and `command` are likely to regenerate unchanged.
- `Reference.pdf` (built from REGISTERS.md) and `Intro.pdf`.

**Figures** (`diagrams/`):
- 1.0 sort: the measured cycles, and 128-bit values as two packets.
- 1.2 system and 1.1 cards: the 102-bit link label.
- 1.3 tile: register names and the send port.
- 1.4 packets: link word and modes.
- 2.1 control and 2.2 tile edge: `DMA_PULSE`.
- 2.3 wake: the third wake source.
- 2.4 conditions: the bit numbering and bad mode.
- 2.5 reset: `ROUTER_SET_HI`.
- 3.1 controller: two banks per memory, one `STREAM` macro.
- 3.2 register decode: the map and layouts.
- 3.4 send port.
- 4.1 router tile 0 and 4.3 router: 102-bit FIFOs.
- 5.1 traffic director: mode 8, bad packets, port B sizes.
- 5.2 `PTR`/`HIST` increment: +2.
- 6.1–6.4 address generator: `STREAM` layout, 2-cycle 128-bit visits, the 2*t* period.
- 7.1 big SRAM: "a 64-bit write fills half a row".
- 7.2 read queue and 7.3 returns: 128-bit returns as two packets.
- 8.1 parameters.
- Probably current: 3.3 read port (no offsets drawn), 4.2 router (no widths), 7.4 and 7.5 gather (64-bit gather values are unchanged).
- The worked-example diagrams `sw/regdoc/svg/ex1`–`ex5`: their program lines now say `TILE_PULSE` and `ROUTER_SEND_32`; redraw them with `regdoc/src/build_pdf.sh`.
- `intro/`: `Intro.md` gives 128-bit values at Σ 8*R* (two packets per visit) and v1.0's measured times; `fig11_timeline` uses them too.

**Recipes** (`sw/RECIPES.md`): every recipe compiles against `mesh.h` 1.0 and must be rebuilt. The changes that matter:
- 0 (`local_run`, `set_stream`: three arrays, `TILE_PULSE`).
- 1 (done is `TILE_PULSE`).
- 3 (block copy: three stores; can run at 128 bits).
- 5 (augment: a 32-bit mode-4 stream still works; a 128-bit one is now a bad packet).
- 7 and 17 (`send_words`, block to big SRAM: `ROUTER_SET_W1`/`ROUTER_SEND_32`/`_64`/`_128`; 17 can send 128 bits).
- 9 (all-to-all: 128-bit values at T = 4, not 8).
- 10 (the vector reduction could use four 128-bit packets).
- 11 (read port offsets).
- 12 (status and condition bits).
- 13 (synchronised start, `TILE_PULSE`).
- 14 (the cycles table: re-measure).
- 15 (the rules that bite: two-packet values, `DMA_PULSE`).
- 20 (REPORT through the send port, C&C's receive FIFO).

**Conventions** (`sw/CONVENTIONS.md`), section by section:
- 1 (`mesh.h` 1.0);
- 3 (boot staging at 64 bits, even lengths);
- 4 (`DMA_PULSE`);
- 5 (the wake term);
- 6 (`ROUTER_SET_HI`, `ROUTER_SEND_LO`);
- 6a (the 3-bit stream mode, modes 1–7, two-packet 128-bit values);
- 7 (status and condition bits);
- 8 (block copy, `STREAM`);
- 9 (read-port offsets).

**Software that hard-codes v1.0** (rebuilt in the software package):
- `sort/split/split_regs.h`: every register address moved.
- `sort/skew/planner/skew.h`: `write_stream` packs v1.0's high word by hand, `count | d << 18 | mode << 24 | size << 27`, and `HW_STREAM_HI` writes `STREAM[2s + 1]`.
- `sort/program/sw/config.c`: writes `STREAM[2 * i + 1]` directly.
- `sort/program/sw/*.c`: `STREAM_LO`/`STREAM_HI`, `DMA_PULSE`. The 128-bit-value streams (`SIZE_128` in `transfer.c` and `config.c`) become one packet per value, and their groups run at T = 4.
- `sort/program/patches/`.
- `meshsim/sw/` (`loader.S`, `tests/*.c`, `ext/`).
- `kit/image/*` and `kit/HASHES`.
- `verify-v1.0/`.
- Package docs: `README.md`, `BUILDING.md` and `DEVIATIONS.md` describe v1.0.

**Measurements labelled "v1.0 measurement; to be re-measured in v1.1"** (spec item 24):
- spec 3.2: the fan-in table and the 63 × 1 result;
- spec 4.2: the TH/BT queue peaks with gap 4, and the nine-group comparison;
- spec 4.3: the read-port tolerances, 15 results at full rate and 32 at half rate;
- spec 14: the measured sort in `results/`;
- `spec/schedule.md` §5 (queue peaks) and §6 (the measured comparison table).

The `results/` tree and `meshsim/results/` are the v1.0 logs. Unlabelled measurements that v1.1 doesn't touch (lead-in times, `ts_engine_latency` sweeps, the read port's loopback cost, the split on the RV32 ISS, the REPORT settle) are expected to stand, but the re-run will confirm them.

## c. Handoff for the simulator session

**Must rebuild:**
- `sort/split/split_regs.h`, because every register offset after `TILE_CONDITIONS` moved;
- every tile image: the sort's (`kit/image`), meshsim's test programs and loader, and the split's driver;
- `kit/HASHES`;
- `tools/check_layout.sh` then checks meshsim's copies (`meshsim/sw/ext/`, `meshsim/sw/mesh.h`) against the rebuilt `sort/` and `sw/`.

**Classes** (`meshsim/src/`):

| class | what must change |
|---|---|
| `LinkWord` | A 168-bit word. The payload becomes 128 bits (a second `long`), and the size goes in header bits [39:38] (free above bit 38). Add `M_REPLAY` = 8. Size encoding 00 = 32, 01 = 64, 10 = 128 (1 << size chunks); 11 is undefined, flagged by meshsim, not a bad packet. Bad-packet test: mode 0/9–15, or mode 4 with size 128. Update `describe()`. |
| `Router`, `WordBuses`, `Buses`, `InjectionPath`, `Endpoint`, `Drain` | Carry the 128-bit payload and the size through every FIFO, link pipe, selector and injection (FIFO entries are 168 bits); widths in any reports. |
| `AddressGenerator` | One packet per element for every size: drop `visitWait` for `SIZE_128` (around lines 245 and 313) and the `2t` period (320, 324). Stamp the size from the read size. Modes 5 and 8 step the destination word by 1, 2 or 4 with a 20-bit wrap (line 260 now masks 12 bits and steps 2 or 4). Read `STREAM` as three parallel 32-bit reads at the same index, same latency (`readStream`). New field positions; allow mode 8. Reserved encodings: mode 0/6/7/9–15, size 11 (undefined), mode 4 with size 128. Drop the one-size-per-group report if proposed item 27 is accepted. |
| `TableStore`, `SRAMMacro` | `STREAM` becomes three 256 × 32 macros (`STREAM_SRC`, `STREAM_DEST`, `STREAM_COUNT`) read in parallel. Window decode: 0x2800, 0x2C00, 0x3000, `GROUP` at 0x3400, unmapped 0x3600–0x3FFF. `ts_busy` covers all of them. |
| `SRAMReadQueue` | A 128-bit return is one packet: remove `hiPending`, the second-packet emit and its mode-5 `+2`. Stamp every return with its size, read-port returns with 32. The payload is 128 bits. |
| `WriteEngine` | Modes 2 and 3 write at the packet's size, `PTR` += 1, 2 or 4; a 32-bit write is one chunk (per-chunk enables); a 128-bit write is one full-row access at chunk 0, or a spanning write. Mode 4 takes the low 32 bits plus the ordinal (target − base)/2, writes a 64-bit record and does `PTR` += 2. Pointer-stage entries carry the 128-bit payload and size. |
| `TrafficDirector` | Bad packets: drop, uncounted, set condition 1. Mode 5 goes to port B with 1, 2 or 4 words. Mode 8 goes to a replayer stub: dropped, recorded, uncounted, no condition (proposed item 31). Modes 1, 6 and 7 read fixed bits at any size (proposed item 26). |
| `ControllerMemories` | Four word-interleaved 512 × 32 banks per memory; port B writes 1, 2 or 4 consecutive words per cycle; invariant 11 is checked per word. |
| `BigSRAM` | 32-bit and 128-bit writes through the per-chunk enables; 128-bit row writes. |
| `GatherEngine` | Gather values are 64-bit mode-2 packets with size 64 stamped; otherwise unchanged. |
| `ReadPort` | Results are 32-bit mode-7 packets. |
| `Controller`, `Tile` | The send port: `ROUTER_SET_W1`–`W3` and `ROUTER_SEND_32`/`_64`/`_128`, with the size taken from the register. All three `W` registers clear after each send; words beyond the size go out as 0; addr steps +1/+2/+4 for modes 5 and 8 and +1 for 6 and 7. The stall-and-retry path applies to all three send registers (line 225 tests `ROUTER_SEND_LO` only). Bus errors at 0x844–0xFFC and 0x3600–0x3FFF. |
| `TileControl` | `DMA_PULSE` → `TILE_PULSE`. [2] is a no-op that meshsim flags until the replayer exists; [3] is flagged. Wake = GO \| local \| `replayerWake` (constant 0), with the done gate blocking it (proposed item 30). The new `TILE_STATUS` positions. HALT holds the replayer stub in reset. |
| `Conditions` | Renumber to spec 4.13: `ROUTER_OVF` 0, `BAD_MODE_OR_SIZE` 1, `UNEXPECTED` 2, `DRIFT` 3, `WE_IN_OVF` 4, `TABLE_OVF` 5, `GATHER_OVF` 6, `RDPORT_OVF` 7, `DONE_EARLY` 8, `BUS_ERROR` 9, `ILLEGAL` 10; 11–13 reserved (read 0). |
| `Registers`, `RegisterBlock`, `Fields` | The 17-register map at 0x800–0x840 (spec 8.1), with 0x844 onward unmapped. Fields for `TILE_STATUS`, `TILE_CONDITIONS`, `TILE_PULSE` and the seven `ROUTER_` registers; the three `STREAM` entries and `GROUP` at 0x3400. `mesh.h` 1.1 macros: `TILE_PULSE_*`, `ROUTER_SET_W1..3`, `ROUTER_SEND_32/64/128`, `STREAM_SRC/DEST/COUNT` field macros and a `set_stream()` that writes three words, `MODE_REPLAY` = 8, and the new `TILE_ST_*` and `TILE_COND_*` positions. Version strings 1.1. |
| `RegDoc` | Regenerate every block under the v1.1 markers listed in (b). The `TILE_PULSE` block moved to §2.1.3 (anchor `tile-pulse`; summary links to `#dma-pulse` must follow). There is a new `entry STREAM_DEST`. Then delete the "v1.1 status of the generated blocks" paragraph in REGISTERS.md §0. |
| `CnC` | The send register carries a size; receive FIFO entries hold a 128-bit payload plus its size (proposed item 29); REPORT parsing. |
| `Config`, `Chip`, `PhaseLog` | Any width constants; strict flags for the new checks; packet accounting (a 128-bit value is now one packet); the phase CSVs. |

**Tests** (`meshsim/test/`):

| test | what must change |
|---|---|
| `RegDocTest` | Names, offsets and fields against `mesh.h` 1.1 and REGISTERS.md's v1.1 markers; `split_regs.h` after its rebuild; the new bus-error ranges. |
| `AgTest`, `AgBench` | 128-bit visits in 1 cycle and group period *t*; modes 5 and 8 stepping 1/2/4 with a 20-bit field; the three `STREAM` arrays; reserved encodings (mode 4 with 128); the v1.0 rulings restated; start ignored while running; lead-in re-measured. |
| `EngineTest` | Write sizes and `PTR` steps; mode 4 at 32 and 64 (record, +2) and at 128 (bad packet); 128-bit row writes and spanning writes. |
| `MemTest` | 32- and 128-bit writes at the areas; port B's four banks at 1, 2 and 4 words. |
| `NetTest` | 168-bit words end to end; bad packets (mode 9–15 from C&C, mode 4 at 128 bits); re-measure the fan-in skew table (spec 3.2). |
| `CondTest` | The new bit numbers; every bad-packet case; replayer bits read 0. |
| `CtrlTest` | `TILE_PULSE` ([2] and [3] flagged); the seven send-port registers and their clears; the new window and register bus errors. |
| `ReadPortGatherTest` | Results at size 32; re-measure the tolerances (spec 4.3). |
| `RecipeTest` | Every recipe rebuilt against `mesh.h` 1.1; the sync start and REPORT pacing; the cycles table. |
| `BusTest` | The payload width in the generic buses. |
| `RiscvTest` | The rebuilt tile images. |
| `SortTest`, `SortCompare`, `SortSummary`, `PhaseNeutral`, `VerifySummary`, `SplitRun`, `SearchBench`, `A2A` / `AllToAllTest measure` | Re-run and record old and new; `PhaseNeutral` and `VerifySummary` compare v1.0 with v1.1 (the 128-bit-value format sends half the value packets). |
| new: mode-8 stub | A mode-8 packet is dropped at the replayer port, uncounted, with no condition and no effect on the local wake; `TILE_STATUS` [3]–[4] and `TILE_CONDITIONS` [13:11] stay 0. |

## d. Open and proposed items for an owner decision

New in v1.1 (spec section 13):
- **23. The replayer** (open, deferred): everything marked *to be specified with the replayer*. Its direction is in the design notes (spec 4.17 points there): an ALU, small queues and a resident trace instead of 768 KB of resident replay memory. This includes the items moved from the earlier plan:
  - the replayer's table and template details;
  - 128-bit-only replayer memories;
  - the two latches and one `WFI`;
  - "invalid class";
  - padding rows;
  - the area of its memories.
- **24. Re-measurement** (open): the labelled v1.0 measurements, once meshsim models the 168-bit word.
- **25. `STREAM` count width** (open, no change proposed): 17 bits would serve the sort (every count ≤ 65,536, 128-bit streams included), but would cap 128-bit streams at half the big SRAM, and `STREAM_COUNT` has 5 spare bits anyway. Confirm 18 bits stays.
- **26. Fixed payload bits for modes 1, 6 and 7** at any defined size *(Proposed)*.
- **27. Mixed read sizes within a group** *(Proposed)*: every visit is one cycle now.
- **28. 128-bit staging from C&C** *(Proposed)*, with images row-aligned and padded to whole rows (touches the boot document, item 12).
- **29. C&C's receive FIFO** holds the 128-bit payload and size *(Proposed)*; part of C&C's register block, item 11.
- **30. The `done` gate blocks the replayer wake** *(Proposed)*.
- **31. Interim replayer hooks** *(Proposed)*: zeros, a no-op pulse, unmapped offsets, and mode 8 dropped at the port.
- **32. What the hooks require of the replayer** *(Proposed)*: HALT reset, stopped by a halted controller, and idle bits that read 0.

Carried over from v1.0:
- Items 1–22 still stand.
- Item 3 (write combining) is narrowed: 128-bit values are now one write.
- Item 16 (128-bit gather reads) now halves the injection cost as well.

---

## e. Addendum: the request bus (worker 1, 2026-10-23)

This pass adds the tiles' **request bus** to LPDDR and C&C, the **memory system's contract** as a tile sees it, and the changes the owner ruled to make them work. It edits only `spec/machine-spec.md` and the prose of `sw/REGISTERS.md`; `request-bus.diff` is the unified diff of `spec/` and `sw/REGISTERS.md` against the input tree. The replayer (spec 4.17, mode 8, the replayer's registers and bits) is worker 2's and is untouched here.

**Owner rulings (2026-10-23) that this pass implements:**
- `STREAM_DEST`'s *d* is 7 bits, [27:21]: 0–63 a relative tile, 64–125 an absolute non-tile endpoint (64 C&C; 65 the memory system, reserved and served at C&C's port; 66–125 future engines), 126–127 invalid for streams. A write stream is mode 9 to C&C, each packet's addr its chunk offset within the write. Packets from any non-tile source (64–125) are never counted.
- Completions, write tokens and write acks are **mode 10**, which sets a bit of a new **`WAKE_EVENTS`** register (tags [15:0]; replayer [20:16]; [31:21] reserved), with **`WAKE_MASK`** choosing which events wake `WFI` and which a load consumes. The C&C mode-7 counting exception in the design notes is withdrawn.
- Eject overload is solved in hardware: big SRAM returns reads in acceptance order, the token FIFO holds metadata only, the loopback FIFO is at least 64 deep, and a loopback packet holds its token until it leaves the loopback FIFO *(Proposed)*.
- Registers: 0x844 `WAKE_EVENTS`, 0x848 `WAKE_MASK`, 0x858–0x86C `REQUEST_SET_W0`–`W2`, `REQUEST_SEND_32`, `_64`, `_128`; `TILE_STATUS` [11] `request_busy`. This supersedes the design notes' allocation table: `TILE_PULSE` [3] and `TILE_STATUS` [12] are no longer allocated.

### e.1 Sections changed (`spec/machine-spec.md`)

| section | change |
|---|---|
| front matter | Four "Changes from v1.0" bullets: the request bus, wake events, streams to non-tile endpoints, in-order returns and the protected loopback; bad modes are now 0 and 11–15. |
| 1 Conventions | Register families `WAKE_` and `REQUEST_`. |
| 2, 2.1 | The request bus as a system element; 65 the memory system (reserved, served at C&C), 66–125 future engines; non-tile sources never counted. |
| 3.1 | Mode 9 (big-SRAM write at an explicit address; at C&C, write-stream data) and mode 10 (wake event); 11–15 reserved and bad; mode 10 reads fixed bits [5:0]; reads of LPDDR as request-then-push. |
| **3.3 Request bus** (new) | Topology, links, framing (header byte: sender, size), wormhole arbitration, back-pressure, throughput, per-tile order, the 64-bit request's fields. |
| 4 | Units table gains 4.18. |
| 4.1 | Reads return in acceptance order *(Proposed)*; `mem_idle` covers mode 9 and the director-to-engine handoff; mode 9 as a write source. |
| 4.2 | *d* 7 bits [27:21], absolute 64 for C&C; mode 9 in streams with a destination word; spare [31:28]; reserved encodings; loopback reads may wait for tokens (drift in a group run). |
| 4.3 | Memory deliveries count as incoming traffic; under the protected loopback and the eject priority, results no longer cost packets, but the rule stands until re-measured. |
| 4.5 | 64 tokens; the token FIFO holds metadata only; loopback packets (reads and the send port's packets for the tile itself or broadcasts) hold a token until they leave the loopback FIFO; the area steering field is [20:17]; injection fields 21-bit addr. |
| 4.7 | The loopback FIFO is at least the token count (64) *(Proposed)* and cannot overflow; **the eject output serves its loopback FIFO only when every other eject FIFO is empty** (owner ruling; drift to be confirmed, item 70). |
| 4.8 | Modes 9 and 10 routed; bad modes 0 and 11–15; mode 10's event timing. |
| 4.9 | Mode 9 through the write engine, in order through the pointer stage, no `PTR` access, no count-on-arrival; the "no explicit-address write" statement replaced. |
| 4.11 | Stalls on a full request FIFO; mode-9 writes; addr steps for mode 9; send-port loopback packets wait for a token; C&C's port takes mode 9. |
| 4.12 | Non-tile sources never counted; modes 9 and 10 never counted; the event wake replaces the replayer wake in the `WFI` term; the done gate blocks the event term *(Proposed)*; a new **Wake events** subsection. |
| 4.13 | Live bit `request_busy`; bad modes 0 and 11–15. |
| 4.14 | Landing of memory-system reads. |
| 4.16 | The memory system's traffic through tile 0's C&C port. |
| **4.18 Request port** (new) | The six registers, the 2 KB FIFO and its format, the stall rule, `request_busy`, timing, HALT. |
| 5 | Controller stalls; request-bus and wake-event rows. |
| 6.1 | The request port runs in HALT; `WAKE_EVENTS` and `WAKE_MASK` cleared by HALT → RUN and at reset; the request FIFO empty at reset. |
| 6.3 | The event wake as the third wake event. |
| 6.6 | C&C's port shared with the memory system; mode 9 at C&C's receive path goes to the memory system. |
| **6.7 The memory system's contract** (new) | Reads (destination kinds, data format and order, completion, landing, bounds), order between reads, token-gated writes and the optional ack, debug messages, requests to C&C, errors, no per-tile pacing (the eject priority makes it unnecessary), and the program's obligations. |
| 7 | Tiles reach the memory system through the request bus; its contract is 6.7. |
| 8.1 | The register map gains `WAKE_EVENTS`, `WAKE_MASK` and the six `REQUEST_` registers; unmapped 0x870–0xFFC; reset and configuration lists. |
| 8.2 | [11] `request_busy`; spare [20:12]. |
| 8.4 | `ROUTER_SET_HDR`'s addr step includes mode 9. |
| 8.9 | `STREAM_DEST` *d* at [27:21]. |
| **8.10** (new) | `WAKE_EVENTS`, `WAKE_MASK` and the request registers; the 64-bit request laid out in its two words. |
| 9 | Invariant 1 notes the loopback; 5's bad modes; new **invariant 17**, the request bus and the memory system's contract. |
| 10 | Rows for the request bus, request FIFO, tags, request units, the eject priority; token FIFO and loopback depth. |
| 11 | Loopback FIFO (~0.01 mm²), request port (~0.007 mm²), the area queues absorbing the reordering; total ≈ 0.24 mm² per tile; the tree once per die (~0.01 mm² plus wiring). |
| 12 | Placement of the request port, the tree and the wake-event registers. |
| 13 | Item 9 points to 6.7; new items **60–75**. |

`sw/REGISTERS.md` (prose only): the timing note (request stores can stall), access rule 4's unmapped ranges, the reset and configuration lists, the register families, `request_busy` and `mem_idle` in 2.1.1, the synchronised-start rule, the send port's rules (mode 9, mode 10, loopback tokens), 2.3.2's never-counted list, done gate and `WFI` term, 2.4's mixing rule, 2.5's memory deliveries, **new 2.6 `WAKE_`** and **2.7 `REQUEST_`** (with empty generated-block markers `reg WAKE_EVENTS WAKE_MASK` and `reg REQUEST_SET_W0 … REQUEST_SEND_128`), 3.4's fields (*d*, mode 9, 21-bit positions), 4.2's modes 9 and 10, and two rows in section 6's sequences.

### e.2 Proposed and open items (spec section 13)

**Open:** 60 the memory system's side (cache, ordering, token scheduler, port sharing, C&C's request registers); 61 the tree's physical design and `reqbus_latency`; 62 simulating the new paths; **70 the eject output's loopback priority** (ruled by the owner on 2026-10-23 in place of per-tile delivery pacing; open until NetTest confirms that group runs show no drift, since a tile's self-stream now gets only the eject slots its links leave free).

**Proposed** (the owner accepted 63–69 and 71–72 as proposals, and then 73's done-gate extension, 74's send-port extension and 75, on 2026-10-23; their markers are unchanged):
- 63 framing, sender stamping and arbitration;
- 64 the request port's FIFO format, stall rule, `W` clears and HALT behaviour;
- 65 request sizes other than 64 bits;
- 66 mode 9's path through the write engine;
- 67 a read's data format and order;
- 68 controller-memory deliveries under invariant 11;
- 69 the optional write acknowledgement (bit [4]);
- 71 debug messages;
- 72 errors at the memory system (C&C error flag, no tile condition);
- **73 the wake events' reset and HALT → RUN clear, and the `done` gate blocking the whole event term** (extending item 30), with a flag for an event on a pending tag;
- **74 in-order returns and the protected loopback** (the owner's ruling, marked *Proposed* as asked), **extended so that the send port's packets for the tile itself and its broadcasts also take a token.** Without that, they could fill the loopback FIFO without holding tokens;
- **75 write-stream checks at C&C's port.**

### e.3 Merge notes (shared rows; for the owner's merge with worker 2)

Minimal edits were made to these rows and lines, which worker 2 is likely to touch too:
- spec 8.1: the opening sentence (register count), the "0x844–" row (replaced by `WAKE_EVENTS`, `WAKE_MASK`, a "0x84C–0x854 reserved for the replayer" row, the six `REQUEST_` rows and "0x870– unmapped"), and the bus-error rule's ranges. Worker 2 replaces the reserved row with its three registers.
- spec 8.2: "spare [20:11]" became `request_busy` [11] and spare [20:12]. [12] is no longer allocated.
- spec 4.12: the `WFI` term and the wake-sources sentence now name the event wake. The replayer's events are `WAKE_EVENTS` [20:16]; worker 2's 4.17 hook row "the replayer wake", its interim `replayer_wake` = 0, and items 23, 30 and 31 should now point at those bits.
- spec 4.2's `STREAM_DEST` destination-word row (it mentions mode 8), 4.13's live-bit list, 6.1's reset lines, 6.3's wake list, section 9 (new invariant 17 after 16), section 10's table (new rows above "replayer"), section 13 (new block after item 32) and the front-matter bullets (after "Replayer hooks").
- REGISTERS.md: 2.3.2's `WFI` paragraph; access rule 4; the families paragraph; the new sections are numbered 2.6 (`WAKE_`) and 2.7 (`REQUEST_`). If worker 2 adds a `REPLAYER_` section, it fits between them by offset and renumbers 2.7.

### e.4 Handoff for the simulator session

**Classes** (`meshsim/src/`):

| class | what must change |
|---|---|
| `LinkWord`, `Fields` | Modes 9 (`M_WRITE_AT`) and 10 (`M_EVENT`); bad modes 0 and 11–15; mode 10's tag [3:0] and kind [5:4]; `describe()`. |
| `TrafficDirector` | Mode 9 to the write engine; mode 10 sets `WAKE_EVENTS`[tag] at the end of the accepting cycle, uncounted, with no memory write; never count src 64–125; flag an event on a pending tag. |
| `WriteEngine` | Mode 9: through the input FIFO and the pointer stage in order, no `PTR` access, no count-on-arrival, any size, wrap and out-of-range as 4.1, spanning split; `mem_idle` includes the director-to-engine handoff. |
| `BigSRAM`, `SRAMReadQueue` | Returns in acceptance order across areas (a reorder stage inside `BigSRAM`); token FIFO without data slots; 64 tokens, with a loopback packet's token held until it leaves the loopback FIFO; area steering on [20:17]. |
| `Router`, `InjectionPath`, `Endpoint` | Loopback FIFO depth = token count (a `Config` parameter, default 64); the eject selector takes the loopback FIFO only when every other eject FIFO (on tile 0 including from-C&C) is empty, round-robin among the others; release the token on the loopback FIFO's pop; the send port's packets for `me` or 126 wait for and take a token. |
| `AddressGenerator` | *d* 7 bits [27:21]; *d* = 64 sends to dst 64 (mode 9 only); flag *d* 65–127 and *d* = 64 with another mode; mode 9 allowed with destination-word stepping 1/2/4 and 21-bit wrap; loopback reads wait for a token (drift in a group run). |
| `Controller`, `Tile`, `Registers`, `RegisterBlock` | `WAKE_EVENTS` (R, clear-under-mask on load, same-cycle set survives), `WAKE_MASK` (R/W [20:0]); the six `REQUEST_` registers with `W` clears, the 256 × 64-bit FIFO (128-bit requests take two entries) and the stall rule; `request_busy` at `TILE_STATUS` [11]; unmapped 0x870–0xFFC (and 0x84C–0x854 until worker 2's registers exist); addr step for mode 9 at the send port. |
| `TileControl` | Wake = GO \| latch \| ((`WAKE_EVENTS` & `WAKE_MASK`) ≠ 0), the done gate blocking the event term; clear both registers at chip reset and HALT → RUN; the request port runs in HALT. |
| `ReplayerStub` | Its wake becomes `WAKE_EVENTS` [20:16] (still 0). |
| new `RequestBus` (and nodes) | Eight row nodes and a root, an 8-byte buffer per input, wormhole per request, round-robin over inputs holding a header, one byte per cycle per hop with ready/valid, sender stamping, a 64-request root queue, `reqbus_latency` as a parameter. |
| new `MemorySystem` (behind `CnC`) | A behavioural model behind C&C's port: reads by destination kind (128-bit packets, ascending, unicast, then mode 10 kind 0), a configurable latency and reordering, up to one packet per cycle in total (shared with C&C's own sends) with no per-tile pacing; write tokens one at a time, mode-9 stream assembly by offset, an optional ack; a debug log; errors to a C&C flag. |
| `CnC` | Share the port with the memory system; route mode-9 packets from the token holder to it and flag the rest; take target-1 requests from the root queue into C&C-visible state. |
| `Config`, `Chip`, `PhaseLog` | Parameters (`reqbus_latency`, loopback depth, memory latency); strict flags for invariant 17; request and delivery accounting in the phase CSVs. |
| `RegDoc` | Generate `reg WAKE_EVENTS WAKE_MASK` and `reg REQUEST_…` blocks (REGISTERS.md 2.6 and 2.7, empty markers in place), the address map and summary with 0x844–0x86C, `entry STREAM_DEST` with *d* [27:21], and the modes table with 9 and 10. |

`mesh.h` 1.1 gains `WAKE_EVENTS`, `WAKE_MASK`, `WAKE_TAG(t)`, the `REQUEST_` registers, field macros for the 64-bit request (`REQ_TARGET`, `REQ_OP`, `REQ_KIND`, `REQ_TAG`, `REQ_LPDDR`, `REQ_COUNT`, `REQ_TILE`) split across the two words, `MODE_WRITE_AT` = 9, `MODE_EVENT` = 10, `STREAM_DEST_D` at [27:21], `TILE_ST_REQUEST_BUSY`, and helpers such as `mem_read()` and `mem_write()`.

**Tests** (`meshsim/test/`):

| test | what must change or be added |
|---|---|
| `NetTest` | Modes 9 and 10 end to end; bad modes 11–15; loopback FIFO depth and no overflow under a full-rate incoming stream plus a full-rate loopback stream (the case that overflowed before); **the eject priority: the loopback FIFO served only when every other eject FIFO is empty (on tile 0 including from-C&C), the others round-robin; and the group runs of the sort's schedules (and `AllToAllTest measure`) show no drift under it** (item 70). |
| `EngineTest` | Mode-9 writes at every size, spanning, wrap, out of range; ordering with modes 2–4 in the pointer stage; no `HIST` count. |
| `MemTest` | In-order returns across areas with uneven queueing. |
| `AgTest`, `AgBench` | *d* = 64 streams and the flags for bad *d*; mode-9 streams; loopback token waits; group runs show no new drift on the sort's schedules. |
| `CtrlTest`, `RegDocTest` | The new registers, offsets, bus-error ranges, `W` clears, request stalls, `request_busy`; `WAKE_EVENTS` clear-under-mask, peek, same-cycle survival; the regenerated blocks. |
| `CondTest`, `HooksTest` | The event wake with the done gate; wake term with the mask at 0 is v1.0's; `TILE_PULSE` [3] and `TILE_STATUS` [12] read and act as unallocated. |
| `ReadPortGatherTest` | Re-measure the tolerances with the deeper loopback (the half-rate case should no longer lose packets); gathers with held loopback tokens. |
| new `RequestBusTest` | Per-tile order, exact 1/64 fairness under saturation, back-pressure to a stalled store, sender stamping, `reqbus_latency`, draining in HALT. |
| new `MemSysTest` | Reads of each kind landing (event then `mem_idle`/`cm_idle`), reads completing out of order, one read per queue, token-gated writes from several tiles serialised, ack ordering against a later read, full-rate delivery to a loopback-busy tile with no overflow, every error path, debug messages. |
| `SortTest` and the measurement suite | Re-run to confirm the loopback change costs the sort nothing (item 62). |

**Also stale** (beyond part b): figures 1.2 (no request bus), 1.4 (modes 9 and 10), 2.3 (the wake term), 3.2 (the register map), 5.1 (the director), 6.1 (`STREAM_DEST`), 7.2 and 7.3 (the token FIFO and in-order returns), 4.1–4.3 (the loopback FIFO's depth) and 8.1; `sw/RECIPES.md` and `sw/CONVENTIONS.md` need recipes and conventions for a read from LPDDR, a write and a debug message.

## f. Addendum: the replayer (worker 2, 2026-10-23)

This pass specifies the replayer in full (spec 4.17), from the owner's design notes *Replayer and mesh v1.1* (updated 2026-10-23) and their shared-resource allocation table, with the owner's rulings of 2026-10-23. It ran beside the request-bus pass and edits only the replayer's parts of `spec/machine-spec.md` and `sw/REGISTERS.md`; `replayer.diff` is the unified diff of `spec/` and `sw/REGISTERS.md` against the input. Facts about Jolt are cited to the jolt-cpp tracer on the `tracer-standalone` branch of `jolt-asic-planning` (PR #1, commit `4f36b3d`), paths under `tracer/src/tracer/`.

**Changes after the checkpoint, decided by the owner (2026-10-23):** the wake register is shared (`WAKE_EVENTS`/`WAKE_MASK`, specified with the request bus in 8.10); the replayer owns `WAKE_EVENTS` [20:16], and `REPLAYER_NEED`, `TILE_PULSE` [3]'s request-wake enable and `TILE_STATUS` [12] are gone. The replayer's registers are 0x84C `REPLAYER_OPCODE`, 0x850 `REPLAYER_STATUS` and 0x854 `DMA_REPLAYER_REMAINING` (8.11). Jolt's conventions are table data wherever they can be; what can't be is listed as hardwired (item 57).

### a. Changed sections

`spec/machine-spec.md`:

| section | change |
|---|---|
| front matter | the mode-8 and replayer bullets |
| 3.1 | mode 8's row: what it writes, and counted in `DMA_REPLAYER_REMAINING` on landing when the tile sent it |
| 4.8 | mode 8's row; its path (`rp_latency` = 2, no queue) and counting on landing |
| 4.12 | four down-counters (the table row, the counting bullet, the local wake's "all four") |
| 4.13 | the live bits `replay_running`, `replay_wait`; conditions 11–13 and their kinds |
| 4.14 | how a program knows its mode-8 writes have landed |
| **4.17** | **rewritten in full, 4.17.1–4.17.14:** overview; run log and checkpoint; the expansion word (with `virt`, `imm0_pc0`, `report_c`, `from_log`) and operand codes; the rd = x0 policy; the decode table and its index; the ROM; row execution (register file, ALU, per-opcode classes, schedules, pc and `instr_idx`, hardwired behaviour); queues, ports and credits; wake events and the controller loop; mode 8's map, landing and ownership; start, done, halt, conditions; the output row; the resident region (the software contract); cost |
| 5 | the replayer's timing row |
| 6.1 | HALT: the replayer idle, its memories writable |
| 6.3 | the replayer's events as the third wake source, through `WAKE_EVENTS` |
| 8.1 | the three register rows; the unmapped-offset and `TILE_PULSE` [3] rules; the reset note |
| 8.2 | `TILE_STATUS` [3] `replay_running`, [4] `replay_wait` |
| 8.3 | 14 conditions in [13:0] |
| 8.6 | `TILE_PULSE` [2], replay start |
| **8.11 (new)** | `REPLAYER_OPCODE`, `REPLAYER_STATUS`, `DMA_REPLAYER_REMAINING`; the events in `WAKE_EVENTS` [20:16] |
| 9 | invariant 16, the replayer's invariants |
| 10 | the replayer's parameters |
| 11 | the replayer's line (≈ 0.1–0.15 mm²); total ≈ 0.32–0.37 mm² with it |
| 12 | the replayer's placement and mode 8's path |
| 13 | item 23 (specified; what remains), 30 and 31 (superseded), 32 (kept, met); new items 33–59 |

`sw/REGISTERS.md` (prose only): §1 access rule 4 (unmapped offsets); 2.1.1 `replay_running`, `replay_wait`; 2.1.2 conditions [13:11]; 2.1.3 `TILE_PULSE` [2]; 2.2 mode 8 and addr; 2.3.2 the fourth counter and "never counted"; 2.5 and 3.4 mode 8's destination; 4.2 mode 8; **new 2.6** (anchor `replayer`) with an empty generated block `reg REPLAYER_OPCODE REPLAYER_STATUS DMA_REPLAYER_REMAINING` for RegDoc to fill.

### b. Open and proposed items (spec section 13)

- **Open:** 33 the run log, a requirement on our tracer engine (DIV/REM advice and next `instr_idx` words, on purpose unlike jolt-cpp); 34 the template enumeration; 40 the ROM's fit; 58 the validation harness (what it compares is specified; building it is a later session); 59 the output queue's consumer side and the placement against the materializer.
- **Proposed:** 35 `virt` and `rd0` = 3; 36 the 85 executed opcodes; 37 the row classes, the multiplier without a signed high half, and measuring multiply density; 38 the decode index's special groups; 39 ROM words 0 and 1; 41 register 0 of the image; 42 fetch and decode overlap; 43 contiguous `VirtualPext` masks; 44–46 the mode-8 windows, sizes and offsets; 47 bad replayer writes; 48 `rp_latency` and counting on landing; 49 mode 8 from other tiles; 50 consuming wake events; 51 the expansion low-water rule; 52 queue reset and preload; 53 `replay_wait` and `REPLAYER_STATUS`; 54 ownership in HALT; 55 the conditions; 56 Jolt's conventions as table data; 57 the hardwired Jolt behaviour; 59 the output row's layout.
- **Accepted as proposals on 2026-10-23** (still marked *Proposed* in the spec until ruled final): 35, 37 (classes and multiplier), 44–48, 50, 54, 55, 56 (the principle), 59 (the layout).

### c. Handoff for the simulator session

**Classes** (`meshsim/src/`):

| class | what to add or change |
|---|---|
| new `Replayer` | the three queues with occupancy, pending and the low-water conditions (4.17.8); the decode index and table (4.17.5); the ROM and template walk, including `rd0`, the offset and a mid-sequence start from ROM[start] (4.17.4, 4.17.6); the operand resolver and codes 128–144 (4.17.3); the register file (register 0 hardwired); the ALU for the 85 executed opcodes, following `execute_instruction` with 4.17.7's departures; the 3- and 5-cycle schedules; pc and `instr_idx` with the self-jump compare, `imm0_pc0`, `hold`; the flags `virt`, `report_c`, `from_log`; the 640-bit record into a 32-row output queue drained by a sink (one row per cycle); start, done, halt; the events [20:16] with consume-under-mask; conditions 11–13 |
| `TrafficDirector` | mode 8 to the replayer's write port, landing `rp_latency` later; window decode, sizes, ownership and bad replayer writes (4.17.10); drop the v1.1 mode-8 stub |
| `Counters` / `TileControl` | `DMA_REPLAYER_REMAINING`: the tile's own mode 8, on landing or drop; the local wake over four counters; `TILE_PULSE` [2] starts a pass (ignored and flagged while running), [3] stays flagged; HALT and a halted controller reset the replayer |
| `Registers`, `RegisterBlock`, `Fields`, `RegDoc` | 0x84C–0x854; `TILE_STATUS` [3], [4]; `TILE_CONDITIONS` [13:11]; `WAKE_EVENTS` [20:16] field macros (with the request-bus session's register); `mesh.h` macros for the mode-8 windows; fill REGISTERS.md's new block and regenerate `addressmap`, `summary`, `reg TILE_STATUS`, `reg TILE_CONDITIONS`, `reg TILE_PULSE`, `modes` |
| `Conditions` | 11 bad replayer write, 12 queue overrun, 13 invalid expansion (stops the pass) |
| `Config` | `rp_latency` (2), the sink's rate, strict flags for mode 8 from another tile and for a start while running |

**Tests** (`meshsim/test/`): a `ReplayerQueueTest` (ports, sizes, push order, credits, two outstanding refills, short final push, new-expansion credit, peek, overrun, reset at done and halt, preload); a `ReplayerExecTest` (every class, the schedules, rd = x0 under each `rd0` value, remap offsets, a mid-sequence start, advice rows recorded as `VirtualAdvice`, a self-jump, a JAL to pc 0, `is_compressed` on last rows only, padding, invalid expansions); `CondTest`, `CtrlTest`, `RegDocTest` and the counter tests for the new bits, registers and counter; replace the mode-8 stub test. The row-level proof is the validation harness (item 58), a later session.

**Tables and tools** (a software or tracer session): a template builder that produces the decode table and ROM from `expand_virt_seq`, with the replications 4.17.5 requires and the flags of item 56; the tracer engine's run-log writer (item 33).

**Figures:** a new replayer part (block diagram, queues and credits, the controller loop, the mode-8 map, the row record); and updates to 2.3 (wake), 2.4 (conditions 11–13), 3.2 (register map), 5.1 (director, mode 8), 8.1 (parameters).

### d. Merge notes with the request-bus pass, and stale text

**Left for the merge** (each sits next to the request-bus pass's edits):
- spec 4.12: the C&C bullet's "in any of the three counters" becomes four; the wake-source and wake-term bullets still describe the reserved `replayer_wake` and are the request-bus pass's to replace with the `WAKE_EVENTS` term;
- spec 8.1's "17 registers" and REGISTERS.md's "the 17 registers" and §1 `addressmap`/`summary` counts; spec 8.2's spare row [20:11] (now [11] `request_busy`, and [20:12] spare);
- REGISTERS.md 2.3.2's `WFI` paragraph (the wake term);
- outside this pass's scope, still saying "to be specified with the replayer": spec 4's units table row for 4.17 (suggest "the replayer"), and 4.2's "A copy into the replayer is a mode-8 stream run" (suggest pointing to 4.17.9–4.17.10).

**Stale text found, not fixed here:** REGISTERS.md 2.1.2 and 4.2 call size 00 a bad packet (size 11 is undefined, nothing else); 3.4 gives `STREAM_DEST` as [19:0] with *d* in [25:20] (the spec has [20:0] and [26:21]) and `STREAM_SRC` as [19:0]; §1's reset paragraph names `ROUTER_SET_W1`–`W3` (now `W0`–`W2`); §0's notation still says a 20-bit chunk address.


## g. Merge of the two passes (owner session, 2026-10-23)

The request-bus pass (part e) and the replayer pass (part f) were merged by three-way merge against their shared base; 17 conflicts in `spec/machine-spec.md`, 7 in `sw/REGISTERS.md` and this file's addenda were resolved by combining both sides:

- **Register map (8.1):** 0x844 `WAKE_EVENTS`, 0x848 `WAKE_MASK` (8.10); 0x84C `REPLAYER_OPCODE`, 0x850 `REPLAYER_STATUS`, 0x854 `DMA_REPLAYER_REMAINING` (8.11); 0x858–0x86C the six `REQUEST_` registers (8.10); 0x870–0xFFC unmapped. Families gain `REPLAYER_`.
- **Modes (3.1, 4.8):** mode 8 the replayer write, counted in `DMA_REPLAYER_REMAINING` on landing when the tile sent it; mode 9 big-SRAM write at an explicit address; mode 10 wake event; 11–15 reserved.
- **Wake (4.12, 6.3):** three sources, GO, the local wake over **four** counters (with `DMA_REPLAYER_REMAINING`), and the event wake, where the replayer's events are `WAKE_EVENTS` [20:16]. `TILE_PULSE` [3] and `TILE_STATUS` [12] are unallocated.
- **Counting:** packets from any non-tile source (64–125) are never counted; modes 1, 6, 9 and 10 are never counted; mode 8 only from the tile itself.
- **`TILE_STATUS`:** [3] `replay_running`, [4] `replay_wait`, [11] `request_busy`, spare [20:12].
- **Sections 8.10 and 8.11** both kept; **13:** replayer items 33–59, request-bus items 60–75; invariants 16 (replayer) and 17 (request bus).
- **Timing, parameters, cost, placement (5, 10, 11, 12):** both passes' rows kept. The per-tile total is **≈ 0.34–0.39 mm²** with the replayer, about 23–26 % of the logic budget (≈ 0.24 mm² without it).
- **The remaining "to be specified with the replayer" phrases** (the units table row for 4.17, 4.2's copy into the replayer, 4.14's mode-8 landing) now point at 4.17.
- **REGISTERS.md:** 2.6 `WAKE_`, 2.7 `REQUEST_`, and the replayer's section as **2.8** (its cross-references renumbered); the stale reset list (`ROUTER_SET_W0`–`W2`), `STREAM_DEST`'s 7-bit *d* at [27:21] and the replayer's mode-8 lines fixed in the prose. Generated blocks (register tables, the mode and link-word tables, the `STREAM` field tables) still show older values until the simulator session regenerates them.

**Owner note for review:** item 55 has an invalid expansion **stop the pass** and post done. The owner earlier ruled "raise and run" for replayer conditions; confirm which applies before the simulator session.


## h. Owner rulings after the merge (2026-10-23), not yet in the spec text

1. **All wakes are events, and `WAKE_EVENTS` is renumbered** to keep a large block free for future units: [7:0] request tags (**8 tags**, down from 16); [8] GO; [9] counters done (set on the 1 → 0 transition that leaves every down-counter at 0, once `mem_idle` and `cm_idle` hold: the local wake with its qualifier, as a sticky event); [10] command landed (mailbox slot 0); [11] engine run done (`rd_done`); [12] request FIFO no longer full; [17:13] the replayer ([13] code low-water, [14] value low-water, [15] expansion low-water, [16] new expansion, [17] done); [31:18] reserved for future units. `WAKE_MASK` resets to [8] | [9] (v1.0 behaviour for programs that never write it); the `done` gate becomes a mask setting applied on the `done` pulse. Consequences: the request's tag is [8:6] with [9] spare; mode 10's payload carries the tag in [2:0], kind still [5:4].
2. **`WAKE_REQUIRED`** (R/W, 0x870, reset 0): `wake = ((WAKE_EVENTS & WAKE_MASK & ~WAKE_REQUIRED) ≠ 0) OR (WAKE_REQUIRED ≠ 0 AND (WAKE_EVENTS & WAKE_REQUIRED) == WAKE_REQUIRED)`. A mask bit not required wakes alone (escape hatch); a required bit only counts toward all-pending; with `WAKE_REQUIRED` = 0 the term is `(WAKE_EVENTS & WAKE_MASK) ≠ 0`. Clearing stays under the mask.
3. **The replay start clears the queues.** All three queues are emptied by the start; no preloading; no end-of-pass reset. Software rule: before a start, every queue push the controller issued has landed (counters done; tag events for memory reads).
4. **Item 55 accepted as written:** an invalid expansion stops the pass and posts done. The owner expects it never to happen (the tracer catches bad expansions), so the choice is immaterial; the spec keeps the replayer pass's version to avoid churn. Worker 3 may drop its (Proposed) marker.

5. **Big-SRAM read order is per client, not global.** Big SRAM returns each client's reads (the address generator, the GATHER engine, the read port) in the order that client issued them; reads of different clients may complete in any order relative to each other. This replaces the request-bus pass's global in-order rule (4.1, 4.5, item 74's first half): the token FIFO becomes an in-order return queue per client, and a gather's scattered reads no longer block a stream's sequential ones. The read port's mode-7 results carry explicit addresses and need no order at all.
6. **A software rule, with a meshsim flag and an invariant:** a tile must not have two clients sending order-placed packets to the same destination at once: modes 2–4 into the same `PTR` entry of the same receiver, or mode 8 into the same replayer queue. Order is guaranteed only within a client, so such a mix would interleave unpredictably.

`WORKER3_PROMPT.md` is the spec pass that applies 1–3, 5 and 6; it is done, in part i.

## i. Wake events and read order (worker 3, 2026-10-23)

This pass applies part h's rulings 1–3, 5 and 6 to `spec/machine-spec.md` and to `sw/REGISTERS.md` (prose only; the generated blocks are untouched), drops item 55's *(Proposed)* marker (h.4), and records six further owner rulings made during the pass (i.1): three on the wake design and the acceptance of items 79–81. Nothing else was edited: `meshsim/`, `sort/`, `kit/`, the PDFs, `DESIGN-NOTES.md`, `sw/mesh.h`, `sw/CONVENTIONS.md` and `sw/RECIPES.md` are as they were. The unified diff of `spec/` and `sw/REGISTERS.md` against `…-2026-10-23a.tgz` is `deliverables/wake-pass.diff` in this tree.

### i.1 Owner rulings made in this pass (2026-10-23)

1. **Consuming GO and counters done: the helper load.** Hardware never consumes an event on wake. The `sw/mesh.h` wait helpers (`wfi()`, `wait_for_command()`) follow every `WFI` with a load of `WAKE_EVENTS`. Every tile program is rebuilt against `mesh.h` 1.1, so the spec says "a program that uses the helpers behaves as in v1.0", not "a program that never writes `WAKE_MASK`". A counter store clears counters done ([9]) and its qualifier, as it cleared the latch.
2. **The `done` gate stays a gate, not a mask setting.** From the `done` pulse until GO the wake term is overridden to "GO ([8]) pending" and nothing else can wake `WFI`; counters done cannot be set in that window. `WAKE_MASK` is never written by hardware except its reset to [8] | [9] at chip reset and HALT → RUN, so after GO the program's own mask is in force with nothing to restore. (This replaces h.1's "the `done` gate becomes a mask setting applied on the `done` pulse".)
3. **`WAKE_PEEK`** (R, 0x874): returns exactly what a load of `WAKE_EVENTS` would and never clears or credits anything; an alias of the same state with no storage of its own. Software reads `WAKE_PEEK` to look and `WAKE_EVENTS` to consume; "a load with mask 0 is a peek" is no longer the documented way to peek. Unmapped offsets start at 0x878.
4. **Keep GO in `WAKE_MASK`** (item 79, accepted as written): the gate wakes on a pending GO whatever the mask, but only a load with [8] in the mask consumes it, so software keeps [8] in `WAKE_MASK` at every `wait_for_command()`; the replayer loop keeps it throughout, and meshsim flags a `wait_for_command()` with [8] clear.
5. **The send port is a client for invariant 18** (item 80, accepted as written): its packets are not ordered against the read clients' returns, so it counts in the one-client-per-order-placed-destination rule.
6. **The new events' details** (item 81, accepted as written): command landed ([10]), engine run done ([11]) and request FIFO no longer full ([12]) are set on the edge, in the cycle the matching status bit sets or falls; `WAKE_REQUIRED` is cleared by HALT → RUN as well as at chip reset; `WAKE_MASK`, `WAKE_REQUIRED` and `WAKE_PEEK` cover [17:0], with [31:18] reserved.

The owner also accepted this pass's proposals, now items 76–78 (*Proposed, accepted*): pushes while the replayer is stopped are discarded at the next start; per-client in-order return queues for the address generator and GATHER, with the 64 tokens a shared pool and injection by the existing request priority, and read-port returns unordered; a kind-2 read's landing proven by its tag event alone. Items 30, 52, 73 and 74's first half are superseded, and item 55 is unmarked.

### i.2 Sections changed

**`spec/machine-spec.md`:**
- **Front matter:** the wake-events and read-order bullets rewritten; the replayer's hooks at `WAKE_EVENTS` [17:13].
- **3.1:** mode 10's tag in payload [2:0], [3] spare, kind [5:4]; the fixed-payload-bits note.
- **3.3:** the request's tag [8:6] (8 tags), [9] spare.
- **4.1:** read order per client (address generator, GATHER), none between clients, read port unordered; the one-client-per-destination rule.
- **4.2:** the token FIFO becomes the address generator's return queue; the rise of `rd_done` posts [11].
- **4.3:** read-port results may return in any order.
- **4.5:** tokens and the return queues (shared pool of 64, per-client in-order queues, injection by request priority); the one-client-per-order-placed-destination rule.
- **4.9:** one sending client per `PTR` entry at a time.
- **4.12:** retitled *Mailbox, counters and the wake events*. The local wake becomes counters done ([9]); the full event table ([7:0] tags, [8] GO, [9] counters done, [10] command landed, [11] engine run done, [12] request FIFO no longer full, [17:13] the replayer, [31:18] reserved); `WAKE_MASK` (reset [8] | [9], never written by hardware), `WAKE_REQUIRED`, `WAKE_PEEK`; the wake term with the gate; a wake consumes nothing; the helpers' load; the `done` gate as a gate; timing, reset and HALT; uses.
- **4.13:** item 55's marker dropped.
- **4.14:** counters done; the memory system's queue refills land by their tag event.
- **4.17.1, 4.17.8:** the replayer's bits [17:13]; **the start clears the queues**, no preloading, no end-of-pass reset, the software rule before a start; `WAKE_PEEK` changes nothing.
- **4.17.9:** the event table renumbered; the controller loop rewritten (`WAKE_MASK` = GO | REPLAY, the squelch sets it to GO | counters done, a load after the run's `WFI`); LPDDR refills by tag event; what happens between passes. The stale "completion counts in `DMA_REPLY_REMAINING`" is gone.
- **4.17.10:** a requested transfer lands by its tag event (was "its mode-7 completion"); the ownership table's stopped row and pushes while stopped (item 76).
- **4.17.11:** the start empties the queues; done no longer resets them; item 55 unmarked.
- **4.17.13:** the preload rules replaced by "before a start, every queue push has landed".
- **4.18:** 8 tags; the fall of `request_busy` posts [12].
- **5:** the counters, GO and wake-event rows.
- **6.1:** HALT → RUN and chip reset (the gate, the qualifier, `WAKE_EVENTS`, `WAKE_REQUIRED` cleared, `WAKE_MASK` = [8] | [9]; the return queues).
- **6.3:** command landed's event; GO as a sticky event; `WFI`'s wake; `wait_for_command()` loads `WAKE_EVENTS` to consume GO; the synchronised-start path includes that load.
- **6.4, 6.5:** counters done in place of the local wake.
- **6.7:** mode 10's tag [2:0]; kind-2 landing by the tag event (item 78).
- **8.1:** the map gains `WAKE_REQUIRED` (0x870) and `WAKE_PEEK` (0x874); unmapped from 0x878; reset values.
- **8.6:** replay start empties the queues.
- **8.7, 8.11:** a counter store clears counters done; the replayer's bits [17:13].
- **8.10:** retitled; the four wake registers; the wake term; the request's tag [8:6], [9] spare.
- **9:** invariant 16 gains "every queue push has landed before a replay start"; new **invariant 18**, one client per order-placed destination.
- **10, 11, 12:** tokens and return queues, 8 tags, the wake-event layout, the register count and the control-island rows.
- **13:** items 4, 30, 50, 52, 55, 73 and 74 updated; new items 76–81.

**`sw/REGISTERS.md`** (prose only): section 1 (unmapped from 0x878, reset, families); 2.1.3 (done and replay start); 2.3.2 (retitled; counters done replaces the local wake); 2.6 (rewritten: the event table, `WAKE_MASK`, `WAKE_REQUIRED`, `WAKE_PEEK`, the wake term, the gate, the helpers, rules); 2.7 (tag [8:6], [9] spare, event [12], kind-2 landing); 2.8 (start empties the queues, no preloading, bits [17:13], `WAKE_PEEK`, the loop); 4.2 (mode 10's tag); 5 (`wait_for_command()`); 7.1, 7.2 (`wfi()` consumes counters done).

### i.3 Items

- **Superseded:** 30 (the gate now overrides the whole wake term to GO), 52 (queue reset and preload), 73 (wake reset and the gate; the tag-reuse flag stands), 74's first half (in-order returns with one token FIFO; the protected loopback stays proposed).
- **Decided:** 55, accepted as written.
- **New, proposed and accepted:** 76 pushes while stopped; 77 per-client return queues; 78 kind-2 landing.
- **New, decided by the owner (i.1, 4–6):** 79 keep GO in `WAKE_MASK`; 80 the send port as a client for invariant 18; 81 the new events' details. Their *(Proposed)* markers are removed in the spec and REGISTERS.md.

### i.4 Handoff for the simulator session

meshsim's tree predates the request bus and the replayer (it still has `ReplayerStub`), so this adds to parts c, e.4 and f.c rather than replacing them.

- **`TileControl`, `Controller`:** replace the local-wake latch, `go_q` and the event term with one `WAKE_EVENTS` register: [8] set by GO, [9] by the qualified rising edge (cleared with its qualifier by any counter store, the `done` pulse, HALT → RUN and a consuming load), [10] by a slot-0 mode-6 write, [11] by the rise of `rd_done`, [12] by the fall of `request_busy`; `WFI` released by the wake term with `WAKE_MASK` and `WAKE_REQUIRED`, overridden to [8] from the `done` pulse until GO; no consumption on wake; [9] cannot be set in the gate. Same-cycle rules: an event set in a load's cycle survives; a clear beats a set.
- **`Registers`, `RegDoc`, `Buses`/`WordBuses`:** `WAKE_EVENTS` (0x844, [17:0]), `WAKE_MASK` (0x848, reset [8] | [9] at chip reset and HALT → RUN), `WAKE_REQUIRED` (0x870, R/W, reset 0), `WAKE_PEEK` (0x874, R, the load's value with no side effects, a store a bus error); unmapped from 0x878. Then regenerate `sw/mesh.h`, `meshsim/sw/mesh.h` and REGISTERS.md's generated blocks (the address map, the summary table, the 2.6 block, which must add `WAKE_REQUIRED` and `WAKE_PEEK`, and the mode table's mode-10 row).
- **`sw/mesh.h` helpers:** `wfi()` = `WFI` then a load of `WAKE_EVENTS`; `wait_for_command()` = done pulse, `WFI`, load of `WAKE_EVENTS`, read of slot 0; a `wake_peek()` reading `WAKE_PEEK`; constants for the 18 event bits and 8 tags.
- **`TrafficDirector`:** mode 10's tag from payload [2:0].
- **The request port and the memory-system model** (part e.4): the tag in [8:6], 8 tags; [9] ignored.
- **The replayer model** (replacing `ReplayerStub`, part f.c): events at [17:13]; the start empties the queues (contents, occupancy, pending); done no longer resets them; pushes while stopped accepted and discarded at the start; `WAKE_PEEK` moves no credits.
- **`BigSRAM`, `SRAMReadQueue`, `Tile`, `Config`:** returns ordered per client (address generator, GATHER) instead of globally; read-port returns unordered (a test option to shuffle them); the 64 tokens a shared pool; a return queue per ordered client; injection among ready returns by request priority (gather, address generator, read port).
- **Invariants:** 16's new clause (a replay start with a queue push in flight: a tile's own mode-8 queue packet sent and not landed, or a kind-2 read outstanding); 18 (two of address generator, GATHER and send port sending modes 2–4 into the same `PTR` entry of one receiver, or mode 8 into one replayer queue, overlapping in time); and item 79's flag (a `wait_for_command()` with [8] clear in `WAKE_MASK`).
- **Tests:** CtrlTest and `sw/tests/wfi.c` (counters done as an event: sticky across a late `WFI`, cleared by a counter store and by `done`, not settable in the gate, consumed only by a load; GO's event; `WAKE_REQUIRED` all-of and the escape hatch; `WAKE_PEEK` side-effect free; same-cycle cases), HooksTest and `sw/tests/regs.c` (the new offsets, resets and bus errors), CondTest and `sw/tests/cond.c`, `cmd.c` and `rcp_cmdloop.c` (`wait_for_command()` consuming GO), MemTest, EngineTest and ReadPortGatherTest (per-client order, a gather overlapping a stream, unordered read-port results), RecipeTest and `rcp_sync.c` (the start path's extra load keeps lockstep: re-measure 6.3's 9–10 and 17–18 cycles), A2A and SortTest (unchanged results), and a replayer test that pushes after done and checks the start discards it. Every test program is rebuilt against the new `mesh.h`.

### i.5 Now stale elsewhere

- **`sw/mesh.h`, `meshsim/sw/mesh.h`:** the helpers and the register list (i.4).
- **`sw/CONVENTIONS.md`, `sw/RECIPES.md`:** the local-wake latch ("a `WFI` consumes the local-wake latch; GO consumes it too"), `wait_for_command()`'s two instructions, and the recipes' bare `wfi()` are now the helpers' behaviour; the text still describes the latch.
- **`DESIGN-NOTES.md`** (not edited, by rule): its controller loop sets `WAKE_MASK` to the replayer bits without GO; it says "the done gate becomes a mask setting", "a load with mask 0 is a peek" and "a v1.0 program that never writes it behaves as before". The spec now follows i.1 instead.
- **Figures:** 2.3 (the local wake) and the register figures for 0x870–0x874; the PDFs.

## j. The request port and the peripheral slot (2026-10-26)

This pass applies the owner's change descriptions part A (the request port) and part B (the peripheral interface), with the owner's rulings during the pass, to `spec/machine-spec.md` and the prose of `sw/REGISTERS.md`, plus a set of leftover inconsistencies found on review (j.5). Its input is `mesh-sorter-v1.1-2026-10-23g.tgz`. Nothing else was edited: `meshsim/`, `sort/`, `kit/`, `DESIGN-NOTES.md`, `sw/mesh.h`, `sw/CONVENTIONS.md`, `sw/RECIPES.md` and every generated block in `REGISTERS.md` are as they were (four generated-block markers are renamed or added, empty, for RegDoc to fill). The unified diff of `spec/` and `sw/REGISTERS.md` against the input is `deliverables/request-periph.diff`. **Scope** (owner): only what changes inside a tile; the controllers, the endpoint map (2.1, 4.16, 6.6, 7) and who serves LPDDR are deferred to a later part (item 82).

### j.1 Owner rulings in this pass (2026-10-26)

**Part A, the request port.**
1. **Two directions in every tile.** The request port has a send side and a receive side; on tiles 0–63 the receive side is connected to nothing. Send and receive share offsets: a store sends, a load receives.
2. **Every request is 128 bits.** The hardware commits only requestor [6:0] (the sender's 7-bit endpoint id, overwritten by the tree: the row node for a tile, the root for a controller) and target [9:7]. **[127:10] is defined by the target** and specified with each receiver. v1.1's 64-bit request and its field table are removed, not moved; 6.7's tile-side answer rules stay, naming the fields abstractly. The target numbering (0–2 C&C 1–3 at 64–66, 3 the Updater/Binder/RW engine at 73, 4–7 reserved) stays *Proposed* until the endpoint map.
3. **A 16-bit tree**, 8 beats per request, a 16-byte buffer per node input, **one queue per target at the root**, controllers entering at the root. **The root skips an input whose head request's target queue is full; head-of-line blocking inside a row is accepted and noted.**
4. **Registers:** `REQUEST_SET_W0`–`W2` / `REQUEST_GET_W0`–`W2` (0x858–0x860) and `REQUEST_SEND` / `REQUEST_RECEIVE` (0x864); `REQUEST_SEND_32`, `_64` and `_128` are removed. `REQUEST_RECEIVE` pops; reading it empty returns 0 and sets **receive underflow, `TILE_CONDITIONS` [14]**. The receive queue is the root's queue for the target; tiles carry no storage for it.
5. **`REQUEST_PENDING`, `WAKE_EVENTS` [18],** live, true while a request is waiting; its `WAKE_MASK` bit resets to 0. [12] is unchanged.
6. **Answers are unchanged;** an answer carrying a value is a 128-byte block delivered like a read, then the completion. **Mode 10 is sent by the request's receiver, or a unit acting for it.**

**Part B, the peripheral slot.**
7. **One peripheral slot per tile** with one interface (spec 4.17, new); the replayer becomes peripheral type 1 and moves to 4.19, renumbered, keeping its v1.1 behaviour bit for bit except `replay_wait`.
8. **Renames:** `TILE_PULSE` [2] → `PERIPH_RUN` (peripheral run); `replay_running` → `periph_running`; `WAKE_EVENTS` [17:13] → `PERIPH_SERVICE_1`–`5`; `REPLAYER_STATUS` → `PERIPH_STATUS_0`; `REPLAYER_OPCODE` → `PERIPH_STATUS_1`; `DMA_REPLAYER_REMAINING` → `DMA_PERIPH_REMAINING`; mode 8 → "peripheral write"; `TILE_CONDITIONS` [11] → **bad peripheral write** (generic, every peripheral), [12] and [13] per peripheral.
9. **No code register:** eight read-only, side-effect-free `PERIPH_STATUS_0`–`7` at **0x880–0x89C**; at most one service line presents an item, in one designated status register. **Consuming a service line claims one unit of service** (an event clears, a queue condition credits, an item FIFO pops into its status register).
10. **Identity in `TILE_STATUS`:** [3] `periph_running`; [4] `sender_busy` (`replay_wait` dropped); [15:12] peripheral type (0 none, 1 replayer); [19:16] spare; **[26:20] the 7-bit endpoint id** (was the 6-bit tile index at [26:21]; **its low bit moves to [20]**); [31:27] size.
11. **Mode 8 from any source.** Counting: every mode-8 packet from a tile (src 0–63, the tile itself included) counts in `DMA_PERIPH_REMAINING` when it lands or is dropped; non-tile sources never. Invariant 16 loses "only the tile itself and C&C"; **invariant 18 extends to one source at a time per peripheral queue port.** The fixed `rp_latency` with no queue is a **requirement on every peripheral** (it is what item 78's proof rests on).
12. **The sender:** peripheral → up to 4 output queues → sender → its output port. `SENDER_SEND` (0x868: dest [6:0], mode [10:7], queue [12:11], word count [28:13]) starts one send; `SENDER_ADDR` (0x86C, [20:0]) is sticky and advances for modes 5, 8 and 9. 128-bit packets, then 64 for 2, 32 for 1, **64 then 32 for 3**. **A store while `sender_busy` is ignored and flagged** (a stalling store could deadlock); **sender idle, `WAKE_EVENTS` [19]** (sticky), posts when it falls; **a send with no peripheral output is ignored and flagged.** Mesh tiles' peripherals have no output; peripherals with output belong to non-mesh tiles and connect through an extra router port, as C&C at tile 0, with their endpoint ids, deferred to item 82.
13. **`WAKE_EVENTS`:** [17:13] service lines, [18] `REQUEST_PENDING` (live), [19] sender idle (sticky), [31:20] reserved; `WAKE_MASK`, `WAKE_REQUIRED` and `WAKE_PEEK` cover [19:0]. Live events have three cases on consumption: a credit, nothing (`REQUEST_PENDING`), or a pop.
14. **Register map,** no renumbering: 0x84C and 0x850 reserved; 0x854 `DMA_PERIPH_REMAINING`; 0x858–0x864 the request port; 0x868–0x86C the sender; 0x870–0x874 `WAKE_REQUIRED`, `WAKE_PEEK`; 0x878–0x87C reserved; 0x880–0x89C `PERIPH_STATUS_0`–`7`; unmapped from 0x8A0.

**Also.**
15. **Mode 9 is aligned to its size** (a multiple of 2 at 64 bits, 4 at 128); a misaligned one is a bad packet (condition 1), so it can't go unnoticed, and a mode-9 write never spans. `ROUTER_SET_HDR`'s addr is unchanged: it was already a 21-bit word address.

### j.2 Sections changed

**`spec/machine-spec.md`:**
- **Front matter:** bad packets (11–15 and misaligned mode 9), mode 8, the peripheral slot, the replayer, the request bus, the wake events, `STREAM`'s modes 5, 8, 9; families gain `PERIPH_` and `SENDER_`.
- **2:** the request bus paragraph (16-bit, targets). 2.1 untouched (deferred).
- **3.1:** modes 8, 9 (alignment) and 10 (sender); bad packets; fixed payload bits.
- **3.3:** rewritten (j.1, 1–6).
- **4 (units):** 4.1's wording; 4.17 the slot, 4.18 send and receive, 4.19 the replayer.
- **4.1:** mode 9's alignment exception; the token FIFO replaced by tokens and return queues.
- **4.2:** mode 8's destination word; a copy into the peripheral; misaligned mode-9 streams as errors.
- **4.3:** read-port results land in any order; the free-count paragraph corrected.
- **4.5:** invariant 18's extension to peripheral ports.
- **4.8, 4.9:** mode 8 to the peripheral's port (type 0: bad peripheral write); mode 9 aligned, no spanning.
- **4.11:** addr [20:12]; "the send port checks nothing" (renamed from "the sender", now a unit); request and sender stores.
- **4.12:** the counter row and counting; the event table ([17:13], [18], [19], [31:20]); the three live cases; reset and uses.
- **4.13:** live bits; conditions 1 (misaligned mode 9), 11–13 (the slot's), 14 (receive underflow); grouping and kinds.
- **4.14:** peripheral landing.
- **4.17 (new):** the peripheral slot, 4.17.1–4.17.9.
- **4.18:** rewritten (send and receive).
- **4.19 (was 4.17):** the replayer as type 1, every subsection renumbered; renames throughout; the service-line table with the claim column; the controller loop; counting from any tile; ownership; `replay_wait` removed; the software rules.
- **5:** peripheral slot, replayer, request-bus and wake-event rows.
- **6.1:** HALT and chip reset (the peripheral, the sender, the root queues, sticky vs live events). **6.2:** A7 at the default geometry. **6.3:** the wake list.
- **6.7:** rewritten around abstract fields (j.1, 2 and 6); value answers; mode 10's sender; errors; tags across targets.
- **7:** mode-8 loads for the peripheral.
- **8.1:** the map and rules (j.1, 14; shared offsets as the exception to "write-only reads 0"; reserved offsets; configuration registers gain `SENDER_ADDR`).
- **8.2:** `TILE_STATUS` (j.1, 10). **8.3:** 15 conditions. **8.6:** `PERIPH_RUN`. **8.7:** `DMA_RECORD_BASE_ADDRESS` spare [31:21].
- **8.10:** rewritten (wake coverage [19:0], the request port's eight registers; the 64-bit table removed). **8.11:** rewritten (the peripheral registers, the replayer's use). **8.12 (new):** the sender.
- **9:** invariants 5, 16 (split: the slot's and the replayer's), 17, 18.
- **10, 11, 12:** parameters, cost (≈ 0.35–0.40 mm² per tile; the tree ≈ 0.02 mm² plus wiring) and placement.
- **13:** items 23, 32, 47–50, 53, 54, 61, 63–65, 69, 71, 72, 78, 81 updated; new 82–89.

**`sw/REGISTERS.md`** (prose only): §0 (21-bit chunk address, the request store); §1 (access rules 2 and 4, reset, families); 2.1 (intro, the live bits, the fixed fields with the endpoint id and the size in 256 KB units, the conditions, `PERIPH_RUN`); 2.2 (the send-port prose brought up to the reorder, mode 9's alignment, mode 8 and 10); 2.3.2 (the counter, never counted); 2.4 and 7.4 (read-port results in any order); 2.5; 2.6 (the event table, the kinds, [19:0]); **2.7 rewritten** (send and receive); **2.8 rewritten** as the peripheral slot, with **2.8.1 the sender** and **2.8.2 the replayer**; 3.4 and 4.2 (modes 8, 9, 10); section 6 (an LPDDR row fixed, a receive row added). Generated-block markers: `reg WAKE_EVENTS WAKE_MASK WAKE_REQUIRED WAKE_PEEK`; `reg REQUEST_SET_W0 … REQUEST_SEND REQUEST_GET_W0 … REQUEST_RECEIVE`; `reg DMA_PERIPH_REMAINING PERIPH_STATUS_0 … PERIPH_STATUS_7`; new `reg SENDER_SEND SENDER_ADDR`, all empty.

### j.3 Items

- **Superseded:** 49 (mode 8 from another tile), 65 (request sizes); 63 in part (by 84).
- **Updated:** 23, 32 (generalised to every peripheral), 47, 48, 50, 53 (`replay_wait` gone), 54, 61, 64 (128 × 128 FIFO), 69, 71, 72, 78 (now holds for any peripheral), 81 ([19:0]).
- **New:** 82 the endpoint map *(Open)*; 83 target numbering *(Proposed)*; 84 the request (decided, with proposed beat order, requestor stamping and queue depth); 85 the receive side (decided, with proposed empty-`GET` and HALT behaviour); 86 the peripheral slot (decided, with the empty slot's behaviour proposed); 87 the sender (decided, with addr 0 for other modes and count-0 behaviour proposed); 88 mode-9 alignment (decided); 89 the endpoint id (decided).

### j.4 Handoff for the simulator session

This adds to parts c, e.4, f.c and i.4; where they disagree, this part wins.

**Classes** (`meshsim/src/`):

| class | what to add or change |
|---|---|
| `LinkWord`, `Fields` | Mode 9 misaligned to its size (64-bit addr odd, 128-bit addr not a multiple of 4) is a bad packet. Mode 8's name. |
| `TrafficDirector` | Mode 8 to the peripheral's write port from any src; on a type-0 tile every mode-8 packet is a bad peripheral write (condition 11), counted at once from a tile src. Misaligned mode 9 dropped with condition 1. Mode 10 accepted from any non-tile src (a receiver or a unit acting for it). |
| `WriteEngine` | Mode 9 never spans (alignment guaranteed upstream); keep the spanning path for modes 2–4. |
| new `PeripheralSlot` (replacing `ReplayerStub`; the replayer model of f.c plugs into it) | Type constant; `PERIPH_RUN` (ignored while running and on type 0, flagged); `periph_running`; five service lines with kinds (event, queue condition, item FIFO) and claim-on-consume; `PERIPH_STATUS_0`–`7` with no read side effects; mode-8 write port with fixed `rp_latency`; `DMA_PERIPH_REMAINING` counting every tile src on landing or drop; conditions 11–13; HALT and halted-controller reset. The replayer as type 1: `PERIPH_STATUS_0` (rows run, next offset), `PERIPH_STATUS_1` (the opcode, set with line 4); no `replay_wait`. |
| new `Sender` | `SENDER_SEND`/`SENDER_ADDR`; up to 4 output queues from the peripheral; packet split (⌊n/4⌋ × 128, then 64/32/64+32); addr stepping for modes 5, 8, 9 and 0 otherwise; `sender_busy`; [19] on its fall; a store while busy, a send with no output or no such queue ignored and flagged; reset in HALT. On tiles 0–63 nothing is attached; give it a test-only output queue and output port so it can be exercised. |
| `Controller`, `Tile`, `Registers`, `RegisterBlock` | The map of j.1, 14: shared offsets 0x858–0x864 (store → `SET`/`SEND`, load → `GET`/`RECEIVE`); `REQUEST_SEND` only; 0x84C, 0x850, 0x878, 0x87C and 0x8A0– bus errors; `SENDER_*`; `PERIPH_STATUS_*`; `DMA_PERIPH_REMAINING`. `TILE_STATUS`: [3] `periph_running`, [4] `sender_busy`, [15:12] type (1), [26:20] endpoint id. `TILE_CONDITIONS` [14] receive underflow. |
| `TileControl` | `WAKE_EVENTS` [17:13] from the slot, [18] from the receive side (always 0 on tiles), [19] sticky on `sender_busy`'s fall; mask, required and peek over [19:0]; consuming [18] does nothing; HALT → RUN clears sticky events only. |
| request port and `RequestBus` (part e.4 replaced) | 128-bit requests, a 128 × 128 FIFO, 16-bit links, 8 beats, 16-byte node buffers, the row node stamping the whole requestor, the root stamping controllers', per-target root queues (16 deep, a `Config` parameter), the root skipping inputs whose target queue is full, controllers' inputs at the root; the receive side reading a target queue's head (`GET` no pop, `RECEIVE` pop, empty → 0 and condition 14). A drop at the root for a reserved or absent target, flagged. |
| `MemorySystem` | Behind target 0 for now (until item 82), with a meshsim-defined layout for [127:10] (op, destination kind, tag, ack, LPDDR address, length, destination); deliveries for mode 9 aligned; value answers as a 128-byte block plus completion; mode-9 write streams must be aligned. |
| `Config`, `Chip` | `rp_latency` per peripheral (2); root target-queue depth; strict flags for the new invariant clauses (16's slot part, 17's receive underflow and target checks, 18's cross-tile peripheral-port rule). |
| `RegDoc` | Fill the four markers above; regenerate the address map, the summary, `reg TILE_STATUS`, `reg TILE_CONDITIONS`, `reg TILE_PULSE`, `modes` and `linkword`. |

`mesh.h` 1.1 gains `TILE_PULSE_PERIPH_RUN`, `TILE_ST_PERIPH_RUNNING`, `TILE_ST_SENDER_BUSY`, `TILE_ST_PERIPH_TYPE`, `TILE_ST_ID` (at [26:20]), `TILE_COND_RECV_UNDERFLOW`, `WAKE_PERIPH(n)`, `WAKE_REQUEST_PENDING`, `WAKE_SENDER_IDLE`, `REQUEST_SET_W0`–`W2`, `REQUEST_SEND`, `REQUEST_GET_W0`–`W2`, `REQUEST_RECEIVE`, `REQ_REQUESTOR`, `REQ_TARGET`, `PERIPH_STATUS(n)`, `DMA_PERIPH_REMAINING`, `SENDER_SEND`, `SENDER_ADDR` and its field macros, `MODE_PERIPH` = 8; and loses `REQUEST_SEND_32/_64/_128`, the 64-bit `REQ_*` fields, `REPLAYER_*` and `MODE_REPLAY`. Any code extracting the tile index changes with `TILE_ST_ID`.

**Tests** (`meshsim/test/`): mode-9 alignment (each misaligned case a bad packet; aligned write streams and deliveries unaffected); a `PeripheralSlotTest` (type 0: run ignored, mode 8 a counted bad write, lines and status 0; claims per line kind; no status side effects; `WAKE_PEEK` claims nothing; mode 8 from another tile counted; two sources into one queue port flagged); a `SenderTest` (packet split for n mod 4 = 0–3, addr stepping, busy and [19], store while busy ignored, no-output ignored, HALT abandons); `RequestBusTest` and `MemSysTest` rewritten for 128-bit requests, 8 beats, per-target queues, the root skip and its head-of-line effect inside a row, requestor stamping; a receive-side test on a controller endpoint (GET/RECEIVE, [18] live and non-merging, underflow on empty and on every tile); `CtrlTest`, `CondTest`, `HooksTest`, `RegDocTest` and `sw/tests/regs.c` for the new map, `TILE_STATUS` layout and condition [14]; the replayer tests from f.c under the new names, without `replay_wait`.

### j.5 Cleanups (leftovers found on review of 23g)

- Front matter: bad packets were still "mode 0 or 9–15".
- `STREAM_SRC` spare [31:21] (was [31:20]); `DMA_RECORD_BASE_ADDRESS` spare [31:21]; 4.11's mode-5 check addr [20:12]; section 11's `PTR` (256 × 21) and the 21-bit element-address adder.
- 4.3 still said read-port results land in request order "as a prefix"; now any order, and the free-count paragraph says what the count does and doesn't tell. `REGISTERS.md` 2.4 and 7.4 likewise.
- 4.1's "the token FIFO absorbs it" → the tokens and return queues.
- The units table's "8 areas", 6.2's A7 and 12's eight areas tied to the default geometry.
- `REGISTERS.md`: §0's 20-bit chunk address; 2.1.1's size field (still 512 KB areas at [31:29]); 2.1.2's bad packets (still "9–15, size 00"); 2.2's send-port prose (still `W1`–`W3` with the low word sending).

### j.6 Notes for the owner

- **`WORKER4_PROMPT.md` was removed as obsolete** (`…-26b`): it predated the peripheral slot and the 128-bit request.
- **4.17.1 states that every mesh tile holds a replayer (type 1),** so `TILE_STATUS` [15:12] reads 1 on tiles 0–63. To confirm. **Confirmed** (owner, 2026-10-27; part t).
- **`DESIGN-NOTES.md`** (not edited) still describes the 64-bit request, the 8-bit tree, the replayer's own registers, `replay_wait` and [31:18] reserved.

### j.7 Now stale elsewhere

- `sw/mesh.h`, `meshsim/sw/mesh.h` and every generated block of `REGISTERS.md` (j.4).
- `sw/CONVENTIONS.md`, `sw/RECIPES.md`: any mention of the request registers, the tile-index field or the replayer's registers.
- Figures: 1.4 (modes 8–10), 2.3 (the wake events), 2.4 (conditions 11–14), 3.2 (the register map and `TILE_STATUS`), 5.1 (the director: mode 8, misaligned mode 9), 8.1 (parameters); the new figure parts for the replayer (part f) and now the peripheral slot and the sender; the request-bus figure (16-bit, per-target queues, receive side).

## k. Modes, counting, commands, replies, the read port and one tile design (2026-10-26)

This pass applies the owner's part C (modes, counting, commands, replies and the read port) with the owner's decisions, and the one-tile-design items, to `spec/machine-spec.md` and the prose of `sw/REGISTERS.md`. Its input is `mesh-sorter-v1.1-2026-10-26b.tgz`. Scope as before: only what changes inside a tile. `sw/mesh.h` is **not** edited (it is generated; the constants are in k.4). The diff of `spec/` and `sw/REGISTERS.md` against the input is `deliverables/modes-counting.diff`.

### k.1 Owner rulings (2026-10-26)

1. **Modes renumbered:** 0 invalid; 1 gather request (never counted); 2 big-SRAM write by source; **3 big-SRAM write at an explicit address** (v1.1's 9; word address, aligned; now counted in `DMA_SRAM_REMAINING`); 4 bucket write (v1.1's 3); 5 augmented write (v1.1's 4; not 128-bit); 6 controller-memory write (v1.1's 5); **7 command write** (v1.1's 6, the mailbox write); **8 reply** (v1.1's 7); 9 peripheral write (v1.1's 8); 10–15 reserved. Groups: big SRAM 2–5, controller memory 6–8, the peripheral 9. **v1.1's mode 10 (the wake event) is removed.**
2. **Counting policy:** every mode is counted from any source, except command writes and gather requests; bad packets are dropped uncounted; **a halted tile counts nothing** and sets no unexpected arrival (boot loads); **counted data reaches a running tile only when it expects it** (staging and overlays go into a halted tile or follow a command that arms the counters). **If C&C needs an uncoordinated write, it halts the tile first.** The counters-done qualifier stays.
3. **No wake-event mode.** A request is answered by its bulk data, counted against what the requester armed, then, **only if it has a value answer, exactly one reply**. A read's answer is its data alone, so reads may be outstanding in any number. Write tokens, write acks and value answers (an fd, an address, an error code) are replies. **At most one reply-producing request per responder**; a write's token reply already serializes writes to one responder. Invariant 17 becomes **the request contract** (tag clauses removed, this rule added).
4. **The command block:** 16 words at the top of data memory (0x5FC0–0x5FFF), slot 0 the command word, slots 1–15 arguments (**even parity 1–7, odd 8–15**); command writes are 32 bits only (64/128 a bad packet), at slot addr[3:0] with the rest of addr ignored; only command writes write it.
5. **Replies:** 32 or 64 bits (128 a bad packet), landing **by source** at the reply block's word 2·src (addr unused, senders put 0); **a 32-bit reply writes word 0 and leaves word 1 alone**. The **reply block** is 256 words (0x5BC0–0x5FBF) directly below the command block; only replies write it; the stack below. Invariant 11 covers both blocks; invariants 12 and 13 are withdrawn.
6. **The read port** returns each result as **one mode-6 write to the tile itself**, counted in `DMA_CM_REMAINING`. `SRAM_RD_DEST_ADDR` is a setup register: destination [13:0], size [15:14] (32/64/128 bits). Each read fetches 1, 2 or 4 words aligned to the size; **a misaligned read is dropped and sets condition 7, renamed read-port error**. The split's search loop waits on `DMA_CM_REMAINING`.
7. **Packed controller-memory words are up to 14 bits,** words beyond the memories an error. **HIST counts on arrival only for modes 2, 4 and 5.**
8. **The replayer's LPDDR overflow** is fetched into big SRAM (mode 3) and refilled locally; the direct read into a queue port leaves its contract.
9. **Counter add aliases:** `DMA_SRAM_ADD`, `DMA_CM_ADD`, `DMA_REPLY_ADD`, `DMA_PERIPH_ADD` at **0x8A0–0x8AC** (write-only). A store adds *N* atomically, in step with arrivals (a same-cycle arrival is judged against count + *N*); **an add of *N* > 0 arms like a store** (clears [9] and the counter's done event, sets their qualifiers); an add of 0 does nothing; the hardware wraps past 2²⁰ − 1 and meshsim flags it. Unmapped now starts at 0x8B0.
10. **Per-counter done events** in the freed `WAKE_EVENTS` [3:0]: [0] `DMA_SRAM_REMAINING` done, [1] `DMA_CM_REMAINING` done, [2] `DMA_REPLY_REMAINING` done, [3] `DMA_PERIPH_REMAINING` done; [7:4] reserved. Each sticky, set on its own counter's transition to done (with `mem_idle` for SRAM, `cm_idle` for CM), with its own qualifier. [9] keeps its meaning. **The replayer's refill wait uses [3] alone** (the loop text changes only in naming that bit), so an LPDDR fetch in flight doesn't hold it up. This resolves the Open point raised in this pass (item 94).
11. **One tile design:** tiles are "identical in design; differing only in big-SRAM size and in what is attached and strapped".
    - **A receive queue in every tile:** 16 × 128 bits; the root holds no target queues, arbitrates and forwards with **one credit counter per target** (16, returned on each `REQUEST_RECEIVE` pop); HALT keeps the queue's contents. Reverses part j's "no storage in the tile".
    - **A fifth router port in every tile,** connected only where something is attached (today tile 0's, to C&C); an unconnected port never receives and is never chosen by routing.
    - **`TILE_PULSE` [3], GO,** in every tile, driving the global GO net only where wired (C&C); elsewhere ignored, meshsim flags it. C&C's READY and error inputs wait for the endpoint map.
12. **Not this pass (part D, recorded):** the UART, which was to start at 0x8A0, moves to **0x8B0–0x8BC**.

### k.2 Sections changed

**`spec/machine-spec.md`:** front matter (link word, modes, counting, blocks, read port, counter adds, GO, wake events, request bus); 2 (one tile design); 3.1 (mode table rewritten, groups, counting, bad packets, fixed payload bits); 3.3 (no root queues, credits, forwarding links); every mode number in 4.1–4.19 and 5–14 (mechanical renumbering, each hit reviewed); 4.1 (`mem_idle` and mode 3); 4.2 (stream modes 1–6 and 9; reserved 0, 7, 8, 10–15); 4.3 (rewritten for the setup register and mode-6 results); 4.5–4.8 (the fifth port generic; the director's table rewritten; counting in RUN only); 4.9 (staging rule); 4.11 (memory map with the two blocks, 14-bit packed words, the send port's addr steps: +1/+2/+4 for modes 3, 6, 9, +1 for mode 7, none for mode 8); 4.12 (rewritten first half: the command and reply blocks, the counting policy, the add aliases, per-counter done events, the event table, uses); 4.13 (condition 7, bad packets); 4.14; 4.16 (the fifth router port, generic); 4.17.5 and 4.19.10 (mode 9 counted from any source); 4.18 (receive queue in every tile, credits, HALT); 4.19.8–4.19.13 (no LPDDR reads into queues; the loop's [3]); 5; 6.1; 6.2 (staging only into a halted or expecting tile; the halt-first rule); 6.3 (command block, argument blocks 1–7/8–15, GO pulse); 6.7 (rewritten: answers without wake events, replies, arming, one reply-producing request per responder); 7; 8.1 (map: adds, unmapped from 0x8B0); 8.3 (condition 7); 8.6 (`TILE_PULSE` [3] GO); 8.7 (adds); 8.8 (setup register); 8.10 ([3:0], [7:4]); 8.11; 9 (invariants 4, 5, 11–14, 16, 17); 10, 11 (per-tile ≈ 0.38–0.45 mm² with the fifth port), 12; 13 (items 26, 66–69, 72, 78, 84, 85, 86, 94 updated; new 90–101); 14.

**`sw/REGISTERS.md`** (prose only): §0 intro; §1 (unmapped from 0x8B0); 2.1.1 (`landed`, `mem_idle`); 2.1.2 (read-port error, bad packets); 2.2 (modes); 2.3.2 (counting policy, add aliases, per-counter events; new empty marker `reg DMA_SRAM_ADD DMA_CM_ADD DMA_REPLY_ADD DMA_PERIPH_ADD`); 2.4 (setup register, mode-6 results); 2.5; 2.6 ([3:0], [7:4]); 2.7 (answers, receive queue and credits); 2.8 and 2.8.2 (counting, the loop's [3], LPDDR overflow via big SRAM); 3.4, 4.2, 4.3 (modes, packed words); **§5 rewritten** as the command block and the reply block; 6 (rows); 7.2–7.4 (example 3 now an exchange of by-source replies, example 4 with mode-6 results and `DMA_CM_REMAINING`). The generated blocks (including §5's `command` block, which still shows the mailbox's slots) are untouched.

### k.3 Items

Updated: 26 (superseded), 66, 67, 68, 69, 72, 78 (superseded), 84, 85 (reversed by 99), 86. New: 90 the renumbering, 91 the counting policy, 92 answers without wake events, 93 the two blocks, 94 the replayer's fetches and per-counter done events (decided), 95 the read port, 96 14-bit packed words, 97 the GO pulse, 98 HIST and mode 3, 99 the receive queue in every tile, 100 one tile design and the fifth port, 101 the add aliases.

### k.4 Handoff for the simulator session

Adds to parts c, e, f, i and j; where they disagree, this part wins.

| class | change |
|---|---|
| `Fields`, `LinkWord` | The new mode numbers (k.1, 1). Bad packets: 0, 10–15, mode 5 at 128, mode 3 misaligned, mode 7 above 32 bits, mode 8 at 128. |
| `TrafficDirector` | Route by the new table (spec 4.8). **Count every mode from any source except 1 and 7; count nothing and flag no unexpected arrival in HALT.** Mode 7 to command slot addr[3:0]; mode 8 to the reply block at 2·src (32-bit: word 0 only). Mode 3 counted in `DMA_SRAM_REMAINING`. Remove mode 10. |
| `WriteEngine`, count-on-arrival | Mode 3 counted but not histogrammed; `HIST` for 2, 4, 5. |
| `ReadPort` | Setup register (destination [13:0], size [15:14]); reads of 1, 2 or 4 aligned words; misaligned or overflow → dropped, condition 7 (read-port error); results as one mode-6 packet to self, counted in `DMA_CM_REMAINING`; queue entries 37 bits. |
| `TileControl` | `WAKE_EVENTS` [3:0] per-counter done events (sticky, own qualifiers, `mem_idle`/`cm_idle` as each counter's "done when"; cleared by arming the counter, done, HALT → RUN, consuming load; blocked by the done gate). Remove the tags. |
| counters | Add aliases at 0x8A0–0x8AC: atomic add, same-cycle arrival against count + *N*, *N* > 0 arms (clears [9] and the counter's event, sets qualifiers), 0 does nothing, wrap and flag past 2²⁰ − 1. |
| `Controller`, `Registers` | Map: adds at 0x8A0–0x8AC, unmapped from 0x8B0; `TILE_PULSE` [3] GO (ignored and flagged unless wired); condition 7's name. |
| `Router` | Every tile has the fifth route block, its five feeder FIFOs and its output; connected only at tile 0. |
| `RequestBus`, request port | No root target queues: per-target credits (16), forwarding links, skip on no credit; a 16 × 128 receive queue in every tile, a credit returned on each pop; HALT keeps the queue. |
| `MemorySystem` | Answers per spec 6.7: reads as counted data with no reply; token, ack and value answers as replies (mode 8) from src 64; one reply-producing request per responder checked. Deliveries counted at the tile. |
| `Chip`, C&C | Staging only into halted tiles or after an arming command; meshsim flags counted data to a running tile that hasn't armed (unexpected arrival does this). |
| `sort/` | Split: search results via `DMA_CM_REMAINING` and the setup register; probe keys as by-source replies; vectors as mode 6 outside the blocks. Keys all-to-all in mode 5, values in mode 2, local sorts' bucket writes in mode 4. Command argument blocks 1–7/8–15. |
| `RegDoc` | Regenerate everything with mode numbers or the mailbox, the read port's register, the counters (new marker in 2.3.2), `WAKE_EVENTS`, `TILE_PULSE`, the address map and §5's `command` block. |

**`sw/mesh.h` 1.1 constants** (generated from `Registers.java`, not edited here): `MODE_*` renumbered (`MODE_EXPLICIT` 3, `MODE_BUCKET` 4, `MODE_AUGMENT` 5, `MODE_CM` 6, `MODE_COMMAND` 7, `MODE_REPLY` 8, `MODE_PERIPH` 9; `MODE_WAKE` removed); `MAILBOX_BASE` → **`COMMAND_BASE`** (0x5FC0) and new **`REPLY_BASE`** (0x5BC0), `REPLY_WORD(src)` = `REPLY_BASE` + 8·src; `CMD_ARGS(w)` → slots 1–7 / 8–15; `SRAM_RD_DEST(dst, size)` with `SRAM_RD_SIZE_32/_64/_128`; `DMA_SRAM_ADD`, `DMA_CM_ADD`, `DMA_REPLY_ADD`, `DMA_PERIPH_ADD`; `WAKE_SRAM_DONE`, `WAKE_CM_DONE`, `WAKE_REPLY_DONE`, `WAKE_PERIPH_DONE` ([0]–[3]); `TILE_PULSE_GO` ([3]); `TILE_COND_READ_PORT` (condition 7's new name); `WAKE_TAG(t)` removed; the stack top moves to 0x5BC0 (linker script).

**Tests:** renumbered modes everywhere; bad-packet cases for modes 7 and 8; counting from C&C and in HALT; staging into a halted tile; add aliases (same-cycle arrival, arming at 0, wrap); per-counter events and their qualifiers; read-port sizes, alignment and condition 7; reply landing by source (32/64); command block bounds; receive queue and credits; fifth port unconnected on tiles 1–63; GO pulse ignored off C&C.

### k.5 Now stale elsewhere

`sw/mesh.h` and `meshsim/sw/mesh.h`; every generated block of `REGISTERS.md`; `sw/CONVENTIONS.md` (mailbox, stack, argument blocks), `sw/RECIPES.md` (modes, `xchg()`, `send_words()`, `search()`); `sort/` (modes, the read port, replies); `DESIGN-NOTES.md`; figures 1.4 (modes), 2.1/2.3 (wake events), 3.2 (map), 3.3 (read port), 5.1 (director), 8.1, the request-bus and router figures (credits, the fifth port everywhere), and examples 3 and 4.

## l. The UART, the replayer's split run log, decompression and best-effort residency (2026-10-26, part D)

Input: `mesh-sorter-v1.1-2026-10-26c.tgz`. Scope: inside a tile; the endpoint map and controllers stay deferred. Diff: `deliverables/part-d.diff`.

### l.1 Owner rulings (2026-10-26)

1. **UART in every tile** (spec 4.20, 8.13): 8N1; 16-byte TX and RX FIFOs *(Proposed)*; timed from the reference oscillator; `UART_DIVISOR` reset = round(`f_ref` / 57,600), `f_ref` an Open parameter (25 MHz → 434). Registers 0x8B0–0x8BC: `UART_TX` (W, stalls while full), `UART_RX` (R, [8] valid, pops only when valid), `UART_STATUS` (R/W, occupancies, [16] overrun and [17] framing error sticky, write 1 to clear, reads side-effect free), `UART_DIVISOR` (R/W, takes effect at each direction's next idle point). Overrun and framing errors drop the byte. On an unwired tile the registers decode as no-ops. HALT doesn't reset the UART; chip reset does. `UART_TX` is a third stall source. meshsim prints every tile's TX bytes to its console log. Unmapped now starts at 0x8C0.
2. **Wake events:** six service lines `PERIPH_SERVICE_1`–`6` at [18:13]; `REQUEST_PENDING` [18] → **[19]**; sender idle [19] → **[20]**; **[21] UART RX not empty, [22] UART TX not full** (both live, mask bits reset 0, consuming changes nothing); reserved [31:23]; mask, required and peek cover [22:0].
3. **The run log in three streams:** code, a **read log** (LD values, advice, the next `instr_idx` of non-sequential rows) and a **write log** (SD and AMO pre-values). *(Proposed:)* a row whose opcode is SD takes the write log, every other log row the read log.
4. **Queues:** four, all of **128-bit entries**: code 4 KB, read log 8 KB, write log 8 KB, expansion 4 KB; **ports take 128-bit packets only** (anything else a bad peripheral write); streams padded to a whole entry; refill units are entries (halves 128, 256, 256, 128). Read-log port 0x80000–0x9FFFF, write-log 0xA0000–0xBFFFF. The tracer is to measure the read/write split (item 103).
5. **The replayer's lines:** [13] code low-water, [14] read-log low-water, [15] write-log low-water, [16] expansion low-water (live), [17] new expansion (event, presented in `PERIPH_STATUS_1`), [18] done (event).
6. **Decompression** (new 4.19.15): per window and per stream, raw or a 16-entry true-LRU code (hit 1 + 4-bit index; miss 0 + size bit + 32/64 bits, a 32-bit miss zero-extended); the move-to-front update order as specified; tables reset at every run; bits least significant first within each entry, entries in order; raw code four 32-bit words per entry, raw logs two 64-bit words, low first; a pop past a stream's exact length is an invalid expansion. **Amendment (owner, 2026-10-26): the write-log table's entry 15 is a last-load entry** (every traced LD row copies its loaded value there; a hit at 15 reorders nothing; the LRU runs over entries 0–14; reset with the rest; the encoder prefers index 15 when a value matches both). The **stream word**: code length [23:0] and flag [31], read log [55:32] and [63], write log [87:64] and [95]. **Amendment (owner, 2026-10-26): region 0 is reordered state first** (state word 0x00000–0x00003, stream word 0x00004–0x00007, register image 0x00008–0x00107, each image word a register pair, low register low; in 128-bit words state 0, stream 1, image 2–65; decode table and ROM unmoved), and **a run latches the stream word** for the decompressors as it copies the state word and the image; replay never writes region 0. The tracer's checkpoint record, and a future tracer peripheral's state queue, emit this same order so a record streams straight into region 0 (item 108). **Further amendment (owner, 2026-10-26): the state word carries the per-run flags** in its spare bits, [112] code, [113] read log and [114] write log LRU-compressed and [115] **includes store log**, and the stream word keeps only the three lengths (code [23:0], read log [55:32], write log [87:64]). With [115] = 0, SD rows pop nothing and output a pre-value of 0, the write log is never fed and its low-water line stays quiet (the tracer emits 1). A run latches the four flags and three lengths. Recommended practice, with a worked example in `REGISTERS.md` 2.8.2: the controller loads or edits region 0 from data memory through its send port (`ROUTER_SET_HDR` = {me, mode 9, 0}, `DMA_PERIPH_ADD`, `ROUTER_SET_W0`–`W2` + `ROUTER_SEND_128` per word, wait on [3], `PERIPH_RUN`): a full checkpoint is 66 words (about 264 stores), a state-word change about seven.
7. **Expansion word:** [55] `untraced` (an invalid expansion if it reaches a replayer), [62:56] `advice_reg` (ignored by a replayer), [63] spare.
8. **ROM policy:** a cache of the program's most frequent templates, filled by the table builder; any instruction may be controller-fed; DIV/REM and the atomics may move out if full.
9. **Residency is best effort** (4.19.13): no fixed size; the rest streams from LPDDR through a big-SRAM staging buffer and is refilled locally.
10. **4.25 MB per tile** is a tentative working figure (item 107); the default big-SRAM size is unchanged.

### l.2 Sections changed

**Spec:** front matter (UART, six lines, three streams); units table; 4.11 (stalls); 4.12 (event table [18:13]–[22], live cases, uses); 4.17 (six lines, sender idle [20]); 4.18 ([19]); 4.19.1, 4.19.2 (three streams, densities split [derived]), 4.19.3 (expansion word), 4.19.6 (ROM policy), 4.19.7 (stalls), 4.19.8 (four queues, 128-bit entries and ports), 4.19.9 (six-line table, loop's `PERIPH` = [18:13]), 4.19.10 (map: stream word, two log ports, 128-bit only), 4.19.11 (invalid expansion causes, four queues), 4.19.13 (best effort), 4.19.14 (about 41 KB per tile), **4.19.15 new**; **4.20 new (UART)**; 6.1 (HALT); 6.3; 8.1 (map, families); 8.10; 8.11; **8.13 new**; 10, 11; 13 (items 81, 85, 86, 87 noted; new 102–107).

**`REGISTERS.md` prose:** §1 (families, unmapped from 0x8C0); 2.6 (event table, coverage [22:0]); 2.7 ([19]); 2.8 ([18:13], sender idle [20]); 2.8.2 (three streams, lines, queues and stream word, map, loop, refill sizes, best-effort residency); **2.9 new (UART)** with an empty marker `reg UART_TX UART_RX UART_STATUS UART_DIVISOR`; section 6 row.

### l.3 Handoff for the simulator session

| class | change |
|---|---|
| new `Uart` | FIFOs, divisor timing against a reference-oscillator clock, framing and overrun, wired/unwired, the console print for every tile's TX, HALT behaviour |
| `TileControl` | events [19]–[22] moved and added; coverage [22:0] |
| `PeripheralSlot` | six lines |
| replayer model | four 128-bit-entry queues (128-bit ports only); read/write log split by row kind; three `Decompressor`s (LRU exactly as 4.19.15; the write log's last-load entry E[15], updated by every traced LD row's read-log value, hit 15 without reordering, LRU over E[0]–E[14]; raw formats; bit order; length counters; pop past end → invalid expansion); region 0 reordered (state 0x00000, stream word 0x00004, image 0x00008–0x00107); the run latches the state word's flags [115:112] and the stream word's three lengths; includes-store-log 0 → SD rows pop nothing, pre-value 0, write log unfed, its low-water quiet; `untraced` → invalid expansion; tables reset at run |
| `Registers`, `RegDoc` | UART registers; map to 0x8BC; regenerate the event tables, the replayer's map and the new 2.9 block |

`mesh.h` constants: `UART_TX`, `UART_RX`, `UART_STATUS`, `UART_DIVISOR`, `UART_RX_VALID`, `UART_ST_OVERRUN`, `UART_ST_FRAMING`; `WAKE_REQUEST_PENDING` = [19], `WAKE_SENDER_IDLE` = [20], `WAKE_UART_RX` = [21], `WAKE_UART_TX` = [22], `WAKE_PERIPH(n)` for n = 1–6. Tests: UART on wired and unwired tiles, stall, errors, divisor change at idle; bit moves; the LRU decoder against a reference encoder (including the last-load entry: an LD then an SD of the same value, a value in both E[15] and the LRU coded as 15, non-LD log rows leaving E[15] alone), raw and compressed streams, past-end; queue ports rejecting 32/64-bit packets.

### l.4 Now stale elsewhere

`sw/mesh.h`, the generated `REGISTERS.md` blocks, `DESIGN-NOTES.md`, the tracer engine's encoder spec (must match 4.19.15), and figures 2.3 (wake events) and 3.2 (map), plus the replayer figures.

## m. Additive counters, the renumbered register map and register-load latency (2026-10-26, part E)

Input: `mesh-sorter-v1.1-2026-10-26g.tgz`. Diff: `deliverables/part-e.diff` (`spec/machine-spec.md` and `sw/REGISTERS.md` against 26g).

### m.1 Owner rulings (2026-10-26)

1. **Counter writes are additive.** The add aliases `DMA_SRAM_ADD`–`DMA_PERIPH_ADD` (0x8A0–0x8AC) are removed. A write of *N* to any `DMA_*_REMAINING` counter adds *N* atomically, in step with arrivals: `next = count + N − arrive`, a same-cycle arrival judged against count + *N*, a write of 0 does nothing, a write of *N* > 0 arms counters done and the counter's own done event and sets their qualifiers; a sum past 2²⁰ − 1 wraps, and meshsim flags it. **A counter is never overwritten:** it returns to 0 only by counting down, at chip reset or **on entering HALT, which now clears all four counters, their done events and their qualifiers** (they stay 0 while halted). Writes to a non-zero counter are legitimate (pipelined top-ups) and are **not** flagged. At 0, a write is v1.1's store, so existing arming code is unchanged.
2. **The register map is renumbered,** every family contiguous with no holes; 0x800–0x828 unchanged:

| offset | register | was |
|---|---|---|
| 0x82C | `DMA_RECORD_BASE_ADDRESS` | 0x838 |
| 0x830 | `DMA_SRAM_REMAINING` | 0x82C |
| 0x834 | `DMA_CM_REMAINING` | 0x830 |
| 0x838 | `DMA_REPLY_REMAINING` | 0x834 |
| 0x83C | `DMA_PERIPH_REMAINING` | 0x854 |
| 0x840 | `SRAM_RD_ADDR_QUEUE` | 0x83C |
| 0x844 | `SRAM_RD_DEST_ADDR` | 0x840 |
| 0x848 | `WAKE_EVENTS` | 0x844 |
| 0x84C | `WAKE_PEEK` | 0x874 |
| 0x850 | `WAKE_MASK` | 0x848 |
| 0x854 | `WAKE_REQUIRED` | 0x870 |
| 0x858–0x86C | `REQUEST_*`, `SENDER_*` | unchanged |
| 0x870–0x88C | `PERIPH_STATUS_0`–`7` | 0x880–0x89C |
| 0x890–0x89C | `UART_TX`, `UART_RX`, `UART_STATUS`, `UART_DIVISOR` | 0x8B0–0x8BC |
| 0x8A0– | unmapped (bus error) | |

   This reverses part j's "no renumbering". The old reserved holes (0x84C, 0x850, 0x878, 0x87C) are now registers.
3. **Register-load latency is implementation-defined:** a load from the register block or the **table window** takes 1 or 2 cycles, **fixed per register for an implementation, the same on every access and tile**, never data- or traffic-dependent (so synchronised starts stay identical). **Sampling rule:** a load at *t* with latency *L* returns the state at the end of *t* + *L* − 2 and takes its side effect at the end of *t* + *L* − 1, removing exactly what it returned (*L* = 1 is v1.1's rule). **Stores** are single-cycle and posted: the core doesn't wait, a store takes effect at the end of its cycle, and accesses complete in program order. The three stalling stores keep stalling. **meshsim models 2** for every register and table-window load; 2 is the reference setting. Loads from controller memory are unchanged.

### m.2 Sections changed

**Spec:** front matter (three new bullets; the register line); 4.3 (measurements tagged); 4.11 (the latency, sampling, posted-store and stall rules, replacing "the only extra cycles"); 4.12 (additive writes, never overwritten, HALT clearing, the same-cycle rule restated against the sample point, the helper load's latency; the add-alias bullet and the "store wins" rule removed); 4.17 and 4.17.4, 4.17.5 (offsets, sample point); 4.18 (timing restated); 4.19.10 (region-0 loading armed by a counter write); 4.20; 6.1 (entering HALT clears the counters); 6.3 (the load after `WFI` is fixed-latency); 6.7 (arming by writing the count); **8.1 rewritten** (map, family ranges, timing rule, no holes); 8.2, 8.7 (heading, alias row removed, write semantics), 8.8, 8.10, 8.11, 8.13 (offsets); 10 (`reg_load_latency`); 13 (items 86, 101, 102 noted; new 109–111).

**`REGISTERS.md` prose:** §0 timing; §1 (rule 4: no holes; the families paragraph with the new ranges); 2.3.2 (additive writes, HALT clearing; the aliases' heading and generated marker removed); 2.6 and 2.8 offsets; 2.7 (writes add); 2.8.2's worked example (`DMA_PERIPH_REMAINING = n`, which adds); 2.9 offsets; section 6 rows. The generated map tables still show the old offsets until RegDoc is rerun.

### m.3 Audit: code that relied on overwriting a counter

A static review of every counter write in `sort/` (about 25 sites in `split/split.c`, `program/sw/{local,exchange,transfer,bt_plan,bt_run,skew,main}.c`) and `sw/RECIPES.md` (about 19 sites) found **none that overwrites a non-zero count:** each is the first store of a phase or step, made after the previous `WFI` (or before the start pulse, with the counter already done), and several carry comments to that effect ("first: no word can arrive before it"). No write of 0 is used to cancel a count. So additive writes change none of them. (The same code is stale for other reasons: parts C and D's modes, by-source replies and the read port.)

### m.4 Handoff for the simulator session

- **Counters:** remove the alias registers; a write to a counter adds (same-cycle arrival against count + *N*; *N* = 0 no-op; *N* > 0 arms [9] and the counter's event; wrap flagged); clear all four counters, their events and qualifiers on the RUN → HALT edge; don't flag writes to a non-zero counter.
- **Map:** the moves in m.1 (2); unmapped from 0x8A0; no reserved holes.
- **Load latency:** a `reg_load_latency` parameter per register (reference 2 for all, register block and table window), with the sampling rule for side-effecting loads (`WAKE_EVENTS`, `REQUEST_RECEIVE`, `UART_RX`); stores unchanged; program order kept. Re-measure SearchBench, SplitRun and every [meas] figure with controller register loads at 2.
- **`mesh.h` 1.1** (generated): the new offsets for `DMA_RECORD_BASE_ADDRESS`, the four counters, `SRAM_RD_ADDR_QUEUE`, `SRAM_RD_DEST_ADDR`, `WAKE_EVENTS`, `WAKE_PEEK`, `WAKE_MASK`, `WAKE_REQUIRED`, `PERIPH_STATUS(n)`, the UART; `DMA_*_ADD` removed. `sort/split/split_regs.h` hard-codes addresses and must be rebuilt.
- **`RegDoc`:** regenerate the map, the summary and every `reg` block with an offset.

## n. The replayer's opcode semantics (2026-10-26, part F)

Input: `mesh-sorter-v1.1-2026-10-26h.tgz`. Diff: `deliverables/part-f.diff` (`spec/machine-spec.md` and `README.md` against 26h; `spec/replayer-opcodes.md` and `tools/replayer-opcodes/` are new files).

### n.1 Owner rulings (2026-10-26)

1. **`spec/replayer-opcodes.md`** (new): the appendix derived from the jolt-cpp tracer at commit 4f36b3d, with its text unchanged except a new header. **Sections 1–5 are normative for what each row computes** (slots, rd, control, log word, LD/SD memory fields); sections 6–9 are informative. 4.19.7's "what `execute_instruction` computes" paragraph now points to it; 4.19.7 keeps the classes, schedules and pc rules, 4.19.12 the record layout (item 112).
2. **JAL with imm 0:** `imm0_pc0` stays table data, and the template builder sets it in **every JAL word, ROM or controller-fed** (4.19.3, 4.19.7, item 56).
3. **EBREAK** stays controller-fed; its one word is **JAL, rd = temporary 0 (v40), imm 0, `imm0_pc0` and `next_from_value` set** (4.19.5, 4.19.4).
4. **A new pc of 0 ends the program from any row** (JAL with imm 0, JALR to 0, a branch wrapping to 0): pc = 0, `instr_idx` = 0 (4.19.7's pc table, item 57).
5. **LD's memory value comes from rd after the row,** so an LD to x0 (LR.D with rd = x0) reports 0 (4.19.7, 4.19.12).
6. **Extension is per opcode:** 4.19.7's "the W forms sign-extend their low 32 bits" is replaced by a pointer to the appendix; several word forms zero-extend.
7. **Masks:** the masks reaching `VirtualSRL`, `VirtualSRLI`, `VirtualSRA` and `VirtualSRAI` are never 0, and those reaching `VirtualPext` and `VirtualPextSigned` are contiguous and non-zero (the tracer guarantees both). The replayer builds only those cases; any other mask gives an undefined result; meshsim flags it (4.19.7, invariant 16, item 43).
8. **rd = x0 (4.19.4):** the remapped link is written and stays in v40; the exempt instructions emit rows that name x0 (`LD x0`, `VirtualPextSigned x0`, `XORI x0`); EBREAK's single row is `JAL v40, 0`.
9. **Items:** 34 partly addressed (the appendix's section 6); 36 and 37 decided, citing the appendix, except 36's temporaries clause (Proposed) and 37's multiply/MULHU density measurement (Open); 43 decided; 56 and 57 gain the two rules; new item 112.
10. **The harness** goes in the package as `tools/replayer-opcodes/`, with a README on building it against the tracer and regenerating the tables for a later commit.

### n.2 Sections changed

**Spec:** front matter (the replayer bullet); 4.19.3 (`imm0_pc0`); 4.19.4 (the remap and exemption paragraph); 4.19.5 (EBREAK's word, before the controller-fed paragraph); 4.19.7 (the computation paragraph, the pext departure, the edge-case paragraph replaced with pointers, zero shift masks, LD's memory fields, the pc table's JAL row and new row for pc 0, the hardwired list, EBREAK in the transfer list); 4.19.12 (two LD wordings); 9 (invariant 16); 13 (items 34, 36, 37, 43, 56, 57; new 112). **`README.md`:** the `spec/` row and a `tools/` row. **New:** `spec/replayer-opcodes.md`, `tools/replayer-opcodes/` (`enum.cpp`, `vec.cpp`, `enum.txt`, `summary.txt`, `sites.json`, `vec.tsv`, `summarize.py`, `render/`, `README.md`).

### n.3 Handoff for the simulator session

- **meshsim's ALU tests** take their vectors from `spec/replayer-opcodes.md` section 9 (machine-readable as `tools/replayer-opcodes/vec.tsv`): about 240 rows computed by the tracer's own `execute_instruction`, covering 0, −1, INT64_MIN, INT32_MIN for the word forms, shift amounts 31, 32, 63 and 64, MULHU at the top of the range, self-jumps, program end at pc 0, and every assertion's pass and fail. A replayer model must reproduce every non-ABORT row's rd, new pc and next-step outcome.
- **The replayer model** computes each row per the appendix's sections 1–3, with the two narrowings: flag a zero mask at the 64-bit right shifts and a zero or non-contiguous mask at `VirtualPext`/`VirtualPextSigned` (invariant 16), and form LD's memory value from rd.
- **pc:** end the program on a new pc of 0 from any row; treat `imm0_pc0` as set on every JAL word (check the templates do set it); EBREAK's controller-fed word is `JAL v40, 0`.
- **The template generator** (item 34) can take its slot forms and producers from the appendix's section 6 (`summary.txt`, `sites.json`).

## o. Ground truth, template numbering, the state record and the replayer-model rulings (2026-10-26, part G)

Input: `mesh-sorter-v1.1-2026-10-26i.tgz`. Diff: `deliverables/part-g.diff` (`spec/machine-spec.md`, `spec/replayer-opcodes.md`, `sw/REGISTERS.md` against 26i).

### o.1 Owner rulings (2026-10-26)

1. **Ground truth** (4.19, item 113): upstream Jolt, meaning **`LayerZero-Research/jolt` at `735355e43026e9ef392b36b4ab5ed0f5e680127f`**, the version our baseline is built against (vendored by jolt-cpp at `309b4c4`; jolt-asic-planning `docs/provenance.md`), is the authority for the trace's content; later upstream versions, `a16z/jolt` HEAD included, are tracked as future compatibility (clarified in `…-26k`) (the 80-byte row, the pc and `instr_idx` sequence); jolt-cpp counts only where shown to match; our own formats are ours provided the rows match. `spec/replayer-opcodes.md` stays normative for the replayer as built and is corrected where upstream differs. 4.19.12, invariant 16 and items 58 and 108 are reworded to match; the appendix's header gains a line.
2. **Tags:** each jolt-cpp-derived rule under the upstream check carries *(pending the upstream check, item 113)*: a new pc of 0 ending the program (4.19.7's pc table and hardwired list, item 57), LD to x0 (4.19.7), the rd = x0 remap (4.19.4's rule and its "what the remap produces" paragraph), EBREAK (4.19.5) and padding (4.19.2's end of trace). The tags come off when the check reports. **N6** (whether a JALR or branch to 0 logs a next-index word of 0 and ends the program) waits for it; the text is unchanged.
3. **Template numbering** (4.19.5, item 114): the builder numbers controller-fed templates 0–127; the template set is the words plus an index (number, word offset, length); an instruction maps by its exact instruction word; numbers may share words, so the 128 limit counts index entries; a tile trimming to its bitmap keeps every word any of its numbers references. To be measured: distinct controller-fed instruction words per program.
4. **The tracer's state record** (4.19.2, 4.19.10, item 108): word 0 the window's template bitmap, words 1–66 the region-0 image; a controller sends it from byte 16; region 0 unchanged.
5. **The chip's used set** (item 115, Proposed, with item 82): OR the 64 bitmaps, broadcast once as a coordinated transfer, each tile keeps what its bitmap names; for now every tile preloads the whole set (4.19.5's controller-fed paragraph).
6. **N3** (4.19.3, 4.19.4): rd = x0 handling applies to every expansion word, ROM or controller-fed; `rd0` is read from the first word delivered; on a mid-sequence start the controller copies the template's `rd0` into the first word it sends; under a NOP policy with rd = x0 the controller supplies the single NOP word.
7. **N5** (4.19.15): on every traced LD row, write-log E[15] takes the value popped from the read log, an LD to x0 included, stated beside F4 (whose record reports 0).
8. **N11** (4.19.12): serialized, the replayer's record is 80 bytes, words 0–4 in order, each 128-bit word little-endian; our layout, not `RiscCycle`'s field order.

### o.2 Handoff for the simulator session

- **The controller loop and meshsim's checkpoint loads** skip the state record's first 16 bytes (the template bitmap).
- **Controller-fed expansions:** the replayer model reads `rd0` from the first delivered word and applies the offset as for ROM words; the controller copies `rd0` on a mid-sequence start and sends the NOP word under a NOP policy with rd = x0.
- **Template numbers:** the controller maps `PERIPH_STATUS_1`'s exact instruction word to a number (0–127) through the program's index; preload the whole set on every tile.
- **The tracer's encoder** (and the reference decoder in meshsim): E[15] takes an LD's popped value even for rd = x0.
- **Record output:** serialize each row as words 0–4, each little-endian.
- **Validation (item 58):** compare against jolt-cpp where it matches upstream; treat the tagged rules as provisional.

## p. Ground truth finished: jolt-cpp as the gold standard, and the potential differences (2026-10-26, part H)

Input: `mesh-sorter-v1.1-2026-10-26k.tgz`. Diff: `deliverables/part-h.diff` (`spec/machine-spec.md`, `spec/replayer-opcodes.md` against 26k). The version pin from 26k (upstream = `LayerZero-Research/jolt` at 735355e) is unchanged.

### p.1 Owner rulings (2026-10-26)

1. **jolt-cpp is the gold standard for now:** the replayer, its tests and the reference rows match jolt-cpp; upstream at 735355e overrides jolt-cpp only once a difference is confirmed by running both, and then the appendix (and if need be jolt-cpp) is corrected. Applied in 4.19's ground-truth paragraph (which now points to the tagged rules instead of listing them), item 113, 4.19.12's record values, invariant 16, item 58 and the appendix header.
2. **Potential differences recorded in item 113, each unconfirmed:** LR/SC with rd = x0 (upstream may remap to v40; F4 and N5 depend on it); the shared NOP row's virtual flag under `rd0` = 1 (item 35's question); the self-jump rule (differs at a16z HEAD, probably unobservable at 735355e); a new pc of 0 from a JALR or a branch (N6; undetermined upstream). **Future compatibility only:** a16z HEAD (`bd9aef4`) adds the runtime advice tape (`AdviceLB/LH/LW/LD` → `VirtualAdviceLoad`) and defines but never emits `VirtualPow2I`/`VirtualPow2IW`.
3. **The plan (Open, item 113):** guest programs per potential difference, run through Rust Jolt at 735355e and jolt-cpp, rows diffed; decide per case whether jolt-cpp or the spec changes; remove the tags as each settles.
4. **Tags** *(pending the upstream check, item 113)*, existing ones kept, added at: 4.19.4's exempt list (LR/SC) and its `rd0` = 1 NOP-policy sentence; 4.19.3's `rd0` row; 4.19.7's self-jump rule and the hardwired "self-jump compare"; item 57's self-jump clause; item 35; 4.19.15's N5 bullet; 4.19.12's LD `mem_value` wording; the appendix header's F3 entry (the appendix body stays verbatim). The appendix header's F4 entry notes its dependence on the LR/SC question.

### p.2 Handoff for the simulator session

- **Reference rows are jolt-cpp's.** meshsim's replayer tests and the validation harness (item 58) compare against jolt-cpp; the tagged rules may move once the guest-program check confirms a difference.
- **The guest-program check** (item 113) is a separate task: small RISC-V programs for LR/SC with rd = x0, an rd = x0 instruction under the NOP policy (the `virt` flag), a JALR that overwrites its base and jumps to itself, and a JALR or branch to address 0; each traced by Rust Jolt at 735355e and by jolt-cpp, with the rows diffed.


## q. The controller's NOP word carries rd0 (2026-10-26, part I)

Input: `mesh-sorter-v1.1-2026-10-26l.tgz`. Diff: `deliverables/part-i.diff` (`spec/machine-spec.md` against 26l).

### q.1 Owner ruling (2026-10-26)

1. **4.19.4, N3's NOP-policy bullet:** the single NOP word the controller sends under a NOP policy with rd = x0 **carries the template's `rd0` (1 or 3) in bits [47:46]; otherwise it is ROM word 1's.** So the NOP row's virtual flag comes out as on the ROM path: `rd0` is read from the first word delivered, and the NOP word is the only word delivered. The sentence sits under the `rd0` = 1 NOP policy and carries its *(pending the upstream check, item 113)* tag.

### q.2 Handoff for the simulator session

- The controller's NOP word is ROM word 1 with the template's `rd0` in [47:46]; the replayer model derives the NOP row's virtual flag from that `rd0`, exactly as on the ROM path (from ROM[start]).


## r. Counter writes add: leftovers fixed (2026-10-26, part J)

Input: `mesh-sorter-v1.1-2026-10-26m.tgz`. Diff: `deliverables/part-j.diff` (`spec/machine-spec.md`, `sw/REGISTERS.md` against 26m).

Since part E every write to a `DMA_*_REMAINING` counter adds (4.12, 8.7). Places that still described a write as setting the count, fixed:
- **8.11:** `DMA_PERIPH_REMAINING` said "a store of *N* sets the count"; it now gives the additive rule, arming, the write of 0, the wrap and the return to 0 only by counting down, chip reset or entering HALT.
- **Spec wording that said "set" or "store" the counters:** 4.3 (a phase that *armed* `DMA_CM_REMAINING`), 4.12's uses list, 4.19.9's controller loop (`write DMA_PERIPH_REMAINING (adds)`), 6.4's start-path rule, 6.5's block-copy steps, and 14's one-word totals.
- **`sw/REGISTERS.md`:** 2.3.2's counters-done clearing (by any *non-zero* counter write) and its arming advice; 7.3's note (the counter already *armed*); 2.8 (a write arms `DMA_PERIPH_REMAINING`); 2.8.2's controller loop.

The listings that write a counter at a phase start (`DMA_X_REMAINING = n;`) are unchanged: the count is 0 there, so the write adds to 0. No other description of a counter write as a set remains in the spec or in `REGISTERS.md`'s prose (the generated blocks are RegDoc's).


## s. Fix K10: the write stream's mode in 4.2 (2026-10-26)

Input: `mesh-sorter-v1.1-2026-10-26n.tgz`. Diff: `deliverables/part-k.diff` (`spec/machine-spec.md` against 26n).

- **4.2's reserved-encodings bullet** said a stream with *d* = 64 is an error "with a mode other than 9"; it now says **3**, matching the `STREAM_DEST` table and invariant 17. A leftover from part k's renumbering (v1.1's mode 9, the explicit-address write, is mode 3). No other stale reference to the write stream's mode remains in the spec or `REGISTERS.md`'s prose.


## t. Part K: corrections from the meshsim and tracer sessions (2026-10-27)

Input: `mesh-sorter-v1.1-2026-10-26o.tgz`. Diff: `deliverables/part-k2.diff` (`spec/machine-spec.md`, `sw/REGISTERS.md` against 26o). K10 is in part s.

### t.1 Corrections and rulings

1. **K1 (4.13, condition 1):** the bad-packet list now matches 3.1, 4.8 and invariant 5: mode 0 or 10–15, mode 5 at 128 bits, a misaligned mode 3, a mode-7 command write above 32 bits, a mode-8 reply at 128 bits.
2. **K2 (4.1, 4.5):** read-port results are mode-6 controller-memory writes (three places said mode 8).
3. **K3 (section 5):** the controller row gives register and table-window loads 1 or 2 cycles (fixed per register; meshsim 2) and adds `UART_TX` to the stalls; the registers row states 4.11's sampling rule.
4. **K4 (4.17.5):** the fixed `rp_latency` is what lets `DMA_PERIPH_REMAINING` count a write on landing, so a count of 0 proves landing; the withdrawn wake-event clause is gone. 4.17.8 and 4.19.10 needed no change.
5. **K5 (4.17.1):** every mesh tile is type 1, confirmed by the owner (2026-10-27); part j.6's note is annotated.
6. **K6 (4.20, 8.13, `REGISTERS.md` 2.9):** on an unwired tile `UART_DIVISOR` reads its reset value; writes stay ignored.
7. **K7 (4.19.5):** the code stream carries whichever word the tracer's bytecode decodes, and the decode table and template index are built for it: under the raw-ECALL host-I/O encoding, the canonical `VirtualHostIO x0, x0, 0` word **0x0000205B** (custom-2 funct3 2, a one-word template under the rd = x0 remap, tagged), not ECALL's word; ECALL's own entry stays controller-fed. This replaces the old sentence that pointed ECALL's entry at the `VirtualHostIO` template.
8. **Gold standard (4.19, item 113):** jolt-cpp's **interpreter rows** (`execute_instruction`, `expand_compressed_chunk`); its JIT is not a reference, and its disagreements are findings.
9. **K8, K9 (item 113), unconfirmed against 735355e:** jolt-cpp's JIT executes a self-targeting JALR twice (8 cycles against the interpreter's 7 rows), agreeing with a16z HEAD; its JIT crashes on a JALR to 0, which the interpreter handles; a branch wrapping to 0 can't be built as a guest (±4 KiB reach, code at 0x80000000) and can only be checked at the unit level.

### t.2 Handoff for the simulator session

- **Bad packets:** meshsim's test covers the full condition-1 list (modes 0 and 10–15, mode 5 at 128, misaligned mode 3, mode 7 above 32 bits, mode 8 at 128).
- **Read-port results** are mode 6, counted in `DMA_CM_REMAINING`.
- **UART:** an unwired tile's `UART_DIVISOR` reads back its reset value.
- **Host I/O:** the code stream's host-I/O word is 0x0000205B; build the decode table and template index for it.
- **Reference rows** come from jolt-cpp's interpreter, not its JIT.


## u. Part L: an explicit zero-immediate operand code (2026-10-27)

Input: `mesh-sorter-v1.1-2026-10-26p.tgz`. Diffs: `deliverables/part-l.diff` (`spec/machine-spec.md`, 26p to 26q) and `deliverables/part-l2.diff` (the count correction, 26q to 26r).

### u.1 Ruling

1. **Operand code 145 is "immediate 0"** (4.19.3): an immediate slot whose value is 0, independent of `const` and of the source instruction. Reserved codes become 146–255 (4.19.3; condition 13 in 4.19.11). A word holds one constant, so a row with two immediate slots, a non-zero constant and a zero, writes the zero as 145, in ROM and controller-fed words alike: secp256k1's `LUI rd, #0x1000003d1, #0` in the controller-fed templates and `LUI v40, #0x80000000, #0` as the first ROM row of the LR/SC and AMO templates, **17 rows in EVM Zone's tables and 28 in Cyclone's** (15 and 26 controller-fed, plus 2 ROM each). *(Corrected in `…-26r`: 26q's "15 and 26" counted only the controller-fed rows.)* A slot that is 0 in every instance, with no other constant, is written as code 128 with `const` = 0, as the tracer now does. Code 128 with `const` = 0 stays valid; the old practice of borrowing code 132 (which reads 0 only for R-format templates) still replays correctly, so the tracer can switch at any time. Item 34 records the finding.

### u.2 Handoff

- **The tracer's template builder** writes 145 for a zero immediate slot beside a non-zero constant, in ROM and controller-fed templates alike, and code 128 with `const` = 0 for a slot that is always 0 with no other constant in the row.
- **meshsim and the replayer model** accept 145 as an immediate slot of value 0 (`rsN_is_imm` set, value 0, contributing 0 to the row's immediate) and treat 146–255 as invalid expansions.


## v. Part M: measurements from meshsim (2026-10-27)

Input: `mesh-sorter-v1.1-2026-10-26r.tgz`. Diff: `deliverables/part-m.diff` (`spec/machine-spec.md` against 26r). Delivered as `mesh-sorter-v1.1-2026-10-27a.tgz`.

### v.1 Rulings and records

1. **M1 (6.4):** GO to every tile's first request at 2-cycle register loads [meas: meshsim]: **12–13 cycles** for a stream run (8–9 to the start store) and **20–21** for a group run (13–14); a shortest-path group run 15–16 [derived]. v1.0 measured 9–10 and 17–18 with 1-cycle loads; the three extra cycles are GO's event becoming visible one cycle after GO is seen (the event pulse's flop into the island) and `wait_for_command()`'s two 2-cycle loads.
2. **M2 (6.2, section 10):** announced staging waits for the arming: C&C sends the first staged packet no sooner than **`stage_delay` = 32 cycles** after its GO store *(Proposed)*. The tile arms about 10 cycles after GO [derived]; the bound is tied to the start path, and a command whose start path arms later says so. A packet that beats the counter is an unexpected arrival.
3. **M3 (4.3, item 116):** meshsim measures no loss when read-port reads overlap an all-to-all or a full-rate stream (v1.0 lost 561 packets). The rule and invariant 14 are unchanged; recorded as a candidate to relax.
4. **M4 (3.3, sections 5 and 10, item 61):** the request bus measures **17 cycles** unloaded, inside the 15–20 estimate.

### v.2 Handoff for the meshsim session

- The staging harness can wait `stage_delay` = 32 cycles after GO instead of 64; please report the exact cycle at which the receiving tile's counter write lands, to replace the derived "about 10".
- For item 116, report what explains the change from v1.0's 561 lost packets (expected: the protected loopback and the eject priority, items 24 and 70).
