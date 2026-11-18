# The master tile's firmware: the contract, v1.37

*Chapter 30 of the machine specification (`spec/machine-spec.md`), which keeps a stub there. Section numbers are the
machine specification's. What the master tile's firmware must do, with Ingress/Egress (17.6), the Updater (17.10)
and the frames (29), for one proof on one ASIC: at boot, with each frame, with the Updater's runs and the free page
queue, at the hand-over to the tracer tile, at the segment's end, and on an error. A contract, not a program: any
firmware that keeps it is correct. Its working version is meshsim's `fw/cc/master.c`.*

### 30.1 What the image is given

The master tile starts from an image placed in its IMEM and DMEM before the reset ends (`boot_local`, 6.5), with
these **arguments** (6.5: data at addresses the image fixes, with no hardware of their own):

| Argument | What it is |
| --- | --- |
| *N* | the number of ASICs, so of segments: 64, 128 or 256 (29.1) |
| *s* | the segment this ASIC runs, 0 to *N* − 1 |
| the compute tiles' image, the tracer tile's image | the images it loads at their `LOAD`s (6.5) |
| the tracer tile's tables and templates | the decode table, the ROM and the core-fed templates (17.9.4) |

**How the image and its arguments reach a chip** is the master tile's boot over LVDS, which is not designed
*(item 166)*; until it is, they are placed before the reset ends, as the simulator places them. What no frame
carries is item 192.

### 30.2 The boot

1. **The UART,** if the image prints: one store of its setting to `UART_CONTROL` (17.6.9), which is 0, the UART off,
   at the reset.
2. **Its masks:** its tile's and Ingress/Egress's `EVENTS_MASK`, for the events it waits on below. Ingress/Egress's
   resets to 0 (17.1).
3. **The loads, as v1.36's** (6.5): it answers every compute tile's `LOAD` with its image, and releases once every
   compute tile has arrived. **It holds the tracer tile's load back** until its segment is ready (30.5): the tracer
   tile waits in its boot ROM.
4. **The segment's first barrier:** every compute tile's program has started with nothing armed; its release lets
   the tiles arm for their records (17.9.10).
5. **The page map:** one bit for each 4 KiB page of LPDDR, every page free but page 0, which is never handed out.
   In v1.37 it frees nothing: every page it hands out is new (23.6.8).
6. **The host's frames** then start to arrive; nothing waits for them but the loop of 30.3.

**The watchdog** (17.6.2) is the firmware's to set: no frame or link packet may be relied on to arrive in a bounded
time, so a limit, if one is set, covers the longest wait the host may make.

### 30.3 The frame loop

**Every frame arrives on the background stream** (29.1). The firmware keeps track of frames; Ingress knows words.

**A wake:** on `background_service` (17.6.7), and on every other wake, the firmware loads `DMA_FORWARD_COUNT` and
`BACKGROUND_WORDS`, and acts only on a count of 0 with words waiting (17.6.13). **A flag is a reason to look, never
proof.**

**A frame's start:** it copies one 128-bit word, the frame's header word, into its own memory with Ingress's DMA,
`DMA_HDR.queue` 1, armed on one of its own counters, and waits for that counter's landing; `dma_done` may stay masked
off (17.6.4). Then, by the header's `type`:

| Frame | What the firmware does with its data |
| --- | --- |
| 1, 3, 4: header frames | copies it into its own memory with the DMA, in pieces of at most `BACKGROUND_WORDS`, and reads its fields as they land |
| 2, 5, 6: one run each | sets up and starts the Updater's run (30.4), then stores the frame's `length` in `DMA_FORWARD_COUNT`, once, `discard` 0 |
| 7: register states | drops the records before *s* with one discard count; copies records *s* and *s* + 1; drops the rest |
| 8: advice logs | copies the *N* counts; drops the logs before *s*; copies log *s*; drops the rest |
| 9: memory segments | the Updater's three runs (30.4), with one count for the whole frame |
| any other | drops it, with one discard count of its `length` |

- **The bulk data never passes through the core:** a frame of update words is one wake, a run's set-up and one store
  of the count, whatever its length. The next `background_service` is the next frame's header word.
- **A count is stored, or a background transfer started, only after a load of the count reads 0,** with no
  background transfer in flight (17.6.13). Drops that follow one another are one count.
- **A frame for the Updater waits for the last run's `done`** before it sets up the next run; a frame that is not the
  Updater's goes ahead, and the run overlaps it. So frames 7 and 8 go on while frame 6's run does.
- **What it keeps of the header frames:** frame 1's file length and frame 2's counts; frame 3's counts and page
  counts, from which it derives frame 9's three runs (29.7); frame 4's *k*, *p*₁, *p*₃ and frame 5's counts, and
  whether it names a path (a base table) or not (the proof's table, from the last base established; item 189). The
  strings it passes over: v1.37 has no file system.

### 30.4 The Updater's runs and the free page queue

Each run (17.10.5) is set up while the Updater is idle: `RUN_CONFIG`, `WORD_COUNT`, `PAGE_COUNT` and the roots,
then `START`.

| Frame | The file in `IMAGE_ROOT` | `RUN_CONFIG.root` | `continue` | `tracer_copy` | `WORD_COUNT` | `PAGE_COUNT` |
| --- | --- | --- | --- | --- | --- | --- |
| 2 | the program image | 1, empty | 0 | 0 | frame 1's 64-bit words | frame 1's pages |
| 5, a base table | the base table | 1, empty | 0 | 0 | frame 4's | frame 4's |
| 5, a proof's table | the proof's table | 1 for a full new table; 2 from the base (`SOURCE_ROOT`) | 0 | 0 | frame 4's | frame 4's |
| 6 | the cycle-0 image | 2, from the program image | 0 | 0 | frame 3's data image words | frame 3's data image pages |
| 9, before *s* | the start image | 2, from the cycle-0 image | 0 | 0 | the first *s* segments' words | ASIC *s*'s start image pages |
| 9, segment *s* | the final image; the Tracer's copy in `TRACER_ROOT` | 2, from the start image, both | 1 | 1 | segment *s*'s words | twice ASIC *s*'s Tracer's copy pages |
| 9, after *s* | the final image | 0 | 1 | 0 | the rest | ASIC *s*'s final image pages less its Tracer's copy's |

- **Every root is a new page** from the page map, written to `IMAGE_ROOT` (and `TRACER_ROOT`) before the run; it is
  not taken from the free page queue and not counted in `PAGE_COUNT` (29.4). `depth` is the file's (29.4).
- **The free page queue gets exactly `PAGE_COUNT` pages for each run,** from the page map, a page an entry in
  128-byte units, under its credit: up to 128 entries for each `free_page_credit` (17.10.4). The firmware refills
  it as credits come back, while the run goes on.
- **Frame 9's count** is stored once, for the whole frame, after the first run is started. Between the first run's
  `done` and the second's `START` the stream waits in the background queue and the word port: nothing else is
  needed.
- **At every `done`, `PAGE_COUNT` reads 0:** the frames' counts are exact (29.7).

### 30.5 The hand-over to the tracer tile

**At the second of frame 9's runs' `done`, the Tracer's copy is complete:** the firmware starts the third run, and
the stream goes on, while it hands the tracer tile its segment.

1. **It writes record *s*'s files word** (word 66): the Tracer's copy's root and depth as `memory_root` and
   `memory_depth`, the proof's table's as `map_root` and `map_depth`; `log_k` and `io_size` as the frame had them.
2. **It answers the tracer tile's `LOAD`,** held back until now (30.2), with one load of one kind, uncounted, from one
   sending unit, so that it lands in order (6.5): record *s* where the tracer tile's program keeps its record; record
   *s* + 1, the end state the tracer tile checks its segment against (17.9.11), with no check for the last segment;
   advice log *s*; the proof's *p*₁, *p*₃ and *k* and log *s*'s count; then the tracer tile's image and the load's `COUNT`,
   last. The tracer tile's program finds its segment in place when it starts.
3. **The line between the special tiles** (16.1) carries the load from the master tile to the tracer tile; the
   master tile paces it as one unicast stream (16.3).

### 30.6 The segment

As v1.36's (17.9.10, 17.9.15): the tracer tile traces the segment's 64 windows and sends each to its compute tile;
each compute tile arrives when its window has landed; at the **second barrier,** with every window landed, the master
tile releases and every compute tile replays; at the **third,** the passes end. The master tile serves the requests
it receives meanwhile, the tiles' results and the tracer tile's among them, and the Updater's third run ends.

**The tracer tile's result** carries its last record, the Tracer's record at the segment's end (17.9.2), to the
master tile (owner): the master tile does not reach into the tracer tile.

### 30.7 The segment's end: the reply

When the segment has run (the three releases, every result in, the Updater's last `done`), **the master tile sends
the reply** (29.14):

1. **It builds the frame in its own memory:** 72 128-bit words, the header word first, `length` 71.
2. **It stores 72 in `EGRESS_COUNT`** (17.6.8), which is 0.
3. **It writes the 72 words into part 1 of its own region 0x04** with 128-bit `WRITE`s, from one sending unit, in
   order. 72 words take one credit of the two the reset gives, so no wake is needed; a reply of more than 256 words
   would wait on `egress_credit` between each 256.
4. Egress sends it as three link packets on the foreground (17.6.8), behind any it is forwarding from below.

### 30.8 On an error

**Every error stops the chip** (17.6.12; owner). An error of the master tile's own, Ingress/Egress's or the Updater's
among them, raises its `ERRORS.peripheral_n` and its NMI (6.2), and the toast pin (17.6.2); another tile's reaches it
through `tile_error`. The handler reports and parks, as v1.36's does (6.5, 22.28). **Nothing is recovered:** what the
host does then, draining the queues, a ready message and the next proof are not designed in v1.37 *(item 181)*.

### 30.9 What it costs

`[meas: v1.37's simulator]`, the forward count, the link at a byte a cycle, the 37 proof runs:

| | |
| --- | --- |
| the master tile's wakes a proof, on the small guests | 46 to 54 |
| a frame of update words | 2 to 4 wakes and 450 to 1,200 cycles awake, whatever its length |
| the master tile awake, of the link's time to the hand-over, at 2²⁹ | 2.4 % to 7.0 % (EVM Zone 4.5 % to 7.0 %, Cyclone 2.4 % to 4.1 %); 2.0 % to 4.2 % without the boot |

What is left is a cost a proof, not a word: the boot (about 86,000 cycles), parsing the proof header (about 14,000 at
*N* 64) and frame 8's counts. A proof is 50 to 100 ms (owner), of which this is 0.2 % to 0.6 %.
