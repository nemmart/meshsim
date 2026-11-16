# Design notes, 2026-11-15: the Updater, the cache, the bus driver, the frames, and the v1.4 deliverable

The second day of the discussion with the owner, continuing `DESIGN-NOTES-2026-11-14.md`. No code was written. The
same marks: **Decided (owner)**, **Proposed**, **Open**. Where this note and the first disagree, this one is later.

It is written in v1.35's terms (C&C 2, big SRAM, the peripheral's window), as the discussion was. v1.36 renames most
of them and has built some of what is described here: `V136-SCOPE.md`.

With it: `HOST-FRAMES.md` (the frames from the host), `UPDATER-INTERFACE.md` (the Updater's registers), and the figure
`diagrams/svg/fig_m1_lpddr_path.svg`.

## 1. The v1.4 deliverable

- **Decided (owner): the Tracer running on memory images that the Updater built from the host's frames.**

| part | in the deliverable as |
|---|---|
| gold's frames for a proof (`HOST-FRAMES.md`) | real; the Java work comes first |
| the Updater: its queues, the LRU, its page memory, its state machine and its control | **real, built in full** (owner) |
| the Tracer | real, as it is |
| the cache, with its look-up interface and the fill bus | a black box: right answers and simple timing |
| the bus driver, the memory controller and LPDDR | a black box, behind a model of AXI |
| Ingress | a black box: it plays the frames into the segment queue and obeys the credits |
| the RW engine, the Binder | not used |
| C&C 2's firmware | a stand-in: it formats, serves free pages and sets up the Updater's runs |

- **Decided (owner): the Updater first, the file system second.** Directories, names and the `fs.h` calls are outside
  this deliverable; files are known by their roots.
- **Decided (owner): the program is placed by a black box.** The ELF frame's program image goes straight into LPDDR,
  the base jump table into the pages after it, both off the free list before C&C 2 starts; C&C 2 gets every other
  page as free.
- **Decided (owner): C&C 2's stand-in page map.** 128 KB of C&C 2's big SRAM, a bit per page; C&C 2 formats, marks
  everything free and serves pages from it. The page manager on C&C 3 comes later and replaces it; C&C 3's firmware
  is not built now.

## 2. The path to LPDDR (the figure)

- **Decided (owner): LPDDR, the memory controller with its PHY, AXI, the bus driver.** There is no separate AXI
  controller: AXI is the memory controller's own front.
- **Decided (owner): the bus driver** (first called the splitter) **is the only thing on AXI.** It serves requests of
  three sizes, read and write: **128 bytes** (a line), **2 KB** (a half page), **4 KB** (a page).
- **Decided (owner): its bus is always bus driver to or from a consumer, never consumer to consumer.** The consumers
  are the Updater, the RW engine, the Binder and the cache. The bus driver owns the bus, so there is no separate
  arbitration.
- **Decided (owner): that bus is as wide as AXI, not 1,024 bits,** and carries data only. A request is narrow: an
  LPDDR address, a size, read or write. **The bus driver returns each write's confirmation to its consumer.**
- **Decided (owner): the Tracer's fill bus is the other bus,** 1,024 bits, between the cache and the Tracer's three
  stations: its data cache, its instruction cache and its jump-table read. The cache is its only server.
- **Considered and dropped:** one 1,024-bit bus with every station on it and a token or an arbiter; a hub with spokes.
- **Proposed:**
  - one address space: the controller drives both LPDDR4X channels as one memory;
  - **a model of AXI in the simulator,** at the level of transactions and beats (owner: "some kind of AXI bus
    simulation"): the five channels, bursts, the write response, the data width, the delays and the requests in
    flight as parameters, set so a line read still takes about 80 cycles; a mode that varies the delays, to catch a
    read overtaking a write;
  - the bus driver may split a page into shorter bursts to let a line in between, as a parameter.
- **AXI's rules that the design leans on** (general knowledge; to check against the controller chosen): a burst does
  not cross 4 KB; reads and writes are not ordered against each other, so a read that must see a write waits for
  that write's response.

## 3. The cache

512 KB, 8 ways by 512 sets, keyed by LPDDR line address, as in the simulator's model.

- **Decided (owner): the Updater, the RW engine and the Binder reach it by a narrow look-up interface, not a line
  bus.** About 100 bits out and 36 back.
  - **A request port for each consumer, and one shared reply bus** carrying a consumer id (owner). A consumer has
    one request outstanding and always takes its reply.
  - **A page reference is the stored form:** a 31-bit address in 128-byte units, the owned bit above it (owner).
- **Its operations:**

| operation | request | reply |
|---|---|---|
| look up | the file's location (its root), its depth, a page number | the entry where the walk stopped, and that level |
| update | the same, a level, a new 32-bit entry | done |
| flush a page | a page | done, once its dirty lines have landed in LPDDR |
| drop a page | a page | done, once its lines are discarded, without writing them back |

  - **The entry says which case it is:** the hole value, a page with the owned bit clear (shared), or a page with it
    set. "Found" is the bottom level with the owned bit set.
  - **Levels count from the top:** level 0 is the top index page (owner's usage). Depth is the file's; level is the
    request's.
- **Decided (owner): the cache never allocates and never builds a page.** The Updater and the RW engine (the file
  system) handle a page fault.
- **Decided (owner): "drop a page" exists.** A deleted file's lines can't be found by file, and don't need to be: a
  free page's stale lines are harmless until the page is used again. **Proposed: the file system sends it when it
  hands a page out,** so one place covers every consumer; a page never used since the format needs none.
- **Decided (owner): no list of pending writes.** A line being written back stays in its place, marked "write in
  flight", until the bus driver confirms it. A fetch that wants its place waits, and so does a request for that
  line: it was the least recently used, so this is rare.
- **Decided (owner): the two ways of reaching a file.** The Tracer goes through the cache for everything, index and
  data lines, of its copy of memory and of the jump table. The Updater and the RW engine use the cache for the index
  levels only, and move data pages directly. They never meet on a page: a file is finished by the Updater before
  the Tracer starts on it, and the only pages the Tracer dirties are its own copy's.
- **Proposed:**
  - request queues from the file system (C&C 2's firmware) to the cache and to the RW engine (owner: "maybe we
    need"), each a small FIFO in the window, with a reply FIFO and an event;
  - the cache serves hits while a miss waits (non-blocking), now that background work shares it with the Tracer.
- **Open:** whether the cache is a peripheral of its own (C&C 2 has room for three) or a group of registers in the
  one peripheral.

## 4. The Updater

Its registers are `UPDATER-INTERFACE.md`.

- **Decided (owner): one 4 KB page memory.** The Updater is strictly sequential: fetch the page if it must, patch it,
  write it. The LRU waits meanwhile. Latency rises; bandwidth does not.
- **Decided (owner): after a page write it idles until the write's confirmation,** and only then updates the cache.
  So an entry never points at a page that has not landed.
- **Decided (owner): its inputs.**
  - **The segment queue, 4 KB,** filled from the peripheral bus, its credits to Ingress on a private signal
    (section 8 of the first note; the queue was "about 8 KB" there).
  - **The LRU sits after the queue,** on the Updater's side (the docs put the decoder in Ingress). The queue holds
    the data as sent.
  - **The LRU gives three things for each word:** a 32-bit address, a 64-bit value, and **a type: words, a half
    page or a full page.** It knows each run's start and count, so it knows what is still to come.
  - **The free page queue,** filled by C&C 2 from the peripheral bus.
- **The type lets it skip the read:**

| type | page already owned | page shared | page is a hole |
|---|---|---|---|
| full page | no read | no read | no read |
| half page | no read | the other half is read | no read; the other half is zeros |
| words | read | read | no read; patched into zeros |

- **Decided (owner): how a page fault goes.**
  - **A shared data page:** read it, take a free page, patch, write it there, wait for the confirmation, update the
    entry.
  - **A shared index page:** flush it, read it, clear the owned bit of its 1,024 entries, write it to a free page,
    wait, update the entry above. The entry for the level below is set later, by its own update.
  - **Top down, one page a level.** Once a level is not owned, nothing below it is.
  - **A missing page:** nothing is read; the page memory is filled with zeros, or with the hole value for an index
    page.
- **Decided (owner): the Updater builds all four images:** the cycle-0 image, the two at the start of segment *s*
  (the start image and the Tracer's copy), and the final image.
- **Decided (owner): the Tracer's copy is filled as each page of segment *s* is prepared.** The page is read, written
  unchanged to the Tracer's copy, then patched and written to the final image: two writes a page, in that stretch
  only, and every page read. Index pages and the two roots are read once and written twice, since both copies are
  the same; one look-up serves both trees, which stay in step.
- **Decided (owner): its unit of work is a run,** set up by C&C 2 in registers and started by `PULSE_RUN`, as on the
  replayer and the Tracer. **The type of run is a register** (owner). Three types:

| run | target | data |
|---|---|---|
| jump table | the run's table: empty, or a copy of a base table | whole pages only |
| data image | the cycle-0 image, a copy of the program image | update words |
| memory segments | the start image, then the final image and the Tracer's copy | all 128 segments as one stream |

- **Decided (owner): the memory segments are one run, found by counts.** C&C 2 writes the 64-bit words before
  segment *s*, the words in *s*, the total, the total of 128-bit words, and whether the stream is LRU-coded. The
  Updater switches images itself when a count runs out. (One run per segment was agreed first, then replaced.)
- **A jump table is the same machinery,** its easiest case: every write a full page, never a read.
- **Proposed:** its 128-bit total is a check only; the lowest priority for LPDDR.
- **Open:** the roots and counts as window registers or as a state record in the region; what a table smaller than a
  page looks like (proposed: a table file always has an index level).

## 5. The frames and the files

`HOST-FRAMES.md` has the frames. What was decided on the way:

- **Decided (owner): every count the Updater needs comes from the proof header** (first called the run header), which
  C&C 2 copies into the Updater's registers; nothing travels at the front of the data.
- **Decided (owner): the memory segments are one contiguous stream.** The register states and the advice logs, which
  go to C&C 3, are frames of their own.
- **Decided (owner): the advice logs' table holds the exact number of 64-bit values;** a log's 128-bit words are
  that, rounded up to even and halved. The Tracer needs the exact count (its advice field).
- **Decided (owner): no reserved fields.**
- **Decided (owner): the jump table is out of the proof header, and a run's table is either a full new table or
  entries added to an existing one.** The existing one is a **base jump table**, loaded on its own: from a common
  run, from many runs, or from the static analysis.
- **For consideration, not decided (owner, 2026-11-16): the Tracer's jump cache from 64 entries to 128.** An entry is
  8 bytes and saves at most one 128-byte line of the big cache. With base jump tables that a run can add to, a run's
  table may be dense enough that the larger cache is not needed; to look at again as more of the design is done. To
  measure when building: the map's stalls at 64, 128 and 256 entries, and a set-associative one of 1,024 or 2,048.
  The Tracer is built, so it would be a request to Tony, not a change made here.
- **Considered and set aside:** a bitmap of the targets a run uses with an on-chip builder; a table for each
  segment; a table ordered by address.
- **Decided (owner): the copy-on-write files go in the working directory,** and go with it at cleanup.
  **Proposed:** a file there is copied only from a permanent file or from a file in the same working directory, so
  cleanup is never blocked.

## 6. Open, beyond what each section lists

- From the first note, still open: paths in requests; C&C 2's code size; the shutdown sequence; whether normal
  operation can resume from the shell.
- The bus driver's interface to a consumer, as exact fields.
- The cache's and the RW engine's request queues, as exact fields.
- Everything `V136-SCOPE.md` raises.
