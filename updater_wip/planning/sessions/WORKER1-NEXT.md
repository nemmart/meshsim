# For WORKER1: the owner's rulings on the report (2026-11-16)

*Closed and merged into `main` on 2026-11-16. The files this one calls `NOTES.md`, `REPORT.md`, `QUESTIONS.md` and
`NEXT.md` are `WORKER1-NOTES.md`, `WORKER1-REPORT.md`, `WORKER1-QUESTIONS.md` and `WORKER1-NEXT.md`, beside it.*

Quest 197 passed on the runner (it was down for an expired token); record it in REPORT.md.

The owner went through your ten suggestions. These rulings change the frames. **`HOST-FRAMES.md` on main now has all
of them: merge main in, and it is the specification again.** Still don't edit it.

1. **The program image is two frames,** as the jump table is. Frame 1 is "program image header": the program stem,
   the file length in pages, the 64-bit word count, the 128-bit word count and the page count. Frame 2 is "program
   image data": update words in whole pages into an empty file, every page that holds a non-zero byte. No u64
   offsets. The Updater will make the file.
2. **A base jump table has no frame of its own.** It is frames 4 and 5 sent on their own, ahead of the proof. Frame 4
   gains a first field, a path: the base table's path when it is sent on its own, length 0 for a run's table. So a
   frame file is 1, 2, then 4 and 5 for a base table if there is one, then 3 to 9.
3. **No length fields are added.** A copy has its source's length; a jump table's follows from k.
4. **Page counts exclude the root.** The file system's firmware makes every root (creating a file or copying one)
   before the Updater runs, so a count is the pages the Updater takes from the free page queue. Change it in both
   places (`FileTree` and `Reference`). A page written in several segments still counts once per file.
5. **Frame 6 sends the least data.** Gold may end a run and start another to skip a stretch of zero words when that
   is smaller in the form being sent. What it skips must be zero underneath; check that against the program image.
6. **The program id is dropped:** remove it from frame 3 and from the checks.
7. **The segment count is *N*, the number of ASICs:** 64, 128 or 256, default 128. It is system configuration and is
   in no frame. Add an option for it; every "× 128" table becomes "× *N*"; a shorter cut list is padded to *N*; the
   reader infers *N* from frame 7's length. The old `segments` command is unchanged.
8. **Compression is in the frame header:** bits [7:0] of the low 64 are the compression type (0 none, 1 LRU, 2 to 255
   reserved), and [63:8] stay reserved and zero. The LRU flags leave frames 3 and 4. It applies to frames 2, 5, 6 and
   9. Each of these frames is sent in whichever form is smaller, always, by default. Keep an option to force raw or
   LRU only for the tests.
9. **A run's base is always a base table established by the host,** as you have it. Add a test that forces the
   fall-back from adding to a base to a full new table, since no real run exercises it.
10. **The per-ASIC page counts stay** (without the root).

Then update the Choices list, rerun every check, send a new Quest task for 2²⁹ (claim a fresh number), and update
`REPORT.md` with the new size tables. Put anything unclear in `QUESTIONS.md` and carry on with the simplest reading.
