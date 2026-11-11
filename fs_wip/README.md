# fs_wip: v1.4's file-system work, in progress

A **temporary** area (owner, 2026-11-10). Workers build v1.4's file-system pieces here while **v1.35** (the fast simulator,
`sim/`, and nothing else) is finished; once v1.35 is baked, the pieces are integrated into **v1.4** and this directory goes
away.

**v1.4's new functionality** (owner, 2026-11-10): the C (compressed) extension on the controllers; the **recovery shell**
on C&C 1 over the UART; the **big file system** (C&C 2's file system, C&C 3's page manager, copy-on-write images); and
**run mode** (C&C 1's normal-mode firmware around a proof). It is DECISIONS-v1.3's F1, moved here.

## Rules

- **C or C++ only, no Java** (owner: Java is being phased out for the simulator's speed). Firmware is freestanding C for
  the controller (RV32I + Zmmul today; `_c` as a build option for v1.4); host tools (`mkfs`, `fsck`) and test harnesses
  are C or C++.
- Work here builds and tests **on its own**, against dummy integration points (console, file I/O, the memory system),
  without depending on `sim/`'s internals while v1.35 is in flux.
- Nothing outside `fs_wip/` is changed by work here.

## Layout

| path | what | status |
|---|---|---|
| `planning/SHELL-SPEC.md` | the console shell's spec | written, 2026-11-10 |
| `shell/` | the shell, per its spec | to be built (a worker session) |
| `planning/` (more to come) | the file system's design: item 119's opens, the page manager, copy-on-write, recovery mode | in discussion |
| `fs/` | C&C 2's file system, C&C 3's page manager, `mkfs`/`fsck` | later |

## Decided so far (2026-11-10, owner)

- **The unit API** (`fs.h`): `fs_open` (`FS_READ`, `FS_APPEND`, `FS_WRITE`), `fs_read`, `fs_write` (appends),
  `fs_fprintf`, `fs_close`, `fs_mkdir`, `fs_remove`, `fs_list`, `fs_read_page`, `fs_write_page` (4 KB, by page index).
  Copy-on-write, page calls beyond these and arenas are C&C 2's internal business, not the units'.
- **Free pages:** C&C 3 keeps a bit per page of the 4 GB pool (2²⁰ pages, 128 KB) in its SRAM, with 2 × 2 KB cached in
  the controller, a first word with free pages, and a last word from which every page is free.
- **Copy-on-write** as DECISIONS-v1.3 section 20: `freeze`, `cow_open` (a copied root with every owned bit cleared), path
  copying on write (a whole-page write skips the data copy), delete frees what owned bits reach. The Tracer's copy is
  pre-copied from frame 1's write sets, so the Tracer never path-copies.
- **Per-proof pages** are taken all at once at a proof's start (the host's exact counts) and released all at once at its
  end.
- **Shutdown:** C&C 1 keeps `/syslog` open from boot. On a hardware error it sends C&C 2 a shutdown (pending requests
  failed, every file but syslog closed), writes the error to syslog and closes it; that close makes C&C 2 flush syslog
  and the shared cache's dirty lines to LPDDR, ack, and halt until reset. **Recovery mode** then runs the shell on a small
  file system (software tree walks, direct LPDDR reads, no caching).
- The shell's commands, editor and redirection: `planning/SHELL-SPEC.md`.

## Open

- The recovery allocator (for `>`, `>>`, `rm` in recovery mode): the bitmap saved at shutdown, rebuilt at mount, or a
  reserved pool.
- The per-proof pages: one contiguous run, or a short list of extents.
- Item 119's opens: the superblock, the type values, the tag-window protocol, the request layout, the page manager's
  interface.
- The `fs.h` additions in SHELL-SPEC section 14 (`FS_TRUNC`, `fs_stat`, `fs_df`, `fs_list`'s cursor).
