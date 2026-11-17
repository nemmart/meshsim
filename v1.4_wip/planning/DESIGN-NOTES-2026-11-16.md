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
