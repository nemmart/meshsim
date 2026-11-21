# The file system's API, v1.40 (draft)

*The FS-API session, started 2026-11-19. (owner) marks a ruling, with its date; a dagger (†) marks what this session
proposes and the owner has not ruled on. Section numbers without a document are `v1.37/spec/machine-spec.md`'s;
chapter 29 is `host-frames.md`, chapter 30 `master-firmware.md`. Nothing here is built.*

**What this document is.** The file system that v1.40 builds, as its callers see it: the hardware it runs on, the
work it does for the host, the structures it keeps in LPDDR and in its own memory, and its calls. Every later step of
v1.40 is built from it: the shell, the C++ emulation, the RW unit's design, the file system on the chip. It does not
design the RW unit, the mesh's regimes, error recovery (item 181) or the layout of a request on the wire.

## 1. The system

**The ASIC is a server.** The host sends it frames; each request the frames make is a remote call, which the ASIC
serves and answers (29.14) (owner, 2026-11-19). **The file system lives as long as the power does:** a hard reset,
power-on among them, formats it, leaving an empty root directory; a soft reset keeps it, so that what a failed proof
left can be read (section 3.8) (owner, 2026-11-19).

**What v1.37 does and v1.40 changes.** v1.37 has no file system. Its master tile keeps a page map of its own, takes
pages from it in order and frees nothing; it gives the Updater every root and every free page, and throws away every
name a frame carries (30; `fw/cc/master.c`). v1.37 is a stage in the design, not the system's final shape
(owner, 2026-11-19): in v1.40 programs are stored ahead of time, and a proof uses what is stored.

## 2. The hardware it runs on

### 2.1 Who is where

| Tile | What it does for the file system |
| --- | --- |
| **the memory tile** | **runs the file system** (owner, 2026-11-19). Its peripheral is the RW unit, its way to LPDDR. **Its 32 KB of data memory** holds the fd table and the working directories, **its 1 MiB of SRAM** the pool of 2 KiB buffers that are its cache and its writers' buffers; its code is in 32 KB of program memory (owner, 2026-11-20; `FS-DATA-PATH.md` 6, 8) |
| **the master tile** | serves the host's requests: takes the frames from Ingress, calls the file system, drives the Updater (its peripheral 2, 17.10), hands the tracer tile its segment. It feeds the Updater's free page queue from the pages the tracer tile's page service gives it |
| **the tracer tile** | traces the segment through the Tracer (17.9), which reads and writes files through the cache; writes each compute tile's three run logs into the file system (section 3.4). **It runs the page service** (owner, 2026-11-20): the page maps, the two pools, the discards (4.1, 4.2; `FS-DATA-PATH.md` 7) |
| **the compute tiles** | each writes logs of its own into the file system (section 3.4) |

**Every caller is remote:** a call is a request to the memory tile (11.5) and its answer. Up to three requests from
one tile may be outstanding to the memory tile (11.3). A request is 128 bits; what does not fit (a path, a call's
data) travels by `WRITE` packets, as 23.4's writes do today. How is item 119's, and step 4's.

### 2.2 Files, as the hardware fixes them (23.5)

- **LPDDR is pages of 4 KiB.** A page's address is in **units** of 128 bytes, so a multiple of 32. LPDDR has up to
  2²⁰ pages (4 GiB).
- **A file is a tree:** index pages of 1,024 entries of 32 bits above the data pages. Its **depth**, 0 to 3, gives it
  room for 2^(10·depth) data pages; a file of depth 0 is its root alone, which is its data page. Its **root** is the
  address of its top page.
- **An entry** is [30:0] a page's address in units and [31] **owned**: 1 when the page is the file's own, 0 when it
  is shared with the file it was copied from. 0xFFFF_FFFF is a **hole**, and reads as zeros.
- **Owned all the way down:** below an entry that is not owned, nothing is the file's own.
- **A copy** is its source's root copied with every entry's owned bit cleared. A writer makes a shared page its own,
  top down, before it changes it.

### 2.3 The Updater (17.10)

The master tile's peripheral. A **run** applies update words from the host's stream to one file, making each page it
changes the file's own first, with pages from its **free page queue** (256 entries, refilled by the master tile under
a credit of 128 entries). Before a run it can make the file's root itself (`RUN_CONFIG.root`): 1 an empty root at
`IMAGE_ROOT`; 2 a copy of the root at `SOURCE_ROOT`, and with `tracer_copy` the same copy at `TRACER_ROOT` too.
**The Updater is not changed** (owner, 2026-11-19: its state machine is complex, and changing what it does is not a
small task). So the Updater keeps making roots, and the file system leaves that to it (section 5.3).

### 2.4 The cache (23.6)

512 KiB of lines of LPDDR, 8 ways by 512 sets, shared by the chip. The Tracer reads and writes its files through it
(`fill`); the Updater looks up and updates index entries through look-up port 1, and moves data pages on its own
AXI port. **Index lines are never dirty:** an update writes through. The Tracer's data lines are dirty until evicted.

**What it asks of its clients (23.6.8),** and who keeps each in v1.40:

| Rule | Kept by |
| --- | --- |
| the data is in LPDDR before an entry names it | the Updater (17.10.5); the file system, for its own files |
| one writer for a file at a time | the file system's calls (section 5); the master tile, which starts the runs |
| an owned index page changes only by `update`, unless it is new | the Updater; for a streamed file, section 4.5's rule |
| **a page is used only after its discard's reply** | the tracer tile's page service: every page it hands out is new (section 4.2) |
| a data page the Tracer reads is not written directly while it does | the master tile: no run on the Tracer's files while it traces |
| a page the Tracer has written is not read directly | the file system: it never reads a COW file's data page directly (section 4.4) |

**A discard** is a request on a look-up port (op 2): every line of a page that is in the cache is invalidated, and
nothing is written. In v1.37 it holds the cache's walker for the page's 32 lines, about 35 cycles, during which
nothing else is served. **In v1.40 it is background work** (owner, 2026-11-19): see section 7.

## 3. The flows

The work the file system does, as the calls it makes (section 5). Paths here are examples; the working directory
`wd` is the one the proof's frame names (29.7).

### 3.1 Power-on and the resets: `fs_startup` (owner, 2026-11-20)

After every reset the file system is shut down: every call is `FS_ESHUTDOWN` until **the master tile calls
`fs_startup()`**, which looks at the superblock and returns the mode it ran in:

- **no superblock → it formats** (`FS_STARTED_FORMAT`): the superblock and an empty root directory, in page 0;
- **a superblock marked shut down cleanly → it mounts** (`FS_STARTED_MOUNT`);
- **a superblock not shut down cleanly → it checks** (`FS_STARTED_FSCK`).

**The mount and the check are one walk** (4.4): every entry from the root, each file's pages reached by its owned
entries, the "taken" map built from them. The check also repairs what a stop can leave: a page linked nowhere is free
again (the walk never reaches it); a pending root is forgotten; every directory's count of live entries and every file's
count of copies is recomputed and rewritten where it differs; an entry that makes no sense (an unknown type, a root
outside LPDDR, a depth over 3, an unterminated name) or whose tree reaches a page another file owns is dropped, its
pages free again, and so is a copy whose source is gone. **Lengths** (owner, 2026-11-20): a streamed file's or a
directory's length past what its tree holds is cut back to it, and a directory's to whole entries before any hole in
its tree; a COW file whose depth is not its length's, or a copy not of its source's length and depth, is dropped. A
length too short is left as it is: what was past it cannot be known. Then **the "taken" map goes to the tracer tile** (128 KiB for
2²⁰ pages, built in the memory tile's cache buffers, which are empty at a reset), and its "may be cached" map starts
clear: a reset empties the cache (23.6.10). The superblock is marked mounted. Every tile's working directory is the
root.

**No bit from the hardware is needed:** the superblock decides (owner, 2026-11-20). A power-on loses LPDDR's contents,
so it finds none.

### 3.2 Storing a program (a request from the host)

The host stores its programs ahead of the proofs that use them (owner, 2026-11-19). A program is **two files at one
stem**: `<stem>.pages`, its memory image, and `<stem>.map`, its jump table. There is no `<stem>.sha` (owner,
2026-11-19). The image is a COW file built by the Updater, as v1.37's frames 1 and 2 build it † (owner, 2026-11-19:
"I'm guessing it's some kind of COW and built by the updater, but it doesn't have to work that way").

```
fs_reserve_pages(pages + 2)                                 // the run's pages and the two roots, in the Updater's pool
cow_create("<stem>.pages", length_pages)  -> root, depth    // its root page taken; the entry's root 0 until committed
  Updater run: root 1 (empty), IMAGE_ROOT root, depth; its pages by fs_acquire_pages(...) as credits come
cow_commit("<stem>.pages")                                   // at the run's done: the entry names its root
cow_freeze("<stem>.pages")
the map the same way: cow_create, a run, cow_commit, cow_freeze   // a base table (29.8)
fs_unreserve()
```

### 3.3 Running a proof (a request from the host)

What v1.37's frame loop does (30.3 to 30.5), with names. Every COW file is made by `cow_create` or `cow_copy` and
built by the Updater's next run; it is committed at that run's done, and then frozen, all but the Tracer's copy.

```
fs_mkdir(wd)                                  // FS_EEXIST: the proof is refused (DESIGN-NOTES, section 6)
fs_reserve_pages(the proof's pages)           // FS_ENOSPC: refused, before anything is written
the proof's table:   cow_copy("<base>", wd/map) and a run with root 2, or cow_create(wd/map, pages) and root 1
                     cow_commit(wd/map); cow_freeze(wd/map)
the cycle-0 image:   cow_copy("<stem>.pages", wd/cycle0); frame 6's run, root 2
                     cow_commit(wd/cycle0); cow_freeze(wd/cycle0)
the start image:     cow_copy(wd/cycle0, wd/start); the run of segments before s, root 2
                     cow_commit(wd/start); cow_freeze(wd/start)
the final image and  cow_copy(wd/start, wd/final); cow_copy(wd/start, wd/tracer)
  the Tracer's copy: the run of segment s, root 2 with tracer_copy, which makes both roots
                     cow_commit(wd/final); cow_commit(wd/tracer)
                     the run after s, root 0
                     (each image held by the master tile with fs_open(FS_ROOT) from its copy to its run's done,
                      and closed then; a source's root read by holding it a moment: cow_root(fd))
the hand-over:       record s names wd; the tracer tile holds wd/tracer and wd/map itself, fs_open(FS_ROOT),
                     and reads their roots with cow_root(fd) (owner, 2026-11-20)
the segment:         the tracer tile writes the run logs; the compute tiles write their logs (3.4)
                     the tracer tile closes its two; cow_freeze(wd/final), once the third run is done; closed
the reply (29.14);   then 3.5 or 3.6
fs_unreserve()
```

- **The pages a run takes** come from `fs_acquire_pages(buf, k)`, k ≤ 128, as the free page queue's credits come
  back. The master tile pushes them into the queue, as `feed_pages` does today: that stays the master tile's own code
  (owner, 2026-11-19).
- **A root is taken from the reservation too** †: v1.37 takes roots from its map and leaves them out of `PAGE_COUNT`
  (29.4), so a reservation is the frames' page counts plus one page for each file made.
- **The run logs' pages** are the file system's, taken as the logs grow. Whether the proof's reservation covers them
  is open † (the host does not know their size).

### 3.4 The logs written during a segment

- **The run logs** (owner, 2026-11-19): for each compute tile, the Tracer produces three, the **code log**, the
  **read log** and the **write log**. Today they are copied to the compute tiles as they are made; in some
  computations a tile's SRAM cannot hold them, so they go into the file system. **The tracer tile writes them with
  `fs_open`, `fs_write` and `fs_close`:** they are streamed files. It has three open at a time.
- **The tracer tile and each compute tile first `fs_chdir` into the proof's `logs` directory** and name their logs
  relative to it (4.7); they `fs_chdir("/")` at the proof's end, so that the directory can be removed.
- **Their volume** (owner, 2026-11-19): 24 to 48 MB over all 192 files, made in about 10 ms. At 1 GHz that is 2.4 to
  4.8 bytes a cycle, a 128-bit word every 3 to 7 cycles, from the tracer tile to the memory tile, for the whole
  trace: 125 to 250 KB a file on average.
- **Each compute tile's own logs** (owner, 2026-11-19): a compute tile may have at least four files open.
- **If the proof succeeds,** the logs can probably be removed at the proof's end (owner, 2026-11-19). **If it fails,**
  they are kept, to be read with the shell (3.8).

### 3.5 The proof succeeds

The logs and the Tracer's copy are removed (`fs_remove`); the images, `wd` itself and when it goes are the host's †
(DESIGN-NOTES section 6: a later request from the host, perhaps once the proof has verified).

### 3.6 The proof fails in software

A check fails, or a compute tile reports an error, with the chip still running: the master tile has every file
closed, and replies that the proof failed. `wd` and everything in it are kept †.

### 3.7 A hardware error in a compute tile

The master tile learns of it (its NMI, 30.8) and **closes out the file system** (owner, 2026-11-19): it calls
`fs_shutdown`: the calls in hand finish, every open file's buffered bytes and index halves go to LPDDR, the
superblock is marked shut down cleanly, and every call after is refused (owner, 2026-11-20); then it answers. Then the master tile parks, as today. **Errors in the file system's own path,** the memory tile, the RW unit
or the mesh between, are not covered (owner, 2026-11-19: "we're really thinking about errors in the compute tiles,
not the FS"); what follows any error is item 181.

### 3.8 After a soft reset: the shell

The memory tile mounts (3.1); the master tile runs the shell on its UART (`fs_wip/planning/SHELL-SPEC.md`), which
reads the logs with `fs_list`, `fs_stat`, `fs_read` and `fs_read_page`, and can remove files.

### 3.9 Removing a working directory (a request from the host)

Every file in it removed with `fs_remove`, a copy before its source: the Tracer's copy and the final image, then the
start image, the cycle-0 image, the proof's table; then the logs and `wd`. Each COW file's removal lowers its source's
count of copies (4.3). Whether a call removes a tree in one go is open †.

## 4. The data structures

### 4.1 The page maps, on the tracer tile (owner, 2026-11-20)

| Map | Size | A bit is 1 when |
| --- | --- | --- |
| **taken** | 2²⁰ bits, 128 KiB | the page is in use: in a file, in a pool's prepared batch, or page 0 |
| **may be cached** | 2²⁰ bits, 128 KiB | the page was handed out by `fs_acquire_cow_pages` (or as a COW file's root) since its last discard, so lines of it may be in the cache (owner, 2026-11-20) |

- Page *p* is bit *p* of each, as v1.37's map has it (`master_layout.h`): 128 pages to a 128-bit word.
- **Neither is kept in LPDDR** (DESIGN-NOTES section 2): a hard reset starts them fresh, and a mount rebuilds both by
  the walk of section 4.4, setting "may be cached" for every page of a COW file.
- **A pool for each tile that asks** (owner, 2026-11-20): the master tile's, drawn on for every run and every COW
  file's root, and the memory tile's, for its directories and streamed files. **The acquire says what the pages are
  for; nothing else does:** `fs_acquire_cow_pages` marks its pages "may be cached", since they go to the Updater or the
  Tracer; `fs_acquire_pages` does not. Either tile may make either call. Reserving and freeing say nothing of a page's
  kind.
- **A reservation is a count** (owner, 2026-11-20): `fs_reserve_pages(n)` holds n pages in the caller's pool, or fails
  with no room; no other pool takes what is held. One reservation a pool at a time (`FS_EBUSY`). In practice only the
  master tile reserves, once per request; the memory tile takes its pages as needed, `FS_ENOSPC` when none.

### 4.2 Freeing and handing out pages

- **Freeing marks a page free and does nothing else** (owner, 2026-11-20): nothing is discarded then.
- **A page is discarded when it is handed out again, and only if it may be cached**: the tracer tile keeps **a prepared
  batch of 256 pages for each pool** (owner, 2026-11-20), taken and discarded before anyone asks, so an acquire of 256
  or fewer is a copy out of the batch; it refills the batch in the background. **Every page it hands out is new**
  (23.6.8). The Updater's pool takes the pages that need a discard first, the file system's the others †, so that the
  discards fall mostly outside tracing (`FS-DATA-PATH.md` 7).
- **The file system's own pages** (directories, streamed files) are never in the cache (4.5), so their bit is never set.

### 4.3 The directory entry

A directory is a file of 128-byte entries, 32 to a page (DECISIONS-v1.3, section 20). An entry, least significant
byte first:

| Bytes | Field | |
| --- | --- | --- |
| 0–95 | name | up to 95 bytes, zero-padded, so always followed by a zero byte |
| 96–103 | length | u64, in bytes |
| 104–107 | root | u32, the file's root in units; 0xFFFF_FFFF none (an empty file) |
| 108–111 | type | u32: 0 a free entry, 1 a directory, 2 a file † (item 119's type values) |
| 112–115 | count † | u32: for a file, the copies made from it; for a directory, its live entries (DESIGN-NOTES section 4, decided) |
| 116–119 | source † | u32, an entry's unit address: **for a copy, its source's; for a directory, its parent's** (owner, 2026-11-20), the root's its own; 0xFFFF_FFFF none |
| 120–123 | flags † | [0] frozen; [1] COW, made by `cow_create` or `cow_copy`; [3:2] depth; the rest 0 |
| 124–127 | pending | u32: a COW file's root to be, from `cow_create` or `cow_copy` to `cow_commit`; 0 otherwise (owner, 2026-11-20) |

- **A file's depth** follows from its length for a streamed file, and is fixed when a COW file is made (29.4); it is
  kept in the flags † so that neither has to be worked out.
- **An entry can be removed only when its count is 0** (DESIGN-NOTES section 4, decided).
- **The root directory has no parent:** its entry is in the superblock (4.4).
- **A root of 0 is a COW file not yet committed** (owner, 2026-11-20): page 0 is the superblock's, so no file's root
  is ever 0. A walk of such a file reads nothing; 0xFFFF_FFFF is an empty streamed file.

### 4.4 The superblock, and the walk

**One superblock, in page 0** (owner, DESIGN-NOTES section 2), by line †:

| Line | Holds |
| --- | --- |
| 0 | magic (8 bytes), version (u32), LPDDR's size in pages (u32), **state** (u32, owner 2026-11-20): 1 mounted, in use; 2 shut down cleanly. The rest 0. Written at the format; the state also at every mount (1) and by `fs_shutdown` (2), so a mount knows whether the last shutdown was clean |
| 1 | the root directory's entry (4.3), name `/` |
| 2–31 | reserved, 0 |

**The walk** follows a file's owned entries from its root, stopping at any entry that is not owned or is a hole. It
reaches exactly the pages the file owns: the root, its owned index pages, and the data pages its owned entries at the
bottom level name. **Every page has one owner** (DESIGN-NOTES section 3), so the walk counts no page twice.

- **It reads only index pages, through the RW unit,** directly from LPDDR †: an index line is never dirty in the
  cache, so LPDDR has it as it stands, and nothing is updating a file that is being walked. **It never reads a data
  page:** the level says an entry names one, and that is all it needs (owner, 2026-11-19).
- **Removing a COW file is that walk,** every page reached freed (owner, 2026-11-19). It costs a read of each owned
  index page and nothing more.
- **A mount** walks every file from the root directory, so rebuilds the maps.

### 4.5 Streamed files

- **A streamed file is written only by the file system,** data and index pages alike, through the RW unit, and read
  only by it. **It is never walked through the cache:** its root is never given to the Updater or the Tracer
  (`cow_root` refuses one) †. So the file system may write its index pages directly at any time, and its pages never
  need a discard.
- **Its entries are written owned,** so that removing it is section 4.4's walk.
- **It grows in depth** as it passes 4 KiB, 4 MiB and 4 GiB: a new index page above, the old root its entry 0
  (DECISIONS-v1.3 section 20); its directory entry's root and depth change then.

### 4.6 An open file, in the memory tile

**The fd table** (owner, 2026-11-20): **at most 512 files open at once, an entry of 16 bytes each**, 8 KiB. An fd is
its entry's index.

| Bytes | Field |
| --- | --- |
| 0–3 | [19:0] the root's page number; [21:20] the depth; **[31:22] the buffer** (owner, 2026-11-20): a writer's current half, or for a reader the buffer last holding its current half, a hint checked against that buffer's tag; 1023 none |
| 4–7 | the length: **in bytes for a streamed file, in pages for a COW file** (owner, 2026-11-20), so that a COW file's 32 GiB (29.4) fits |
| 8–11 | [24:0] the directory entry's address, in 128-byte units (its page × 32 + its slot; 25 bits for 4 GiB); [27:25] the mode (`FS_READ`, `FS_APPEND`, `FS_WRITE`); [28] COW; [29] in use; [31:30] 0 |
| 12–15 | **a reader's position**, for `fs_read`: **its low 11 bits are the offset into its buffer** (owner, 2026-11-20), the bits above say which half of the file. A writer's: 0, since an append writes at the length and `fs_write_page` names its page |

- **A streamed file is at most 4 GiB,** its length's 32 bits.
- **128 writers at once** (owner, 2026-11-20): the 64 compute tiles' logs, the tracer tile's three and room to spare.
  An `fs_open` to write when 128 are open for writing is `FS_EBUSY`. A writer's buffer names its current half in the
  memory tile's pool (`FS-DATA-PATH.md` 6).
- **The fd table is in the memory tile's data memory** (owner, 2026-11-20), not its SRAM: the core reads and writes it
  on every call.

**A writer's buffers**: **the 2 KiB half it is filling** (owner, 2026-11-20: "we build up the partial writes to 2kb"),
written to LPDDR when full and at `fs_flush`, `fs_close` and `fs_shutdown`; and **the cached half of each index page
above it** (`FS-DATA-PATH.md` 6). An appended file fills its entries in order, so only one half of each index page
changes at a time; it is written as a 2 KiB half when its 512 entries are full, every 2 MiB of data, and at a flush.
**The RW unit moves only 2 KiB and 4 KiB** (owner, 2026-11-20): every smaller change is made in a buffer and written
as its half.

### 4.7 The working directories, in the memory tile

**Each tile has a single working directory** (owner, 2026-11-20), kept beside the fd table: a directory entry's unit
address for each tile, 4 bytes, **66 of them** (the compute tiles 0–63, the master tile 64, the tracer tile 65), in
the memory tile's data memory (owner, 2026-11-20). **A tile sets its own,** with `fs_chdir`; **every tile's
is the root at a reset** (owner, 2026-11-20). A path not starting with `/` is resolved from it, so a tile's requests
carry short paths (`tile3.log`) to the memory tile.

- **The master tile has one,** shared by its firmware and its shell. The shell keeps a working directory of its own,
  as a string, and passes the file system absolute paths, so that its `cd` never moves the firmware's.
- **A directory that is any tile's working directory is not removed** (`FS_EBUSY`).
- **`..` is a directory's parent,** from its entry's source field (4.3): one step, no search. The root's parent is
  itself. A parent never changes, since nothing renames.

## 5. The calls

All return a negative error (5.5) or a value of 0 or more. *Who* is the callers that may make the call (section 6).

### 5.1 Pages (owner, 2026-11-19)

The raw pages everything else is built on. **The master tile, to drive the Updater, and the file system itself;**
not the shell, not a compute tile.

| Call | Who | Does |
| --- | --- | --- |
| `int fs_reserve_pages(u32 n)` | master | holds n pages in the caller's pool for one request, or fails with `FS_ENOSPC`; `FS_EBUSY` while it holds one (owner, 2026-11-20) |
| `int fs_acquire_cow_pages(u32 *pages, u32 k)` | master | k ≤ 256 pages of the caller's pool, from its reservation if it holds one, into `pages` as addresses in units; each new (23.6.8), and **marked "may be cached"**: for the Updater's runs. Returns k, or fewer if the pool has fewer (owner, 2026-11-20) |
| `int fs_acquire_pages(u32 *pages, u32 k)` | master, the file system | the same, **not marked**: for directories and streamed files, which never pass through the cache. The memory tile's stash is filled this way (owner, 2026-11-20) |
| `int fs_release_pages(const u32 *pages, u32 k)` | master, the file system | k pages back to the free map, by address alone: whether each needs a discard was marked when it was handed out (owner, 2026-11-20) |
| `int fs_unreserve(void)` † | master | ends the reservation; what is left of it is free again |

**Served by the tracer tile** (owner, 2026-11-20): the master tile's page calls go to it, not to the memory tile; the
memory tile takes the file system's pool's pages from it the same way.

### 5.2 Streamed files (the calls of 2026-11-10, owner; `fs_wip/README.md`)

| Call | Who | Does |
| --- | --- | --- |
| `int fs_open(const char *path, u32 flags)` | all | an fd; `FS_READ`, `FS_APPEND` (creates the file if missing), `FS_WRITE` (page writes); `FS_TRUNC` † with `FS_APPEND` empties it first; `FS_ROOT` alone, the master and tracer tiles', holds a COW file for hardware (5.3) |
| `long fs_read(int fd, void *buf, long n)` | all | up to n bytes at the position; 0 at the end |
| `long fs_write(int fd, const void *buf, long n)` | all | appends n bytes; they are in the file system's buffer when it returns, not yet in LPDDR. **`FS_ENOSPC`:** no page for the bytes; the file keeps every byte in the pages it has stored, its length cut back to them, and the bytes of the page that found no room are lost (owner, 2026-11-20) |
| `int fs_flush(int fd)` † | all | returns when every byte written so far, and the index lines naming them, are in LPDDR |
| `int fs_close(int fd)` | all | as `fs_flush`, then the fd is free, even when the flush is `FS_ENOSPC` (as `fs_write`'s): the index lines and the entry are written all the same |
| `int fs_read_page(int fd, u64 page, void *buf)` | all | 4 KiB at page × 4,096 |
| `int fs_write_page(int fd, u64 page, const void *buf)` | all | 4 KiB at page × 4,096; never to a COW file (`FS_EROFS`) |
| `int fs_fprintf(int fd, const char *fmt, ...)` | all | formats into the file, as `fs_write` |
| `int fs_chdir(const char *path)` | all | the caller's tile's working directory (4.7); `FS_ENOTDIR` for a file (owner, 2026-11-20) |
| `int fs_mkdir(const char *path)` | all | an empty directory, its parent in its entry |
| `int fs_remove_directory(const char *path)` | all | **a directory and every file in it, in one call** (owner, 2026-11-20): refused, with nothing removed, if it has a subdirectory (`FS_ENOTEMPTY`), any file in it is open, `FS_ROOT` included (`FS_EBUSY`), it is a tile's working directory (`FS_EBUSY`), or a file in it has a copy outside it (`FS_EBUSY`). Copies inside are freed leaves first; a copy of a file outside lowers that file's count. No entry inside is written: one write of the parent's entry. A proof's working directory goes in two calls, its `logs`, then itself |
| `int fs_remove(const char *path)` | all | a file with no copies and not open, or an empty directory that is no tile's working directory (`FS_EBUSY`); a COW file by the walk (4.4) |
| `int fs_list(const char *path, u32 *pos, struct fs_dirent *e)` † | all | the entry at the cursor `*pos`, which starts at 0 and is advanced past it; 1 an entry, 0 the end. **In slot order,** not creation order: a new entry may take a removed one's slot. The shell sorts what it prints (owner, 2026-11-20) |
| `int fs_stat(const char *path, struct fs_stat *st)` † | all | a file's type, flags, length, pages, depth and copies |
| `int fs_df(u64 *total, u64 *free)` † | all | the pages of LPDDR, and those free |

**One writer, or readers** (owner, 2026-11-20): a file is open for writing by one fd, or for reading by any number,
never both. `FS_READ` does not combine with `FS_APPEND` or `FS_WRITE` (`FS_EINVAL`); opening a file to write while it
is open at all, or to read while it is open to write, is `FS_EBUSY`. (A writer's newest pages are named only by the
index lines in its buffer, 4.6, until it flushes.)

**Served in parallel, up to four at a time: reads and writes only** (owner, 2026-11-20): `fs_read`, `fs_write`,
`fs_fprintf`, `fs_read_page`, `fs_write_page`, `fs_flush`. **Every other call is served one at a time**, in the
order it arrives. The memory tile keeps four request contexts in its data memory (`FS-DATA-PATH.md` 8).

### 5.3 COW files (master tile only)

Made only by the master tile, built only by the Updater (owner, 2026-11-19). **The Tracer may change the contents of
its copy, but adds no pages** (owner, 2026-11-19); the hardware refuses it a write into a hole or a page not its own
(23.6.4). Nothing writes a COW file with `fs_write` or `fs_write_page`.

| Call | Does |
| --- | --- |
| `int cow_create(const char *path, u64 length_pages, u32 *root, u32 *depth)` | a new COW file, its depth fixed by its length; its root page taken from the master tile's reservation and returned in `*root`, **not yet made**: the next Updater run on it, with `root` 1, makes it. The entry's root is 0 and the page is its pending root |
| `int cow_copy(const char *src, const char *dst, u32 *root, u32 *depth)` | a new COW file with src's length and depth, src's count raised; src must be a frozen COW file (`FS_EINVAL`). Its root page as `cow_create`'s: the next run, with `root` 2 and `SOURCE_ROOT` src's root, makes it |
| `int cow_commit(const char *path)` | the entry's root becomes its pending root: called once the run that makes the root is done. `FS_EINVAL` for a file not pending (owner, 2026-11-20) |
| `int cow_freeze(const char *path)` | sets the frozen flag; `FS_EBUSY` before `cow_commit` |
| `int cow_root(int fd, u32 *root, u32 *depth)` | the root and depth of the COW file held by fd, which was opened `FS_ROOT`; `FS_EBADF` for any other fd, `FS_EBUSY` before `cow_commit`. The master tile's and the tracer tile's (owner, 2026-11-20) |

**`FS_ROOT`: a COW file held for hardware** (owner, 2026-11-20). The Updater and the Tracer use a file by its root,
not through an fd, so the file system cannot see them; the tile that drives them holds the file open instead:
`fs_open(path, FS_ROOT)`, alone, on a COW file, frozen or not, committed or not. **One owner:** it is refused
(`FS_EBUSY`) while the file is open at all, and while it is held nothing else opens the file. The fd is not read or
written through; it names the file's root for `cow_root`, and `cow_commit` and `cow_freeze` are allowed while it is
held. **The master tile holds an image from its copy to its run's done, then closes it; the tracer tile then holds
the Tracer's copy and the map for the trace.** A file held is not removed (`FS_EBUSY`). A source needs no holding:
its copies' count keeps it. The master tile's and the tracer tile's alone.

**`cow_create` and `cow_copy` leave the root to the Updater** † (the owner's ruling on the Updater, 2.3; item 191 would
close with it), **and the file is committed when the run is done** (owner, 2026-11-20). Until then its entry's root is
0, so nothing walks a root the Updater has not yet written: `fs_open` and `cow_root` are `FS_EBUSY`, `cow_freeze` is
`FS_EBUSY`, `cow_copy` from it is `FS_EINVAL` (not frozen), `fs_stat` counts no pages, and `fs_remove` frees the
pending page alone, which was never named and never cached. A mount forgets a pending root, so its page is free
again. The master tile commits a file only after the run that makes its root is done; that rule is its own.

### 5.4 The file system as a whole (master tile only)

| Call | Does |
| --- | --- |
| `int fs_shutdown(void)` | **the calls in hand finish first; then** every open file's buffers, index halves and entry go to LPDDR and every fd is closed; **then** the superblock is marked shut down cleanly; **then** the file system is shut down: every call waiting, and every call after, is `FS_ESHUTDOWN`, until the next reset. Answers when it is done (owner, 2026-11-20) |

**`int fs_startup(void)`**, the master tile's, once after every reset (3.1): a format, a mount
or a check, as the superblock says; it returns `FS_STARTED_FORMAT` (1), `FS_STARTED_MOUNT` (2) or `FS_STARTED_FSCK`
(3), or `FS_EBUSY` if the file system is started already (owner, 2026-11-20).

### 5.5 The errors

From the shell's spec (`SHELL-SPEC.md` section 5): `FS_ENOENT` −1, `FS_EEXIST` −2, `FS_ENOTDIR` −3, `FS_EISDIR` −4,
`FS_ENOTEMPTY` −5, `FS_EROFS` −6 (frozen, or a COW file), `FS_EBUSY` −7 (has copies, or is open), `FS_ENOSPC` −8,
`FS_EBADF` −9, `FS_EINVAL` −10, `FS_EIO` −11, `FS_ENAMETOOLONG` −12, `FS_ESHUTDOWN` −13.

**Paths** (owner, 2026-11-20): absolute from `/`, or relative to the caller's tile's working directory (4.7); `.`
and `..` may appear anywhere. No empty name (`//`) and no trailing `/` but the root's (`FS_EINVAL`).

**The limits:** a name of at most 95 bytes; a path of at most 1,023 bytes and 10 names (DECISIONS-v1.3 section 20).

## 6. The callers

| Caller | Its calls |
| --- | --- |
| the master tile | all: the pages (5.1) and the COW files (5.3) are its alone, and `fs_shutdown` |
| the tracer tile | the streamed files (5.2): the run logs; `fs_open(FS_ROOT)` and `cow_root` for the trace (5.3) |
| a compute tile | the streamed files (5.2): its own logs; at least four open |
| the shell, on the master tile | the streamed files (5.2), to read, list, remove and write; never 5.1 or 5.3 (owner, 2026-11-19) |
| the file system itself | the pages (5.1), for its directories and streamed files |

There is no kernel and no permission check (owner, 2026-11-19): who may call what is a rule its callers keep.

## 7. What it asks of the hardware (a list, for step 4)

- **The RW unit:** the tracer tile's run logs into LPDDR at 2.4 to 4.8 bytes a cycle for about 10 ms (3.4); the
  walk's index reads (4.4); the file system's own page and line writes (4.5, 4.6).
- **The mesh:** the same from the tracer tile to the memory tile, 15 % to 30 % of a link's 16 bytes a cycle.
- **A look-up port for the tracer tile,** one of ports 0, 2 and 3, for the page service's discards (4.2; owner,
  2026-11-20). The memory tile does not use the cache.
- **The RW unit moves only 2 KiB and 4 KiB** (owner, 2026-11-20).
- **The discard as background work** (owner, 2026-11-19): the walker serves a discard a line at a time, and between
  its lines serves any request of the Tracer's that **hits** in the cache. A Tracer request that misses may wait until
  the discard is done, and so may every look-up port's. The reply still follows the 32nd line. A change to the cache
  (23.6.3, 23.6.5).
- **The free page queue cannot give pages back:** pages pushed and not used by a run stay in it until the reset
  (17.10.4). With the host's counts exact (29.7) none are left; if a count is ever wrong, they are lost to the file
  system until the next hard reset.

## 8. Open points †

1. `cow_create` and `cow_copy` leave the root to the Updater's next run (5.3), committed at its done; or `cow_copy`
   makes it with a run of no words.
2. Whether roots come from the reservation (3.3), and whether the run logs' pages do.
3. `fs_unreserve`. (A reservation is a count, ruled 2026-11-20.)
4. The "may be cached" map; which pages each pool's batch prefers (4.2).
5. The directory entry's fields in bytes 112–127, and the type values (4.3).
6. The superblock's lines (4.4).
7. `fs_flush`. (The writers' buffer pool: 128, ruled 2026-11-20.)
8. The shell's four additions: `FS_TRUNC`, `fs_stat`, `fs_df`, `fs_list`'s cursor (`SHELL-SPEC.md` section 14).
9. Whether the proof's success removes the logs and the Tracer's copy, and when `wd` goes (3.5); whether one call
   removes a tree (3.9).
10. A request for the host to read a file back, or list what is stored.
11. The size of a call's data and how many calls a caller may have outstanding (2.1), against the tracer tile's rate.

## 9. What the older sources say that this overtakes

- **`fs_wip/README.md` and `SHELL-SPEC.md`:** the file system on C&C 2 and the page manager on C&C 3 are the memory
  tile's together; "per-proof pages" are a reservation; the shutdown is `fs_shutdown`, and the cache is not flushed
  (23.6.3: there is nothing to flush).
- **DESIGN-NOTES-2026-11-14:** `<stem>.sha` is gone (owner, 2026-11-19); the Updater is the master tile's peripheral,
  with registers and one run at a time, not a queue of jobs on C&C 2 (section 7 there); the Updater makes the roots.
- **DECISIONS-v1.3 sections 20 and 22:** the per-proof arena and its bitmap merge give way to the walk (4.4); the
  file system does not use the cache for its own files (4.5).
