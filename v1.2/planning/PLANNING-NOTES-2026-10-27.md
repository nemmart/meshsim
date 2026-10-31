# Mesh sorter v1.1: planning notes, 2026-10-27

These notes collect what the planning session of 2026-10-24 to 2026-10-27 decided, measured and left open,
so the next revision can start from one place. They sit beside the package's own records:
`spec/machine-spec.md` (the authority), `CHANGES-v1.1.md` (parts a-v), `SIM-CHANGES-v1.1-26n.md`,
`DEVIATIONS.md`, `RESULTS-full-size.md`, `RESULTS-n2000.md` and `deliverables/`.

Labels follow the repository's convention: M measured, D derived, E estimated, O owner decision.

The chip-level design (controllers, engines, memory system, tracer as a peripheral) is agreed in outline but
**not yet in the spec**; section 5 summarizes it. The Claude Doc "Controllers, memory and tracer: design
notes v2" (2026-10-25) has the long form, but it predates parts A-M and the engine renumbering, so where it
and these notes differ, these notes win.

---

## 1. Milestone status

The milestone was: **the sort runs on the new spec, and the replayer reproduces real traces when all state
is preloaded.** Both are met.

### 1.1 The sort on meshsim v1.1

| check | result |
| --- | --- |
| quick suite (35 suites, shuffled runs) | all pass |
| kit, 92 runs at 64 x 2,000 | all pass; shuffled runs equal fixed-order runs; sweeps within -0.27% to +0.86% |
| 43 full-size p4 runs, 64 x 65,536 (runner task 065, 21 min at -j 8) | all pass; all four paths occur naturally (BT on the trace sets and few5, TH, LB, direct) |
| full-size suites (runner task 067) | CtrlTest full 7,167 passed; EngineTest full 383 passed; skew table and full sweep clean |
| forced BT, 64 x 16,384, trace hot 10/10% | exact in both formats |

Totals against v1.0 (cycles from L1 GO to L2 all READY, M):

| set | v1.0 | v1.1 | change |
| --- | --- | --- | --- |
| uniform-128 (direct) | 257,089 | 258,194 | +0.4% |
| uniform-32 (direct) | 237,054 | 241,114 | +1.7% |
| reverse-128 (TH) | 287,938 | 289,132 | +0.4% |
| forced BT, 32-bit values | 701,028 | 709,658 | +1.2% |
| forced BT, 128-bit values | 998,040 | 927,893 | -7.0% (a 128-bit value is now one packet) |

The small rises come from 2-cycle register loads in the split's tight loops. Full-size p4 totals are about
0.71-1.36M cycles (32-bit values) and 1.17-2.03M (128-bit), with the comparison in `RESULTS-full-size.md`.

### 1.2 Replay

**Stage A** (the replayer model with an ideal controller): 240 window runs, 15,728,640 rows (about 80
distinct windows, each in three stream forms: raw, all-LRU, the tracer's pick), **byte for byte** against
jolt-cpp's interpreter rows, across the five item-113 guests, EVM Zone (trace-23, trace-29 at 2^29) and
Cyclone (input/25, input/29 at 2^29). Every run consumed each stream exactly and ended in the tracer's end
state.

**Stage B** (the real C controller on a meshsim tile, all state preloaded in a 2 MB big SRAM): exact on every
window tried. Cycles per row (M):

| window | controller-fed rows | Stage A | Stage B, first controller (cp9) | Stage B, fast path (cp12/cp14) |
| --- | --- | --- | --- | --- |
| EVM Zone 2^29, window 53 | 62% | 3.270 | 3.814 | 3.479 |
| EVM Zone 2^29, window 54 | 61% | 3.272 | 3.825 | 3.492 |
| Cyclone input/25, window 78 (also 83, 84) | 83% | 3.006 | 3.169 | 3.090 |
| a guest window (no expansions) | 0% | 3.002 | 3.004 | 3.004 |

The only Stage B cost is serving new expansions: about 157 cycles of replayer stall per expansion with the
first controller, about 58 with the fast path (preloaded stream entries, a direct-mapped template table, the
N3 checks off the common path). Software tuning is closed: a further variant (option (a)) measured slightly
worse. What remains would be hidden by proposal P1 (section 7).

### 1.3 Tracer measurements (M, from the tracer session's deliveries)

| quantity | EVM Zone | Cyclone |
| --- | --- | --- |
| read-log share of log words (2^29) | 63% | 67% |
| write-log LRU-16 ratio, with / without the last-load entry | 2.03 / 1.45 | 2.38 / 1.86 |
| MUL-class rows (window max) | 5.3% (13%) | 6.4% (25%) |
| MULHU rows | 0.02% | 1.6% |
| distinct controller-fed instruction words (executed) | 32 (24) | 23 (17) |
| ROM words used (of 512) | 239 | 221 |
| controller-fed rows, window max | 62% | 83% |
| legacy host-I/O ECALL encoding used | no | no |

Log densities at 2^29 reproduce the design notes' figures (about 0.28-0.32 words per row). Item 114's
128-template limit and item 40's ROM fit are non-issues for these programs.

---

## 2. Package and provenance

| piece | version | where |
| --- | --- | --- |
| spec | through part M (`machine-spec.md`, `replayer-opcodes.md`) | `spec/` |
| change log | parts a-v | `CHANGES-v1.1.md` |
| meshsim and the sort | ported to the spec in this tree | `meshsim/`, `sort/`, `sw/`, `kit/` |
| replayer model, loader, Stage B controller, guests | replayer-cp14, final for this milestone | `meshsim/` and `sw/replay/` |
| runner jobs | p4 runs, full-size suites | `runner/` |
| run results | kit at 64 x 2,000; runner tasks 065-067 | `results/` |
| tracer tool and replay inputs | the tracer session's deliveries 1-3 (36, 27, 5.5 MB) | kept outside the package; `REPLAY-INPUT-REQUIREMENTS.md` and `FORMAT.md` describe them |

Ground truth for the trace (item 113): upstream Jolt pinned at `LayerZero-Research/jolt@735355e43026e9ef392b36b4ab5ed0f5e680127f`
(the Rust that `snarkify/jolt-cpp@309b4c4` vendors), with **jolt-cpp's interpreter rows** as the gold
standard for now. The JIT is not a reference. Later upstream versions (a16z/jolt HEAD, `bd9aef4` when
checked) count as future compatibility.

Not rebuilt this revision: `Intro.pdf`, `Reference.pdf` and the figures predate the spec changes;
`make_docs.sh` rebuilds them.

---

## 3. What the spec now says (parts A-M, tile side)

- **One tile design.** Every tile is identical except big-SRAM size and what is attached and strapped:
  every tile has the peripheral slot, the sender, a 16-entry request receive queue, a fifth router port
  (connected only where something is attached), the GO pulse and the UART registers.
- **Request port (A).** Fixed 128-bit requests on a 16-bit tree; hardware stamps requestor [6:0], software
  sets target [9:7]; [127:10] belongs to the target. Per-target credits at the root; `REQUEST_PENDING` [19].
- **Peripheral slot (B).** Run pulse, `periph_running`, six service lines [18:13] (consuming a line claims
  one unit of service), `PERIPH_STATUS_0`-`7`, mode 9 as the peripheral write. Type in `TILE_STATUS` [15:12].
  The sender (`SENDER_SEND`, `SENDER_ADDR`) moves a peripheral's output queues onto a link.
- **Modes and counting (C).** Modes renumbered: 0 invalid, 1 gather request, 2 by source, 3 explicit
  address, 4 bucket, 5 augmented, 6 controller-memory write, 7 command write, 8 reply, 9 peripheral write.
  A halted tile counts nothing; gather requests and command writes are never counted; everything else is
  counted from any source. No wake-event mode. 16-slot command block; 256-word reply block landing by
  source; read-port results as mode-6 writes with a setup register (destination plus size). Per-counter
  done events at `WAKE_EVENTS` [3:0].
- **UART, split run log, expansion word (D).** UART in every tile (decoded everywhere, a no-op where
  unwired); six service lines; the replayer's read-log and write-log queues with LRU-16 decompression and
  per-window raw fallback; the write log's last-load entry (E[15]); `untraced` [55] and `advice_reg`
  [62:56]; the ROM as a frequency-filled cache; a best-effort resident region.
- **Counters, map, latency (E).** Counter writes add; entering HALT clears the counters; register map
  renumbered with every family contiguous; register and table-window loads take 1 or 2 cycles (meshsim
  models 2).
- **Opcode semantics (F).** `spec/replayer-opcodes.md` is normative for what each row computes, derived
  from jolt-cpp's tracer and checked by about 240 tracer-generated vectors.
- **Ground truth and templates (G, H, I).** The pinned upstream version and jolt-cpp gold standard
  (item 113, with 18 "pending the upstream check" tags); template numbering; the state record (word 0 the
  template bitmap); rd = x0 handling on every word; the controller's NOP word carries the template's rd0.
- **Corrections and fixes (J, K, L).** Counter wording; condition lists; read-port mode; host-I/O word in the
  code stream (4.19.5); operand code 145 "immediate 0" in ROM and controller-fed words.
- **Measurements (M).** Start path 12-13 / 20-21 cycles at 2-cycle loads; `stage_delay` = 32 cycles;
  request bus 17 cycles; item 116 (read-port overlap rule) a candidate to relax.

Region 0 of the replayer, state first: state word (pc, instr_idx, next offset, flags [112]-[115]), stream
word (three exact lengths), then 64 register-pair words. The controller can keep its window's checkpoint in
data memory and send it through the send port (mode 9, addr advancing by 4).

---

## 4. Measured facts worth keeping

- **Arming time** (meshsim, counted from C&C's GO store): the counter is written at +7 to +8 cycles on the
  shortest start path and **+14 to +15 on the sort's staging path**. The spec's "about 10" is low for the
  sort; `stage_delay` = 32 still gives about 2x margin. To fix in 6.2.
- **Item 116's cause:** the 64-deep loopback FIFO alone removes v1.0's 561 lost packets (with v1.0's
  16-deep FIFO meshsim loses exactly 561 again; at 64 deep, nothing is lost with tokens and loopback-last
  priority on or off).
- **Request bus:** 17 cycles unloaded, inside the 15-20 estimate.
- **jolt-cpp findings (all unconfirmed against 735355e, recorded in item 113):**
  - its fast JIT executes a self-targeting JALR twice (8 cycles where the interpreter's rows have 7),
    agreeing with a16z HEAD rather than with jolt-cpp's own rows;
  - its JIT crashes (SIGSEGV) on a JALR to 0, which the interpreter handles;
  - F4: its JIT's memory-log index drifts on the BLAKE3 inlines (26 entries per call; syscalls count 32-bit
    elements, expansions use 64-bit LD/SD), so Cyclone shows gaps at slice boundaries. Rows are unaffected.
- **Potential differences from upstream (item 113):** LR/SC with rd = x0 (upstream may remap to v40); the
  shared NOP row's virtual flag for an rd = x0 instruction; the self-jump rule; a new pc of 0 from a JALR or
  a branch (N6). a16z HEAD also adds the runtime advice tape (AdviceLB/LH/LW/LD -> VirtualAdviceLoad) and
  defines but never emits VirtualPow2I/IW.
- **The runner** (`godspeed`): Intel i5-14400F, 10 cores (6 P with HT + 4 E), 16 threads, 62 GB with no
  cgroup cap, 346 GB disk, OpenJDK 21, gcc 13.3, cmake 3.28, Python 3.12; no RISC-V gcc, no sudo, no Ubuntu
  archive; about 4x the sandbox's single-core speed. Tasks go to the Quest repo's `tasks/` as small scripts
  only (no data); inputs are copied to the box by hand (e.g. `/tmp/runner.tgz`) and checked by SHA-256.
  Tasks 064 (probe), 065 (p4, passed), 066 (suites; failed on a packaging gap), 067 (suites, passed).

---

## 5. Chip-level design (agreed in outline, not yet in the spec)

**Topology.** Three controllers, each a normal tile instance with 2 MB of big SRAM, sit on the fifth links
of tiles 0, 1 and 2; each drives one engine on its +8 link. All ids stay below 128:

| id | unit | role |
| --- | --- | --- |
| 64 | C&C 1 | proof flow (READY/GO), boot and the UART console, the code repository and overlays, the outside world |
| 65 | C&C 2 | the file system: directories, inodes, fds, translating (fd, offset) to pages; serves run logs to tiles |
| 66 | C&C 3 | the page manager: page pool, per-proof bitmaps, image page tables, exact reservations; drives the tracer |
| 72 | Ingress/Egress | LVDS in with an inline LRU decoder; a single LVDS pin out |
| 73 | Updater/Binder/RW | the only bulk LPDDR client: file reads and writes, applying updates, binding |
| 74 | Tracer | the RV64 row machine producing each shard's run log; its cache misses use a private link to 73 |

Routing to d >= 64: over the mesh to tile d[2:0], up its fifth link, along the column (+-8) to d.

**Memory system.** One engine (73) owns LPDDR; arbitration: tracer misses, then the Updater (a stall stalls
the card's broadcast), then tile reads, writes and binding. Files are small on purpose (inodes with page
lists, directories as files, fds, software translation in C&C 2). Images are page tables over 32 KB pages:
each ASIC holds the initial image (permanent), the slice-start image and the final image, built by the
Updater from the address-ordered update stream with streaming copy-on-write; the tracer gets a throwaway
layer; binding runs on the slice-start image. The host computes each ASIC's exact page count, so memory
never runs out mid-proof; per-proof bitmaps free everything in one merge.

**Outside world.** LVDS in through an 8 KB byte queue with flow control (off at 4 KB, on at 1 KB), an
uncompressed message header, an inline LRU decoder (bounded expansion, so no LPDDR ring), and credit-based
flow to the Updater (cumulative-count acks). The Updater must never stall, since the stream is a broadcast.

**Tracer.** A row machine with 256 registers in two banks (untraced rows compute advice in bank 1), a 128 KB
D-cache, a 128 KB predecoded I-cache, the whole page table locally, a window ring of about 1 MiB that
buffers each window raw, counts, then compresses into three streams, and four output queues (state, code,
read log, write log) drained by the sender. Back-pressure runs all the way from LPDDR to the tracer.

**Boot and bring-up.** Level-triggered reset driven by the card FPGA (pull-downs, board oscillator, PLL
lock gate); a boot sequencer copies a 16 KB ROM into C&C 1; the ROM boots from LVDS or the UART and
includes an ASCII monitor (57,600 baud by default). Emulation plan: one Alveo U250 holds a card-FPGA model
and the chip (4x4 tiles at 1 MB for the network, 2x2 at 4 MB for tile fidelity), with DDR4 throttled to
LPDDR's latency and bandwidth.

**Worst cases.** Memory-side extremes (update streams up to about 2 GB per proof) and replay density
(a small-region random copy maxes every window) are both visible to the host before proving, which prices
or deprioritizes them. Tile memory: 4.25 MB is a **tentative** working figure; the resident replay region is
best effort.

---

## 6. Rulings made in this session (selected)

- jolt-cpp's interpreter rows are the gold standard; upstream is pinned at 735355e; a16z HEAD is future
  compatibility. (O)
- Only the bits the hardware needs are committed in a request: requestor and target, 10 bits. (O)
- Peripherals use one interface; lines are defined by the peripheral; consuming a line claims one unit. (O)
- Mode 10 (wake event) is gone; answers are counted data plus at most one reply. (O)
- Uncoordinated writes into a tile require HALT. (O)
- Counter writes are additive; the batch rule is the normal way to arm. (O)
- Register loads are 1 or 2 cycles, implementation-defined; stores single-cycle; meshsim uses 2. (O)
- Templates are numbered by exact instruction word; ROM is a cache; rd = x0 handling applies to every word.
  (O)
- Code 145 is "immediate 0". (O)
- The resident replay region is best effort; adversarial density is priced by the host. (O)
- Task scripts may go in the Quest repo; data may not. (O)

---

## 7. Open design discussions

- **Condition handling.** Direction: a condition raises a local interrupt; a resident handler reads the
  state (conditions, `TILE_STATUS`, counters, mepc/mcause/mtval, `PERIPH_STATUS_0`-`7`, x1-x31: about 50
  words) and sends it to C&C 1 as about 17 request-bus reports (header word: op, sequence number; three
  data words each), then parks; C&C 1 recovers with HALT. Needs machine-mode traps on the controller core;
  **the core (Tony's RV32 design) is not in jolt-asic-planning**, so its trap support is to be confirmed with
  Tony. Open: whether per-tile error lines to C&C 1 are still needed, or whether C&C 1's phase watchdog is
  enough as the backstop for a wedged core or a stuck request path.
- **HALT.** One line or separate lines for compute tiles and controllers; which peripherals stop; what HALT
  clears (request FIFO and receive queue with credits returned, the send holding register); a fixed hold
  before RUN (about 1,000 cycles if RW stops, more if not); per-tile halt for recovery.
- **P1, raising "new expansion" early.** The replayer's front end presents the next controller-fed
  instruction while earlier rows still run, hiding the remaining ~58-cycle expansion service time: Stage B
  would approach Stage A (about 3.27-3.30 cycles per row on EVM Zone window 53). A spec change (front end,
  STATUS_0's offset, the expansion queue holding two expansions' boundary). N12 (narrowing 8.5's
  DMA_CONFIG rule to arrivals that read its digit and k fields) goes with it.
- **A 2-cycle replay schedule when rd_prev isn't needed:** a per-pass state-word flag [116]; simple and log
  rows drop from 3 cycles to 2 (multiplies probably 5 to 4); roughly a third off pass time for stages that
  don't use rd_prev.
- **32 KB + 32 KB controller memories.** The controller now has 32 KB of code space: spec part N (memory
  sizes, 14-bit packed addresses, the command and reply blocks at the top of data memory, the resident
  loader's address); the sort fits as one image. The overlay and block-copy mechanism stays for larger
  programs (sumcheck kernels).

---

## 8. To-do list

1. **Spec, small:** replace 6.2's "about 10 cycles" with the measured arming times (+7-8 shortest path,
   +14-15 sort staging path); record item 116's cause (the 64-deep loopback FIFO).
2. **Spec part N:** 32 KB + 32 KB controller memories; then meshsim and the sort as a single image.
3. **Condition handling:** after Tony confirms the core's trap support: the interrupt, the report format
   (op, sequence number, payload), the handler's parking rule, error lines vs watchdog.
4. **HALT design** (section 7).
5. **Replayer spec changes:** the rd_prev-free 2-cycle schedule ([116]); P1 with N12.
6. **Item 113:** run the guest programs (`meshsim/guests/item113/`) under Rust Jolt at 735355e and jolt-cpp,
   diff the rows, settle the potential differences, remove tags as they settle.
7. **Full-program replay on the runner:** every window of EVM Zone and Cyclone at 2^29, compared by row hash
   against the tracer's manifest (needs a way to get the tracer and program inputs onto the box without
   committing a token).
8. **Opcode coverage report:** which of the 85 executed opcodes and which row kinds real windows exercise.
9. **Multi-tile and multi-pass replay** in meshsim (64 tiles at once; about 44 passes with per-pass flags).
10. **The sumcheck memory squeeze** (decides the 4.25 MB tile size): 6a akita and 6b base are each about
    255 KB over the 3,407,872-byte target.
11. **Chip-level spec work:** the endpoint map (ids 64-66, 72-74, routing), C&C 1-3, Ingress/Egress, the
    tracer as a peripheral, the Updater/Binder/RW engine and the memory side, per-target request layouts,
    the materializer interface (item 59).
12. **Docs:** rebuild `Intro.pdf`, `Reference.pdf` and the figures; refresh the design notes v2 against
    parts A-M.
13. **Read/write configuration registers** (owner, 2026-10-28; a later spec part, not yet made):
    - rename `ROUTER_SET_HDR` to `ROUTER_HDR` (0x80C), read/write: a store sets dst, mode and addr; a load
      returns the latched value with addr as advanced by every send since, i.e. the header the next
      packet will use. `ROUTER_SET_W0`-`W2` keep their names and stay write-only (they clear after a send);
    - `SENDER_ADDR` (0x86C) read/write the same way, returning its auto-advanced value;
    - `DMA_CONFIG` (0x828) read/write, returning the latched fields (spare bits read 0); 8.5's rule
      against writing it during a run or its arrivals is unchanged;
    - `DMA_RECORD_BASE_ADDRESS` (0x82C) read/write, returning the latched chunk address;
    - `SRAM_RD_DEST_ADDR` (0x844) read/write, returning the latched setup: the destination word as advanced
      by every read queued since (by the read size) and the size field, i.e. where the next result lands.
    All five follow 4.11's register-load timing. Touches 4.3, 4.11, 8.1, 8.4, 8.5, 8.7, 8.8, 8.12, `REGISTERS.md`
    and its generator, meshsim's register table and load path, and the software's uses of the name
    (sort, recipes, `sw/replay/`); keep `ROUTER_SET_HDR` as a deprecated alias in `mesh.h` for one
    revision.
14. **Access labels read R/W, not W/R** (owner, 2026-10-28): where an address pairs a load-side and a
    store-side register (the request port, 0x858-0x864), list the load side first, e.g.
    `REQUEST_GET_W0` / `REQUEST_SET_W0` with access "R / W", and `REQUEST_RECEIVE` / `REQUEST_SEND`. Applies
    to spec 8.1 and 8.10 and to `REGISTERS.md`'s generated table (change the generator, not the output).

---

## 9. Sessions used

| session | role | state |
| --- | --- | --- |
| ASIC design spec review | spec editor; parts A-M | idle; edits the merged tree from now on |
| Upgrade meshsim to v1.1 spec | meshsim port, sort, Stage B, runner jobs, the merge | done for this milestone |
| Replayer peripheral model and verification | replayer model, loader, Stage B controller, guests | done (cp14) |
| Jolt-cpp tracer for replayer inputs and reference rows | tracer tool, deliveries 1-3, measurements | done; code 145 switched in |
| (opcode semantics) | `replayer-opcodes.md` and its harness | done (part F) |
| (controller investigation) | searched jolt-asic-planning for an RV32 core with trap support | none found; ask Tony |
| planning (this one) | design, rulings, coordination, runner tasks | this document |
