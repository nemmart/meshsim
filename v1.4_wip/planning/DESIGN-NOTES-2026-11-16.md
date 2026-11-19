# Design notes, 2026-11-16: the cache as a controller, updates that write through, and the order of the work

The third day of the discussion with the owner, continuing `DESIGN-NOTES-2026-11-15.md`. No code was written. The
same marks: **Decided (owner)**, **Proposed**, **Open**. Where this note and the earlier two disagree, this one is
later.

The same day's earlier work, after Tony's review and the first pass over v1.36, is section 7 of the second note.

## 1. The cache is a controller and a cache memory

- **Decided (owner): the cache box is two things.**
  - **The cache memory:** the data memory and the tag memory, 512 KB in 8 ways by 512 sets. These are plain
    memories, the kind a memory macro gives.
  - **The cache controller:** a state machine and its circuitry. It walks a file's index, fetches lines, and
    serves every kind of request.
- **Decided (owner): the controller is designed as a real unit, with detailed timing.** It is no longer the black
  box of the second note's section 1. The owner counts it with the Updater's as one of the design's "two
  controllers with moderate complexity".
- **A memory macro does none of the page operations.** Discarding a page's lines, or writing them back, is a
  sequence the controller runs over the page's 32 lines.
- **In v1.36's terms** the controller is the translator together with the control of the shared cache, which v1.36
  leaves deferred behind the `fill` port (`V136-SCOPE.md`). So it is this design's to specify.
- **Proposed: the controller's parts.**

| part | what it does |
|---|---|
| the front end | takes requests from the Tracer's fill port and from each look-up port, and picks the next |
| the walker | steps down a file's index from its root, one look-up in the cache memory for each level |
| the miss handler | fetches a line from LPDDR on the cache's AXI port; picks the line to evict, and writes it back first if it is dirty |
| the page sequencer | goes over a page's 32 lines, for a discard |
| the replies | drives the shared reply bus and the fill port's answers |

- **Proposed: it is built in two steps:** first one request at a time, then serving hits while a miss waits.
- **Open:** whether the figure's cache box is split into the controller and the cache memory.

## 2. Flush and discard

- **Decided (owner): they are two operations, and are named apart.**
  - **Flush:** write a page's dirty lines to LPDDR, and keep them.
  - **Discard:** throw a page's lines away without writing them. This was "drop a page" in the second note.
- **Decided (owner): a page that is handed out again is discarded, not flushed.** Its old contents are dead, so
  nothing in them needs to reach LPDDR.
- **Discard is as the second note had it:** the file system sends it when it hands a page out (proposed there, and
  still proposed), so one place covers every client.
- **Proposed: flush leaves the look-up interface.** Its one use was the Updater's direct read of a shared index
  page, which could be stale in LPDDR while the cache held a newer line. Section 3 removes that case.
- **The look-up interface's operations are now three:**

| operation | request | reply |
|---|---|---|
| look up | the file's root, its depth, a page number | the entry where the walk stopped, and that level |
| update | the same, a level, a new 32-bit entry | done, **once the changed line has landed in LPDDR**; or an error |
| discard a page | a page | done, once its lines are discarded |

- **Open:** whether anything ever needs the Tracer's dirty data lines in LPDDR: a flush at the end of a trace, or at
  shutdown. The Tracer's copy is consumed by the trace, so perhaps nothing does.

## 3. Updates write through

- **Decided (owner): an `update` writes its line through to LPDDR, and its reply comes only after the write is
  confirmed.**
- **Decided (owner): only updates from the look-up interface do.** The Tracer's writes through the fill port stay
  dirty in the cache, and are written back when their line is evicted, as before.
- **What follows:**
  - **An index line is never dirty.** Only the Tracer's data lines are.
  - **Once a client has its reply, LPDDR holds the new entry.** A read of that index page issued afterwards sees
    it, whether it goes through the cache or directly to LPDDR.
  - **The Updater's fault on a shared index page loses its first step.** The second note's section 4 began it with
    a flush; now the Updater simply reads the page.
  - **The first note's worry about shutdown is met.** Recovery's tree walk needed the index pages in LPDDR to be
    current; they always are.
- **The cost:** each update takes one line write, about 80 cycles. The Updater already idles for each page write's
  confirmation, so this is a second wait of the same kind.

## 4. The sequence of an `update`

**The working design.** The owner asked for the sequence so that the reply does not return until the write is
complete, and left it to this session to work out (2026-11-16). The cache controller's simulation is what tests it.

1. **Accept the request** from a client's look-up port: the file's root and depth, the page number, the level and
   the new entry. That client's port is now busy.
2. **Walk to the level.** Each step is one look-up in the cache memory for the index line on the path. A line that
   is not in the cache is fetched from LPDDR, and the walk continues.
3. **Check the target line.** If an earlier update's write to the same line is still in flight, wait for its
   confirmation.
4. **Change the entry in the cache,** 32 bits of the line, and mark the line "write in flight". It is never marked
   dirty.
5. **Send the whole line to LPDDR** on the cache's AXI port: the address, then the line's 8 beats.
6. **Park the update and carry on.** The controller notes which client waits on which line, and serves other
   requests.
7. **The write's confirmation arrives.** The line's "write in flight" mark is cleared; the line is clean.
8. **Reply to the client:** done, or an error if LPDDR reported one. Its port is free again.

The reply is step 8, and nothing before step 7 can produce it.

- **Other requests may read the line while its write is in flight.** They see the new entry, from the cache.
- **A line in flight is not evicted and not discarded** until step 7. This is the second note's rule for a line
  being written back.
- **The controller is not strictly one request at a time,** because of step 6. Holding everything until the
  confirmation would make a Tracer read wait about 80 cycles behind every update.
- **One update is in flight at a time,** so parking needs a single slot. A second update waits at its port. With one
  slot, step 3 never has anything to wait for; it matters only if there is ever a slot for each client.
- **Open:** whether that should be one slot for each client instead.

What the sequence rests on, checked when it was worked through:

- **The data is in LPDDR before the index points to it.** A client writes its page, waits for that write's
  confirmation, and only then sends the update. So a reader that sees the new entry, in the cache or in LPDDR, finds
  the page behind it.
- **A direct read sees the update because of the wait, not because of ordering on the bus.** The cache's write and a
  client's later read are on different AXI ports, and AXI promises no order between them. The reply is held until
  the write's response, so a read issued after the reply is after the write.
- **One writer for a file at a time.** Two clients updating the same index page would each read it directly while
  the other's update was in flight. The file system keeps that from happening; the cache does not.
- **An `update` only ever writes an index line,** and the Tracer only ever writes data lines, so a line is never both
  dirty and in flight for an update.
- **Proposed: the walk checks ownership.** If an index page on the path is not the file's own, the cache replies with
  an error and writes nothing. The client is meant to have copied the page first; this stops a mistake there from
  changing a frozen file.
- **An error from LPDDR goes back in the reply.** The line in the cache then holds an entry LPDDR does not; the
  client treats it as fatal (`ERRORS.lpddr_error`).

## 5. The order of the work

The usual flow is three sessions: a spec'ing session that updates the full system specification, a simulator
session that brings the simulator to it, and a coder session that updates the firmware and runs the tests.

- **Owner's thought: full software implementations of the two controllers come first,** as a step before the
  hardware. The two are the Updater's controller and the cache controller.
- **Proposed: the order.**
  1. **Gold writes the frames** (`HOST-FRAMES.md`), as already decided.
  2. **The two controllers are prototyped in software,** in a small harness of their own: the frames in, a
     transaction-level model of AXI with an LPDDR image behind it, and the four images checked against gold's.
  3. **The spec'ing session** writes the specification from the working prototypes.
  4. **The simulator and coder sessions** follow as usual.
- **Proposed: two rules for step 2.**
  - **They are written in the shape of hardware:** explicit states and registers, and a function that advances one
    clock, so that the RTL can be read off them.
  - **They are written in C++,** so that they move into the simulator as its units and are not thrown away.
- **Proposed: within step 2, the Updater first,** against a simple stand-in for the cache; then the cache
  controller replaces the stand-in.
- **Open:** whether the prototypes are cycle-level from the start, or a functional model comes first.

## 6. What this changes elsewhere

| where | what |
|---|---|
| the second note, section 1 | the cache is no longer a black box in the deliverable |
| the second note, section 3 | the operations are look up, update and discard; "drop" is "discard"; flush is gone |
| the second note, section 4 | the fault on a shared index page no longer starts with a flush |
| the first note, section 1 | recovery's walk no longer depends on a flush at shutdown |
| `UPDATER-INTERFACE.md` | the Updater's look-up port carries look up and update |
| the figure | unchanged for now; the cache box may be split (section 1) |

## 7. Open, beyond what each section lists

- The look-up interface's requests and replies, as exact fields.
- How the front end chooses between the Tracer's fill port and the look-up ports.
- Whether the Updater's roots and counts are window registers or a state record (carried from the second note).
- The walk through the Updater's registers one at a time, which was begun and not finished.

## 8. Building on a simulator, not a harness (later the same day)

- **Decided (owner): the two controllers are built on an existing version's simulator,** not in a harness of their
  own as section 5 proposed. The real Tracer running on the Updater's images is then the check.
- **Decided (owner): the work is in `updater_wip/`, and is not tied to a version.**
- **Tony pushed v1.36's firmware the same day:** v1.35's C firmware upgraded to v1.36's hardware, and a new program
  for the memory tile (`v1.36/fw/cc/memory.c`), which serves read and write requests through the RW engine.
- **v1.36's own checks pass on the owner's runner** (Quest 198 and 199, at main 60ec71cf): `run_tests.sh all --check`
  in 183 s and `make check` in 770 s, on 16 threads. They include the trace and replay of three real segments, the
  sort at full size, and the memory tile's requests.
  - One step was skipped: `fw-build`, which rebuilds the firmware images. It needs xPack `riscv-none-elf-gcc`
    15.2.0-1, which the runner does not have. New firmware on v1.36 will need it there.
- **Decided (owner): v1.36 is the base.** Its names and register layout are the ones this design uses, so nothing is ported
  later, and `memory.c` is where the firmware that minds the Updater starts.

## 9. Gold's frames, and the rulings on them

Gold now writes the frames, reads them back, and builds reference images from them (`updater_wip`, WORKER1; its
`REPORT.md` has the checks and the sizes). Its report made ten suggestions for `HOST-FRAMES.md`; the owner went
through them one at a time. `HOST-FRAMES.md` now has all of this.

| | ruling |
|---|---|
| the frame header | **Decided (owner):** one 128-bit word. Bits [7:0] the compression type (0 none, 1 LRU), [63:8] reserved, the type in [95:64], the length in [127:96]. Lengths and counts are 32 bits, never 64 |
| 1. the program image | **Decided (owner):** frame 1 is "program image" (not "ELF"), sent as update words in whole pages into an empty file. **The Updater makes the file,** which saves a firmware path |
| 2. the base jump table | **Decided (owner):** the same treatment |
| 3. file lengths | **Decided (owner):** no field. A copy has its source's length; a jump table's follows from k. A file's length is fixed when it is made, so no update ever adds a level |
| 4. roots and page counts | **Decided (owner):** the file system's firmware makes every root, by a copy, before the Updater runs. Page counts exclude the root. A page written in several segments counts once for each file |
| 5. the data image | **Decided (owner):** gold sends the least data; it may skip zero words, and what it skips is zero underneath |
| 6. the program id | **Decided (owner): dropped,** with the `.sha` file. The host manages what it loaded |
| 7. the segment count | **Decided (owner):** *N*, the number of ASICs: 64, 128 or 256. *N* and *s* are the system's configuration, told to each ASIC at start-up ("69 of 128"), and are in no frame |
| 8. compression | **Decided (owner):** the type is in the frame header, and each frame is sent in whichever form is smaller. LRU stays: it is about 2,000 flip-flops and halves the stream |
| 9. base tables | **Decided (owner):** the host establishes them. A run's own table is dead when the run ends |
| 10. the per-ASIC page counts | **Decided (owner): kept.** The firmware allocates a run's pages ahead of time from them, to keep the free page queue full |

What follows for the Updater:

- **Decided (owner): memory segments is two runs.** The Updater applies the words before segment *s* to the start
  image and raises `done`. The firmware freezes the start image and makes the final image and the Tracer's copy.
  The Updater is started again for segment *s* and the rest, while the stream waits in the segment queue.
- **Decided (owner): minding the Updater is the file system firmware's job,** as minding the Tracer is the tracer
  tile's. (*Later the same day the Updater moved to the master tile, and the minding with it: section 10.*)
- **`SRC_ROOT` and the "empty" setting leave the Updater:** it always starts on a file that exists and has a root.
- **No new modes** (owner's condition): a program image and a base table are the whole-pages run into an empty
  file that a full new jump table already is.
- **Proposed:** the program image becomes a header frame and a data frame (frames 1 and 2), and a base table uses
  frames 4 and 5 with a path, so that each frame has one destination and its data starts on a 128-bit word.

What gold measured, both workloads at 2²⁹:

- **The Updater's load is small:** 1,761 pages written over a whole proof on EVM Zone, 6,093 on Cyclone; one ASIC
  needs at most about 2,000 pages for its start image.
- **Frame 9 is at most 17.7 MB raw;** LRU brings it to 0.53 to 0.61 of that.
- **The static table as the base takes nearly every target:** a run adds 0 to 3 pages, against 257 for a full table.
- **Every image is depth 2.**

## 10. The Updater moves to the master tile

Reached while working out how a frame gets from Ingress to the Updater. In v1.35's names the master tile is C&C 1,
the memory tile C&C 2 and the tracer tile C&C 3.

How the question arose:

- **The owner's first sketch:** a frame comes in on Ingress; the master tile reads its header word; it sends the
  file system a request to set up for the frame; the file system allocates room and says where to write, with a
  counter, as an open does.
  - **Decided (owner): a request is the way between the special tiles.** v1.36 has it already: any tile, the master
    tile included, sends a 128-bit request to the memory tile, and the reply names an address and an armed counter
    (machine-spec 11.5, the memory tile's `WRITE`). It has no mailboxes.
- **The owner's objection:** the frames that go to the Updater carry no file name; they rely on something having
  been set up earlier.
- **The owner's question: what does the Updater get from the file system?** Very little:

| what the Updater is given | where it really comes from |
|---|---|
| the file's root, and the Tracer's copy's root | the file system: it turns a path into a root, creating or copying the file |
| free pages | whoever owns the page map |
| the depth | the file's length |
| the word count, the page count, the compression type | the frame |
| which run comes next, and `START` | whoever runs the proof's sequence |

So:

- **Decided (owner): the Updater is the master tile's peripheral,** beside Ingress/Egress, not the memory tile's.
- **What it buys:**
  - **The frame's data never crosses the mesh.** Ingress and the Updater are on one tile. The credits across tiles,
    the private signal from the Updater to Ingress and the worry about the control row's bandwidth (first note,
    section 8) all go.
  - **Ingress and the Updater have to be integrated, and that is easier on one tile** (owner): "less weirdness".
  - **The master tile has everything from the frame,** so it writes the Updater's registers itself. The names and
    the data are handled by one core, in order, which answers the owner's objection.
  - **The file system becomes a plain service:** create a file, copy one, freeze one, and give back a root.
- **What it costs:**
  - **The master tile runs the proof's sequence:** which copy when, and the freeze and the two copies at the cut.
    Section 9 gave "minding the Updater" to the file system's firmware; it is now the master tile's.
  - **Free pages come from another tile.**
  - **The Updater's AXI port and its look-up port reach across to the memory side.**
- **Decided (owner): that distance does not matter.** The Updater and the RW engine work in big bursts, so latency
  is nothing to them. Where latency may matter is the tracer tile and the cache.
- **The page map is probably the tracer tile's** (owner). For this deliverable the stand-in on the memory tile
  (second note, section 1) stands; the master tile asks for pages by request either way.
- **"The master tile's peripheral" says which core controls it and where its data comes from, not where it sits.**
  The Updater could sit by the arbiter and the cache with its control and its feed stretched instead. That is a
  floorplan question, put to Tony (`RTL-CHANGE-REQUESTS.md`, comment D).

**Proposed: one frame's path.**

1. Ingress tells the master tile a frame has arrived. The master tile reads the header word, then the names and the
   counts.
2. It asks the file system for what it needs, by request: create, copy or freeze a file; the root comes back. It
   fetches the run's page list into its own memory.
3. It writes the Updater's registers, fills the free page queue, and starts the run.
4. Ingress streams the data into the segment queue, on the same tile, paced by the queue's credit.
5. The master tile refills the free page queue from its list as credits come back.
6. On `done` it does the sequence's next step: the freeze and the two copies at the cut, then the next run.

**Open:** whether a frame for the Updater should carry its own head (the names and counts first, then the data,
with the head's length in the frame header), instead of a header frame before it. It was proposed in answer to the
owner's objection, before the move; with one core handling both frames in order it may not be needed.
`HOST-FRAMES.md` is unchanged.

## 11. The Updater's registers, walked through

`UPDATER-INTERFACE.md` now has them. What was ruled:

| | ruling |
|---|---|
| `RUN_CONFIG` | **Decided (owner): fine as proposed.** There is no `type`: every run applies a number of update words to a file, and three bits say how: `lru`, `continue` (the same frame as the last run) and `tracer_copy` |
| the depth | **Decided (owner):** a 2-bit `depth` in `RUN_CONFIG`, named as the Tracer's `memory_depth` is. No file length: the Updater needs the depth and nothing else |
| the two roots | **Decided (owner): `IMAGE_ROOT` and `TRACER_ROOT`.** The first is the file a run updates, whichever file that is; the second is the Tracer's copy |
| the page count | **Decided (owner): `PAGE_COUNT` is read/write and counts down.** The firmware writes how many pages it will queue; read while running it may be stale. Needing a page at 0 sets an error bit, `out_of_pages` |
| left-over input | **Decided (owner): never an error.** The replayer has `ERRORS.leftover`; the Updater must not, since its stream runs on across runs. A run's end touches nothing |
| the two queues | **Decided (owner): the replayer's and the Tracer's credit scheme, identically.** 256 entries; a credit is half a queue, counted to depth 2, returned as 128 entries leave, taken by a load of `EVENTS` |
| the word count | **Proposed:** `WORD_COUNT`, read/write and counting down as `PAGE_COUNT` does, in place of a count and a separate progress register. The owner asked what it counts: the words of this one run, never the total |
| three runs for memory segments | **Proposed:** before *s*, segment *s*, after *s*. The Tracer's copy is complete when the second ends |
| window words, not a state record | **Proposed.** The Tracer's depths are in its state record because that is its working state |

**The owner's sequence for the images**, which the runs follow:

1. Build the cycle-0 image.
2. Make a copy of it.
3. Run the segments up to *s* into the copy.
4. Freeze it and make two copies of it. (The owner said three; the frozen file is itself the kept copy.)
5. Run segment *s*'s updates. For every page written, the data goes into one file, and the other file gets a copy
   of the page with its original data.
6. Run the rest into the first of those.

## 12. The link from the host

The owner then made notes on Ingress and the link under the frames. They are `HOST-LINK.md`: two streams (the active
proof and the background), chunks with a 128-bit header word, the two checks, "resend", and recovery by zeros and
"I'm lost".

## 13. What sections 10 to 12 change elsewhere

| where | what |
|---|---|
| the figure | stale: it shows the Updater by the RW engine, fed over the peripheral bus, with credits going off to Ingress |
| `HOST-FRAMES.md`, the "goes to" column | written when the Updater was the memory tile's; the master tile now reads the header frames |
| the first note, section 8 | the credits from the Updater to Ingress across tiles are gone |
| the second note, section 4 | the segment queue is fed on the master tile, and the free page queue by the master tile's core |
| `V136-SCOPE.md`, outcome 3 | the Updater is the master tile's peripheral 2, not the memory tile's |
| `updater_wip` | the Updater is built on the master tile; the firmware that minds it is the master tile's |

## 14. v1.37: something that works, with black boxes

The owner's plan for the next version, on v1.36 (he called it a moving target, and this is where it stands):

| piece | in v1.37 |
|---|---|
| the input | **Decided (owner):** the simulator is started with a file, the data that comes over Ingress: gold's frame file, **packetized** with the right CRC values. Every packet is on the background stream |
| Ingress | **Decided (owner): a black box.** It presents the file's packets to the master tile (`INGRESS-INTERFACE.md`). A packet that fails its CRC is fatal |
| the master tile | **Decided (owner): real firmware,** which drives the Updater |
| the Updater | **Decided (owner): a black box, a software implementation.** Proposed: behind the registers and queues of `UPDATER-INTERFACE.md`, so that the firmware written against it stands when the real controller replaces it |
| the cache | **Decided (owner): a black box, a software implementation.** v1.36's simulator has one already: the model of the memory system behind the Tracer's fill port, on a copy-on-write file store |
| the memory tile | **Decided (owner): nothing runs on it.** The Tracer's output goes from the tracer tile straight to the compute tiles (checked in v1.36's `fw/tracer/tracer_ctl.c`) |
| the page map | **Decided (owner): on the master tile, for now.** The 128 KB bit array, one bit for each 4 KB page, moves there, so the master tile knows which pages are free and asks the memory tile for none |
| a file's root | **Proposed (the owner: "sure"; all of this will be refined): the Updater black box makes it,** as two extra commands: an empty root, or a copy of another file's root with the owned bits cleared. The master tile picks the root's page from its bit array (`UPDATER-INTERFACE.md`) |
| the file system | **not in v1.37.** v1.36 has none either: no firmware there knows about files; its copy-on-write file store is called only by the C++ test harnesses, to build the Tracer's images before a run |
| the check | the Tracer traces on the images the Updater built, and the images equal gold's reference (`frames-images`) |

- **Why black boxes first** (owner): "this way we can have something that actually works". The two real controllers,
  the Updater's and the cache's, then replace their black boxes one at a time, with everything around them running.
  This takes the place of section 5's order and section 8's.
- **What it replaces in v1.36:** today a test harness, "standing in for C&C 1 and C&C 2", applies the update stream
  and makes the Tracer's copy and the jump table before a run. v1.37 puts the frame file, the master tile's firmware
  and the Updater in its place.
- **The interface between Ingress and the master tile was settled first** (owner: a worker would otherwise "ask us a
  million questions about it or get it completely wrong"). It is `INGRESS-INTERFACE.md`:
  - a service flag and a status bit for each stream, and a register giving the words in the packet that asked;
  - no further flag on a stream until that many words have been read out;
  - reading out by Ingress's DMA, in pieces: one word for the frame's header, then the rest wherever it belongs;
  - `dma_done` and a counter tell the core a transfer is complete;
  - a packet is at most 32 words, and a frame ends at the end of a packet;
  - the Updater's credits come to the master tile to manage.
- **The streams are named foreground and background** (owner), not active and background.
- **A thing to know about v1.36's simulator:** the RW engine's LPDDR and the file store's LPDDR are two separate
  models. The Tracer's images are in the file store, and the memory tile's read and write requests do not reach it.
- **Open:** where the work lives. Proposed: `v1.37_wip/`, a copy of v1.36, as earlier versions were made.

## 15. v1.37 runs, and what it showed

**v1.37 works end to end** (`updater_wip`, WORKER3, merged into `main` the same day; its report is
`updater_wip/planning/sessions/WORKER3-REPORT.md`). Given a packet file, *N* and *s*, the chip builds the images,
traces segment *s* on them and replays it.

- **What passes:** `segment-real`'s three segments from packet files; 24 runs on the small inputs (raw and LRU,
  with and without a base table, *s* = 0, 1 and a middle one); 10 runs at 2²⁹ on both workloads; and everything
  v1.36 passed (Quests 204 and 205 on the owner's runner). In every run each image equals gold's, `PAGE_COUNT` is 0
  at every `done`, and no error bit is set.
- **The tracer tile's firmware did not change.** The master tile holds its load back and sends its segment's
  record, advice and roots in front of its program.
- **The packetizer and the Ingress black box agree,** written apart from the documents (WORKER2 and WORKER3).

What the figures say (the simulator's cycles, with the black boxes' timing, not the RTL's):

| | EVM Zone 2²⁹ | Cyclone 2²⁹ |
|---|---|---|
| the packet file | 4.4 MB | 16.9 MB |
| cycles to build the images, about | 4 M | 17 M |
| cycles for the whole run | 27 to 31 M | 31 to 42 M |
| pages of LPDDR a proof takes | about 1,000 | about 4,000 |
| the Updater's busy cycles spent waiting for data | 57% | 60% |
| the master tile's core awake | 9 to 10% | 25 to 34% |

- **The link sets the pace of the images;** the Updater hardly ever waits for pages.
- **Tracing dominates:** about 24 to 26 M cycles for a segment of 2²² rows.
- **The third run of the memory segments was worth having** (section 11's proposal): the Tracer starts at the second
  run's `done`, and the third overlaps tracing entirely.

What it left for the design:

| finding | where it stands |
|---|---|
| the firmware wakes for every packet, about 330 cycles against the packet's 528 on the link | **Decided (owner): a threshold.** The master tile tells Ingress how many words to gather before waking it: 1 word to start, then 128 once the frame's length is known (`INGRESS-INTERFACE.md`) |
| the size of a packet | **Decided (owner): small,** 512 bytes or perhaps 256, because every hop of the daisy chain and the FPGA can hold a foreground packet behind a background one. 1,024-byte packets and a second, one-line link for the foreground were considered and not taken (`HOST-LINK.md`) |
| the last segment has no record *N* to check its end state against | open |
| a segment's rows are in no frame | open; nothing needs it while a segment is always 2²² rows |
| the decode table and ROM, the templates and the tiles' firmware are in no frame | still placed by the harness; whether they become frames is open |
| frame 8's logs start on a 128-bit word only because *N* is a multiple of 4 | to be said in `HOST-FRAMES.md` |
| what a run does after an error; the unit of a free page queue entry | to be said in `UPDATER-INTERFACE.md` |

The report's full lists, about fifteen points over the four documents, are not yet worked in.

**Next:** the Updater's real controller in place of its black box, then the cache's. v1.37's Ingress and firmware
also want the threshold.

Two more rulings the same evening:

- **Decided (owner): Ingress's buffers are oversized, 16 KB on each stream,** to be sure the far side can be stopped.
- **Decided (owner): the Updater's segment queue is 8 KB, with a credit of 256 words.** Proposed with it: the
  threshold after a frame's header is a credit's worth, 256 words, where the owner first said 128.
- WORKER4 (`updater_wip`) puts the threshold and both sizes into v1.37.

## 16. The threshold built, the two interfaces agreed, and a target for the master tile

- **The threshold is in v1.37** (WORKER4, merged; `updater_wip/planning/sessions/WORKER4-REPORT.md`), with Ingress's
  16 KB buffers and the Updater's 8 KB queue. All 37 proof runs pass, and another 174 with the link at 2, 4 and 8
  bytes a cycle and the threshold at 32, 64 and 128, every image the same.
  - **The master's cost fell by three:** about 100 cycles awake a packet's worth where it was 330; 0.17 to 0.20 of
    the link's time at a byte a cycle.
  - **A transfer still costs 2.3 to 3 wakes,** and the firmware is not tuned.
  - **With a faster link the Updater is the limit, not the firmware:** from 4 bytes a cycle the black box is busy 94
    to 97% of the time, its memory work 53 to 61% of that and decoding the rest. It does the two one after the
    other; a controller that overlaps them moves the limit to decoding, then to the firmware.
- **Decided (owner): the Updater is at most 10% of the master tile's load,** so that its core has slack for the
  command stream. At a byte a cycle that is 53 cycles awake a packet's worth, about 420 for each 4 KB. Today's
  figure is 18 to 23%.
  - If tuning the firmware does not reach it: a larger credit, or Ingress feeding the Updater under credits kept in
    hardware, the firmware waking once a frame.
- **The two real controllers are being built by two workers who share no code** (owner): the cache's by WORKER5,
  the Updater's by WORKER6. Ingress stays a black box.
  - **WORKER5 wrote the interface between them first:** `AXI-INTERFACE.md` and `CACHE-INTERFACE.md`, with a header
    each. They were reviewed and agreed the same day, with one change: a client has one number on AXI and on the
    look-up port.
  - **What they settle:** the look-up port's fields and every reply in every case; the arbiter cutting bursts into
    32-beat pieces that a client never sees; one LPDDR model on the file store's pages; and a stand-in cache that
    holds nothing, which WORKER6 tests against and WORKER5 uses as its reference.
- **The mesh's two regimes** (the owner asked): v1.36's hardware has only the strict one. Links have no flow
  control and a full router queue is always an error. The elastic regime, with a flag in each tile, is recorded in
  v1.36's `spec/OVERVIEW.md` and `GUIDELINES.md` as proposed, "in neither the spec, the RTL nor meshsim". Nothing
  in v1.37 needs it, now that the Updater's stream does not cross the mesh.

## 17. The forward count: the firmware leaves the path of the bulk data (that evening)

Section 16's target, the Updater at most 10% of the master tile's load, was to be met by tuning the firmware. The
owner replaced the question instead.

**The owner's pipeline:** the link; a buffer that holds a packet while it is checked, and accepts or rejects it; a
queue for each stream; then two ways out.

| way out | what it is |
|---|---|
| the DMA | the standard one, with its `queue` field choosing the foreground or the background queue. For what the firmware must see |
| `DMA_FORWARD_COUNT` | a register. While it is not 0, a word a cycle leaves the background queue for the Updater, when the Updater can take one. The only way into the Updater's data input |

- **The Updater's input is flow-controlled, with no credits** (owner): the two units are side by side.
- **The count may be written before the words have arrived** (owner). It waits on an empty queue. So the firmware
  writes a frame's length once.
- **Its top bit says send or discard** (owner). A frame, or part of one, that nobody on this ASIC needs is dropped
  at the link's rate.
- **Service** (owner): Ingress asks when the foreground queue has data, or when the background queue has data and
  the count is 0.
- **Undefined** (owner): a DMA transfer from the background queue while the count is not 0. Forwarding and a DMA
  transfer from the foreground queue may share a cycle.

**How it came to this form.** The owner's first sketch gave the background stream a DMA of its own, with a mode
that sent to the Updater's queue and a count that could run ahead of the words. v1.36's DMA (17.4) has a 12-bit
count, three unused header bits and two reserved modes. Rather than stretch it, the owner cut the idea down to one
register beside the standard DMA.

**What it removes:**

- the thresholds (section 16), built that afternoon;
- the Updater's 8 KB segment queue, its region part and `segment_credit`;
- the firmware's allowance and its transfers of 256 words;
- the tuning of the firmware (WORKER7's first brief, withdrawn before it started).

**Frames 7 and 8 stay as they are.** They carry *N* records and *N* logs, and an ASIC needs two records and one
log. Moving the advice to a file was considered (owner), since each log is short; the discard bit does the same
job with no path from a file to the tracer tile. The firmware discards up to its own, copies it with the DMA, and
discards the rest.

| gold's runs, *N* 128 | all of frame 8 | one log, on average |
|---|---|---|
| EVM Zone | 30 KB | about 15 words |
| Cyclone, small | 2.8 KB | 1 or 2 words |
| Cyclone 2²⁹ | 774 KB | about 380 words |

**Proposed here, not ruled on (†):**

- a queue asks for service only while nothing is taking its words, and a flag is posted when it starts to ask and
  when a DMA transfer from it ends with words left;
- a store of 0 stops the forwarding; a load gives what remains;
- the DMA does not wait for words that have not arrived, because there is one DMA and a foreground message would
  wait behind it. The firmware copies what is in the queue;
- the Updater keeps only a few words behind its port; `EVENTS` [2] is left unused for now.

**Open: the size of the two queues.** The owner's sketch had 4 KB; that afternoon they had been made 16 KB so
that the far side could be stopped in time. The owner: the sizes are arbitrary and change little in the design,
so 8 KB each is pencilled in and the size is to be settled, by the room the chain's packets in flight need.

**The work:** WORKER6 builds the Updater's controller to the word port, with today's segment queue as a shim in
front so that it can be checked on v1.37 as it stands. WORKER7 puts the forward count into v1.37: Ingress's black
box, the port, the firmware. `INGRESS-INTERFACE.md` and `UPDATER-INTERFACE.md` are rewritten to it.

## 18. The cache's controller is built (late that evening)

- **WORKER5's Part B is merged:** the cache's controller with its cache memory, the AXI arbiter and the LPDDR
  model, each a state machine with registers and one function a clock, behind a setting that is off by default
  (`updater_wip/planning/sessions/WORKER5-REPORT.md`). The Tracer runs on it in all 37 proof runs, every image
  equal to gold's. With the setting off, v1.37 is unchanged to the cycle.
- **Its figures:** a hit costs 2 cycles a level (v1.36's model charged 6); a depth-2 look-up 6 cycles on hits and
  173 on misses; an update 87, its write and the response included. Against the model the runs are −2.7% to +4.4%.
  - Faster where a segment mostly hits. Slower where it writes much, because a dirty victim is written back
    before the fetch (the owner's ruling: no list of pending writes).
  - **Recommended by WORKER5, not built:** write a set's dirty least recently used line back early, in place. It
    keeps no list and changes no interface. Recorded in `CACHE-INTERFACE.md`'s Open list.
- **Serving hits while a miss waits earned nothing in these runs,** since the Tracer's own requests never overlap.
  **The owner:** the Updater will often be working on the next proof while the Tracer runs, so in reality they do
  overlap. The proof runs hold one proof each and cannot show it; a run with two proofs, or the joining step's
  tests, must.
- **Found by the arbiter's test:** a client with more pieces at the controller than its limit of 16 can hold a
  line's address behind them. The Updater's page at a time, 8 pieces, stays under it. In `AXI-INTERFACE.md`'s
  Open list.
- **WORKER6's plan is agreed** (the Updater's controller, to the word port, with the shim), and **WORKER7 is
  started** (the forward count in v1.37).

## 19. The link's header: 18-bit numbers and a hops field (just after midnight, 2026-11-17)

- **Decided (owner): the packet number and the acknowledge field are 18 bits each,** down from 20, and the four
  bits go to a new field. A stream's numbers come round after 262,142 packets, about 138 MB.
- **The field counts hops** (owner). Whoever makes a packet sends 0, and every ASIC that forwards it along the
  daisy chain adds one. Towards the host it then says which ASIC sent the packet, by its place in the chain, and
  Egress along the chain is "check, count, forward" with no ASIC knowing its own place.
  - The owner's first form was a source number that each ASIC filled in; the count needs no numbering.
- **The same rule in both directions** (owner). From the host every ASIC gets the same data, so the field is
  ignored there.
- **A hop always computes the header CRC again** (owner), since the field is under it. Left outside the CRC, one
  flipped bit would credit a packet to the wrong ASIC.
- **A chain is probably no more than 4 ASICs** (owner).
- **Every link of the chain is a link of its own** (owner). Packet numbers, acknowledgements, the flags, sending
  again, recovery and "stop" mean something only on the link a header is travelling on. A garbled packet is sent
  again only over the link that garbled it.
  - A forwarding ASIC takes the whole packet in, checks it, writes the outgoing link's fields, adds one to hops
    and computes the header CRC again. It keeps what it has sent on each link until its neighbour acknowledges it.
  - **What the field is for** (owner): the FPGA sorts what arrives by it, and so rebuilds each ASIC's replies to
    a query sent to all of them.
  - **"Stop" for each link:** a full background queue stops the neighbour up the chain, and so on upward, so a
    chain's background stream moves at its slowest ASIC's pace. The room a queue must keep is then one link's
    packets in flight, not the chain's.
  - (The design session first read it as numbers kept between the host and each ASIC. The owner corrected it.)
- **The chain comes before an ASIC's own data** (owner). Towards the host an ASIC sends, in this order: a
  foreground packet to forward, a foreground packet of its own, a background packet to forward, a background
  packet of its own. It fills the gaps with its own.
- **What it changes that is built:** gold's packetizer, its reader and the vectors; v1.37's link header and
  Ingress's black box. The first example header is the same bytes as before. Given to WORKER7.

## 20. The rest of Ingress/Egress: Egress, the UART, the reset (2026-11-17, about 1 to 2 a.m.)

The owner: "I think that's the vast majority of ingress/egress." What was added to the link and the forward count:

- **Egress** (owner): the core builds a frame in its own memory, registers a transmission, and sends the words
  into one outgoing queue. A register holds the frame's length; a credit event says there is room for 4 KB more.
  The firmware turns the event's mask bit on only while a transmission is registered.
  - **Replies are on the foreground stream only** (owner).
  - The owner weighed reading the length off the frame's header word against a register, and took the register:
    Egress then knows nothing of frames, as Ingress knows nothing.
  - † Proposed here: the queue is 8 KB and the credit the replayer's (half the queue, depth 2); the count runs
    down as words are pushed, not as they leave, so the core can queue the next frame without waiting for the
    link.
- **The old host interface goes** (owner, agreed with Tony): `STREAM_SEND`, `STREAM_RECEIVE` and their words, bits
  and events. This design no longer sits beside it.
- **The UART is redesigned** (owner). `UART_TX` and `UART_RX` move to words 30 and 31. `UART_CONTROL`, at 32 or
  above, is a plain register with the divider, enable read and enable write. The FIFO levels and the two line
  errors move into `STATUS`. `UART_DIVISOR` and `UART_STATUS` go.
  - **The value 0 in `UART_CONTROL` is the UART's reset,** and it is 0 at start-up: the core turns the UART on
    with one store. "If the uart is running and no one is polling it, well, it's pointless."
  - **The enables only gate the line,** to ignore noise.
  - What the owner asked first, and what it gives: of the peripherals' registers at 32 and above, only
    Ingress/Egress's had loads that act (`UART_RX`, `STREAM_RECEIVE`). Now none does, on any peripheral.
  - The UART is built in the RTL, so this is request 2 in `RTL-CHANGE-REQUESTS.md`.
- **Ingress/Egress has no reset of its own, and cannot have** (owner): it carries the chain's packets and shares
  packet numbers with its neighbours. The link and the queues the core sees are cleared only at power-on; the
  reset reaches the UART alone.
  - Ways the design session offered and the owner did not take: bits of `UART_CONTROL` that clear when written;
    the enables holding the FIFOs empty; `START` as the UART's reset.
- **In v1.37 none of this is built.** WORKER7 is in Ingress's black box for the forward count and the header's
  new widths. Taking the old words out, the UART and Egress are a later step.

`INGRESS-INTERFACE.md` now covers the whole peripheral.

## 21. Ingress/Egress: where everything is, and where it stands (2026-11-17, about 3 a.m.)

Written at the owner's request, so that a session starting cold can pick this up.

### Where it is written

| document | what it holds |
|---|---|
| `HOST-LINK.md` | the link: 8 LVDS lines; the two streams; the packet and its header (18-bit numbers, hops); CRC-32C, its definition, its hardware and the four units; the order of the tests; a bad packet; "lost" and recovery; stop and resume; **the chain** (every link its own, what a forwarding ASIC does, what is left of a forwarded header, the order of sending) |
| `INGRESS-INTERFACE.md` | the peripheral as the master tile's core sees it: "The peripheral in short"; the registers; the forward count; the word port; the service rule; the DMA; frames; **Egress**; **the UART**; **reset**; errors; "In v1.37" |
| `UPDATER-INTERFACE.md` | the Updater, peripheral 2: its data input is the word port from Ingress |
| `HOST-FRAMES.md` | the frames inside the packets |
| `RTL-CHANGE-REQUESTS.md` | request 2, the UART; comment C, the old host interface removed (agreed with Tony) |
| `v1.37_wip/src/units/word_port.h` | the word port's types |
| this file, sections 12, 14, 16, 17, 19, 20 | how each decision was reached |

### The decisions, in one list (all the owner's unless marked †)

1. **The link:** 8 LVDS lines, about 1 GB/s; packets of a 128-bit header and 0 to 32 words; small packets (512
   bytes, perhaps 256); two streams, foreground and background, that never block each other.
2. **The header:** stream, flags (resend, lost, recovery, stop), length, an 18-bit packet number, an 18-bit
   acknowledge, 4 bits of hops, 8 reserved, the data checksum, the header CRC. Both checks CRC-32C †.
3. **The chain:** probably no more than 4 ASICs. Every link is a link of its own: numbers, acknowledgements,
   flags, sending again, recovery and stop belong to one link. A forwarding ASIC checks the whole packet, writes
   the link's fields afresh, adds one to hops, seals the header again, and keeps the packet until its neighbour
   acknowledges it. The data and its checksum go through untouched. The FPGA sorts what arrives by hops.
4. **Towards the host:** data on the foreground stream only; a forwarded packet before the ASIC's own.
5. **Four CRC units an ASIC,** a byte a cycle.
6. **In:** two queues (8 KB each pencilled in); a service flag and a word count for each; the standard DMA with
   its queue field; `DMA_FORWARD_COUNT`, top bit discard, the only way into the Updater's data, flow-controlled.
7. **Out:** one queue; `EGRESS_COUNT`, the frame's length; a credit event worth 4 KB; the firmware turns the
   event's mask on only while a transmission is registered.
8. **The UART:** `UART_TX` at word 30, `UART_RX` at 31; `UART_CONTROL` at 32 or above, a plain register (the
   divider, enable read, enable write), 0 its reset and its value at start-up; the flags in `STATUS`.
9. **Reset:** none of its own. The link and the host's queues are cleared only at power-on; the reset reaches
   the UART.
10. **The old host interface** (`STREAM_SEND`, `STREAM_RECEIVE` and theirs) is removed.

### What is proposed and not ruled on (†)

- Every word number and bit number of the new registers (`UART_TX` and `UART_RX` at 30 and 31 are the owner's).
- The service rule's details: a queue asks only while nothing takes its words; a zero-word transfer counts.
- A store of 0 to the forward count stops it; a load gives what remains.
- The DMA does not wait for words that have not arrived (one DMA; a foreground message would wait behind it).
- `EGRESS_COUNT` runs down as words are pushed, not as they leave; the outgoing queue is 8 KB, filled by
  `WRITE`s into region 0x04, part 1; its credit is the replayer's.
- With enable write off, a store to `UART_TX` is dropped.
- What the Updater keeps behind the port (4 words as built).
- CRC-32C as the CRC; a stream says stop at half a queue.

### Open

- **The foreground stream's content:** the host's commands and the chip's replies as frames. Nothing is defined.
- The size of the queues (8 KB is a placeholder; with stop for each link the room needed is a packet or two).
- Whether another tile may push into the outgoing queue itself.
- The link's timers and the size of the window of kept packets; whether the recovery packet states its number.
- What a soft reset is (machine-spec item 124), and whether anything short of power-on empties the host's queues.
- Egress's wiring of acknowledgements into outgoing headers, and the way back to the host in general, are designed
  only as far as `HOST-LINK.md` goes: nothing of it is built or simulated.

### Where v1.37 stands

| piece | state |
|---|---|
| gold's frames, the packetizer, the vectors | merged (WORKER1, WORKER2). **The header there is still the 20-bit one** |
| Ingress's black box with thresholds; the firmware moving every word | merged (WORKER3, WORKER4): what `main` runs today |
| the cache's controller, the arbiter, the LPDDR model | merged, behind a setting (WORKER5) |
| **the forward count,** the word port, the firmware waking once a frame | built on `updater-w7` at `18b16507` (WORKER7). Quest 213 passes: the 37 proof runs. Quests 214 (`make check`) and 215 (the link at 2, 4, 8) queued. **Not merged** |
| **the Updater's real controller,** behind a setting, with the segment queue as a shim | built on `updater-w6` at `5c1faf4b` (WORKER6); passes alone, against the black box, and in the chip on `test_master`. Quests 216 to 219 queued. **Not merged** |
| the 18-bit header and hops in the packetizer, the reader, the vectors and `host_link.h` | **not done.** The message asking WORKER7 for it had not reached it when its report came |
| the old stream words out of the black box; the UART as redesigned; Egress | not started, in no brief |

**The figure the owner asked for** (Quest 213): the master's time awake is 2.4 to 7.0% of the link's time at
2²⁹, where the target was 10%. `INGRESS-INTERFACE.md`, "In v1.37", has the table.

### What comes next, in order

1. **Read Quests 214 to 219** when they are back.
2. **Merge `updater-w6`, then `updater-w7`** (the integrator; `git merge --no-ff`). They touch the same shared
   files in small ways (`periph_slot`, `proof_bench`, `proof_run_main.cpp`, `run_tests.sh`, `CHANGES-v1.37.md`).
   WORKER6 compares against the black box with the segment queue, which WORKER7 removes: after both are in, the
   shim goes.
3. **Join them:** in `updater_top.h`, delete the lines marked `shim` and give `UpdaterTop::source` Ingress's
   forwarder; give `UpdaterTop::lpddr` the arbiter's client port 1 and `UpdaterTop::cache` the cache's look-up
   port 1. WORKER7's slot wires peripheral 1's source to peripheral 2's sink by four method names:
   `port_source()` and `port_sent(t)` on the source, `port_sink()` and `port_take(t, word)` on the sink.
   `UpdaterTop` needs the two sink methods.
4. **A run with two proofs,** so that the Updater building the next proof's images overlaps the Tracer on the
   cache (the owner: in reality they overlap; the single-proof runs cannot show it).
5. **The header change** in gold and v1.37, then the proof runs again, since the packet files change.
6. **Later, as briefs of their own:** the old words out and the new UART in v1.37 (with a store to `UART_CONTROL`
   added to each firmware image that prints); Egress in v1.37, once there is something to send.

### The workers' lists not yet applied to the documents

(Applied on 2026-11-18, section 34, but for the figure.)

- **WORKER6, for `UPDATER-INTERFACE.md`:** the decoder's type counts min(`run_left`, `WORD_COUNT`); after an
  error a run stops with `busy` 1 and no `done`; `beyond` is the Updater's own check before it writes the held
  page; a fault's pages are counted before any is taken; a free page entry's unit; what `STAT_PAGES_READ` and
  `_WRITTEN` count. **For `CACHE-INTERFACE.md`:** the wording of the ask about new pages, so that v1.37's roots
  fit; nobody discards in v1.37; an update answered `hole` or `beyond` may be treated as fatal.
- **WORKER6's finding in the black box:** its "covered" test is `run_left` ≥ 256 alone, so when a run ends inside
  a run of values its `STAT_PAGES_READ` is one low for each such half. Its images are right. Not changed.
- **WORKER7, for `word_port.h`:** name the four methods the slot wires.
- Earlier lists still open: WORKER3's and WORKER4's for `HOST-FRAMES.md` (five items); the figure
  `fig_m1_lpddr_path.svg`, which still shows the Updater on the memory tile.

## 22. The v1.37 milestone (2026-11-17, about 4 a.m.)

The owner set the next milestone; the plan is `updater_wip/planning/MILESTONE-v1.37.md`.

- **v1.37:** the cache's and the Updater's state machines simulated in full; AXI a model; Ingress/Egress and the
  UART a black box; the master tile's real firmware; no RW engine.
- **A reply:** at the end of its segment the master writes a frame out that shows it ran. † Proposed: the
  Tracer's end-state record with a few counts, which the driver can check exactly against gold's record *s* + 1.
- **The daisy chain made to work:** DRIVER ↔ ASIC0 ↔ ASIC1, the ASICs running segments 0 and 1.
  - Three processes, joined by shared memory, with a sync every 64 cycles (owner). † The wire's delay is the
    window, so a run is the same to the cycle every time.
  - `--clients 2 --client 0` or `1` (owner).
  - The driver reads gold's data, sends the packets, and checks what comes back (owner).
- † Left out of the black box for now: acknowledgements, sending again, recovery.
- **The 10% question is closed** (owner): a proof is 50 to 100 ms, 50 to 100 million cycles, and the master is
  awake 200,000 to 315,000 of them before the hand-over, 0.2 to 0.6%. On a faster link the share of the set-up
  window alone rose to 21% (Quest 215), only because the same work was divided by less time.
- Quests 213 to 216 pass: WORKER7's forward count on the 37 proof runs, `make check` and the faster links; and
  WORKER6's controller in the chip on the 37 proof runs.

## 23. The link's proposed items ruled on; a simulation of the protocol; the merges (2026-11-17, about 5 a.m.)

- **The owner went through `HOST-LINK.md`'s eight proposed items** and ruled on each; the document has them:
  numbers for each link and stream; hops 4 bits, 16 the absolute maximum; CRC-32C; a packet with a field that
  cannot be is thrown away like one that failed a check; the recovery answer repeated until acknowledged; the
  stop or start status sent at once on a change and again whenever 512 cycles pass since the last packet sent
  (the owner's rule, in place of a heartbeat with zeros); stop at half a queue; 8 packets kept a stream.
  - **Still to review:** Ingress/Egress's proposed items, 11 when grouped, starting with the register and bit
    numbers; then the Updater's.
- **A robust simulation of the protocol before any hardware** (owner): WORKER8, in `updater_wip/link/`, with no
  chip. Randomness drives every condition (owner): each wire's noise, each consumer's stalls (up to 1 ms), each
  sender's traffic, with each run's parameters drawn at random too; rare events counted; an exhaustive search of
  a shrunken model beside it; every gap in the document reported with a rule proposed. Its link end becomes the
  black box's link part.
- **Quests 213 to 221 all pass.** `updater-w6` is merged and WORKER6 closed.
- **`updater-w7` is checked and not merged.** It takes the segment queue out of the black box, and WORKER6's
  bench and view feed the black box through that queue, so the two cannot both be on `main` and working without
  the shim coming out. That is the joining step's first piece (`MILESTONE-v1.37.md`, step 3).
- **A spec session** was asked about by the owner, to fold the agreed design into v1.37's specification. Its
  scope and brief are not written yet; the review of proposed items came first.

## 24. The protocol does not hold as written; what WORKER8 found (2026-11-17, about 7 a.m.)

WORKER8 built one link from `HOST-LINK.md` and searched it exhaustively at small sizes: about 49 million states
(`updater_wip/planning/sessions/WORKER8/QUESTIONS.md` on `updater-w8`; each flaw has a kept case).

| | as written | with the rule proposed |
|---|---|---|
| a good packet with the wrong number | the link stops for ever after one lost packet | holds |
| the last packet of a burst lost | stops for ever | holds, **and no resend timer is needed:** a packet of length 0 carries the sender's next number |
| both ends lost at once | stops for ever | holds: a lost side answers a "lost" |
| a lost receiver hunting in the data | takes data for a packet | holds: it waits for 528 zeros first |
| the stop bit lost | a queue overruns | holds **if the window is no more than half a queue** |
| one stream's status behind the other's data | the background stands still 102,447 cycles | 544: a status timer for each stream |
| one end starts again from power-on | stops for ever | a restart handshake, bit 0 of the reserved byte; the link starts again and raises an error |
| a bit flipped in a kept packet | the same packet sent again for ever | the sender checks what it sends; the link starts again and raises an error |
| a false acknowledgement (a forged header) | a packet released that never arrived | the link tells, starts again, raises an error |
| a false packet number (a forged header) | a packet delivered twice and the next lost, silently | **not fixed** |

- **Decided (owner): Ingress's queues are 16 KB a stream.** The window of 8 packets against 8 KB sat exactly at
  the overrun's limit. v1.37 has 8 KB as one constant (WORKER7); it goes back.
- **The false packet number, recommended by the design session and put to the owner:** no change to the header.
  It needs a damaged header to pass its CRC (2⁻³²) and to land on the number the receiver expects (about 2⁻¹⁸
  more). The 32-bit data checksum was accepted knowingly, and damaged data that passes it is the likelier silent
  fault. A wider data check is the lever if a stronger link is wanted; a wrong trace fails verification.
- **Recommended and put to the owner:** an acknowledgement as soon as the line is free (400 one-word foreground
  packets: 28,463 cycles as written, 15,313 with it).
- **`HOST-LINK.md` still has the protocol as first written.** WORKER8's rules go in once its random campaign and
  the chain have confirmed them.
- WORKER10's plan for the wire between processes is agreed in outline: a process waits only for its neighbours;
  an anonymous region inherited from the launcher; an explicit `--driver`.
- Ingress/Egress's register and bit numbers are to be **packed, like the other peripherals'** (owner):
  `STATUS` [2], [3] for the two queues' flags; `EVENTS` [6], [7], [8] for the three new events; words 64 to 67
  for the four host words. v1.37 changes over when the old stream words come out. The document's tables are
  still to be brought to this.
- The forward count's small rules are accepted (owner): a store replaces and a store of 0 stops; a load gives
  what remains; `discard` stays as written; a store with a background transfer in flight is undefined.
- **An Updater error is not recovered from** (owner): a peripheral's error is its tile's error, which kills
  everything. The host resets the chip; the boot firmware drops what is left in the queues with the discard
  count, says it is ready, and the proof starts over.

## 25. The header grows to 20 bytes and speaks for both streams (2026-11-17, about 7:30 a.m.)

- **The owner saw the starvation at once** ("if one stream is banging away, the other stream can get stuck") and
  asked what it costs: on a direction that one stream fills, the other's status as packets of its own would be
  about 3% on top of the headers' 3%.
- **The ways weighed:**
  - one status packet for both streams, using the fields a packet of length 0 does not need: it fits, and does
    nothing for the busy case;
  - a longer repeat interval: safe up to about 2,048 cycles with a window of 8 and queues of 16 KB;
  - both streams' status in every header of 16 bytes: only with the numbers cut to 10 or 11 bits. Enough for a
    window of 8, since a wire cannot reorder packets (PCI Express uses 12);
  - the numbers kept at 18 bits by moving the data checksum to the end of the data;
  - **the owner's: a header of 20 bytes.** "It's header info that gets stripped anyway."
- **Decided (owner): 20 bytes.** Four 18-bit fields, the foreground's number and acknowledgement and the
  background's, in the same place in every header; stop and resend for each stream; lost, recovery and restart
  for the link; hops; the data checksum; the header CRC over the 16 bytes before it. `HOST-LINK.md`, "A packet",
  with four example headers.
  - It sets aside the rule that everything rides in 128 bits: the header is not a word. The data still is.
  - 3.8% of a link, flat, where the worst case was heading for 6%.
  - One status timer a direction is enough again, as the owner first ruled, and "lost" and recovery need one
    packet, not one for each stream.
- **WORKER8 takes the new header as its baseline** before its random campaign and the chain, and searches it.
- **What is built has the 16-byte header:** gold, the vectors, v1.37 (and `updater-w7` its 18-bit form). The change
  is step 2c of the milestone, after WORKER8 confirms it.

## 26. One run of packet numbers for a link; the header is 16 bytes again (2026-11-17, about 10 a.m.)

- **The owner:** "why have separate packet numbers for the streams? The packet number is about the link, not the
  stream."
- **Decided (owner): a direction of a link has one run of numbers, shared by both streams.** One acknowledgement,
  one resend flag, one window of 8 packets, one set of kept packets. Both streams' stop bits are in every header.
  `HOST-LINK.md`, "One run of numbers for a link".
- **The header is one 128-bit word again,** nearly the one WORKER7 built: the stop bits move to bits [57:56] and
  restart takes the old stop bit's place. Section 25's header of 20 bytes lasted two and a half hours; WORKER8
  had built it.
- **The design session's reason for numbers of their own was wrong** (item 1 of the link's review: "a stopped
  stream leaves gaps"). A stopped stream has no packets sent, and the other's numbers stay consecutive.
- **Why a stopped stream cannot block the other:** both stop bits reach the sender in anything it hears; a queue
  says stop with 8 KB free, and a window is 4 KB; so a good packet always has room and is never refused.
  - **To be shown by WORKER8's search,** not assumed.
- **What it costs:** a damaged background packet has the foreground packets behind it sent again; the window is
  shared.
- **While this was decided the workers moved on:**
  - WORKER8 built the 20-byte header, the chain and the random runs (`updater-w8`, `e8f28bcd`). It goes to one
    run of numbers next.
  - WORKER9 finished step 1: `updater-w7` merged, the shim out; Quests 222 to 226 pass, the 37 proof runs' cycles
    equal to Quest 220's with every setting off (`updater-w9`, `b32cca91`). It waits for the integrator to merge
    that much.
  - WORKER10 reports the wire built and every check passing where it ran, with a task for the runner
    (`updater-w10`, `670c84b2`).

## 27. The link document brought up to what is known (2026-11-17, about 1:30 p.m.)

- **The owner: "let's update our link doc with the things we know we need."** `HOST-LINK.md` now has WORKER8's
  rules in place, each marked: the wrong-number rows and the empty packet's number (so no resend timer); the
  528 zeros before hunting; a lost end answering "lost"; acknowledgements counting in any state; **a new
  section, "Starting a link again"** (the restart flag, the sender checking what it keeps, a packet made wrong,
  the false acknowledgement told by acknowledgements going backwards); an acknowledgement as soon as the line is
  free; the window no more than half a queue, the host included; and in the chain, acknowledging on taking in,
  the last node told it has no neighbour, forwarding buffers with the same rule as queues.
- **What they rest on, and what they do not:** the searches and runs were on numbers for each stream. One run of
  numbers for a link is still to be searched. The document's opening says so.
- **Kept as open there:** the two failing seeds of Quest 227; a neighbour that goes silent; the numbers marked †;
  the name of the error a restarted link raises.
- **The false packet number is recorded as a known limit,** with the figures: the header's CRC catches up to 7
  flipped bits, and damaged data passing its checksum is at least 600 times as likely.
- **The runner:** Quests 227 to 229 (WORKER8's campaigns on the 20-byte header) were parked in `tasks/hold` and
  the running one killed by the owner. The runner has 16 CPUs and 62 GB.
- WORKER9's step 1 is merged (section 26); WORKER7 is closed.

## 28. The owner goes through WORKER8's findings one at a time (2026-11-17, about 2 p.m.)

Rulings as they came; `HOST-LINK.md` has each.

- **Findings 1 to 7 understood and accepted:** a good packet with the wrong number ("clearly a mistake in our
  rule writing"); the last packet of a burst lost, found from the next number on an idle line; both ends lost
  ("lost" is a good packet: it tells the last packet received); a lost receiver waiting for the zeros; the lost
  stop bit and the window; one stream's status behind the other's data ("there is really just one stream with
  two destination queues"); one end starting again.
- **The lost side's repeat stays 1,024 zeros and "lost",** not every 512 idle cycles: the other end may be lost
  too, and a lost receiver reads no header until it has seen a run of zeros longer than a packet.
- **Decided (owner): the restart packet is a "magic" full packet,** its data the first 32 primes as bytes, 2 to
  131, repeated 16 times; checksum 0xC8981D21. An end acts on it only if the header, the checksum and the pattern
  all agree. "Lost" and its answer stay 16 bytes, since they must get through noise.
  - The owner's first pattern was the primes below 256, 54 bytes, repeated to 512; the second is two whole words.
  - (The design session first committed the first pattern with a checksum it had not computed, and corrected it
    in the next commit.)
- **Decided (owner): both checks are computed afresh whenever a packet is made ready to send,** forwarded ones
  and resends included: "use the fresh value as the packet checksum, recompute the header checksum."
  - This replaces WORKER8's rule for a kept packet gone bad (compare with the checksum held; start the link
    again), and the design session's keeping of the data checksum end to end along the chain.
  - **Given up, knowingly:** the checks cover the wire and not the memory. A bit that flips in a queue, a kept
    packet or a forwarding buffer goes out with a matching checksum.
  - A sending unit now passes over every packet's data once before it leaves.
- **A packet made wrong** (owner): asked for again the first time, since it may be noise; the error only if it
  fails the same way again. One flag in the receiver tells: set by a wrong packet with the number expected,
  cleared only when a packet is taken. This replaces WORKER8's rule, which judged at the first time by whether
  the data was whole, and would have blamed the sender for noise in the header alone.
- **A false acknowledgement** (owner): not watched for. If the other end later asks for a packet no longer kept,
  that is the error. "If a resend is never requested, then no harm, no foul": the packets let go early usually
  arrive whole. This replaces WORKER8's rule (acknowledgements going backwards twice), which would have raised
  the error with no harm done.
- **A false packet number: "we can live with that"** (owner). Higher than expected is a resend; lower is taken
  for a duplicate; only a number that lands exactly on the one expected, on the packet after a lost one, does
  harm.
- **The chain** (owner): a node's job on a packet is done when it has received it; the last node is told by a
  pin.
- **A neighbour that goes silent: not designed for now;** left open.
- The walk through is complete. WORKER8 is to be told what changed in its rules.

## 29. One run of numbers was unsound as written; "stop" holds only packets not yet numbered (2026-11-17, about 3 p.m.)

- **WORKER8 stopped as its brief ordered,** within half an hour of starting: the protocol as it stood was not
  sound. A lost background packet, while the background is stopped, held the foreground for as long as the
  background stayed stopped. Its search found it in 4 events, 21,208 states in, with a new check: from every
  state, one stream is never taken again and the other must still finish.
- **The design session's argument was incomplete.** It showed that a good packet always has room and is never
  refused. It missed a packet of the stopped stream that is damaged on the way and must be sent again: "stop"
  was blocking the resend, and with one run of numbers everything behind it waits.
- **Decided (owner): "stop" holds only packets that have no number yet.** A packet that already has a number is
  sent again whatever its stream's stop bit. Its room was counted when it was first sent. The other way out was
  numbers for each stream again; not taken.
- **The two failing seeds of Quest 227 were a different hole:** a resend request lost, and a receiver that asked
  again only at a packet of length 0, which a busy sender does not send. **Decided: the receiver asks again at
  any sign the sender is still ahead.** The host's window of 64 only made the stall long enough to be noticed.
- **The stop rule earned its place** (owner: "it should stop as soon as it's sure the protocol is not sound and
  it has identified a cause"): the flaw came back as a four-event case and a one-sentence change, before any
  campaign was run on it.
- WORKER8 carries on with its brief on that basis. `HOST-LINK.md` has both rules.

## 30. A packet hidden in the data; the data is masked on the wire (2026-11-17, about 8 p.m.)

- **WORKER8 stopped a second time,** with the afternoon's two rulings built and everything else it searched
  holding. A header that reads as zeros on the wire is taken for idle; the first byte of the data that is not
  zero then starts a "header"; and if the data holds a whole packet with the number expected, it is taken. A
  word is delivered that is not the next, a true packet is lost, no check fails. Two events on the shrunken
  model. As old as the link's framing; found only now because the search had no damage that zeroed a header and
  left its data, and no data that held a packet.
- **By chance it is inside the limit already accepted; with data that is itself link packets it is not.**
- **Decided (owner): the data is masked on the wire,** each byte XORed with a sequence started from that
  packet's header CRC. A 32-bit shift register a direction; no new field. Whether the link works then does not
  depend on what the data holds. `HOST-LINK.md`, "The data is masked on the wire". The sequence is WORKER8's to
  define exactly and test.
- **A stopped stream cannot block the other:** shown by WORKER8 on eight searches with "stop" holding only
  packets not yet numbered, up to 3.3 million states each, and the first-written rule fails beside them.
- **The larger search is not finished:** Quest 230 tried 71,162,948 states with no failure and was cut by its
  45-minute limit, not by memory. To be run again with a longer limit, reporting how many states were waiting.
- WORKER9 has not started the joint and WORKER10's runner task is not queued: both wait on the owner.

## 31. The protocol on a solid footing; the link document brought up to the code (2026-11-17, about 11 p.m.)

- **WORKER8, with the mask in:** 71.8 million states searched, every search complete, on one link and chains of
  1, 2 and 3; 18 planted bugs caught; a kept case for every rule; 1,380 random runs on chains of up to 16, none
  failing. A stopped stream cannot block the other: 7 searches, 9.7 million states. The owner: "it sounds like
  our protocol is on a solid footing."
  - Still out: one long search (Quest 231, breadth-first to count what waits, then depth-first, under the
    runner's limit, which the owner raised to 3h30m), and the random campaign on the runner.
- **The mask is defined:** the CRC-32C register started at the header CRC with bit 16 set, eight shifts with
  nothing fed in before each data byte, its low byte masking the byte. Bit 16 keeps it out of the two states
  that stand still. Three worked examples; the design session computed two of them again and they agree.
- **The mask closes the false packet number** for any packet with data: a forged header takes the wrong mask
  off. The limit the owner had accepted that afternoon is gone.
- **The mask's own limit, accepted:** data made on purpose for the header it will travel under. The CRC and the
  sequence are linear, so the data is solved for. It needs the host's own data made against the link.
- **The pass over the data cost more than the design session had written:** 14% of the data rate in the random
  runs at a byte a cycle, 17% saturated, and a full packet's time doubled on an idle link. A unit of 128 bits a
  cycle gives it all back.
- **Decided (owner): two fast units and two slow.** The fast ones, 128 bits a cycle, are for sending: the pass
  over the data, then the header CRC, which the same unit does by a fixed starting value and 32 zero bits in
  front. The slow ones, a byte a cycle, are for receiving. Four mask registers beside them.
- **WORKER8's smaller points taken as it built them:** hops 15 tested in both directions at a node that would
  forward; a packet of length 0 with a wrong field counted; the restart packet's fields and rounds; "asks for a
  packet no longer kept" including a "lost"; the 2,048 cycles holding back only what a packet ahead sets off.
- **`HOST-LINK.md` brought up to the code that passes:** WORKER8's 30 corrections applied, the figures replaced
  by those measured on the protocol as it stands, the forwarding steps given the mask. WORKER8 is asked to read
  it against its code once more.

## 32. The long search is complete (2026-11-18, half past midnight)

- **Quest 231 holds:** 86,515,438 states, every one tried, depth-first, in 47 minutes. From every state, with
  either stream never taken again, the other finished. Breadth-first had reached 39.9 million with 5.3 million
  still waiting when its 1,400 seconds ran out; depth-first visits the same states with few waiting, so time
  and not memory is the limit. No splitting was needed.
- **WORKER8 has made the fast sending unit its default,** since `HOST-LINK.md` now has it, and every case was
  found again. It parked its five queued tasks of the slow unit (Quests 232 to 236) and queued six on the new
  code (237 to 242): the two campaigns, the larger searches, long noise, and the long search once more. The
  figures the document quotes are being measured again; its list of what is still wrong or silent follows.

## 33. WORKER8 reads the link document against its code; the corrections applied (2026-11-18, about 1 a.m.)

- **6 places wrong, 9 unclear, 12 silent,** and most figures measured with the slow sending unit. All applied to
  `HOST-LINK.md`, with the figures of the fast unit. The worked examples all agreed.
- **One sentence of the design session's was wrong:** that the error for a packet no longer kept "is raised only
  when there is real harm". With two forged headers, a false acknowledgement and then a false "resend" or "lost",
  it is raised with nothing lost. Far inside what is accepted, and loud.
- **The longest wait for an acknowledgement does grow with the status timer,** with the fast unit: 2,727, 3,380
  and 8,356 cycles at 512, 2,048 and 8,192. A reason to keep 512.
- **A line that reads as zeros for long looks idle:** in the runner's campaign a foreground packet stood ready
  515,932 cycles so. The wire's side of "a neighbour that goes silent", which stays open by the owner's ruling;
  the measurement is added to it.
- **The fast unit's starting value for the header CRC is 0x641F6454.**
- **The runner's campaign with the slow unit, Quest 232:** 6,071 runs on chains of 1, 2, 4 and 16, 306 million
  packets, none failed, none refused for want of room.
- The owner: fix them now, "and figure out what work we can launch to try to get v1.37 finished."

## 34. The link passes on the runner; what the link and the wire cost; the documents squared away (2026-11-18, about 4 a.m.)

**The link.**

- **The first long run of the protocol as it stands passed** (Quest 237, the fast sending unit): 7,519 random
  runs on chains of 1, 2, 4 and 16, 18 billion cycles, 385 million packets, none failed, no packet refused for
  want of room. The owner: WORKER8 has shown it is pretty likely the link is fine; the rest of the long runs
  (Quests 238 to 242) can wait for a night.
- **A line forced to zeros.** The longest a foreground packet stood ready with the link in step was 384,187
  cycles: one line of a chain of 16 read as zeros for up to 446,377 cycles, which is the test's own damage. Zeros
  look idle, so nothing is noticed, the sender's window fills, and it waits until the line is back. The owner:
  that is sort of correct; "the interesting thing is how long did it take to restart after you let data through
  again." Over 257,035 recoveries of every kind: 669 cycles on average, 5,187 at most. Now in `HOST-LINK.md`,
  "Recovery".
- **The fullest queue was 953 words of 1,024,** against 629 before. The campaign draws the host's window at 64 in
  one run of five, which the document rules out; WORKER8 is asked which run it was. At a window of 8 it would be
  a finding.
- **The link end is ready to build on** (`updater_wip/link/hostlink.h`: `End` and `Node`), on `updater-w8`.
- **What it costs as hardware** (`sessions/WORKER8/HARDWARE.md`): a link end 630 flip-flops and 4,608 bytes; an
  ASIC in the middle of a chain 1,424 flip-flops and 77,312 bytes for the link, 110,080 with the chip's two
  16 KB queues; 85% of the link's memory is the four forwarding buffers. The owner: 100 KB is not an issue
  beside the Tracer's 768 KB. Now in `HOST-LINK.md`, "The link as hardware".
- **Two questions from WORKER8, both † and neither ruled on:** what an ASIC does with a background packet that
  has data and comes from below (the model forwards it, through a 16 KB buffer nothing else uses); and the
  header's CRC of a packet of length 0 that starts while the fast unit is passing over the next packet's data.
  Both in `HOST-LINK.md`, Open.

**The wire between processes** (WORKER10, `updater_wip/wire/`, on `updater-w10`).

- **What it is:** the layer under the chain's three processes, and nothing of the protocol or the chip. Each
  process gives a byte and takes a byte on each link every cycle; bytes given in one window of 64 cycles arrive
  in the next; a launcher starts the driver and the ASICs and stops them all if one dies.
- **What it showed:** the same to the cycle as a one-process reference in every case, with the machine free,
  loaded, pinned to one core and paused at random; stopping never hangs; planted bugs caught.
- **What it costs:** about 1.5% at the owner's window of 64, nothing at 1,024, about 20% at 16. No promise of
  silence is needed for the long stretches when a chip traces and the link is idle.
- **Its check on the runner could not finish inside the time limit.** Under the test's own load, a busy loop
  on every core, a chain of 16 at a window of 16 took over six minutes a case where it takes under a second on
  a free machine: 17 processes that must each run every 16 cycles, each waiting its turn behind a busy loop.
  That is the test's load on that machine, and says nothing against the wire. The owner: these big runs seem
  pointless. No campaign on the runner for the wire (owner).
- Now in `updater_wip/planning/MILESTONE-v1.37.md`, "The picture".

**The owner on where v1.37 stands:** the protocol's open questions want a thorough look, "but that's only a
small corner of our v1.37 work." The bulk is the joint (the Updater's controller on the real arbiter and cache),
the master tile's side of Ingress/Egress with the reply frame, gold brought to the final protocol, and the chain
with its driver.

**The documents brought up to what has been decided** (owner: whatever has been discussed about the design goes
into the files):

| document | what went in |
|---|---|
| `INGRESS-INTERFACE.md` | the register and bit numbers packed (`STATUS` [2], [3]; `EVENTS` [6], [7], [8]; words 64 to 67); "Errors" rewritten for the final link: a damaged packet is no error, and the core sees † `link_error` when a link starts again, with `format_error` for a packet made wrong twice; an error is not recovered from; the pin; "stop" at half a queue; `valid` may fall on the word port; "In v1.37" as built on `main` today |
| `HOST-LINK.md` | Quest 237; the recovery times; "The link as hardware"; WORKER8's two questions; the header's CRC during a pass; the error's name |
| `UPDATER-INTERFACE.md` | WORKER6's and WORKER7's lists: what stops a run and what it does then; `beyond` and `out_of_pages`; how a whole page or half is known from the data; what the statistics count; a free page entry's unit; "In v1.37" as built |
| `CACHE-INTERFACE.md` | WORKER6's three: a new page worded so that v1.37's roots fit; nobody discards in v1.37; an update answered `hole` or `beyond` may be fatal |
| `HOST-FRAMES.md` | WORKER3's five: who fills frame 7's roots and depths; frame 8's counts and the 128-bit word; what the firmware derives from the per-ASIC page counts; a segment's rows; frame 4's base without a file system |
| `word_port.h` | the four method names the slot wires; `valid` may fall |
| `MILESTONE-v1.37.md` | WORKER10's eight corrections; the launcher; the final header; where each step stands |

**Not done:**

- The figure `fig_m1_lpddr_path.svg` still shows the Updater on the memory tile.
- The † of `INGRESS-INTERFACE.md` have not all been put to the owner: the review reached the service flags.
  Egress's count and credit, the UART's small rules and the reset's stand as proposed.
- v1.37's code is behind the documents where "In v1.37" says so: the queues at 8 KB, the numbers from before the
  packing, the header from before the one run of numbers and the mask.
- The reply frame's fields, and the foreground stream's frames altogether.

## 35. The specification first (2026-11-18, about 4:30 a.m.)

- **The two state machines each work alone, and have not met.** The cache's controller runs the Tracer through
  the proof runs. The Updater's controller matches the black box byte for byte, on a stand-in cache and a test
  LPDDR of its own. Joining them is still to do.
- **The specification has not been written.** v1.37's `spec/` is v1.36's, unchanged. The design is in these
  planning documents, and the workers built from them: the order of every earlier version turned round.
- **The owner:** "we should follow the tried and true. We build a spec for the full system." The simulator is
  then brought to the specification, then the firmware, then the tests.
- **What follows from it:**
  - a specification session, `updater_wip/planning/sessions/SPEC/PROMPT.md`: the design as designed, every †
    ruled on or made an open item, and a list of where the simulator and firmware differ;
  - the brief for an integrator session, written an hour before, is on hold. It had the simulator leading, and
    left the old stream words, the UART and the register numbers as built;
  - the building that comes after takes in what that brief left out.

## 36. Rulings for the specification (2026-11-18, about 5 to 6 a.m.)

The specification session (`updater_wip/planning/sessions/SPEC/`, branch `v137-spec`) put its outline, about 70
daggers and 14 silences to the owner. These are the owner's rulings on the larger ones. The specification is
where they are stated; this is how they were reached.

- **Whose word wins** (owner): for the cache's controller, the Updater's controller and the link end, the code
  is the authority and the specification mirrors it ("it's actually the spec portion of those codes"). For
  Ingress/Egress, the UART, Egress and the frames, the design documents lead. **Every new component is specified
  to the depth of the ones already there.** What is inside those three machines is accepted as the code has it,
  as a block.
- **One reset.** The session proposed a second net, so that a reset after an error would leave the host link
  alone. The owner: "Error recovery in general is a nightmare. If we try to figure out the error recovery
  strategy for the chip in the session we're going to get it wrong. It's a whole can of worms because there is
  state all over the place." **No second net.** The reset is v1.36's, as at power-on, and reaches the link and
  the host's queues too. The specification states what raises each error and that it stops the chip; what the
  host does after that, draining queues and a "ready" message are one open item, not a design.
  - This undoes "cleared at power-on only" in `INGRESS-INTERFACE.md`, "Reset", and the lines on recovery in its
    "Errors".
- **Where the cache lives.** The memory system (the cache, the arbiter, the `lpddr` port) is **the memory tile's
  peripheral 1,** and as little of a peripheral as will do: it is there so that the cache's error goes out on
  the tile's error line, with no new net. **The RW engine becomes peripheral 2.** The hardware keeps v1.36's RW
  engine; nothing in v1.37's proof runs uses it.
- **The links between the special tiles stay** (owner: "We should put the links in"), though the Updater's move
  to the master tile took their first reason away.
  - **Which ports.** The session proposed east and north. The owner: "that's a terrible scheme. Think of the
    numbers. 64 = c&c1, 65 = c&c2, 66 = c&c3. We have a +/- 1 relationship." **East and west, as in the mesh:**
    east is tile id + 1 and west is id − 1. Master east to memory west; memory east to tracer west; the route is
    the ring's rule on the difference of the ids.
  - **A peripheral's DMA enters on south, the +8 port** (owner: "the peripherals connect via the +8 router"),
    as v1.35's DMA had the tile's id + 8 for its source. The RTL has it on west today.
  - **No flow control.** The session proposed credits, from the request's own words, "always flow-controlled".
    The owner: "I've had this conversation so many times. There are regimes on the mesh. Some need flow
    control, some don't. Right now, we don't actually have a file system. So as far as I know, flow control is
    not necessary, and it hasn't been spec'd." **The line's links are the mesh's, with none.** Flow control
    belongs with the regime that needs it and is designed with the file system. The request's wording was the
    design session's mistake and is corrected (`RTL-CHANGE-REQUESTS.md`, 1, and the paragraph under the table).
- **128 bits is "a 128-bit word",** not a beat: the Tracer's and the replayer's chapters say so, and their
  counters are named `…_words`. `FOREGROUND_WORDS` and `BACKGROUND_WORDS` keep their names. "Beat" keeps the
  specification's meaning, one item a handshake moves.

## 37. The specification is done; the building is next (2026-11-18, about 9:30 a.m.)

- **v1.37's specification is on `main`** (`v1.37_wip/spec/`, merged at `ffd17dad`): `machine-spec.md` with
  Ingress/Egress rewritten (17.6), the Updater (17.10), the memory system (17.11, 23) and the special tiles'
  line (16), and three new documents, `host-link.md` (28), `host-frames.md` (29) and `master-firmware.md` (30).
  No dagger is left in it: each ended as the owner's ruling or as an open item, 178 to 191.
- **It was reviewed against the code by the four who wrote the code** (the owner's plan), each from a checklist
  of what the writer had derived and not run. What that found:
  - **a bug in the cache's controller** (WORKER5): a read on the fill port could be served before a write
    accepted earlier, and return the old line. The specification's sentence was right and the code wrong. Fixed
    and merged (`cache-fill-order`; Quest 244, the 37 proof runs), with a sentence added to 23.6.4 for a read
    and a write taken in the same cycle;
  - the Updater's chapter right in the main, with a width, a sentence and the statistics' rules corrected
    (WORKER6); two limits of its addresses made UB;
  - Ingress/Egress's and the firmware's chapters corrected in eleven places (WORKER7); an unknown frame type is
    a stop, as the firmware has it, where a known frame the ASIC has no use for is discarded (owner);
  - the link's chapter breaks no rule of the code (WORKER8). **A reset in a running chain behaves as the
    chapter says,** in 1,380 runs: both neighbours raise their error and the links come back. With one reset,
    resetting one ASIC of a chain so stops its neighbours; what follows is item 181.
- **Merged the same morning:** the link (`updater-w8`, with counters, the cause of an error and the reset run
  added) and the wire (`updater-w10`). The 953-word queue was a run with the host's window at 64: not a finding.
- **Next is the simulator, then the firmware, then the tests** (owner), in one session:
  `updater_wip/planning/sessions/INTEGRATOR/PROMPT.md`, rewritten against the specification. Its worklist is
  the specification session's `FOR-THE-SIMULATOR.md`.

## 38. v1.37 built to the specification; the controllers sized (2026-11-18 evening to 2026-11-19)

- **The build session brought the simulator, the firmware and the tests to the specification in a day**
  (`updater_wip/planning/sessions/INTEGRATOR/REPORT.md`), using the state machines already built as its basis
  (owner). Merged into `main`: the Updater's controller on the real arbiter and cache, with the memory system as
  the memory tile's peripheral 1; the special tiles' line; Ingress/Egress at the specification's registers,
  with Egress and the new UART; WORKER8's link end as the chip's link; the reply frame; the driver and two
  chips as three processes on WORKER10's wire. At 2²⁹ on both workloads the driver and two chips pass every
  proof check against gold, both replies match, and a second run is the same to the cycle (Quests 260 to 263).
  - It found a firmware bug (a header frame one row too long did not stop the master) and left six notes for
    the specification (`INTEGRATOR/FOR-THE-SPEC.md`), two of them gaps: how the tracer tile's 68-word record
    reaches the master, and that nothing tells the master the reply has left the outgoing queue.
  - What is left is the owner's: what is frozen and when, and WORKER8's parked runs of the link (Quests 238 to
    242).
- **The owner asked how big the two controllers are,** and why: "we're really replacing a general processor with
  a fixed logic controller. Is that the right call?" A session counted them from the code. **Each is about one
  and a half cores of logic** (the cache's controller 41 to 84 kGE, the Updater's 42 to 59, the core 34 to 43),
  **and in firmware the same work would take a core 25 to 100 times as many cycles:** 3 cores' worth of time to
  keep up with raw update words in short runs, 10 to 23 with LRU-coded ones, and a cache 17 to 48 times slower
  on a hit. So the call rests on speed, not on size. What it gives up is that the update word's format and the
  cache's rules cannot be patched in a made chip; the policy stays in the master tile's firmware.
  - Written up in `CONTROLLER-SIZING.md`; the full report and its scripts are
    `updater_wip/planning/sessions/SIZING/`.
  - It corrected the specification's count of the cache's controller, 6,876 flip-flops to 6,652 (23.6.5 and
    chapter 24).

## 39. Before the freeze: the build session's six notes for the specification, ruled on (2026-11-19, after midnight)

The build session left six notes where the specification was wrong, unclear or silent
(`updater_wip/planning/sessions/INTEGRATOR/FOR-THE-SPEC.md`). The owner went through them one at a time.

| note | section | the ruling |
|---|---|---|
| 1 | 17.10.2, 17.10.11 | "A page at 2²³ or above is UB" cannot arise and goes: a page number is a value's 32-bit index over 512, so the limit on the index (a run past 2³² − 1) is the only one a program can reach. Say that the index's 32 bits are what bound the reach |
| 2 | 20 (invariant 37), 23.6.11 | name the three checks the build added where the Updater meets the cache (an update before its page's write is answered; a direct write to a page the cache holds a line of; a direct read of one it holds a dirty line of), and say which check catches which rule |
| 3 | 17.9.5, 23.6.4 | **a rule on the client, stated loudly, and not a simulator check** (owner: "this is a bug that the RTL will either have or won't have. 'Catching' it in the simulator won't help us make sure the RTL has it right. This probably just needs to be documented loud and clear as 'it better not do this or else'"). Each cycle in which `valid` and `ready` are both 1 is one request; a client that holds `valid` a cycle longer has made a second, which the cache serves. It is to be verified in the RTL, as an assertion on the port. The simulator's check may stay in the code; the specification does not present it as what keeps the rule |
| 4 | 17.7, 21.5 | the RW engine's figure is at its own port; behind the arbiter its bursts reach `lpddr` a cycle later. List it, with the special tiles' DMA on south, where the simulator's cycles no longer match the RTL's tests. The owner: "All that stuff will get reworked once we have a real RW engine anyway" |
| 5 | 30.6, 30.7, 29.14 | **the tracer tile's last record is pulled, not pushed** (below) |
| 6 | 30.7, 17.6.8 | **no bit for "the reply has left".** The owner: "I view it as an RPC system. The FPGA makes requests of C&C1; once C&C1 sends the answer it's done." A frame is the link's once `EGRESS_COUNT` is 0; the core does not wait for it to leave; a simulation ends when the host has every reply, not when a master parks |

**How the tracer tile's last record reaches the master** (owner). The build session had the master name, in the
segment's load, a place in its own memory, and the tracer tile write its record there at its end, before its
result. The owner: "no no, that's not how these should move." As ruled:

1. **When its trace ends, the tracer tile (C&C3) prepares one block in its SRAM,** everything it gives to the
   reply. This overlaps the compute tiles' replays (owner), so it costs the segment no time.
2. **When the master (C&C1) has the report that all the replay is done,** it sets aside the block's words in its
   own memory, **arms a counter** for them, and **sends the tracer tile one request** to move the block there. A
   request names its target (11.2), and the tracer tile receives requests.
3. **The tracer tile sends the block by one DMA.**
4. **When the counter says every word has landed, the master sends the reply through Egress,** and is done.

The master still does not reach into the tracer tile: it asks, and the tracer tile sends.

- **The block is 69 128-bit words:** word 0, [31:0] the rows traced, [63:32] the Tracer's `ERRORS`, [95:64] its
  status, the rest 0; words 1 to 68, the end-state record (17.9.2). The rows traced are the windows run times
  the rows of a window.
- **The block lands in the reply's own buffer,** so the master copies nothing. **The reply (29.14) becomes 72
  data words,** 73 on the link:

  | word | holds |
  |---|---|
  | 0 | the frame header: compression 0, type 16, length 72 |
  | 1 | [31:0] *s*, [63:32] *N*, [95:64] 0, [127:96] `PAGE_COUNT` as the last run left it |
  | 2 | [63:0] the master tile's `mcycle` at the segment's end, [95:64] the Updater's `STAT_CYCLES`, [127:96] its `STAT_STALL_DATA` |
  | 3 | [31:0] the master tile's `ERRORS`, [63:32] Ingress/Egress's, [95:64] the Updater's, [127:96] 0 |
  | 4 | the block's word 0: the rows traced, the Tracer's `ERRORS`, its status |
  | 5–72 | the end-state record, 68 words |

  The rows traced and the Tracer's `ERRORS` move from words 1 and 3 into word 4. What is compared with gold's
  record *s* + 1 is the same 65 words of the record as before.
- The tracer tile's result is as it was: it says the trace is done.
- It is firmware on both tiles and no hardware.

**WORKER8's parked runs of the link (Quests 238 to 242) do not hold the freeze** (owner: not now, "if we're at
least 99% confident the link is right"). The design session's answer: on what the link does in service (noise,
sending again, recovery, "stop", forwarding) the evidence is the searches, 13,000 random runs and 690 million
packets, 1,380 resets in a running chain, the specification's review and the proof runs through the link. What
the parked runs would add is a large campaign of forged headers and of bits flipped in a sender's memory with
the fast unit, cases searched and tested at small sizes, which all end in the link starting again and the chip
stopping. They are owed, as an open item of v1.37, for an idle night.

**What the freeze now waits on, in order:** the specification brought to these rulings; the firmware of both
tiles reworked for the record's path and the reply's layout; the folder made to stand alone, as v1.36's does;
one last pass on the runner from that tree.

**Added about 2 a.m., from the build session's rework** (`INTEGRATOR/FOR-THE-SPEC.md`, 7 and 8):

- **The master answers the tracer tile's result only after the block has landed.** The answer ends the tracer
  tile's image, so before that nothing would be there to serve the request for the block. It is what keeps
  C&C3 ready to respond, and the owner's scheme again: "C&C3 should build the packet in SRAM and be ready to
  respond when tracing finished. C&C1 decides the actual timing: it does the replays first, then asks for the
  packet from C&C3, then sends the response out to the daisy chain."
- **After the reply the master goes on serving its tiles** (it waits for the tracer tile's next `LOAD` before it
  parks, so its count of them is whole). The reply goes out first and waits for nothing; "parks" is the
  simulation's word. The owner: fine; "the software will change a lot, so this is not a critical issue at all."
- **The firmware waits for the block's counter before it sends the reply, and that wait has no test.** With the
  wait taken out, every run still passes: the master needs more cycles to start its transfer into Egress than
  the block needs to arrive. The owner: "The firmware should wait. That eliminates the race. I don't really care
  that we couldn't detect the race; we know it's potentially there if we don't wait." No test is added; it is
  listed in v1.37's open items as a test owed.

## 40. v1.37 frozen, and `updater_wip/` removed (2026-11-19, about 4 a.m.)

- **The last pass on the runner, from the folder standing alone, was green** (Quests 264 to 268): the suites and
  the 37 proof runs, the same with LPDDR's timing varied, `make check` with the sanitizers, and the chain of the
  driver and two chips at 2²⁹ on both workloads, each second run the same as the first.
- **The build session froze it** by renaming `v1.37_wip/` to `v1.37/` (`0775024f`), after merging the
  specification's last changes in. The link's report, its hardware document and its task generator went into
  the folder's `link/` first, and the link's own `make check` passes from inside it.
- **`updater_wip/` is removed** (owner: "we'll want to clean up updater_wip (delete it) at the same time we land
  v1.37"; and of its papers: "let's keep the planning docs around"). Done by the design session:
  - **the code** (the link, the wire, gold with the frames) was already copied into v1.37 and held to its bytes.
    Every one of the 239 files deleted was checked against its copy's hash first;
  - **the planning papers** (the milestone, every session's brief, notes and report, the specification's
    worklist and reviews, the sizing report) moved, as they were, to **`v1.37/planning/updater/`**, as v1.36's
    session reports are in `v1.36/planning/`;
  - **the task generator for the runner** moved to `v1.37/tools/v137-check/`;
  - **gold's own development checks now run in the copy:** three scripts' paths to `v1.36/` and `v1.3/` were one
    folder short there. `build.sh` passes from `v1.37/pipeline/frames/gold`: 80 runs on the guests, 168
    byte-identical to v1.36's gold, the packets. The wire's task generator takes its folder from where it sits;
  - what named a path under `updater_wip/` in v1.37's own pages, the specification and the design documents
    names the new place. Source comments that say "`updater_wip`, WORKER5" name the project and are left; so are
    the dated records, these notes among them: **a path written `updater_wip/planning/...` in them is now
    `v1.37/planning/updater/...`,** and `v1.37_wip` is `v1.37`.
- **After it:** the folder's document check and copies check pass, and the final pass on the runner is made from
  the tree as it then stands.

