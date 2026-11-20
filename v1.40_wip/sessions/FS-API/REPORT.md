# FS-API: the report

*The FS-API session, 2026-11-19 to 2026-11-20. Branch `fs-api`, not merged.*

## What was done

| File | What |
| --- | --- |
| `v1.40_wip/planning/FS-API.md` | the specification: the system and its hardware, the flows, the data structures (page maps, directory entry, superblock, fd table, a writer's buffer), the calls and their callers, what it asks of the hardware, the open points, what it overtakes |
| `v1.40_wip/fs/fs.h` | the header in C: freestanding C (`make header-check`) and C++ |
| `v1.40_wip/fs/fs_emu.*`, `stand_ins.*` | **the file system entirely in software, in C++** (owner, 2026-11-19: the base for step 3), on FS-API.md's structures byte for byte; stand-ins for the Updater and the Tracer |
| `v1.40_wip/fs/tests/` | eight workflows of FS-API.md's flows, and their call logs in `tests/out/`: every call, its caller, its arguments, its result. `make test` passes all eight |
| `v1.40_wip/fs/README.md` | what the emulation found |
| `sessions/FS-API/NOTES.md` | the inventory of `master.c`; the 21 questions and how each ended; the frame loop and the shell's commands against the calls; what is overtaken |
| `sessions/FS-API/FOR-THE-HARDWARE.md` | what the API asks of the RW unit, the mesh and the cache |

**The design in a paragraph.** The memory tile runs the file system. LPDDR holds the superblock in page 0 and every
file as a tree (23.5); the memory tile's scratchpad holds a "taken" map and a "may be cached" map, the fd table and the
writers' buffers. Streamed files (logs, the shell's files) are written by the file system alone, through the RW unit,
and never touch the cache. COW files (the images, the tables) are made by the master tile, built by the Updater, which
also makes their roots, and committed and frozen at each run's done. Removing a file is a walk of its owned entries,
which reads index pages only. A freed page is discarded from the cache only when it is handed out again, only if it was
in a COW file, and at `fs_reserve_pages`, so that nothing is discarded while the Tracer runs.

## What is open

FS-API.md section 8 (11 points) and NOTES.md section 3's four more. The ones that shape the most:

1. **`cow_create` and `cow_copy` leave the root to the Updater** (A, as built) or `cow_copy` makes it with a run of no
   words (B, untested).
2. **The size of a call's data and how many calls are outstanding,** against the tracer tile's 2.4 to 4.8 bytes a
   cycle: it sizes the request protocol and the RW unit.
3. **Whether a proof's reservation covers its run logs' pages,** which the host cannot count.
4. **The shell's four additions** (`FS_TRUNC`, `fs_stat`, `fs_df`, `fs_list`'s cursor): in the API and the emulation,
   not ruled.

## What the next sessions need to know

- **The shell (step 2):** link it to `v1.40_wip/fs/fs_emu.o` in place of SHELL-SPEC's dummy: the same `fs.h`.
  `fs_emu.h`'s `set_caller(Caller::SHELL)` makes the emulation stop on a call the shell may not make. Listings come in
  slot order: sort them, in passes if a directory is large (a proof's `logs` holds about 450 entries).
- **The emulation (step 3):** it is single-threaded and calls are function calls. What it does not model: the requests
  on the wire, timing, the cache beyond which pages may hold lines, the LRU code, the free page queue's credits. The
  invariants (`check()`) are its best test: keep them whole.
- **The RW unit (step 4):** start from FOR-THE-HARDWARE.md; the discard's change to the cache is the owner's ruling.
- **The device's file system (step 5):** `fs_emu.cpp` is laid out as the memory tile's would be (the scratchpad's
  structures, LPDDR's), and every LPDDR access is a whole page or line, counted.
- **The brief changed in the session:** the owner asked for the flows first, then the specification, then an
  implementation in software; so this session wrote step 3's first version (NOTES.md section 1).
