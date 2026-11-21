# The file system's firmware, tile by tile

*The FS-API session, 2026-11-20. (owner) marks a ruling, with its date; a dagger (†) a proposal not ruled on. For
step 5 (the file system on the chip), and for the master tile's and the tracer tile's firmware. It gathers what was
decided about the firmware and points to where each part is specified: `FS-API.md` (the calls, their rules, the
structures), `FS-DATA-PATH.md` (the data path, the cache, the page service, the memory budgets),
`../sessions/FS-API/FOR-THE-HARDWARE.md` (what the hardware must carry), and the emulation in `../fs/`, which does all
of it in software, one call at a time.*

## 1. The tiles and their parts

| Tile | Its firmware's part | Serves | Calls |
| --- | --- | --- | --- |
| **the memory tile** | the file system | every tile's file calls | the tracer tile's page service, for its stash |
| **the tracer tile** | the page service, beside its trace work | the master tile's and the memory tile's page calls | the file system, for the run logs and its `FS_ROOT` holds |
| **the master tile** | the host's requests, the Updater, the hand-over; and the shell | the host | the file system and the page service |
| **a compute tile** | its own logs | | the file system |

**The rule over all of it** (owner, 2026-11-20): work moves from the memory tile to the callers wherever it can, since
the memory tile serves them all.

## 2. The memory tile

### 2.1 An event loop, never blocking

Every call waits on something: a directory half read from LPDDR, a grant's packets, the RW unit's job, the DMA, the
tracer tile's answer. **So the firmware is an event loop and every call is a small state machine,** resumed by the
event it waits for; nothing waits in place. v1.37's master tile firmware is written this way.

**Its events:**

| Event | What it resumes |
| --- | --- |
| a request in the receive queue (`request_pending`, 11.1) | a new call |
| `counter_done` on one of the four grant counters | the write whose packets have landed (2.4) |
| the RW unit's done, for a job | a call waiting for a half or page read or written |
| the DMA's done | a call sending a half to the RW unit, or words to a caller (`fs_read`) |
| an answer from the tracer tile | the call waiting for the stash's refill |

### 2.2 What it serves at once (owner, 2026-11-20)

- **Up to four reads and writes in parallel:** `fs_read`, `fs_write`, `fs_fprintf`, `fs_read_page`, `fs_write_page`,
  `fs_flush`. A context each: four **request contexts** of about 64 bytes in data memory, holding the call's words, its
  fd, where it is up to and what it waits for.
- **Every other call one at a time,** in the order it arrived. † It runs beside the reads and writes in hand: each
  change to a cached half is made whole between events, and the RW unit's jobs go out in order, so they cannot tear.
  A caller makes one call at a time on an fd.
- **Requests that cannot start yet** † are taken out of the receive queue and kept in a list in data memory, 16 bytes
  each: a tile has at most three outstanding (11.3), so at most 198, about 3 KiB. A read is not then held up behind a
  call that must wait for its turn.

### 2.3 Where its state is (owner, 2026-11-20; FS-DATA-PATH 8)

| Data memory (32 KB) | SRAM (1 MiB) |
| --- | --- |
| the fd table: 512 × 16 B | 384 buffers of 2 KiB: the cache and the writers' halves |
| CLOCK: the hash table, a record for each buffer | the path slots: 1 KiB for each of 66 tiles |
| the working directories: 66 × 4 B | |
| the names' hashes of cached directory halves | |
| the stash: 256 page addresses | |
| the four request contexts; the waiting requests † | |
| the grants, the RW unit's jobs, stack | |

**The core reaches the SRAM slowly** (owner, 2026-11-20): it reads a directory entry there only when a name's hash
matches, an fd never, and file data never.

### 2.4 A write (owner, 2026-11-20; FS-DATA-PATH 3, 4)

```
request "write n bytes to fd"
  fd from the fd table; its buffer from the fd's index (FS-API 4.6), or a buffer from the pool
  a grant counter free?            else wait for one (first come, first served †)
  answer: the address (the buffer + length mod 2,048, rounded down to 16), g bytes (≤ what the half has left),
          the counter, armed for ⌈((length mod 16) + g) / 16⌉ packets
wait counter_done
  length += g
  the half full?  a page from the stash if the half starts a page (2.7)
                  the RW unit's job: write 2 KiB at the page's place; the DMA sends the half over +8
                  wait the RW unit's done
                  for a page's first half: its entry into the writer's index half (a dirty buffer of the writer's,
                  written when its 512 entries are full and at a flush: FS-API 4.6)
                  the buffer back to the pool; the writer's next grant takes another
the call ends; the caller asks again for what is left
```

The caller resends its last partial 128-bit word at the front of each grant, so every word lands whole and the core
neither merges nor shifts. One grant at a time for an fd.

### 2.5 The other calls, as steps

| Call | Steps, and what each waits for |
| --- | --- |
| `fs_open` | the path (2.6); each name looked up by hash in its directory's cached halves (a miss: a 2 KiB read job); the checks of FS-API 5.2; a create adds an entry (a new directory page: from the stash, written as a 4 KiB job); an fd; the answer carries the length, for the caller's tail † |
| `fs_read` | the fd's position; the page from the index (its cached half); the data half, by the fd's buffer if it still holds it, else from the cache, loaded **cold** on a miss; the words covering the bytes asked, by the DMA to the caller's buffer, on the caller's counter; the answer |
| `fs_read_page` | the page from the index; a 4 KiB read job whose output goes to the caller, on its counter (open: how, FOR-THE-HARDWARE 8.1); the answer when it is done |
| `fs_write_page` | two grants of a half each (2.4), at the page given |
| `fs_flush` | the writer's part-filled half, its dirty index half, and the half holding its directory entry: a job each; the answer when all are done |
| `fs_close` | as `fs_flush`; the fd and its buffer freed |
| `fs_mkdir`, `fs_remove`, `fs_chdir`, `fs_stat`, `fs_list`, `fs_df` | the path; the directory's halves; for a removal, the walk (index pages as 4 KiB reads, never a data page) and the freed pages queued for the tracer tile (2.7) |
| `fs_remove_directory` | the checks of FS-API 5.2 over every entry; each file's walk, copies inside first; no entry inside written; the directory's own pages; the parent's entry, once |
| `cow_create`, `cow_copy` | the master tile's root page, from its own acquire (`fs_acquire_cow_pages` gives it), to the entry as its pending root; the entry; for a copy, the source's count |
| `cow_commit`, `cow_freeze` | the entry |
| `fs_open(FS_ROOT)`, `cow_root` | the fd; the entry's root |
| `fs_shutdown` | every writer's halves, index halves and entries written; every fd closed; every later call refused |

**At every call's end,** the halves it changed (directory and index halves, not the writers' own) are written, each
once, before its answer (owner, 2026-11-20). So what a call promises is in LPDDR when it answers.

### 2.6 Paths (owner, 2026-11-20)

A path of 12 bytes or fewer comes in the request itself (words 1–3). A longer one, up to 1,023 bytes, the caller
writes into its tile's 1 KiB slot by `WRITE` packets before the request; a tile has one call that carries a path in
flight at a time, so its slot is never overwritten. A relative path starts at the caller's working directory, from
its table; `..` is a directory entry's parent field, one step.

**Open:** how the memory tile knows a long path has landed before it reads the slot. The request travels on the
request tree (11.3) and the `WRITE` packets on the mesh, and between sending units there is no order (14.4). A counter per slot is too many counters; a grant for every
long path is the round trip the slots were meant to save.

### 2.7 Its pages

The stash: 256 page addresses in data memory, taken one at a time for directories and streamed files. **Empty, one
request to the tracer tile** (`fs_acquire_pages`, 256) brings the next batch (owner, 2026-11-20). The pages it frees
(by a walk) it queues and sends to the tracer tile in batches, by address alone (`fs_release_pages`).

### 2.8 The cache (owner, 2026-11-20; FS-DATA-PATH 6)

CLOCK over the 384 buffers: a hit sets the used bit; a miss sweeps, skipping pinned buffers, passing over dirty ones
(an idle writer's half) until a sweep finds no clean one. A half loaded for `fs_read` comes in cold. A half a call
changes is pinned until it is written at the call's end. A half of a freed page is dropped at once, with its names'
hashes.

### 2.9 At a reset

- **A hard reset formats** (owner, 2026-11-19): the superblock and an empty root directory, page 0; every tile's
  working directory the root; and the tracer tile told to start its maps empty †.
- **A soft reset mounts** (owner, 2026-11-19): the walk of every file from the root, every page it reaches sent to the
  tracer tile, which rebuilds its maps (every page of a COW file "may be cached"); a pending root forgotten.
- **Which reset it was** is the hardware's to say (FOR-THE-HARDWARE 7).

## 3. The tracer tile: the page service

### 3.1 What it serves (owner, 2026-11-20; FS-API 5.1)

| Call | From | Does |
| --- | --- | --- |
| `fs_reserve_pages(n)` | the master tile | holds n pages in the caller's pool for a request |
| `fs_unreserve()` | the master tile | ends it |
| `fs_acquire_cow_pages(pages, k)` | the master tile | k ≤ 256 from the caller's pool, each marked "may be cached" |
| `fs_acquire_pages(pages, k)` | the memory tile | k ≤ 256 from the caller's pool, not marked |
| `fs_release_pages(pages, k)` | the memory tile, the master tile | pages back, by address |

**A pool for each tile that asks,** with its prepared batch of 256 and its reservation count. An acquire of 256 or
fewer is a copy out of the batch, sent to the caller's buffer by `WRITE` packets on the caller's counter, and an
answer; **the refill comes after, in the background.**

### 3.2 The refill, beside the trace

- **The tracer tile also runs the trace:** it drives the Tracer and writes the run logs. The page service is events in
  the same loop: a call is answered at once from the batch, and the refill runs between the trace's own events.
- **The master tile's pool prefers pages that need a discard,** the memory tile's prefers clean ones †: the master
  tile's refills come mostly before tracing, the memory tile's during it (the run logs' pages).
- **A page that needs a discard** is discarded through the tracer tile's look-up port before it enters a batch, and
  enters it only at the discard's reply (23.6.8). The discard is background work in the cache (owner, 2026-11-19): it
  does not hold up the Tracer's hits.
- **Its maps are in its SRAM,** which its core reaches slowly too. *Proposed* †: a summary bit per 128-page word in
  data memory (2 KiB), whole words of the maps read and written at once, and a high-water mark above which every page
  is free and clean (FOR-THE-HARDWARE 6).

### 3.3 Freeing and the mount

Freed pages arrive in batches from the memory tile (its walks) and the master tile: each page's "taken" bit cleared,
its "may be cached" bit left as the acquire set it. At a mount, the memory tile's walk sends every page in use, and
the maps are built from them.

**Open** †: where a batch of page addresses lands on the tracer tile (a slot for each sending tile, 1 KiB), and how the
tracer tile learns of a format.

## 4. The master tile

### 4.1 What changes from v1.37's `master.c` (NOTES.md section 2)

- **Gone:** its page map (`map_format`, `map_take`, `map_flush`) and `new_root`: the page service and the file system
  have them. `depth_of` is in `cow_create`.
- **Kept:** `feed_pages`, which now takes pages by `fs_acquire_cow_pages`, at most 128 a credit; the Updater's runs;
  the frame parsing, which now keeps the names (`skip_string` no longer skips them).
- **Its frames become calls:** storing a program (FS-API 3.2), a proof (3.3), the cleanup (3.5, 3.9).

### 4.2 A proof, as the master tile makes it (FS-API 3.3)

```
fs_mkdir(wd); fs_mkdir(wd/logs); fs_reserve_pages(the host's counts + a page per root)
for each image (the map, cycle-0, start; then final and the Tracer's copy in one run):
    the source's root: fs_open(source, FS_ROOT), cow_root(fd), fs_close
    cow_copy(source, image) -> its root to be
    fs_open(image, FS_ROOT)                         // held for the run (owner, 2026-11-20)
    the Updater's run, its pages by feed_pages
    cow_commit(image); cow_freeze(image) (all but the Tracer's copy); fs_close
the hand-over: record s names wd (owner, 2026-11-20); the tracer tile holds wd/tracer and wd/map itself
the third run on final, while the trace goes on; cow_freeze(final); fs_close
the reply; fs_unreserve()
success: fs_remove(wd/tracer); fs_remove_directory(wd/logs), once every tile has left it
```

**On a compute tile's hardware error,** the NMI handler calls `fs_shutdown` and parks (FS-API 3.7).

### 4.3 The shell

It runs on the master tile, and shares its one working directory with the firmware; so it keeps its own as a string
and passes the file system absolute paths. It never makes the page calls or the COW calls (FS-API 6).

## 5. A caller's library (a compute tile, the tracer tile, the shell)

- **Per fd, its tail:** the file's last *length* mod 16 bytes, resent at the front of each grant. The caller is the
  file's one writer, so it keeps the length from `fs_open`'s answer †.
- **One grant at a time on an fd;** a write of more than a half's room is several grants.
- **No buffering of its own** (owner, 2026-11-20): a `printf` goes to the memory tile at once, so that a crash on the
  tile does not lose it. `fs_fprintf` formats on the tile's stack, then writes.
- **A path:** 12 bytes or fewer in the request; longer, into the tile's slot first, one such call in flight (2.6).
- **Its working directory:** `fs_chdir` once, to the proof's `logs`; then short names (FS-API 4.7).
- **The tracer tile, too,** for the trace: `fs_open(FS_ROOT)` on the Tracer's copy and the map, `cow_root`, and
  `fs_close` at the trace's end (owner, 2026-11-20).

## 6. Open

1. How the memory tile knows a long path has landed in its slot (2.6).
2. Whether a serialized call runs beside the reads and writes in hand (2.2).
3. Taking requests out of the receive queue into a waiting list (2.2).
4. How `fs_read_page`'s output is addressed to the caller (FOR-THE-HARDWARE 8.1).
5. Where freed pages land on the tracer tile, and how it learns of a format (3.3).
6. The page service's summary bits, word-at-a-time maps and high-water mark (3.2).
