# The shell session (v1.40, step 2)

**The project.** meshsim is the simulator and specification for a 64-tile mesh processor, an ASIC that proves the Jolt
zkVM. The repository is `nemmart/meshsim`. v1.40 (`v1.40_wip/MILESTONE-v1.40.md`) adds a file system and a small shell.
The owner's order: (1) the file system's API; (2) a tiny shell on top of it; (3) a C++ emulation of the API; (4) the RW
unit and the mesh's changes; (5) the file system on the hardware.

**Step 1 is done, and step 3 has begun** (the FS-API session, branch `fs-api`, not yet merged): the API is specified,
and the file system exists entirely in software, in C++, behind its header. **Your part is step 2: the shell, built
and tested on that emulated file system.**

## What the owner has said of it

- The shell is for debugging after the fact: when a proof fails, the logs its tiles wrote stay in its working
  directory, and the shell reads them (`FS-API.md` 3.4, 3.6, 3.8).
- "Implement the shell on top of the simulated file system" (owner, 2026-11-20): **the emulation in
  `v1.40_wip/fs/` is the shell's file system**, in place of `SHELL-SPEC.md`'s dummy.
- `fs_list` gives a directory's entries in slot order, not creation order; **the shell sorts what `ls` prints**
  (owner, 2026-11-20).
- The shell's four additions to the API (`FS_TRUNC`, `fs_stat`, `fs_df`, `fs_list`'s cursor) are in the header and the
  emulation, not yet ruled: **use them as the header has them**; the owner will review them later (owner, 2026-11-20).

## The sources

| source | holds |
| --- | --- |
| `fs_wip/planning/SHELL-SPEC.md` | **the shell, specified:** its files, the build constraints (freestanding C, no libc, the division helpers), the console, the line editor, parsing and redirection, paths, every command, the matcher, formatted output, the tests, the size budget |
| `v1.40_wip/fs/fs.h` | **the API, as the shell calls it.** It supersedes SHELL-SPEC section 5's copy (section "What changed" below) |
| `v1.40_wip/planning/FS-API.md` | what each call does, its errors, and the rules a caller keeps; section 6 says which calls the shell may make |
| `v1.40_wip/fs/README.md`, `fs_emu.h` | the emulation: how to build it, its hooks (`power_on`, `set_caller`, the call log, `check()`), what it found |
| `v1.40_wip/fs/tests/workflows.cpp`, `tests/out/06-shell.log` | the shell's commands already run as call sequences; how to make COW files, frozen files and a proof's working directory through the API |
| `v1.40_wip/sessions/FS-API/NOTES.md` section 4 | each shell command against the calls it uses |

## What changed since SHELL-SPEC.md was written (2026-11-10)

1. **The file system is the emulation, not a dummy.** No `host/fs_mem.c`. Seed a test's file system through the API
   itself: `fsemu::power_on` (a hard reset formats), then `fs_mkdir`, `fs_open`, `fs_write`, `fs_close` as the shell's
   caller or a tile's, and, for a frozen or sparse file, a COW file made as the master tile makes one (`fs_reserve_pages`,
   `cow_create`, an Updater run by the stand-in, `cow_commit`, `cow_freeze`; `workflows.cpp`'s `store_program` does it).
   **Only a COW file can be frozen;** a streamed file has no frozen flag. Section 12's `.seed` format can stay, mapped
   onto these calls, or change: say which in your outline.
2. **The header:** `fs_open`'s flags are `u32`; there is `FS_F_COW` (4) beside `FS_F_FROZEN` and `FS_F_SPARSE`, and
   calls the shell may not make (the pages, the COW calls, `fs_shutdown`: `FS-API.md` 6). With
   `fsemu::set_caller(Caller::SHELL)` the emulation stops on any of those.
3. **The shell meets rules the dummy did not have:** a file open for writing has no readers, and the reverse
   (`FS_EBUSY`); a frozen COW file can be read and an unfrozen one (the Tracer's copy) cannot (`FS_EBUSY`); `rm` of a
   file with copies is `FS_EBUSY`; `>` on a COW file is `FS_EROFS`.
4. **Recovery mode is overtaken.** In v1.40 a soft reset mounts the file system as it stands and the shell runs on it
   (`FS-API.md` 3.1, 3.8); there is no separate recovery file system. SHELL-SPEC's recovery prompt and its open item 3
   are questions for the owner, not yours to settle.
5. **`ls` sorts** (above). A proof's `logs` directory holds about 450 entries; the shell's static buffers are about
   14 KB. How it sorts within its memory is yours to propose.

## The work

1. **The outline first,** in `v1.40_wip/sessions/SHELL/NOTES.md`: where the code goes (SHELL-SPEC puts it in
   `fs_wip/shell/`, which is a source and not edited; propose `v1.40_wip/shell/`), how the tests seed the file system,
   what you take from SHELL-SPEC as it stands and what changes, the questions, the order you will build in. **Wait for
   the owner's go-ahead. A question from the owner is not a go-ahead.**
2. **The shell,** as SHELL-SPEC sections 2 to 11 specify it, against `v1.40_wip/fs/fs.h`.
3. **The tests** of SHELL-SPEC section 12 on the emulated file system, plus: a proof's working directory as the
   workflows leave it (images, the Tracer's copy, run logs, tiles' logs), and the rules of "What changed" item 3.
4. **The RV32 build and the size check** (SHELL-SPEC sections 3 and 12), with the real `fs.h` and stub calls.
5. **Findings:** anything the shell needs that `fs.h` or `FS-API.md` does not give, or gives unclearly. **This is the
   test of the API:** a call whose meaning you had to guess is a finding. Do not change the API; list it.

## Rules

- **Keep replies to the owner short.** No paragraphs of agreement. Bring questions one at a time, the ones that shape
  the most first, each with your recommendation.
- **Record each ruling the day it is made,** with (owner) beside it; until then a point carries a dagger (†).
- **A frozen folder is never edited** (`v1.37/` and earlier). `fs_wip/` is a source: not edited. **`FS-API.md`,
  `FS-DATA-PATH.md` and `fs.h` are not yours to change:** list what they need in your notes. In `v1.40_wip/fs/` you
  may fix a bug, meaning behaviour that contradicts `FS-API.md`, each fix listed in your notes; nothing else.
- **One name for one thing.** 128 bits is "a 128-bit word". The special tiles are the master, memory and tracer tiles.
- **Branch `shell`, from `fs-api`** (not yet merged into `main`). Commit and push after each part. Do not merge.
- Attach the repository to the session to push. **Never write a token into a file or repeat one in a reply.**
- Never `pkill -f`: use `pgrep` and kill by PID.
- End each turn with a clean `git status`.

## Not your work

The file system and its API; the real `fs.h` client (requests to the memory tile, `FS-DATA-PATH.md`); the UART driver;
the master tile's firmware; recovery.

## Deliverables

| file | what |
| --- | --- |
| `v1.40_wip/shell/` (or where the owner rules) | the shell, its host build on the emulation, its tests, its RV32 build |
| `v1.40_wip/sessions/SHELL/NOTES.md` | the outline; the questions and how each ended; the findings for the API; the sizes |
| `v1.40_wip/sessions/SHELL/REPORT.md` | at the end: what was done, what is open, what the next sessions need to know |
