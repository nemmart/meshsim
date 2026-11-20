# The file system API session (v1.40)

**The project.** meshsim is the simulator and specification for a 64-tile mesh processor, an ASIC that proves
the Jolt zkVM. A proof is cut into segments, and each ASIC proves one. The host sends the data that builds a
segment's memory images; the master tile's firmware takes it in, the Updater builds the images in LPDDR as
copy-on-write files, and the Tracer runs on them through the cache. v1.37, frozen on 2026-11-19, is the first
version with that whole path. The repository is `nemmart/meshsim`.

**v1.40 is the next version** (`v1.40_wip/MILESTONE-v1.40.md`): the RW unit and changes to Ingress/Egress in
hardware, and in software the file system and a small shell. The owner's order for it:

1. **design the file system API;**
2. write a tiny shell on top of the API;
3. write a C++ emulation of the API, so that software can be tested before the device's file system works;
4. the RW hardware and the mesh's changes;
5. implement the file system on the hardware.

**Your part is step 1: the specification of the file system's API, and its header.** Every later step is built
from it by another session: the shell and the emulation call it, the RW unit is specified from what it asks of
the hardware, and the device's file system implements it. It has to be complete and exact enough for all three
to be built without asking what a call means.

## What the owner has said of it (2026-11-19)

- **The calls agreed on 2026-11-10 are the set for streaming files** (`fs_wip/README.md`: `fs_open`, `fs_read`,
  `fs_write`, `fs_close`, `fs_mkdir`, `fs_remove`, `fs_list`, `fs_read_page`, `fs_write_page`, `fs_fprintf`).
  "There will need to be more for copy on write files."
- **The session "needs to look at the exact functions being done by C&C1 in the current code and make sure we
  have equivalents."** C&C1 is the master tile; its firmware is `v1.37/fw/cc/master.c`.
- "Seems like we need: **`cow_pages` to acquire pages.** And we need **some kind of function to build the
  root.** We should have API calls for all of these so that we can build implementations."
- **How the master tile's code divides** (owner): `map_format`, `map_take` and `map_flush`, its page map, are
  what sits behind `cow_pages`, and leave the master tile. `feed_pages` "is probably a local function in C&C1
  that feeds the pages from the `cow_pages` call to the Updater."

## The sources

| source | holds |
|---|---|
| `v1.37/fw/cc/master.c`, `master_layout.h` | **the master tile's firmware as it runs today:** its page map, the roots it makes, the Updater's runs, the free page queue, the hand-over to the tracer tile |
| `v1.37/spec/master-firmware.md` (chapter 30) | the same as a contract; 30.4 is the table of runs and roots, 30.5 the hand-over |
| `v1.37/spec/machine-spec.md` 23.5 | what the hardware fixes of a file: pages, lines, units, the tree, depth, roots, entries and the owned bit, copy on write |
| the same, 17.10 | the Updater: `RUN_CONFIG.root`, `IMAGE_ROOT`, `TRACER_ROOT`, `SOURCE_ROOT`, `PAGE_COUNT`, the free page queue and its credit |
| the same, 23.6, above all 23.6.8 | the cache, and **what it asks of its clients:** the rules whoever hands out pages and names them must keep |
| the same, 11.5, 17.7, 23.2 to 23.4 | the memory tile's requests, `READ` and `WRITE`, and the RW engine as it is today |
| the same, chapter 26 | the open items; 119 (the file system), 180 (who discards a page), 181 (after an error), 191 (who makes a root) bear on you |
| `v1.37/spec/host-frames.md` 29.4 | files, lengths and page counts as the host's frames give them |
| `fs_wip/README.md` | the API of 2026-11-10 and what was decided with it |
| `fs_wip/planning/SHELL-SPEC.md` | the shell as specified: the calls it uses (section 5), with four additions never confirmed (section 14), and the dummy file system |
| `v1.4_wip/planning/DESIGN-NOTES-2026-11-14.md`, sections 2 to 6 | the superblock, formatting and recovery, copy-on-write, directory entries, the program's files, the working directory |
| `v1.37/spec/history/DECISIONS-v1.3.md`, sections 19, 20 and 22 | launching a program; the file system; the images and maps as copy-on-write files (`freeze`, `cow_open`) |

**The three older sources are in v1.35's terms, from before v1.36 and v1.37.** C&C 1, 2 and 3 are the master,
memory and tracer tiles. They place work (a page manager on C&C 3, the file system on C&C 2) that v1.37 has
since placed otherwise or left open. **Which wins when two disagree:**

1. **The owner's ruling,** the latest first; `(owner)` marks them.
2. **v1.37's specification,** for the hardware and for what a file is. It is frozen and you do not edit it.
3. **The older design,** where a ruling in it still stands. Where v1.37 has overtaken one, that is a question
   for the owner, not yours to settle.

## The work

1. **The inventory.** Go through `master.c` and chapter 30 and list **everything the master tile does today
   that is the file system's work:** each function, what it does, and what becomes of it: an API call that
   replaces it, code that stays in the master tile and calls the API, or nothing. The owner's division above is
   where it starts. Check it against the code line by line; do not take it from this brief.
2. **The streaming calls:** the set of 2026-11-10, each stated exactly, and the shell's four additions
   (`FS_TRUNC`, `fs_stat`, `fs_df`, `fs_list`'s cursor) each put to the owner.
3. **The copy-on-write calls:** `cow_pages`; the call that builds a root, empty or copied from a source; and
   whatever else the inventory and the older design show is needed, to be proposed: freezing a file, a file's
   root and depth for the Updater and the Tracer, a file made with a length that fixes its depth, giving pages
   back, the working directory.
4. **For every call:** its prototype; each argument and the result; every error; what it guarantees when it
   returns; what the caller must keep to; its limits; whether it can wait, and on what.
5. **Who calls it.** The master tile's firmware, the shell, and a unit on a compute tile are different
   callers. Say which calls each may make, and where a call is one caller's alone.
6. **The cache's rules** (23.6.8). For each, say whether the API keeps it for its callers or leaves it to them.
7. **The proof that nothing is missing.** In your notes, the master tile's frame loop and hand-over written
   again as calls to the API, in outline and not as code to build, and each of the shell's commands against the
   calls it uses. Anything with no equivalent is a finding.
8. **What it asks of the hardware.** A list, not a design: what the API implies the RW unit and the mesh must
   carry (who sends what to whom, how much, and whether a full queue can be an error there or must hold the
   sender back). It is the starting point of step 4.

**It must be implementable twice:** by an emulation in C++ on a host, and on the chip. Nothing in it may depend
on how either is built. Where a call's behaviour would differ between them, say what the specification
requires of both.

## The questions

A specification cannot say "proposed". **Every open point ends as the owner's ruling, or as an open item** with
what depends on it.

- **List them first,** in your notes, each with your recommendation and what it would cost to change later.
- **Bring them to the owner one at a time,** the ones that shape the most first: the question in a sentence or
  two, your recommendation, and why. No lists of questions.
- **Record each ruling the day it is made,** with (owner) beside it. Until then a point carries a dagger (†).
- **Look for the silences too:** what the shell, the emulation or the device's implementer would need that no
  source states.

## Rules

- **Propose an outline first,** in `v1.40_wip/sessions/FS-API/NOTES.md`, and wait for the owner's go-ahead: the
  inventory, the list of calls, the list of questions, and the order you will write in. **A question from the
  owner is not a go-ahead.**
- **Keep replies to the owner short.** No paragraphs of agreement.
- **Do not design what is not asked.** Not the layout in LPDDR beyond what a call exposes; not the requests'
  layout on the wire; not flow control; not error recovery or a recovery mode, which is not designed (item
  181) and is not to be improvised: where the older design leans on it, that is a question.
- **A change to hardware Tony (the RTL engineer) has built is never made on this side.** If the API needs one,
  say so in your list for the hardware.
- **One name for one thing.** 128 bits is "a 128-bit word". The special tiles are the master, memory and tracer
  tiles.
- **A frozen folder is never edited** (`v1.37/` and earlier). `fs_wip/` and `v1.4_wip/` are sources: you do not
  edit them either. Where one is wrong or overtaken, say so in your notes.
- **Branch `fs-api`,** from `main`. Commit and push after each part. Do not merge into `main`: the design
  session does that when the owner accepts a part.
- Attach the repository to the session to push. **Never write a token into a file or repeat one in a reply.**
- Never `pkill -f`: use `pgrep` and kill by PID.
- End each turn with a clean `git status`.

## Not your work

Any implementation: the emulation, the shell, the device's file system, the firmware. The RW unit's design and
the mesh's regimes. The on-disk structures, but for what a call makes visible. The host's frames.

## Deliverables

| file | what |
|---|---|
| `v1.40_wip/planning/FS-API.md` | the specification of the API: the callers, every call, the errors, the limits, the rules a caller keeps |
| `v1.40_wip/fs/fs.h` | the header, in C, usable from freestanding C on a tile and from C++ on a host; it agrees with the document call for call |
| `v1.40_wip/sessions/FS-API/NOTES.md` | the outline; the inventory of `master.c`; the questions with how each ended; the frame loop and the shell's commands against the API; what in the older sources is overtaken |
| `v1.40_wip/sessions/FS-API/FOR-THE-HARDWARE.md` | what the API asks of the RW unit and the mesh |
| `v1.40_wip/sessions/FS-API/REPORT.md` | at the end: what was done, what is open, what the next sessions need to know |
