# The v1.40 milestone

Started 2026-11-19, from what the owner said to the design session that evening. (owner) marks what the owner
said; a dagger (†) marks what the design session proposes and the owner has not ruled on.

**The scope is not fixed** (owner): "things have been pretty fluid in this project. So scope might grow or
shrink." This page is kept to what is agreed on the day.

## What v1.40 is (owner)

"The big features for v1.40 are underlying hardware improvements (specifically the RW unit and changes to the
Ingress/Egress unit). On the software side, we want to get the file system and a small shell working."

| part | in v1.40 | where it stands today |
|---|---|---|
| **the RW unit** | hardware, improved | v1.36's RW engine, unchanged but for its place; nothing in v1.37's proof runs uses it. Not designed: `v1.4_wip/planning/TO-ADD.md`, entry 3, with the compute tile's two regimes for a full router queue. Tony expects to redesign it in full |
| **Ingress/Egress** | hardware, changed | asked for so far: the statistics for each link, with the names `lvds_parent`, `lvds_child` and `chain_end` (`TO-ADD.md`, entry 1), and the watchdog's fix (entry 2) |
| **the file system** | software, working | deferred since v1.35 (the specification's item 119), and nothing of it is built. What exists is design, in v1.35's terms: the unit API `fs.h` (owner, 2026-11-10; `fs_wip/README.md`), with four additions not confirmed; the superblock, formatting and recovery, copy-on-write, directory entries and the working directory (`v1.4_wip/planning/DESIGN-NOTES-2026-11-14.md`, sections 2 to 6). v1.37 fixes what the hardware sees of a file (23.5) |
| **a small shell** | software, working | specified on 2026-11-10 and never built: `fs_wip/planning/SHELL-SPEC.md`, a console shell on the master tile's UART, against a dummy console and a dummy file system in memory |

## The order (owner, 2026-11-19)

"I think we should tackle things roughly in the following order":

1. **design the file system API;**
2. **write a tiny shell client on top of the file system API;**
3. **write a C++ emulation of the file system API,** "so we can test, without having to have the device level
   file system working" ("maybe this is part of 2");
4. **the RW hardware, and the mesh changes;**
5. **implement the file system on the hardware.**

So the RW unit is specified from what the file system asks of it, not before.

**Of the API** (owner, 2026-11-19): the calls of 2026-11-10 "look like the set for streaming files. There will
need to be more for copy on write files." A session of its own builds the specification: "it needs to look at
the exact functions being done by C&C1 in the current code and make sure we have equivalents. Seems like we
need: cow_pages to acquire pages. And we need some kind of function to build the root. We should have API calls
for all of these so that we can build implementations." The master tile's page map (`map_format`, `map_take`,
`map_flush`) is what sits behind `cow_pages`; `feed_pages` "is probably a local function in c&c1 that feeds the
pages from the cow_pages call to the updater."

## How the work is done (owner)

**The design session manages the other sessions; it does not, in the main, build.** "Your job is not primarily
to implement the changes, rather it's to manage other sessions." It writes their briefs, reviews their plans and
reports, merges their branches, and gives the owner messages ready to paste (`HANDOVER.md`, section 8).

## Carried in from v1.37

Owed, and not part of the four features (`HANDOVER.md`, sections 3 and 4):

- a soak campaign on the cache's controller, before Tony is told the cache is ready for RTL;
- WORKER8's long runs of the link (Quests 238 to 242, parked), the owner's to release;
- a test for the firmware's wait on the block's counter;
- not ruled: the watchdog's reload on a `RELEASE`; whether the twelve counters for each link are final; whether
  the design documents move from `v1.4_wip/` to this folder.

## The sessions

| session | step | what | where | status |
|---|---|---|---|---|
| FS-API | 1, and the start of 3, 4 and 5 | the file system's API and header; at the owner's direction also its data path, its firmware in outline, a C++ emulation with ten workflows, and the list for the hardware | `sessions/FS-API/` (`REPORT.md` first); `planning/FS-API.md`, `FS-DATA-PATH.md`, `FS-FIRMWARE.md`; `fs/` | finished 2026-11-20, on `main` |
| SHELL | 2 | the shell in freestanding C, on the emulation; 19 tests | `sessions/SHELL/`; `shell/` | finished 2026-11-20, on `main` |
| RW | 4 | the design of the RW system with the owner: the RW unit, its paths on the mesh, the two regimes | `sessions/RW/PROMPT.md` | brief written 2026-11-20; not started |
