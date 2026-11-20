# SHELL: the session's notes

*Started 2026-11-20. (owner) marks a ruling, with its date; † a proposal not ruled on. The brief is `PROMPT.md`; the
shell's specification `fs_wip/planning/SHELL-SPEC.md` (section numbers below are its own unless marked); the API
`v1.40_wip/fs/fs.h` and `v1.40_wip/planning/FS-API.md`.*

## 1. The outline (go-ahead: owner, 2026-11-20), as built

### 1.1 Where the code goes (owner, 2026-11-20)

`v1.40_wip/shell/`, with SHELL-SPEC section 2's layout, less the dummy:

| file | contents |
| --- | --- |
| `console.h` | section 4, as it stands |
| `shell.c`, `cmds.c`, `fmt.c`, `str.c`, `div.c`, `grep.c` | as section 2; they include `../fs/fs.h` itself, not a copy |
| `host/console_host.c` | section 4's host console |
| `host/main.cpp`, `host/seed.cpp` | the host entry (C++): powers the emulation on, seeds it (1.2), `set_caller(SHELL)`, runs `shell_main`; then every streamed file is opened to append and closed (a file the shell left open would be `FS_EBUSY`), and `fsemu::check()` |
| `rv32/crt0.S`, `rv32/link.ld`, `rv32/stubs.c` | the link-only RV32 build; `stubs.c` stubs `fs.h`'s streamed-file calls only |
| `Makefile` | `make host`, `make test`, `make rv32`, `make size`; `CC=gcc` (with `g++`) or `CC=clang` (with `clang++`). It builds `../fs/fs_emu.cpp` and `stand_ins.cpp` into its own build directory and changes nothing in `../fs/` |
| `tests/` | `NN-name.in`, `.seed`, `.out` (section 12), run by `tests/run.sh`; each run also writes the emulation's call log to `build/<CC>/out/NN-name.log`, for reading, not diffed. `divtest.c` (section 12's division tests) and `retest.c` (the matcher against Pike's recursive one) |

No `host/fs_mem.*`. The shell's C is the same on both builds; only `host/` is C++.

### 1.2 How the tests seed the file system (owner, 2026-11-20)

Keep section 12's `.seed` file, each line mapped onto API calls by `host/seed.cpp`:

| line | calls |
| --- | --- |
| `cap <pages>` | `power_on(pages, hard)`, rounded up to a multiple of 128 (default 16,384) |
| `dir <path>` | `fs_mkdir`, as the master tile |
| `file <path>` … `.` | a streamed file: `fs_open(FS_APPEND)`, `fs_write`, `fs_close`, as a compute tile. **No `frozen` or `sparse`:** a streamed file has neither |
| `file <path> nonl` | the same, without the last line's newline. A `file` or `gen` of an existing file appends to it |
| `gen <path> <bytes> lines\|bytes\|char:<c>` | the same, generated: `line N` lines cut at `<bytes>`, bytes 0-255, or one byte repeated |
| `program <stem> <pages> <list>` | a stored program, FS-API 3.2: `<stem>.pages` (the listed pages written) and `<stem>.map`, both frozen; `proof` uses it |
| `cow <path> <pages> <list> [frozen]` | a COW file as `store_program` makes one: `fs_reserve_pages`, `cow_create`, an Updater run (stand-in) writing the listed pages (`-` none) with `words_for`'s pattern, `cow_commit`, `cow_freeze` if `frozen`, `fs_unreserve` |
| `copy <src> <dst> <list> [frozen]` | `cow_copy` and a run with `root` 2 writing the listed pages; so `src` has copies |
| `proof <wd> <stem> <tiles>` | a working directory as `run_proof` leaves it after a failure in software: `map`, `cycle0`, `start`, `final` (frozen), `tracer` (never frozen), `logs/` with each tile's three run logs and its own log; tile `tiles/2` logs the failed check. Every tile writes its logs (`run_proof` stops at the failing one). Each tile's own log is held open through the segment, as `run_proof` holds it: 112 tiles and the tracer tile's three are 115 writers, inside the 128 (finding 8). With 112 tiles, `logs/` has 448 entries |
| `open <path> r\|w` | leaves the file open by a compute tile, for the `FS_EBUSY` tests |
| `reset` | `power_on(…, soft)`: a mount |

The Updater-driving code is `workflows.cpp`'s (static there, and `v1.40_wip/fs/` is not refactored), so `seed.cpp`
copies it: `cow` and `copy` from `store_program` and `run`, `proof` from `run_proof`, each copy marked in `seed.cpp`
with the function and the commit of `workflows.cpp` it came from.

### 1.3 What I take from SHELL-SPEC as it stands

Sections 3 (build constraints, `div.c`, the budget), 4 (console), 6 (line editor), 7 (parsing, redirection), 8 (paths),
9 (commands) except as 1.4 says, 10 (matcher), 11 (formatter), 12 (tests, plus 1.5).

### 1.4 What changes

1. **`fs.h` is `v1.40_wip/fs/fs.h`:** `fs_open`'s flags `u32`; the shell calls only section 5.2's calls (FS-API 6).
2. **`ls` sorts, in byte order of the names** (owner, 2026-11-20), within the static buffers: a **batched selection**. Each pass reads
   the whole directory with `fs_list` and keeps, in the 4 KB page buffer (free during `ls`), the 36 smallest entries
   (name, type, flags, length: 112 bytes) above the last one printed; it prints them, and the next pass starts above
   the last. Names are unique, so "above" is exact. 450 entries are 13 passes, about 5,900 `fs_list` calls; no file is
   read. An entry made or removed between passes is shown or not, never twice.
3. **`ls -l` and `stat` show COW** (owner, 2026-11-20): `ls -l` gets a fourth column, `c` for a COW file (`-fsc 2097152 cycle0`);
   `stat`'s `flags:` line lists `frozen sparse cow`, or `none`.
4. **The errors the shell now meets** (PROMPT "What changed" 3) print with section 5's messages, unchanged:
   `cat` of the Tracer's copy or of a file open for writing is `busy`; `rm` of a file with copies is `busy`; `>` on a
   COW file is `read-only file`.
5. **`echo`'s note into `/syslog`** goes from `help` and the docs: v1.40 has no `/syslog`.
6. **No recovery mode, for now** (owner, 2026-11-20): `shell_main(void)`, one prompt `<cwd>> `; section 6's
   `[recovery]` prompt and section 12's test of it go. Section 14's item 3 is overtaken (the mount rebuilds the maps,
   FS-API 3.1) and is not mine.

### 1.5 Tests beyond section 12

- A proof's working directory (`proof` seed): `ls` and `ls -l` of it and of `logs/` (about 450 entries, sorted);
  `hexdump` of a frozen image; `tail` of a run log; `stat` of an image with copies.
- The rules: `cat` of the Tracer's copy (`busy`); `cat` of a file a tile has open for writing (`busy`); `>` and `>>`
  to a file a tile has open for reading (`busy`); `rm` of an image with copies (`busy`); `>` on a COW file
  (`read-only file`); `rm` of a file open (`busy`).
- After `reset`: the same directory, read again.
- Every test ends with `fsemu::check()`, and the shell runs as `Caller::SHELL`, so a forbidden call stops the test.

### 1.6 The order I will build in

Each step committed and pushed.

1. `Makefile`, `div.c`, `str.c`, `fmt.c`; the division tests on the host, both compilers.
2. The host console, the test runner and `seed.cpp`; the line editor, the parser, paths; `help`, `pwd`, `echo`, `cd`.
3. The readers: `ls` (with the sort), `cat`, `more`, `head`, `tail`, `wc`, `hexdump`, `stat`, `df`.
4. `grep`.
5. The writers: redirection, `mkdir`, `rmdir`, `rm`.
6. The tests of 1.5.
7. The RV32 build and `make size`, both compilers, with and without `C=1` (clang 18 with `ld.lld`; GCC 13.2,
   Ubuntu's `gcc-riscv64-unknown-elf`, installed by this session).
8. The stretch of section 6 (cursor, history), only if everything passes inside the budget: **not done** (REPORT).

## 2. The questions, and how each ended

Most-shaping first; each with my recommendation.

| # | Question | Recommendation | Ended |
| --- | --- | --- | --- |
| 1 | How the tests seed the file system | 1.2: keep `.seed`, mapped onto the API, with `cow`, `copy`, `proof`, `open`, `reset` | **yes** (owner, 2026-11-20) |
| 2 | Recovery mode, now that a soft reset just mounts | drop it: `shell_main(void)`, one prompt `<cwd>> ` | **dropped, for now** (owner, 2026-11-20) |
| 3 | `ls`'s order and how it sorts | byte order; the batched selection of 1.4.2 | **byte order** (owner, 2026-11-20): names that should sort by number are written zero-padded (`tile02`); the batched selection † |
| 4 | Showing COW in `ls -l` and `stat` | 1.4.3 | **yes** (owner, 2026-11-20) |
| 5 | Where the code goes | `v1.40_wip/shell/` | **yes** (owner, 2026-11-20) |

## 3. Findings for the API

1. **`FS_F_SPARSE` has no meaning of its own.** The emulation sets it on every COW file and never on a streamed one,
   even one written by `fs_write_page` with holes; FS-API does not define it. (FS-API NOTES 3 has it open.)
2. **`fs_stat` on a directory:** what `length`, `pages`, `depth` and `copies` mean for one is not said (the emulation
   gives the directory file's bytes, its pages, its depth, and 0).
3. **`fs_stat`'s `pages`** is the pages the file owns (the walk), so a copy shows few; FS-API says "pages" only.
4. **`FS_EBUSY` means five things:** copies, open, the Tracer's copy, no free fd, no free writer's buffer. The shell
   can print only `busy`.
5. **A path not normalized** is `FS_EINVAL` in the emulation; FS-API says paths are normalized, not what happens
   when one is not. `fs_remove("/")` is `FS_EBUSY` in the emulation, unsaid in FS-API.
6. **A sorted `ls` costs a request to the memory tile per entry per pass:** about 5,900 `fs_list` calls for a proof's
   `logs/` (1.4.2), on the tile the design keeps free. A batched `fs_list` (several entries an answer) could be
   considered later; not designed here. (448 entries are 13 passes, 5,837 calls.)
7. **The logs' names are not zero-padded:** `workflows.cpp` names them `tile0.code` … `tile63.code`, so `ls` shows
   `tile1`, `tile10`, `tile11` … `tile2`. Names that should sort by number are written zero-padded (owner,
   2026-11-20); FS-API 3.4 names no convention for the run logs or the tiles' own logs.

8. **The writers' buffers were too few for the tiles' own logs.** *Closed: a pool of 128, named by the fd entry's
   position word (owner, 2026-11-20; FS-API 4.6), merged from `fs-api`; the `proof` seed holds 115 open.* FS-API 3.4: each compute tile writes its own logs and
   "may have at least four files open"; 4.6 gives a writer a buffer from a pool of up to 32 (5 bits of the fd entry,
   open point 7); the emulation has 32 and answers the 33rd writer `FS_EBUSY`. 64 compute tiles each with one log open
   through a segment, and the tracer tile's three run logs, need 67; four files a tile, 259. Found by the `proof`
   seed with 112 tiles (`run_proof` uses 4).
9. **What `FS_ENOSPC` leaves behind was not said, and the emulation leaked pages.** *Closed: the file keeps every
   byte in the pages it has stored, its length cut back to them, the bytes of the page that found no room lost;
   `fs_close` writes the index lines and the entry all the same (owner, 2026-11-20; FS-API 5.2), merged from
   `fs-api`. Test 16 checks the shell against that rule, with `check()` (3d).* As found: When `fs_write`, `fs_flush` or
   `fs_close` finds no page, the emulation's `flush` returns before writing the index lines and the entry: the pages
   already written out stay taken and are named by nothing (`check()`: "page N is taken in the map, unused by the
   walk", against FS-API 4.1), the entry keeps the length of its last flush (0 for a new file), and nothing written
   since is kept. A mount would find the pages free again. Test 16 shows it (`cat` of 3.6 MB into a file system with
   32 free pages: all 32 stay taken after `rm`, until a mount). **A fix in `fs_emu.cpp`'s `flush` (the pages written out, and the
   index lines naming them, made the file's) was written and refused by this session's permission check; it is not
   made.** FS-API needs to say what a file holds after `FS_ENOSPC`, whether its pages are kept or freed, and whether
   the writer can go on.
10. **`fs_write`'s result when it runs out of room part way** is `FS_ENOSPC`, and the bytes it did take are not
    counted. With 5.2's rule the caller can learn what the file kept from `fs_stat`'s length, after the call; the
    shell does not need more (it reports `no space` once and stops writing).
11. **`fs_read_page` past a file's end** (FS-API 5.2) is not said; the emulation gives zeros and 0, as for a hole.
    SHELL-SPEC said the bytes past the length are undefined. The shell never asks past the length (from `fs_stat`).

The findings are collected here for the owner's review at the end, not settled one at a time.

## 3a. What the shell decided where SHELL-SPEC is silent †

1. **A script's Ctrl-C** (`host/console_host.c`): in a script, `con_poll` gives the next byte only if it is 0x03. A
   script's bytes are typed ahead; were `con_poll` to give any byte, a long command's `interrupted()` would eat the
   next command line. A test interrupts a command with `\x03` right after its Enter.
2. **Messages SHELL-SPEC does not word:** `syntax error: more than one redirection`, `syntax error: no file after >`
   (for `>>` too), `grep: pattern too long` (worded there), `^C` on its own line after an interrupted command.
3. **A line of only a redirection** (`> f`) opens and closes the file: it is made, or emptied. An unknown command with
   a redirection does not touch the file.
4. **`ls`:** several paths in the order given, a file's name as typed, `<path>:` before each directory's listing and a
   blank line before every listing but the first; a directory has its `/` in `ls -l` too (as `06-shell.log` shows).
5. **`stat`'s `name:`** is the path as typed; a blank line between the paths shown.
6. **`grep`:** a line over 1,024 bytes is lines of 1,024 for `-n` too (each chunk its own number), and `help` says so
   in a last line. Options may be combined (`-ni`), for every command.
7. **`more`:** keys other than Space, Enter, `q` and Ctrl-C are ignored at the prompt; the prompt's percent is of the
   file's length from `fs_stat`; a row is 80 columns, a wrap counted when the 81st byte comes (so a line of exactly
   80 is one row).
8. **`hexdump -s` past the end** prints only the end offset, the file's length.
9. **`df`'s MB** are rounded to the nearest tenth.
10. **`tail` and `hexdump` read with `fs_read_page` only,** each page once; `tail` checks for Ctrl-C at each page.
11. **The matcher is iterative** (`grep.c`): Pike's `matchstar` recursion is 64 deep for a 128-byte pattern of stars,
    2 to 3 KB on RV32, over the 2 KB stack. The same search, in the same order, keeps its backtracking points in a
    static stack of 64 entries (256 bytes). `retest.c` checks it against Pike's recursive matcher on a million
    random patterns and lines.
12. **`div.c`'s `udiv64` shifts by constants only:** GCC calls `__lshrdi3` and `__ashldi3` for a 64-bit shift by a
    variable on RV32, which the link found (section 3 expected this); rewriting the loop was smaller than adding them.

## 3b. The sizes (`make size`, 2026-11-20)

**The shell may use compressed instructions** (owner, 2026-11-20): RV32IC + Zmmul, 11.2 KB (GCC) or 12.7 KB (clang)
of code. The Makefile still builds without C unless `C=1` †.

Code is `.text` + `.rodata`, budget 32,768 bytes. The data: `.bss` 13.5 KB (the line, the cwd, two path scratches,
the page buffer, the redirect buffer, grep's line, the matcher's 256 bytes), `.data` 4 to 8 bytes.

| build | `.text` | `.rodata` | code | `.data` | `.bss` |
| --- | ---: | ---: | ---: | ---: | ---: |
| GCC 13.2, RV32I + Zmmul | 14,504 | 1,656 | 16,160 | 8 | 13,808 |
| GCC 13.2, RV32IC + Zmmul (`C=1`) | 9,564 | 1,656 | 11,220 | 8 | 13,808 |
| clang 18, RV32I + Zmmul | 16,072 | 1,536 | 17,608 | 4 | 13,804 |
| clang 18, RV32IC + Zmmul (`C=1`) | 11,132 | 1,536 | 12,668 | 4 | 13,804 |

Per object, RV32I + Zmmul, `.text` before `--gc-sections` (GCC / clang): `cmds.o` 9,305 / 10,281; `shell.o` 2,853 /
3,087; `fmt.o` 2,016 / 2,240; `grep.o` 1,888 / 1,890; `div.o` 860 / 1,496; `str.o` 744 / 824.

**The stack:** the deepest frames (`-fstack-usage`) are `cmd_ls` 240 / 256 bytes, `cmd_hexdump` 160, `cmd_tail`,
`cmd_grep`, `cmd_more` 112 to 144, `shell_main` 64 / 128; the deepest chain, `shell_main`, a command, `pf`, `vfmt`,
`utoa` and a division helper, is about 0.7 KB, inside the 2 KB.

## 3c. Fixes in `v1.40_wip/fs/`

None by this session. The bug of finding 9 was fixed on `fs-api` (README finding 9 there) and merged into `shell`
(2026-11-20).

## 3d. After the merge of `fs-api` (owner, 2026-11-20)

- **Test 16's consistency check runs again:** the `known-bug` seed line and its skip of `check()` are gone.
- **The tiles' logs are held open** through the segment by the `proof` seed, as `run_proof` holds them.
- **Test 16 is written against FS-API 5.2's rule.** In a file system of 256 pages with 31 free: `cat` of 3.6 MB into
  `/a/copy` is `no space`; the file keeps the 30 data pages it stored (its index page is the 31st), length 122,880,
  every byte an `F`; `df` shows 0 free. `cat /fill >> /a/small` (7 bytes in its page) fills that page, which was
  stored already and is rewritten, and loses the next: length 4,096, `before` then 4,089 `F`. `echo x >> /a/small`
  finds no page at `fs_close` and is `no space`, the length unchanged. `rm /a/copy` gives back all 31 pages, the free
  count as before; `echo after >> /a/small` then works. `check()` holds at the end.

## 4. Pushing

The first request to attach the repository for pushing was refused; with the owner's permission (2026-11-20) it was
attached, and the branch `shell` is pushed.
