# FS-API: the report

*The FS-API session, 2026-11-19 to 2026-11-20, for the design session. All of it is on `main` (owner, 2026-11-20:
`fs-api` and `shell` fast-forwarded, then committed to directly).*

## What was done

| File | What |
| --- | --- |
| `v1.40_wip/planning/FS-API.md` | **the specification:** the system, the flows, the structures (page maps, directory entry, superblock, fd table, working directories), every call, its callers and its errors |
| `v1.40_wip/planning/FS-DATA-PATH.md` | **how the tiles carry the calls out:** the write grant, the RW unit, the cache, the page service, and the memory budgets |
| `v1.40_wip/fs/fs.h` | the header in C, freestanding C and C++ |
| `v1.40_wip/fs/fs_emu.*`, `stand_ins.*` | **the file system in software, in C++** (owner: the base for step 3), on the spec's structures byte for byte; stand-ins for the Updater and the Tracer |
| `v1.40_wip/fs/tests/` | nine workflows and their call logs (`tests/out/`); `make test` passes all nine |
| `v1.40_wip/fs/README.md` | what the emulation found, eleven findings |
| `v1.40_wip/shell/` | **the shell** (step 2), by the shell session on this emulation; `rm -r` added here. 19 tests pass |
| `sessions/FS-API/NOTES.md` | the inventory of `master.c`; **45 questions and how each ended**; the frame loop and the shell's commands against the calls |
| `sessions/FS-API/FOR-THE-HARDWARE.md` | **the hardware, for step 4:** the RW unit, the mesh, the cache, the memories, what is open |
| `sessions/SHELL/` | the shell session's brief, notes and report |

**The design in a paragraph.** The memory tile runs the file system: LPDDR holds the superblock in page 0 and every
file as a tree (23.5); its 32 KB of data memory holds the fd table, the working directories and the cache's tables;
its 1 MiB of SRAM holds 384 buffers of 2 KiB, a cache under CLOCK and the writers' halves. A write is a grant: the
caller sends whole 128-bit words straight into a buffer, so the memory tile never touches file data, and the RW unit
moves only 2 and 4 KiB. The tracer tile runs the page service: the page maps, a pool for each tile that asks, prepared
batches of 256, and every discard, marked by the acquire (`fs_acquire_cow_pages` for the Updater and the Tracer).
Streamed files never pass through the cache. COW files are made by the master tile, built by the Updater, committed and
frozen at each run's done, and held with `FS_ROOT` by the one tile driving hardware on them. Removing a file is a walk
of its owned entries; `fs_remove_directory` removes a directory of files in one call.

## What changed after the first report (2026-11-20)

The write grant and the resent tail; the RW unit on the router's +8 link, 2 and 4 KiB only; the cache of 2 KiB
buffers by CLOCK (`fs_read`'s halves cold, clean before dirty, a call's changes written once); the memory tile's 1 MiB
of SRAM and 32 + 32 KB of local memory, the 4 MiB of the first draft being wrong; the page service on the tracer tile,
with no reservation id; working directories and relative paths; `cow_commit` and the root 0 until it; `FS_ROOT` and
`cow_root(fd)`; `fs_remove_directory`; reads and writes served four at a time, the rest one at a time; the names'
hashes; the out-of-space rule. NOTES.md rulings 22 to 45.

## What is open

FS-API.md section 8, FS-DATA-PATH.md section 9, FOR-THE-HARDWARE.md section 8. The ones that shape the most:

1. **How `fs_read_page` names the caller's buffer and counter,** and how the RW unit addresses its output to another
   tile: the one part of the data path not designed.
2. **The tracer tile's SRAM:** whether the page service's 258 KiB fits beside the Tracer's own use.
3. **Whether a proof's reservation covers its run logs' pages,** which the host cannot count (today: no; the memory
   tile's pool takes them as needed).
4. **The shell's four additions** (`FS_TRUNC`, `fs_stat`, `fs_df`, `fs_list`'s cursor): built and used, not ruled.
5. **The page service's efficiency:** summary bits, word-at-a-time maps, a high-water mark (proposed, not ruled).
6. **Where the shell's spec lives** once `fs_wip/` goes (owner, 2026-11-20: it will); `SHELL-SPEC.md` has the shell's
   changes since, `cat` from the console and `rm -r`, but not `*` (the shell's NOTES.md).

## What the next sessions need to know

- **The RW unit and the mesh (step 4):** start from FOR-THE-HARDWARE.md. The discard's change to the cache and the RW
  unit's data input on +8 are the owner's rulings.
- **The emulation (step 3):** single-threaded, calls are function calls: it does not model the requests on the wire,
  the four calls in parallel, timing, or the cache beyond which pages may hold lines. Every LPDDR transfer it makes is
  2 or 4 KiB, counted. The invariants (`check()`) are its best test: keep them whole.
- **The device's file system (step 5):** the firmware must be an event loop, never blocking: a path landing, a grant's
  counter, an RW job, a page request to the tracer tile all take time, and up to four reads and writes are in hand at
  once. `fs_emu.cpp` is laid out as the memory tile's would be. The 32 KB of program memory is its budget.
- **The brief changed in the session:** the owner asked for the flows first, then the specification, then an
  implementation in software, and then the hardware's design; so this session wrote step 3's first version and much
  of step 4's starting point (NOTES.md section 1).
