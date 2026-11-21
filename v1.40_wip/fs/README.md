# v1.40_wip/fs: the file system's API, and its emulation in software

The first version of step 3's C++ emulation (owner, 2026-11-20: "keep it as the base for step 3"), written against
`../planning/FS-API.md` to find what the API is missing. It uses FS-API.md's structures byte for byte: the superblock
in page 0, directory entries of 128 bytes, the two page maps of 128 pages to a 128-bit word, the fd table of 512
entries of 16 bytes, a writer's buffer of a data page and an index line per level.

| file | what |
| --- | --- |
| `fs.h` | the API in C, usable from freestanding C (`make header-check`) and from C++ |
| `fs_emu.h`, `fs_emu.cpp` | the emulated file system: one memory tile's, over an emulated LPDDR; its hooks (power, the caller, the call log, the invariants) |
| `stand_ins.h`, `stand_ins.cpp` | the Updater (raw update words, `root` 0/1/2, `tracer_copy`) and the Tracer, as far as the file system sees them; both mark what the cache would hold |
| `tests/workflows.cpp` | eight workflows, FS-API.md section 3's flows |
| `tests/out/*.log` | **their call logs:** every call, its caller, arguments and result, in order |

`make test` builds and runs them (about a second). A breach of a caller's rule (a call the caller may not make, a page
handed out with lines in the cache) stops the emulation and names it; after each workflow the invariants are checked
whole: every page in use has one owner, the taken map is exactly the walk's pages and the reservations', a free page
with lines in the cache is in the "may be cached" map, every count is right.

**Not modelled:** the requests on the wire (calls are function calls), timing, the cache beyond which pages may have
lines in it, the LRU code, the free page queue's credits (the pages are fed before each run).

## What the emulation found (2026-11-20), for FS-API.md

1. **A reservation takes the "may be cached" pages first.** Taking clean pages first, as first written, left only
   pages needing a discard for the run logs, which the file system takes while the Tracer runs: workflow 8 made 35
   discards during tracing. Taking them first discards them while the Tracer is idle and leaves the clean pages for
   the file system's own (4.2). With it, workflow 8 makes its 508 discards at `fs_reserve_pages` and none while tracing.
2. **A frozen COW file may be read directly; an unfrozen one may not.** Only the Tracer's copy has lines newer than
   LPDDR (23.6.8), and it is the one COW file never frozen. So `fs_open` with `FS_READ` refuses an unfrozen COW file
   (`FS_EBUSY`) and serves a frozen one: the shell can `hexdump` an image. FS-API.md 2.4's last row says more than
   is needed.
3. **No reader while a file is written.** A writer's newest index lines are in its buffer (4.6), so a reader would
   miss its newest pages: `fs_open` refuses one (`FS_EBUSY`), and `FS_READ` does not combine with `FS_APPEND` or
   `FS_WRITE` (`FS_EINVAL`). One writer and no readers, then any readers.
4. **An unbuilt root is invisible to the file system.** Between `cow_create` or `cow_copy` and the run that makes the
   root, the root page holds whatever it held; a walk of it (`fs_remove`, `fs_stat`, a mount) would read garbage.
   Today it is the master tile's rule alone. A "built" flag set by `cow_freeze` would not do: the Tracer's copy is
   never frozen. **Closed** (owner, 2026-11-20): the entry's root is 0 until `cow_commit`, the page held in the
   entry's bytes 124–127 meanwhile (FS-API.md 4.3, 5.3); workflows 2 and 5 exercise it, a soft reset in between
   included.
5. **The resets work as designed.** After `fs_shutdown` and a soft reset every byte written is there (workflow 5).
   With no shutdown, the bytes up to the last `fs_flush` are, and the pages of the rest are free again after the mount,
   which rebuilds the maps by the walk; so is a reservation that was open.
6. **The host's counts and the roots:** a proof's reservation is the counts of 29.7 plus five roots; every run ends
   with `PAGE_COUNT` 0 and the free page queue empty, and `fs_unreserve` gives back nothing.
7. **Removing a working directory is many calls,** a copy before its source (`fs_remove` of a source with copies is
   `FS_EBUSY`). A call that removes a tree in the right order would serve the host's cleanup and the shell's `rm -r`.
8. **The index lines cost what 4.6 says:** workflow 3 writes a 5 MB run log, 1,281 data pages, among its other files;
   the whole workflow, every directory entry and every other file's index included, makes 162 line writes.
9. **Out of space, a write lost what it had stored** (found by the shell session, 2026-11-20): `fs_flush` stopped at
   the page that found no room, so the index lines naming the pages before it never reached LPDDR; they stayed taken
   and in no file. **Fixed:** the lines and the entry go out all the same, the length cut back to the pages stored
   (FS-API.md 5.2, owner, 2026-11-20). Workflow 9 fills the file system and checks it.
10. **32 writers were too few** (the shell session): 64 tiles each with a log open need 67. A writer's buffer index
   now lives in its fd entry's position word, and the pool is 128 (FS-API.md 4.6, owner, 2026-11-20); workflow 9 opens
   128 at once.
11. **The cache and the 2 KiB transfers** (owner, 2026-11-20): every LPDDR transfer is now a 2 KiB half or a 4 KiB
   page, a directory or index change made in a cached half and written through as the half. The cache (400 buffers
   here, CLOCK) hits about 99 % of the time: a whole proof (workflow 3) makes 1,625 hits and 16 misses. The page
   service's two pools keep prepared batches (256 pages; a sixteenth of LPDDR here when that is less), and workflow 8
   still makes all its 508 discards outside tracing.
