# FS-API: the session's notes

*2026-11-19 evening to 2026-11-20. (owner) marks a ruling; † a proposal not ruled on. The specification is
`v1.40_wip/planning/FS-API.md`; the emulation `v1.40_wip/fs/`.*

## 1. How the session went, and where it left the brief

The brief (`PROMPT.md`) asked for an outline first, then the specification and its header, with no implementation.
The owner changed three things in the discussion:

- **the order:** the flows first, at a high level, then the specification in the order: the hardware and the flows,
  the data structures, the calls (owner, 2026-11-20);
- **a C++ implementation of the whole file system in software,** to find what the API needs, kept as the base for
  step 3's emulation (owner, 2026-11-19 and 2026-11-20). So this session wrote step 3's first version;
- **the structures it designed are used by the emulation byte for byte** (owner, 2026-11-20: "I think that's key").

## 2. The inventory of the master tile's firmware (`v1.37/fw/cc/master.c`)

What each function does today, and what becomes of it with the file system.

| Function | Today | With the file system |
| --- | --- | --- |
| `map_format` | zeroes the page map in the scratchpad, every page free but page 0 | **gone from the master tile:** the memory tile formats at a hard reset (FS-API 3.1) |
| `map_take` | the next free page, by a cursor that only moves up | **gone:** behind `fs_reserve_pages` and `fs_acquire_pages`, on the memory tile |
| `map_flush` | the map's cached row back to the scratchpad | **gone**, with the map |
| `new_root` | a root: `map_take` | **gone:** `cow_create` and `cow_copy` give the root to be, from the reservation |
| `feed_pages`, `work` | the free page queue fed under its credits | **stay** (owner): the pages now come from `fs_acquire_pages(r, buf, k ≤ 128)` |
| `depth_of` | a file's depth from its length in pages | **into `cow_create`**: the depth is fixed by the length (29.4) |
| `run_config`, `run_start`, `run_wait`, `update_frame` | the Updater's runs | **stay**: the Updater is the master tile's. The roots come from `cow_create`, `cow_copy` and `cow_root`; at a run's done, `cow_commit`, then `cow_freeze` |
| `skip_string` | passes over a frame's strings: v1.37 keeps no names | **keeps them:** the program's stem, the working directory, a base table's path are the paths of the calls |
| `frame`, by type | 1 and 2: the program image; 3: the proof's header; 4 and 5: a table; 6: the data image; 9: three runs | 1, 2, 4, 5 with a path: **storing a program** (FS-API 3.2); 3: `fs_mkdir(wd)` and `fs_reserve_pages`; 5, 6, 9: `cow_copy`, the runs, `cow_commit`, `cow_freeze` (3.3) |
| `hand_over` | record s's word 66 from the roots it kept | **stays**; the roots and depths from `cow_root` |
| `finish`, `master_trap`, `bad_frame` | the end, and the error path: report and park | **stay**; on a compute tile's hardware error the handler first calls `fs_shutdown` (3.7). `MR_ROOM` for a full map becomes `FS_ENOSPC` from `fs_reserve_pages` |
| `answer_result`, `drain_requests`, `link_read`, `forward`, `poll`, `idle`, `wait_counter`, `barrier`, `stream`, `image_streams`, `transfer`, `boot`, `drop_now`, `skip`, `words_ready`, `copy`, `parse_*`, `next32`, `reply` | requests, events, Ingress, the boot, the barriers, the reply | **stay;** not the file system's work |
| `main` | `boot`, `map_format`, the frame loop | `map_format` gone |

Its state: `map_row`, `map_at`, `map_cursor`, `map_dirty` go; the roots it keeps (`root_program`, `root_base`, ...)
become paths, and the base table is named, not "the last one established" (29.8, item 187).

## 3. The questions, and how each ended

In the order they were settled.

| # | Question | How it ended |
| --- | --- | --- |
| 1 | The calls for copy-on-write | `fs_acquire_pages`, `fs_release_pages`: the raw pages everything is built on, for streamed files too (owner, 2026-11-19); `cow_freeze`, `cow_copy` (owner) |
| 2 | Who may take raw pages | the master tile, to drive the Updater, and the file system itself; not the shell (owner, 2026-11-19) |
| 3 | Does `cow_copy` make the root in software | **no:** the Updater is not changed, its state machine being complex (owner, 2026-11-19). So the Updater makes every root: option A † (FS-API 5.3) |
| 4 | The system | the ASIC serves the host's requests; the file system lives while the power does; a hard reset formats; programs are stored ahead of the proofs (owner, 2026-11-19) |
| 5 | How a program is stored | as COW files built by the Updater † (owner: "I'm guessing ... it doesn't have to work that way") |
| 6 | Logs, and failures | the compute tiles write logs in the working directory; kept on a failure for the shell (owner, 2026-11-19). On a compute tile's hardware error the master tile closes out the file system (owner); errors in the file system's own path are not covered (owner) |
| 7 | The run logs | three a tile (code, read, write), streamed files, written by the tracer tile with `fs_open`, `fs_write`, `fs_close`, three open at a time; 24 to 48 MB in about 10 ms (owner, 2026-11-19); a compute tile has at least four files open (owner) |
| 8 | Where the file system runs | the memory tile, with the RW peripheral (owner, 2026-11-19) |
| 9 | Pages in batches | `fs_reserve_pages(n)` once per request, failing with no room; `fs_acquire_pages(r, k ≤ 128)` as the credits come (owner, 2026-11-19) |
| 10 | Format or mount | a hard reset formats, a soft reset mounts; the reset's cause from the hardware (owner, 2026-11-19) |
| 11 | The test file system | C++ (owner), the base for step 3 (owner, 2026-11-19) |
| 12 | `<stem>.sha` | gone (owner, 2026-11-19) |
| 13 | Who writes a COW file | built only by the Updater; the Tracer changes its copy's contents and adds no pages (owner, 2026-11-19) |
| 14 | Removing a COW file | the walk of its owned entries; data pages never read, only freed (owner, 2026-11-19); the index pages read through the RW unit † |
| 15 | When a page is discarded | when it is handed out again (owner, 2026-11-19); only if it may be cached †; at `fs_reserve_pages`, those pages first † (the emulation's finding 1) |
| 16 | The discard's cost | about 35 cycles of the cache's walker (`cache_ctl.cpp`). Background work: the Tracer's hits are served between its lines; misses and the look-up ports may wait (owner, 2026-11-19). A change to the cache, for step 4 |
| 17 | Streamed files and the cache | never walked through it; their index pages written directly †; a writer holds a data page and an index line a level † |
| 18 | The fd table | 512 entries of 16 bytes (owner, 2026-11-20); a COW file's length in pages (owner) |
| 19 | Open for reading and writing at once | no: one writer, or readers (owner, 2026-11-20) |
| 20 | A root not yet made | the entry's root is 0 until `cow_commit`, the page waiting in bytes 124–127 (owner, 2026-11-20) |
| 21 | `fs_list`'s order | slot order; the shell sorts (owner, 2026-11-20) |
| 22 | Writing without copying | the caller resends its last partial 128-bit word with each write, so every word lands whole; a write is a grant: an address in a scratchpad page buffer, the bytes allowed, a counter (owner, 2026-11-20). `FS-DATA-PATH.md` |
| 23 | Grants in flight | four, on four of the memory tile's seven counters (owner, 2026-11-20) |
| 24 | The RW unit | the memory tile's peripheral, its data input on the router's +8 link; whole pages of 2 or 4 KiB; data flows in while full pages drain out (owner, 2026-11-20) |
| 25 | Where the write path is written | not in FS-API.md, which it does not change: `FS-DATA-PATH.md` and FOR-THE-HARDWARE.md (owner, 2026-11-20) |
| 26 | The read path | `fs_read_page` straight from the RW unit to the caller; `fs_write_page`, and `fs_read` and `fs_write` of less than a page, through the memory tile's scratchpad (owner, 2026-11-20) |
| 27 | Working directories | one per tile, set by the tile with `fs_chdir`, the root at a reset; kept by the memory tile beside the fd table; `.` and `..` in any path, a directory's parent in its entry's source field (owner, 2026-11-20) |
| 28 | A cache in the memory tile | 2 KiB buffers in its SRAM, so that LPDDR is not read all over the place; replaced by CLOCK (owner, 2026-11-20). `FS-DATA-PATH.md` 6 |
| 29 | The RW unit's transfers | only 2 KiB and 4 KiB; a writer builds its bytes up to 2 KiB (owner, 2026-11-20) |
| 30 | The memory tile's memory | 1 MiB of SRAM and 32 KB of local memory (owner, 2026-11-20); the budget in `FS-DATA-PATH.md` 8 |
| 31 | The page maps | on the tracer tile, which also discards a page from the cache before handing it out (owner, 2026-11-20) |
| 32 | Who reserves pages | two pools only, the Updater's and the file system's; no reservation id (owner, 2026-11-20) |
| 33 | Handing out pages fast | a prepared batch of 256 pages per pool, an acquire of 256 or fewer a copy, then a refill (owner, 2026-11-20) |
| 34 | The buffers | 384 of 2 KiB, 768 KiB of the SRAM (owner, 2026-11-20) |
| 35 | An fd names its buffer | in word 0's [31:22]; a reader's position's low 11 bits are its offset in the buffer (owner, 2026-11-20); the mode and flags move to word 2's top bits |
| 36 | The core's memory | 32 KB of program memory and 32 KB of data memory; the SRAM is slow for the core, so the fd table, CLOCK's tables and the 66 working directories are in data memory (owner, 2026-11-20) |
| 37 | Long paths | a 1 KiB landing slot for each of 66 tiles, and one call that carries a path in flight a tile; no grant for a path (owner, 2026-11-20) |
| 38 | Requests in parallel | up to four, reads and writes only; every other call one at a time (owner, 2026-11-20) |
| 39 | printf on the tile | not buffered there: a crash would lose it; printf logs are for debugging (owner, 2026-11-20) |
| 40 | Look-ups | 16-bit hashes of each cached directory half's names in data memory (owner, 2026-11-20) |
| 41 | The memory tile's pages | a stash of 256, refilled from the tracer tile when empty (owner, 2026-11-20) |
| 42 | Removing many files | `fs_remove_directory`: a directory and its files, no subdirectory, nothing open (owner, 2026-11-20); one `logs` directory, not one a tile |
| 43 | COW files in use | `fs_open(FS_ROOT)`, one owner: the master tile for a run, then the tracer tile for the trace; `cow_root(fd)` (owner, 2026-11-20) |
| 44 | The cache's policy | a half loaded for `fs_read` cold; clean buffers before dirty; a call's changed halves written once at its end. No read-ahead, and no whole-page fill on a miss (owner, 2026-11-20) |

**Still open,** with what depends on each: FS-API.md section 8, and these the emulation added:

- **a call that removes a tree** (README finding 7): the host's cleanup and the shell's `rm -r`;
- **the sparse flag of a streamed file** written by `fs_write_page` with holes: `ls -l`'s `s`;
- **the writers' buffer pool:** its size (5 bits of an fd entry name 32 slots) and what a writer meets when it is full
  (`FS_EBUSY` in the emulation); 192 run logs at three at a time need three;
- **a directory never shrinks:** its pages come back only when it is removed.

## 4. The proof that nothing is missing

**The frame loop and the hand-over, as calls.** FS-API.md 3.3 is the outline; `v1.40_wip/fs/tests/out/03-proof-succeeds.log`
is the exact sequence, run: `fs_mkdir`, `fs_reserve_pages`, then for each image `cow_root` of its source, `cow_copy`,
`fs_acquire_pages` before its run, `cow_commit` and `cow_freeze` at its done; `cow_root` of the Tracer's copy and of
the table for the hand-over; `cow_freeze` of the final image at the third run's done; `fs_unreserve`. Frames 7 and 8
make no call: their records and advice go to the master tile's scratchpad as today. **Nothing in `master.c`'s file
work is left without a call** (section 2).

**The shell's commands** (`fs_wip/planning/SHELL-SPEC.md` section 9) against the calls; `06-shell.log` runs most of them.

| Command | Calls |
| --- | --- |
| `help`, `pwd`, `echo` | none |
| `ls`, `ls -l` | `fs_list`; `fs_stat` for a path that is a file |
| `cd` | `fs_stat` |
| `cat`, `more`, `head`, `grep`, `wc` | `fs_open(FS_READ)`, `fs_read`, `fs_close` |
| `tail` | `fs_stat` for the length, `fs_open`, `fs_read_page` backward, `fs_close` |
| `hexdump` | `fs_open`, `fs_read_page` at `-s`'s page, `fs_read`, `fs_close` |
| `stat` | `fs_stat` |
| `df` | `fs_df` |
| `mkdir` | `fs_mkdir` |
| `rm`, `rmdir` | `fs_stat` for the type, `fs_remove` |
| `>`, `>>` | `fs_open(FS_APPEND \| FS_TRUNC)` or `fs_open(FS_APPEND)`, `fs_write`, `fs_close` |

No command lacks a call. What the shell meets that its spec does not say: an image can be read (a frozen COW file),
the Tracer's copy cannot (`FS_EBUSY`); `rm` of a file with copies is `FS_EBUSY`; listings come in slot order.

## 5. What in the older sources is overtaken

FS-API.md section 9, and:

- **`fs_wip/README.md`:** the shutdown's flush of the shared cache's dirty lines is not needed (23.6.3: nothing to
  flush; the Tracer's dirty lines are thrown away); the recovery allocator is the mount's walk (DESIGN-NOTES section 2
  had decided it); "per-proof pages taken all at once" is `fs_reserve_pages`.
- **`SHELL-SPEC.md`:** "directory order is creation order" is the dummy's; the real order is slot order, and the
  shell sorts (owner, 2026-11-20). Its fourth addition, `fs_list`'s cursor, is in the API; `FS_TRUNC`, `fs_stat` and
  `fs_df` are too, not yet ruled (†).
- **DESIGN-NOTES-2026-11-14:** the program's id file is gone (owner); the job queue on C&C 2 is v1.37's Updater on
  the master tile; a superblock line 1 holding the root's entry is used as proposed.
- **DECISIONS-v1.3 section 20:** the open-file record of 64 bytes is the fd entry of 16 bytes (owner, 2026-11-20); the
  pools of 128 file buffers and 32 directory buffers are a writer's buffer each, and none for directories, which are
  read and written a line at a time.
