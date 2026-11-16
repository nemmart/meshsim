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

**Proposed.** The owner asked for the sequence so that the reply does not return until the write is complete; he
has not yet confirmed this one.

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
- **One update is in flight at a time,** so parking needs a single slot. A second update waits at its port.
- **Open:** whether that should be one slot for each client instead.

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
  tile's.
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
