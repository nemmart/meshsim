# WORKER3 notes: v1.37, the Tracer on images the Updater built

*Closed and merged into `main` on 2026-11-16. The files this one calls `NOTES.md`, `REPORT.md` and `QUESTIONS.md` are
`WORKER3-NOTES.md`, `WORKER3-REPORT.md` and `WORKER3-QUESTIONS.md`, beside it; its prompt is in the repository's history.*

Status, 2026-11-16: **all three stages done** (`REPORT.md`): the units, each tested alone; the master tile's
firmware, on the whole chip with gold's guests; the real workloads from packet files, small and at 2²⁹. Branch `updater-w3`, main merged in at `8385dacd`. The owner's go-ahead and rulings
are in section 9; what was built differently from this plan is in section 11.

Names: II is `INGRESS-INTERFACE.md`, UI `UPDATER-INTERFACE.md`, HF `HOST-FRAMES.md`, HL `HOST-LINK.md`, all in
`v1.4_wip/planning/`. Paths without a folder are in `v1.37_wip/`.

## 1. A real segment today, piece by piece, and what replaces each

`./run_tests.sh` step `segment-real` (`tests/test_segment.cpp`, `real_*`): three segments of 64 windows (EVM Zone
trace-23's segments 0 and 1, Cyclone input/22's 0), about 23 M cycles and 13 to 17 s each on the runner (Quest 198).

| piece | today (`tests/segment_real.cpp`, `tests/segment_bench.cpp`) | in v1.37 |
|---|---|---|
| the guest's memory at the segment's start | the harness: `initial.pages`, then the update lists of the segments before, applied in C++ into a map of lines | **the Updater**, from frames 2, 6 and 9, in the file store |
| which pages the Tracer may write | the harness: every page any host segment writes is `ok`, the rest `not_owned` | **the Updater's Tracer's copy**: it owns the pages segment *s* writes |
| the jump table | the harness: `map.lines` and `proof.json`'s k, p1, p3 | **the Updater**, from frames 4 and 5; k, p1, p3 from frame 4 |
| the memory system on the fill port | `Fill`, the RTL test's model: a map of lines, fixed made-up roots (0x01234567, 0x07654321), random latencies | **v1.36's own model** (`MemSystem` on `FileStore`: hit 6, miss 80), on the store the Updater wrote, with the real roots |
| the segment's record, its advice | the harness places them in the tracer tile's scratchpad, from `segments.seg` | **the master tile's firmware**, from frames 7 and 8 |
| the record's word 66 (roots, depths) | the harness writes it | **the master tile's firmware**, from the Updater's roots |
| the hash constants, the advice count, the expected end state (tracer header words) | the harness | **the master tile's firmware** (frame 4; frame 8; record *s* + 1) |
| the master tile's program | `fw/cc/cc1.c`, with the harness's commands (`CC_SEGMENT`, `CC_WAIT`, `CC_EXIT`) in `boot_desc.script` | **a new program**, `fw/cc/master.c`: the frame loop, the page map, the proof's sequence, then the segment's two barriers |
| the tracer tile's program, the replay controller | `tracer_ctl.c`, `replay_ctl.c` | **unchanged** (section 5.4) |
| the decode table and ROM, the templates and their index, the tiles' layouts, `RUN_CONFIG`, the windows a segment has | the harness places them | **the harness still places them**: no frame carries them (for the report) |
| the firmware images | placed, or loaded by the master from its scratchpad | the same |
| what a window must be (rows, records, streams) | the recorded files (`v1261/`), read by the harness | the same files, the same checks |

Nothing of the proof reaches the chip but through the packet file. The harness still reads the recorded files, to
check what comes out.

## 2. Starting `v1.37_wip`: the freeze marks and the names

As v1.36 did when it began from v1.35 (its commit `9873c28b`): the first commit changes the documents, no code.

- `README.md`: a banner, "v1.37 is in progress", saying what is v1.37's (a list that grows with the stages) and
  that **everything else in the folder is still v1.36's, copied unchanged, and reads as v1.36**. `BUILDING.md` and
  `RUNNING.md` get the new steps as they come; their v1.36 text stays.
- `OPEN-v1.36.md` and `tools/freeze/README.md` are marked as v1.36's pages. `COPIES.sha256` and
  `pipeline/FROM-V1.3.sha256` still hold and are still checked.
- **`spec/` is not touched** (not my work). What v1.37 adds is written in a page of its own at the top,
  `CHANGES-v1.37.md`, citing II and UI as the source.
- `make doccheck` passes at every commit.
- `pipeline/gold/` stays v1.36's, for v1.36's inputs. The frames come from `updater_wip/pipeline/gold/`.

## 3. One switch: the v1.37 master tile

**Proposed (choice C1): `MachConfig::host_link`, off by default.** Off, the chip is built exactly as v1.36's: the
same units, the same ports, the same router on the master tile. On:

| | off (v1.36) | on (v1.37) |
|---|---|---|
| Ingress/Egress | 17.6 as built | + II's words and bits, + a DMA (block 1, two output queues) |
| the master tile's peripheral 2 | absent | the Updater: window 0xFFFF_FC00, region 0x05, type 5 |
| the master tile's router | no DMA input | one DMA input (16.4), as the memory and tracer tiles have |
| the master's `PARAMS.peripherals` | 1 | 3 |
| the file store | the Tracer's own | one, shared by the Updater and the Tracer's memory model |

Why a switch: "a black box must cost the existing runs nothing" then holds by construction. `test_regs` ties the
register map to the specification's tables, `test_ingress_egress` checks that block 1 reads 0, and `rtl-stream`
runs at the RTL chip's cycles on the master's router; none of them sees a change. The alternative, always on, is
what II's "sits beside the old one" describes; it would need each of those shown unchanged, and the spec edited.
To decide at the freeze.

## 4. Stage 1: the units

### 4.1 Files

| file | what |
|---|---|
| `src/units/host_link.h`, `.cpp` (new) | the link's packets: CRC-32C, the header's fields, the packet file's reader and its checks. No simulator types: a test can use it alone |
| `src/units/ingress_egress.h` | under the switch: the two streams' queues, `STATUS` [3] [4], `EVENTS` [8] [9], `ERRORS` [1] [2], words 80 and 81, the DMA's side (`out_queues`, `out_ready`, `out_take`, `out_start`). The RTL's part is not edited |
| `src/units/updater.h`, `.cpp` (new) | the Updater black box, a peripheral of the slot (`peripheral.h`'s method set) |
| `src/units/file_store.h`, `.cpp` | pages placed where told (4.4) |
| `src/units/peripheral.h`, `periph_slot.*`, `tile_ctl.cpp`, `chip.cpp`, `tests/mach.*` | `PT_UPDATER` = 5 in the dispatch; the master's slot with a DMA port and a second peripheral; the router's DMA input; the switch |
| `fw/include/host.h` (new, by hand) | the new words and bits for firmware. `fw/include/mesh.h` is generated from the register table and checked against `spec/`, so it stays v1.36's (C2) |

### 4.2 Ingress, the host side (II; HL)

- **The file** is the bytes as they go on the link: a header word, 16 bytes, least significant first, then its
  data. A zero byte where a header's first byte is expected is idle and skipped (HL, "Why the zeros work").
- **The checks, in HL's order:** the header CRC over bits [95:0] (the word's first 12 bytes); then the fields;
  then the data checksum over the data's bytes; then the packet number against the next one of its stream (1 at
  the start; after 0xFFFFE, 1). A failure sets `ERRORS.crc_error`, `format_error` (a field that cannot be; the owner's ruling
  on Q2) or `sequence_error`, and nothing more is read from the file or presented; packets already presented
  stay readable. A length-0 packet takes no number: its number field holds the number the next data packet on
  its stream will carry, and is checked against it (the owner's ruling; HL on main says the same).
- **Presenting** is II's rule for a service flag, to the letter: `…_ready` and `…_WORDS` when a packet is at the
  front; the flag posted once; no further flag until `…_WORDS` words have been read out, in one transfer or
  several; the two streams independent. A packet of length 0 is checked and counted and asks for no service (†).
- **The DMA** is the slot's standard one (17.4): `queue` 0 the foreground, 1 the background; one word a cycle; a
  count beyond what is left of the packet is undefined (†), so the simulator stops there, as on other UB.
- **Written from the documents.** WORKER2's Java is not read. The CRC-32C is the one WORKER2's prompt names
  (polynomial 0x1EDC6F41, reflected, initial value and final XOR 0xFFFFFFFF, "123456789" gives 0xE3069283); HL
  itself does not say (Q1).
- Numbers that collide with v1.36's: **none found.** `STATUS` [2] is the highest in use, `EVENTS` [7], `ERRORS`
  [0], words 72; block 1 is empty; type 5 and the master's peripheral 2 are free.

### 4.3 The Updater (UI; the second design note, section 4)

Behind UI's registers, queues, credits, events and errors, with nothing added. Inside:

1. **`START`** takes block 2. With `root` 1 or 2 it first writes the root at `IMAGE_ROOT` (empty, or the copy of
   `SOURCE_ROOT`'s with the owned bits cleared; with `tracer_copy`, at `TRACER_ROOT` too). `continue` 0 clears the
   decoder: the LRU table, the bits left over, and the run of values it was in.
2. **The decoder** pops the segment queue as it needs bits: raw, two 64-bit words an entry; or LRU, HF's three
   codes against 32 entries, move to front. It is HF's, written new: `lru_coder.h` is the Tracer's 16-entry encoder.
3. **Each update word** counts `WORD_COUNT` down. A header word opens a run of values; a value goes to its word of
   its page.
4. **A page** is brought into the one page memory when a value first needs it: the walk from the root, top down,
   one page a level. At each level an entry that is the file's own is followed; a shared one is copied to the next
   free page (an index page with its owned bits cleared) and the entry above set; a hole becomes a page of holes,
   or of zeros. Every new page is the next entry of the free page queue, and counts `PAGE_COUNT` down.
5. **With `tracer_copy`,** at each level the Tracer's copy takes its page first, unchanged, then the image takes
   its own; the two trees stay in step.
6. **The page is written** when a value needs another page, or the run ends. `done` is posted then.
7. **Errors:** `queue_overrun`, `beyond` and `out_of_pages` as UI says. `lpddr_error` for a page address outside
   LPDDR or off a page boundary (C10). `not_owned` is never set: the black box makes each level its own before it
   writes below it. A `START` while busy is UB (17.2), so the simulator stops.

It is software on the file store, written to be read: about one function for each numbered step.

### 4.4 The file store

Today `FileStore` keeps its pages in one array and hands out the next address itself. Change:

- **Pages by address:** a table over LPDDR's pages (2²⁰ of 4 KB, the page map's reach), each page its own block,
  made when first placed. A page never moves, so the Tracer can read while the Updater's last run still writes.
- **New:** a page made at a given address, a whole page read and written, "is there a page here".
- **Kept:** every existing call and the addresses it gives (the next free address, 32 apart from `first`), so the
  rig's images, `suite_memsys`' log and the Tracer's cycles on them are what they were. The fingerprint's sum is
  over the same words in the same order.

### 4.5 Tests of each alone (no firmware)

| test | what it checks |
|---|---|
| `suite_memsys`, `tracer_block`, `suite_tracer` | unchanged logs, after the store's change |
| `test_file_store` (new cases) | a page placed anywhere; placed and handed-out pages together; holes, owned bits and the walk on placed pages |
| `test_updater` | the registers and their reset; both queues' credits (a return each 128 entries, depth 2, overrun); raw and LRU, with `continue` across runs at odd counts and mid-word; `root` 0, 1, 2 and with `tracer_copy`; copy-on-write at each level, depths 1 to 3; pages taken in the queue's order; `PAGE_COUNT`, `out_of_pages`, `beyond`; a run of `WORD_COUNT` 0; the statistics |
| `updater_frames` (a tool, and a test on fixtures) | **gold's frames fed directly**, by a C++ driver that does what the firmware will: the program image, a base table, the run's table, the cycle-0, start and final images and the Tracer's copy **equal to `frames-images`' files, byte for byte**, for several *s*; `PAGE_COUNT` 0 at every `done`, so exactly the pages the frames count; raw and LRU, with and without a base |
| `test_host_link`, `test_ingress_link` | WORKER2's vectors and packet files: every packet's words, in order; the service flag's rule; reading in pieces; `…_WORDS`; both streams (a made-up foreground file); idle zeros in counts that are not multiples of 16; the packet number's wrap; a length of 0. **Damage:** the first byte, the stream, the length, a packet number, each check, a data byte, a reserved bit, one at a time: the right error bit, and nothing presented after |
| `ingress-egress`, `rtl-stream`, `regs`, `periph` | unchanged, switch off |

- **Fixtures:** frame files of gold's guests (keccak256 with inputs, jumpmap's `jumps`), made by
  `updater_wip`'s gold with their `frames-images` output, committed if small; a script remakes them and requires
  them equal when a JDK is there. The workloads' frame files are made on the machine (section 7).
- **WORKER2 has not started** (no `updater-w2` branch). Until its vectors exist I test Ingress against packets
  written by a small writer of my own, in the test, from HL. That is one author on both sides, so it is marked
  as provisional, and WORKER2's files replace it as the check (section 9).

## 5. Stage 2: the master tile's firmware

### 5.1 Structure

`fw/cc/master.c`, written for v1.36 in v1.36's names (`include/mesh.h`, as `memory.c` is) with `host.h`; built by
`fw/build.sh`'s part `cc` into `images/cc/master.elf`. **`cc1.c` is not edited** and its image keeps its hash (C3).

| part | what |
|---|---|
| the boot | `cc1.c`'s: every `LOAD` waited for; the compute tiles loaded and released. **The tracer tile's load waits** until its segment is ready (5.4) |
| the page map | format, take a page, take *n* pages into the free page queue |
| the frame loop | II's: on `background_service`, the frame's header word into DMEM; by its type, the rest to the header parser, the Updater, the staging area or the scrap row |
| the header parser | frames 1, 3 and 4 as a stream of 32-bit words, a packet at a time through a 512-byte buffer: strings skipped, the counts kept. For frame 3 it keeps the data image's three counts, the sums of the segments' words before *s*, of *s* and after, and ASIC *s*'s three page counts |
| the Updater's minding | the registers, `START`, the allowance (a `segment_credit` adds 128, a transfer takes its words off), the free page queue on `free_page_credit`, `done` |
| the proof's sequence | UI's runs, section 5.3 |
| the segment | `CC_SEGMENT`'s two barriers and `CC_WAIT`'s results, as `cc1.c` has them, then the exit |
| the error path | the NMI: the cause, both peripherals' `ERRORS` and the error lines into the results, then park. (`cc1.c`'s report drain and UART dump stay `cc1.c`'s, which `test_segment` still runs) |

Results for the harness, by symbol as today: a status, the frames seen, each run's `PAGE_COUNT` at `done`, the
roots, the pages taken, both `ERRORS`, and `mcycle` at each frame's start and each run's `START` and `done`.

### 5.2 Its memory

| where | what |
|---|---|
| DMEM (32 KiB) | the image's data; the frame header word; a packet buffer of 32 words; one row of the page map (128 pages); record *s*'s word 66 while it is patched; the results |
| the scratchpad, as today | the two images it loads, at word 0x0000 to 0x8000 |
| the scratchpad, word 0x40000 | **the page map: 128 KB**, one bit a 4 KB page. The core cannot load or store the scratchpad (5.1), so the firmware keeps the row its cursor is in, 128 bits, in DMEM, writes it back by a `WRITE` to itself and fetches the next with the read unit: one round trip for each 128 pages |
| the scratchpad, word 0x80000 | the staging area for the tracer tile: record *s*, record *s* + 1, advice log *s* |
| the scratchpad, one row | scrap: where the other segments' records and logs are read out to (C15) |

- **Format:** the 128 KB written as zeros, page 0 marked taken (LPDDR's first page is the superblock's).
- **Taking a page:** the next clear bit from a cursor that only moves up. v1.37 frees nothing.

### 5.3 The proof's sequence

Frames arrive in gold's order: 1, 2; with a base, 4 and 5; then 3, 4, 5, 6, 7, 8, 9.

| frame | what the firmware does |
|---|---|
| 1 | keeps the file length (the images' depth) and frame 2's counts |
| 2 | a root from the map; a run, `root` 1; the frame's data into the segment queue |
| 4 with a path | a base table: keeps its root, k, p1, p3. v1.37 keeps no names: "the base" is the last one established (C19) |
| 3 | the counts above. N is checked against frame 7 when it comes |
| 4 without a path | k, p1, p3 for the Tracer; frame 5's counts; whether it starts from the base |
| 5 | a root; a run, `root` 1, or 2 from the base |
| 6 | the cycle-0 image: a root; a run, `root` 2 from the program image |
| 7 | record *s* and record *s* + 1 to the staging area, the others to scrap. Its length must be 68 N |
| 8 | the count table into DMEM a packet at a time; log *s* to the staging area, the others to scrap |
| 9 | three runs (†, as UI has it): before *s*, `root` 2 from the cycle-0 image; segment *s*, `root` 2 from the start image with `tracer_copy`, `continue`; after *s*, `continue`. The third is started at the second's `done`, and then the tracer tile is handed its segment while the stream goes on |

`PAGE_COUNT` for each run, from the frames: frame 1's pages; frame 4's; frame 3's data image pages; ASIC *s*'s
start count; twice its Tracer's copy count; its final count less its Tracer's copy count. Exactly that many pages
are queued, so `PAGE_COUNT` must end every run at 0 and the map marks no page that is not in a file.

### 5.4 What it hands on, with no change to the tracer tile's program

**The tracer tile sits in its boot ROM until the master answers its `LOAD`** (6.5). So the master holds that load
back, and sends it as one transfer of its own DMA unit, uncounted and of one kind as the load is today, with
streams in front of the image: record *s* (its word 66 with the Tracer's copy's root and depth and the table's),
record *s* + 1 as the expected end state, advice log *s*, and the header words (`TL_H_HASH_MUL`, `_STEP`, `_LINES`, `TL_H_ADVICE_N`; `TL_H_EXPECT` 0 for the
last segment), all into the tracer tile's scratchpad where `tracer_layout.h` puts them. The load's `COUNT` lands
last, as it does today, so `tracer_ctl.c` finds everything in place when it starts. **`tracer_ctl.c` and
`tracer_layout.h` are unchanged**, and so is the tracer tile's image.

Why through the staging area and not straight from Ingress: Ingress's DMA and the tile's DMA unit are two senders,
and nothing orders their packets at the tracer tile; the tracer tile, in its ROM, cannot arm a counter for them.

### 5.5 N and s

**Proposed (C16): two words placed with the master's image,** as `boot_desc` is today: `system_config = { magic,
N, s }` in `.data`, filled by the harness before the reset ends. The tools take `--asics N --segment S`. It stands
for whatever tells an ASIC "69 of 128" at start-up; nothing in the proof's data carries it. Alternatives: straps
(new `PARAMS` bits: a hardware change), or a message on the foreground stream (not designed).

## 6. The black boxes' timing

Both are plain, stated in the unit's header and in `CHANGES-v1.37.md`, and each figure is a parameter.

**Ingress:** the file arrives at **one byte a cycle** (HL's 1 GB a second at the 1 GHz the budgets assume). A
packet is at the front of its stream when its last byte has arrived and its checks passed. Each stream's queue
holds 4 packets (C4); the file is not read while the queue it needs is full, which is what "stop" would do.

**The Updater,** from the memory model's two figures (`mem_system.h`: hit 6, miss 80):

| step | cycles |
|---|---|
| one update word decoded | 1 |
| a cache look-up, each level of the walk | 6 |
| a page read, or written and confirmed | 80 + 256 (a beat a cycle); half a page 80 + 128 |
| an entry updated, written through | 80 |

- A page is read by UI's table, by halves: a half the stream covers whole, or a hole, is not read.
- `STAT_PAGES_READ` and `_WRITTEN` count those; `STAT_STALL_DATA` and `_PAGES` the cycles waiting on an empty
  queue; `STAT_STALL_MEMORY` the cycles of the table above.
- **It is not the hardware's timing,** and no cycle count of a v1.37 run is a figure of the design.

## 7. Stage 3: the tests, and what a run costs

**A run:** the chip (switch on), the packet file, N and *s*; the master runs `master.c`; the Tracer's memory is
the model on the Updater's store; the chip in one thread domain, as `segment-real` runs today.

**Checks:** everything `run_chip` checks today (every row, record and stream against the recorded files, the
tiles' and the tracer tile's results, the end state, the toast pin), with word 66 expected as the firmware wrote
it; **the cycle-0 image, the start image, the Tracer's copy and the final image equal to `frames-images`', byte
for byte**; `PAGE_COUNT` 0 at every `done`; both `ERRORS` 0; the page map marking exactly the pages in files. The
Updater calls an observer at each `done`, and the harness writes the images out there, so the Tracer's copy is
taken before the Tracer's first write.

**The small inputs have few segments of 64 windows:** EVM Zone trace-23 has two, Cyclone input/22 one. So:

| step | segments | runs | in |
|---|---|---|---|
| `proof-real` | 64 windows, N 128, the real configuration (static base, the smaller form): EVM Zone *s* 0 and 1, Cyclone *s* 0. Today's three segments, from packets | 3 | `all`, with the inputs |
| `proof-matrix` | **8 windows a segment** (`--segment-rows=524288`), so that there is a middle: EVM Zone *s* 0, 1, 6; Cyclone *s* 0, 1, 3; each raw and LRU, with and without a base | 24 | `all`, with the inputs |
| `proof-damaged` | a guest's packet file with one bit changed: the error bit, the master's error path, the toast pin | 2 | `quick` |
| `master` | the guests' fixtures on the chip, to the final image (no tracer image: no trace): the images, the page map, what was staged for the tracer tile | 4 | `quick` |

- The packet files and the reference images are made on the machine by a script, from `updater_wip`'s gold and
  WORKER2's packetizer, into `$MESHSIM_INPUTS/v137/`; without them the two `proof-` steps print `SKIPPED`, as the
  steps that need the inputs do today.
- `fw_teeth.sh` gains bugs planted in `master.c`, each to be caught.

**Cost of one run** (estimates; the measured figures go in the report):

| part | simulated cycles | from |
|---|---|---|
| the link, to the end of frame 8 | 2.7 M to 4.5 M (small), 2.7 M to 8.0 M (2²⁹) | the frame files' bytes less frame 9 (WORKER1's table), a byte a cycle, + 3 % for packet headers |
| frame 9, to the end of segment *s* | up to 0.2 M to 0.9 M (small), 1.6 M to 17.7 M (2²⁹) | the same; the rest arrives while the Tracer runs |
| the Updater's own work | under 5 M at the worst (Cyclone 2²⁹, a late *s*) | 6,093 pages over the proof at about 750 cycles |
| the trace and the replay, 64 windows | about 23 M | Quest 198 |

So about **26 M to 50 M cycles a run at 64 windows**, and about 6 M to 9 M at 8 windows. Wall time: today's 23 M
cycles take 13 to 17 s on the runner; before the trace only the master tile is awake, so I expect **20 to 30 s a
run at 64 windows and about 5 s at 8**. `proof-real` then adds about 1.5 minutes to `all`, `proof-matrix` about 2.

**At 2²⁹, on the runner:** runs are cheap, so the *s* are chosen for what they exercise, five a workload: 0; 1; 64;
the last with rows (EVM Zone 84, Cyclone 88); and the one with the most pages (from frame 3). Ten runs, about 5
minutes, after the inputs (the runner's persistent data, or built there in about 17 minutes) and gold's frames.
One Quest task, patterned on `tasks/v136-check/`, with the firmware images committed.

## 8. Choices

Every † in II, UI, HF and HL is taken as written. What the documents do not say:

| | choice | why; the alternative |
|---|---|---|
| C1 | the v1.37 master tile behind `host_link`, off by default | section 3; always on |
| C2 | the new words in a hand-written `fw/include/host.h` | `mesh.h` is generated and checked against `spec/`; the register table, with the spec edited |
| C3 | a new program `master.c`; `cc1.c` untouched | `cc1.c` is v1.35's, kept to its diff, and its image's hash is checked; a new command in `cc1.c` |
| C4 | the link at a byte a cycle; a stream's queue of 4 packets | HL's rate; HL leaves the queues open. As fast as taken |
| C5 | CRC-32C as WORKER2's prompt defines it, over the bytes in link order; a checksum over no data is 0 | HL says only "CRC-32C" (Q1) |
| C6 | a header whose CRC is good and whose fields cannot be (stream not 1 or 2, length over 32, a reserved bit) is `ERRORS.format_error`, bit 3. Flags and the acknowledge field are ignored | the owner's ruling on Q2 (II on main has the bit); I had proposed `crc_error` |
| C7 | a page stays in the page memory across runs of values that fall in it, and is written once | a segment's runs are in address order, so a page is visited once; a read and a write for each run of values would cost tens of times more |
| C8 | the run of values the decoder is in is decoder state: kept by `continue`, cleared without | UI lists the table and the bits; with gold's counts a run never straddles |
| C9 | with `tracer_copy`, the Tracer's copy takes its page first at each level, and only where it does not own one | the second note: "written unchanged to the Tracer's copy, then patched" |
| C10 | `lpddr_error` for an address outside LPDDR or off a page boundary; `not_owned` never set | they have no other cause in a black box |
| C11 | depth 0 is the file store's: the root is the data page | it never arises: gold gives every file an index level |
| C12 | LPDDR is 2²⁰ pages; page 0 is never handed out | the page map's size; the superblock |
| C13 | a free page queue entry is the page's address in 128-byte units in [31:0], a multiple of 32 | the form a root and an entry have |
| C14 | the tracer tile gets its segment with its load, through a staging area | section 5.4; a request from the tracer tile, which changes its program |
| C15 | words not wanted are read out to a scrap row of the scratchpad | II has no "skip"; `COUNT`-mode packets |
| C16 | N and *s* in words placed with the image | section 5.5 |
| C17 | the windows a segment has stay the harness's header word (64; 8 in the matrix) | no frame carries a segment's rows (Q3) |
| C18 | the Tracer's memory on the chip is `MemSystem` on the shared store | the owner's "v1.36's software model"; `Fill` has no store |
| C19 | no names: the strings are skipped; the base is the last table established with a path | no file system in v1.37 |

## 9. The owner's go-ahead (2026-11-16)

Go ahead with the plan. The switch, off by default: yes (C1). N and *s* as two words with the master's image: yes
(C16). Today's three segments from packets, plus the 24-run matrix at 8 windows a segment: yes. Ingress built
against my own provisional writer, then checked against WORKER2's vectors.

- **Q1:** `HOST-LINK.md` on main states the CRC-32C as I had it (it does, since `8385dacd`).
- **Q2:** its own bit, `ERRORS.format_error`, not `crc_error` (C6).
- **Q3:** as I had it; listed in the report.
- **A length-0 packet does not take a packet number.** Its number field holds the number the next data packet on
  that stream will carry.
- Merge main in for the documents' wording (done at `8385dacd`).

## 10. Order of work

Each line a commit or a few, pushed, with `./run_tests.sh quick --check` passing; `all --check` and `make check`
at each stage's end, and `main` merged in.

1. The documents' banner (section 2).
2. The file store; then the Updater with `test_updater`; then `updater_frames` against gold.
3. `host_link`, Ingress and their tests; the switch and the master tile's slot, router and DMA. **Stage 1's report.**
4. The toolchain (xPack 15.2.0-1, fetched in the session); `host.h`; `master.c` part by part: the page map, the
   frame loop, the runs, the hand-over; `master` and `fw_teeth`. **Stage 2's report.**
5. The whole chip: `proof-real`, `proof-matrix`, `proof-damaged`; the numbers; the Quest task at 2²⁹; the README
   and the running guide. **Stage 3's report,** with the lists for the four documents.

## 11. As built, where it differs from the plan above

**Stage 1.**

- **The Updater's cycles** are counted as section 6 has them, with one arrangement: the work a word causes
  (bringing a page, the Tracer's copy, a half page read) is done in the word's cycle and its cost is then waited
  out, so a run's cycles are the root's 1, a cycle a word, 2 at the end, and the waits. The figures are the
  black box's (`Updater::Timing`: a look-up 6, a page 336, a half page 208, an entry 80), not the RTL's.
- **The Updater stops at an error** (`beyond`, `out_of_pages`, `lpddr_error`): `STATUS.busy` stays 1 and no
  `done` is posted. UI does not say what a run does after an error; the firmware's error path does not depend on
  it (it reads `ERRORS`). Listed for UI in the report.
- **A run needing more pages than the free page queue holds stalls** (`STAT_STALL_PAGES`) until the firmware
  pushes more; a page is counted as needed for the whole path to it, before any is taken, so a path is never
  half built.
- **The link's timing:** the first byte arrives in cycle 1; a packet whose last byte arrives in cycle c is at its
  stream at the end of c; when a stream's four places are full the link waits, and its next byte arrives the
  cycle after there is room. `link_cycles_per_byte` 0 gives packets as fast as they are taken (for tests).
- **A file that ends inside a packet** sets no error bit (the link has gone quiet, which no check sees); the
  model records it (`link_cut`) for the harness.
- **WORKER2's vectors** are on `updater-w2` (`d3e07c8b`), not yet on main. `test_packet_vectors` reads them from
  `$PACKET_VECTORS` or, once merged, from `../updater_wip/pipeline/gold/test/packets`; without them the step is
  SKIPPED. I have not merged another worker's branch into mine.
- **The small inputs cannot be built in this session:** the tracer's build fetches `fmt` from GitHub's archive,
  which this session's network refuses. The steps on recorded windows (`segment-real`, `tracer-block`) run on the
  runner (Quest), as do `make check` and everything of Stage 3 on the real workloads. Gold's frames of the small
  inputs are made here (gold needs only the kit's files), so the Updater is checked against gold here.

**Stage 2.**

- **The staging area is at word 0x48000,** right above the page map, not at 0x80000: record *s* at 0x48000,
  record *s* + 1 at 0x48200, two rows for the tracer tile's header at 0x48400, 32 rows of scrap at 0x48480, and
  advice log *s* from 0x48800 to the scratchpad's end: 187,904 rows, 375,808 values (`fw/cc/master_layout.h`).
  A log that does not fit, there or in the tracer tile's layout, ends the run with a status of its own.
- **Record *s*'s word 66 goes to DMEM as frame 7 passes,** not to the staging area and back through the read
  unit: the hand-over completes it there (the Tracer's copy's root and depth, the table's) and writes it to the
  staging area. The read unit is used for the page map alone.
- **Both of the segment's barriers are the master's,** as `cc1.c`'s: the first is released in the boot (the
  tiles arm for their records and say so to the tracer tile, whose requests wait in its queue while it sits in
  its boot ROM); the second after the hand-over, when every window has landed.
- **What the master is told** (`master_layout.h`): `boot_desc`, the two images' places in its scratchpad and the
  tracer tile's layout (where its record, its expected end state and its advice go, and the advice's room);
  `system_config`, N and *s*. The tracer tile's header, tables and templates are placed by the harness, as
  today; the master writes five of the header's words (`TL_H_HASH_MUL`, `_STEP`, `_LINES`, `TL_H_ADVICE_N`, and
  `TL_H_EXPECT` 0 for the last segment) as 32-bit streams of the same transfer.
- **One sleep, and everything else polled.** Every wait is a WFI followed by one load of the tile's `EVENTS` and
  of each peripheral's that has one; what the loads return is kept in the program's own state (a load consumes),
  the requests are answered, and the pump, which never waits, moves the frame's data on when the allowance
  covers the packet. So the third run of frame 9 goes on while the tracer tile is loaded and the segment runs.
  The master's counters: 1 for what Ingress's DMA sends it, 2 for the read unit, 3 for its own writes to its
  scratchpad.
- **"A frame ends at the end of a packet" is checked** where the next frame starts: the packet at the front must
  be whole. So is each header frame's length against its fields, each data frame's against its header frame, and
  frame 7's against 68 N: a difference ends the run (`MR_FRAME`).
- **The harness is `run_chip` with a proof** (`segment_bench.h`'s `Proof`; `tests/proof_bench.h`): one machine,
  one set of the tiles' checks, for v1.36's segments and for a proof. A segment with no recorded windows (a
  guest) is run with the checks that need none.
- **The fixtures' segments are 50 rows, one window.** A fourth guest, `divs` (`tests/updater/make_div_guest.py`),
  has advice values and a page written in every segment: none of gold's test guests has advice.
- **`tools/doccheck.sh`** takes a document of `../v1.4_wip/planning/` cited by its name as there: v1.37's
  additions are specified by design documents that are not in the version's folder.

**Stage 3.**

- **The steps are three, not `proof-real`, `proof-matrix` and `proof-damaged`:** `proof-real` (the three recorded
  segments), `proof-matrix` (24 runs), and `proof-29` (both workloads at 2²⁹, five segments each), which is a
  step of `run_tests.sh` like the others and not a task of its own: a 2²⁹ frame file is 4 to 16 MB and a run
  half a minute. The damaged files are `test_master`'s, on the guests: the error path does not depend on the
  workload.
- **The 2²⁹ runs were made here as well as on the runner:** gold makes the frames in seconds, and everything but
  the recorded windows' checks runs in this session.
- **The host's side is a script of `pipeline/`** (`pipeline/frames/build.sh`), into `$MESHSIM_INPUTS/v137/`: gold's
  frames (`--verify`), gold's packets (read back, and compared with the tests' writer's), gold's references. Its
  `RUNS` file is how the steps know N, the windows of a segment, and which segments to run.
- **The matrix's middle segments** are 6 (EVM Zone, 12 segments of 8 windows) and 3 (Cyclone, 6). At 2²⁹ the
  five are the first two, the middle one, and the last two that have rows: 0, 1, 42, 83, 84 of EVM Zone's 85 and
  0, 1, 44, 87, 88 of Cyclone's 89. The 2²⁹ runs are with a static base table and each frame in its smaller
  form.
- **A segment with no recorded windows is checked without them** (the matrix's 8-window segments, the 2²⁹ runs):
  the Tracer's last record against gold's record s + 1 is then the check of the trace, with the tiles' own.

