# SHELL: the session's report

*2026-11-20. Branch `shell`, from `fs-api`, with `fs-api` merged in again (12e3161); not merged into `main`. The notes, with every ruling and finding: `NOTES.md`.*

## What was done

- **The outline** (`NOTES.md` section 1) and five questions, each ruled by the owner on 2026-11-20: the tests seed
  the file system through the API (`.seed`, extended); no recovery mode, for now; `ls` sorts in byte order (names
  that should sort by number are written zero-padded); `ls -l` and `stat` show COW; the code in `v1.40_wip/shell/`.
- **The shell,** SHELL-SPEC sections 2 to 11 against `v1.40_wip/fs/fs.h`, in freestanding C99: the line editor,
  quoting and redirection, paths, all seventeen commands, the matcher, the formatter, our own division. It makes only
  FS-API 5.2's calls; on the host it runs as `Caller::SHELL`, so any other call would stop the emulation.
- **The host build on the emulation:** `../fs/` compiled into `build/`, unchanged. Each test's file system is made
  through the API by `host/seed.cpp`: streamed files as a compute tile writes them, COW files, copies and stored
  programs as the master tile makes them with the Updater's stand-in, a proof's working directory as a failed proof
  leaves it, files left open, a soft reset.
- **The tests:** 16 scripted sessions (`make test`), passing with GCC and clang, covering section 12's list (but the
  recovery prompt), and a proof's working directory after a mount (448 logs, sorted, written with every tile's log
  open), the rules for files open elsewhere, frozen and COW files and the Tracer's copy, Ctrl-C, and running out of
  space against FS-API 5.2's rule. The division helpers
  against the host's division (2.46 million checks), and the matcher against Pike's recursive one (a million cases).
  After every session the harness checks that the shell closed every file, and the emulation's invariants.
- **The RV32 build and the sizes:** links with both compilers, with and without C, with no undefined symbol; the
  division helpers linked are `div.o`'s. Code 11.2 to 17.6 KB of the 32 KB budget; data 13.8 KB; the deepest stack
  chain about 0.7 KB (`NOTES.md` 3b).
- **Eleven findings for the API** (`NOTES.md` section 3), for the owner's review; 8 and 9 closed by the owner.

## What is open

1. **Closed by the merge of `fs-api`** (owner, 2026-11-20): the page leak at `FS_ENOSPC` (finding 9; test 16 now
   checks FS-API 5.2's rule, with `check()`) and the writers' buffers (finding 8; 128, and the `proof` seed holds
   115 logs open).
2. **The findings** 1 to 7, 10 and 11, and the shell's own choices where SHELL-SPEC is silent (`NOTES.md` 3a, all †).
3. **The API's four additions** (`FS_TRUNC`, `fs_stat`, `fs_df`, `fs_list`'s cursor) are used as `fs.h` has them,
   not yet ruled.
4. **Recovery mode** is dropped for now (owner); if it comes back, `shell_main` takes its mode again and the prompt
   its `[recovery] ` prefix.
5. **SHELL-SPEC section 6's stretch** (cursor movement, history) is not done; the budget has room for it.

## What the next sessions need to know

- `make test` and `make size` in `v1.40_wip/shell/`, each with `CC=gcc` or `CC=clang`. The call log of each test,
  every call the shell made, is in `build/<CC>/out/NN-name.log`.
- **The real `fs.h` client** replaces the emulation without touching the shell: the shell calls `fs_open`, `fs_read`,
  `fs_write`, `fs_close`, `fs_read_page`, `fs_mkdir`, `fs_remove`, `fs_list`, `fs_stat` and `fs_df`, and no other
  (`rv32/stubs.c` is that list; the link fails on any other).
- **The real console** replaces `host/console_host.c`: `con_getc`, `con_poll`, `con_putc`. `shell_main` never returns
  on the target.
- `ls` on a big directory makes `(entries + 1) x passes` `fs_list` calls, 36 entries a pass (finding 6).
- `host/seed.cpp` copies `workflows.cpp`'s Updater-driving code (marked "from workflows.cpp"); if the flows change
  there, the seed's copy may need to follow.
