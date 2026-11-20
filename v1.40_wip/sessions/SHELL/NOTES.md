# SHELL: the session's notes

*Started 2026-11-20. (owner) marks a ruling, with its date; † a proposal not ruled on. The brief is `PROMPT.md`; the
shell's specification `fs_wip/planning/SHELL-SPEC.md` (section numbers below are its own unless marked); the API
`v1.40_wip/fs/fs.h` and `v1.40_wip/planning/FS-API.md`.*

## 1. The outline (waiting for the owner's go-ahead)

### 1.1 Where the code goes †

`v1.40_wip/shell/`, with SHELL-SPEC section 2's layout, less the dummy:

| file | contents |
| --- | --- |
| `console.h` | section 4, as it stands |
| `shell.c`, `cmds.c`, `fmt.c`, `str.c`, `div.c`, `grep.c` | as section 2; they include `../fs/fs.h` itself, not a copy |
| `host/console_host.c` | section 4's host console |
| `host/main.cpp`, `host/seed.cpp` | the host entry (C++): powers the emulation on, seeds it (1.2), `set_caller(SHELL)`, runs `shell_main`, then `fsemu::check()` |
| `rv32/crt0.S`, `rv32/link.ld`, `rv32/stubs.c` | the link-only RV32 build; `stubs.c` stubs `fs.h`'s streamed-file calls only |
| `Makefile` | `make host`, `make test`, `make rv32`, `make size`; `CC=gcc` (with `g++`) or `CC=clang` (with `clang++`). It builds `../fs/fs_emu.cpp` and `stand_ins.cpp` into its own build directory and changes nothing in `../fs/` |
| `tests/` | `NN-name.in`, `.seed`, `.out` (section 12); each run also writes the emulation's call log to `tests/out/NN-name.log`, for reading, not diffed |

No `host/fs_mem.*`. The shell's C is the same on both builds; only `host/` is C++.

### 1.2 How the tests seed the file system (owner, 2026-11-20)

Keep section 12's `.seed` file, each line mapped onto API calls by `host/seed.cpp`:

| line | calls |
| --- | --- |
| `cap <pages>` | `power_on(pages, hard)`, rounded up to a multiple of 128 (default 16,384) |
| `dir <path>` | `fs_mkdir`, as the master tile |
| `file <path>` … `.` | a streamed file: `fs_open(FS_APPEND)`, `fs_write`, `fs_close`, as a compute tile. **No `frozen` or `sparse`:** a streamed file has neither |
| `gen <path> <bytes> lines\|bytes` | the same, generated |
| `cow <path> <pages> [frozen]` | a COW file as `store_program` makes one: `fs_reserve_pages`, `cow_create`, an Updater run (stand-in) writing every page with a known pattern, `cow_commit`, `cow_freeze` if `frozen`, `fs_unreserve` |
| `copy <src> <dst> [frozen]` | `cow_copy` and a run with `root` 2; so `src` has copies |
| `proof <wd> <tiles>` | a working directory as `run_proof` leaves it after a failure: `map`, `cycle0`, `start`, `final` (frozen), `tracer` (never frozen), `logs/` with each tile's three run logs and its own log. With 64 tiles, `logs/` has about 450 entries |
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
2. **`ls` sorts, in byte order of the names** †, within the static buffers: a **batched selection**. Each pass reads
   the whole directory with `fs_list` and keeps, in the 4 KB page buffer (free during `ls`), the 36 smallest entries
   (name, type, flags, length: 112 bytes) above the last one printed; it prints them, and the next pass starts above
   the last. Names are unique, so "above" is exact. 450 entries are 13 passes, about 5,900 `fs_list` calls; no file is
   read. An entry made or removed between passes is shown or not, never twice.
3. **`ls -l` and `stat` show COW** †: `ls -l` gets a fourth column, `c` for a COW file (`-fsc 2097152 cycle0`);
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
7. The RV32 build and `make size`, both compilers, with and without `C=1`. This machine has `clang` 18 and `ld.lld`;
   `riscv64-unknown-elf-gcc` is not installed yet.
8. The stretch of section 6 (cursor, history), only if everything passes inside the budget.

## 2. The questions, and how each ended

Most-shaping first; each with my recommendation.

| # | Question | Recommendation | Ended |
| --- | --- | --- | --- |
| 1 | How the tests seed the file system | 1.2: keep `.seed`, mapped onto the API, with `cow`, `copy`, `proof`, `open`, `reset` | **yes** (owner, 2026-11-20) |
| 2 | Recovery mode, now that a soft reset just mounts | drop it: `shell_main(void)`, one prompt `<cwd>> ` | **dropped, for now** (owner, 2026-11-20) |
| 3 | `ls`'s order and how it sorts | byte order; the batched selection of 1.4.2 | † |
| 4 | Showing COW in `ls -l` and `stat` | 1.4.3 | † |
| 5 | Where the code goes | `v1.40_wip/shell/` | † |

## 3. Findings for the API (to grow as the shell is built)

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
   considered later; not designed here.

The findings are collected here for the owner's review at the end, not settled one at a time.

## 4. Pushing

The first request to attach the repository for pushing was refused; with the owner's permission (2026-11-20) it was
attached, and the branch `shell` is pushed.
