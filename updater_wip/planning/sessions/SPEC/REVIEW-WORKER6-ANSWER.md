# WORKER6's answer: the Updater's specification against its code

*Checked against `main` at e7243dd5: `v1.37_wip/src/units/updater_ctl.h` ("ctl"), `updater_top.h` ("top"); line
numbers are `main`'s. `main`'s controller is mine with the shim removed by WORKER9; nothing in `ctl` but comments
changed. "Measured" means run on a scratch copy of `main` with temporary counters added (not committed): its 25
tests, plus two probes, described where used.*

## The 14 claims, in brief

| # | Claim | Verdict |
|---|---|---|
| 1 | 17.10.7's table, 4,204 flip-flops | **The arithmetic is right, and every field of ctl's `Regs` (114–182) is in it, nothing extra.** Some widths are the spec's choice rather than the code's; see list 1, items 1 and 2 |
| 2 | `done` posted, `busy` 0 the next cycle | **Right in effect; the wording is ambiguous.** Measured: list 1, item 3 |
| 3 | the statistics | **Right, with two precisions:** list 1, items 4 and 5 |
| 4 | the decoder | **Right.** One omission, and a cycle the spec does not mention: list 1, items 6 and 7 |
| 5 | `ready` | **Right** (ctl 673–680) |
| 6 | the fault's walk | **Right, with two omissions:** list 1, items 8 and 9 |
| 7 | the pages counted first | **Right** (ctl 544–546) |
| 8 | a flush | **Right** (ctl 490–530) |
| 9 | "never reads a page it will write whole, never writes a page it has not changed" | **Wrong as worded.** Measured: list 1, item 10 |
| 10 | one burst each way; the page memory's port | **Right, and stronger than stated.** Measured: list 1, item 11 |
| 11 | depth 0 | **Right, with an omission:** list 1, item 12 |
| 12 | an error | **Right.** Measured: nothing pending at any stop. One omission: list 1, item 13 |
| 13 | the reset | **Right** (ctl 210, top 156–164). The code also clears `EVENTS.done` and zeroes words 32–37 and the LRU table, which the spec's wording allows |
| 14 | 30.4's table and 29.7's pages | **Right:** 30.4 is the firmware's `master.c` 458–557 run for run; 29.7's counts leave `PAGE_COUNT` 0 at every `done` on gold's five fixtures at four *s* each (`ctl_gold_*`) |

## List 1: where the specification misdescribes the code

1. **17.10.7 / 17.10.1, the roots' width.** The spec gives the three roots 31 bits and `[30:0]` fields. The code
   keeps all 32 bits stored and reads them back (ctl 228–244). Under S11 a root with bit 31 set is UB at `START`.
   But 17.10.1 also says the words "hold the last value stored", so the spec should say which it means: bit 31 reads
   back as stored (the code, 3 more flip-flops), or reads 0 (the spec's 31-bit registers).
2. **17.10.7, widths the spec takes larger than the Updater can use.**
   - The held page number (30 bits; ctl 155) and the look-up request's `page` (30) need only 23 bits. A page number
     is a value's index over 512, and the index is 32 bits (ctl 656), so the Updater never names a page at or above
     2²³. That saves 14 flip-flops.
   - The rows are counted at 8 bits, which holds only because a row past 255 is never used. Rows left (9) is
     needed; ctl holds them in 16.
   - The AR and AW addresses at 32 bits assume `LPDDR_ADDRESS_BITS` is 32.

   With these the total is about 4,190. Otherwise the table is exact.
3. **17.10.5, "`EVENTS.done` is posted, and `STATUS.busy` reads 0 from the next cycle."** Measured, a one-value run
   on the bench as the slot steps it:
   - `START` is at 11.
   - The run's last cycle is 1052, whose edge clears the controller's busy and raises the done pulse (ctl 444).
   - The control block posts `done` in 1053 (top 131).
   - `STATUS.busy` is published 0 at the end of 1053 (top 105: busy holds through the pulse).
   - `EVENTS.done` is readable from 1054.

   So `done` and `busy` 0 become visible together, two cycles after the run's last cycle, as on the black box.
   Suggested wording: "the run's last cycle sets the `done` pulse; the next cycle posts `done`, and `STATUS.busy`
   reads 0 from the same cycle as `done` can be seen."
4. **17.10.1, `STAT_CYCLES`, "from its first to the cycle `done` is set."** Measured: 1,041, the cycles 12 to 1052,
   from the cycle after `START` to the run's last cycle. The cycle `done` is posted (1053) is not counted (ctl 611).
   Say "to the run's last cycle".
5. **17.10.1, `STAT_STALL_MEMORY`.** It counts the cycles a state waits on its burst or reply:
   - **States that count:** look-up, load, flush write, flush update, new write and new update, each while its burst
     or request is out; and root read, root write, half read and flush read while a burst is in flight (ctl 612–615).
   - **Not counted in any wait counter:** the cycle a state issues a burst or a request; the 128 cycles of a fill;
     the cycles of the walk's level and take steps that issue nothing.

   `STAT_STALL_DATA` counts only in the next state (ctl 442). The spec's sentences are true, but a builder matching
   counts needs this list.
6. **17.10.2: the decoder decodes and refills only while a run is in progress and not stopped** (ctl 619, `en`). The
   spec says so of the stopped state (17.10.8) but not of idle. While idle the queue fills (17.10.3) and nothing is
   decoded.
7. **17.10.2, one value ahead: a header word also waits for the output register** (ctl 631: every code needs it free
   or being taken). So when values come steadily, **each header word costs one cycle** in which the run finds no
   value: `STAT_STALL_DATA` + 1. Measured: the condition held in 405,416 cycles over the tests. 21.5's row ("a value a
   cycle while they come and stay in the page") should add "and a cycle for each header word".
8. **17.10.5, step 1, "its source … is read whole into the page memory": not for the image's data page.** Its walk
   step takes the page with no read (ctl 556).
   - Its halves are read when first touched and not covered, or at its flush.
   - The exception: when the Tracer's copy has just loaded the same source at that level, the page is held with both
     halves had (ctl 581).
   - The spec's exception paragraph says the page is "taken but held", but not that it is not read.

   Also, *n* in the type table counts the value itself: ctl 653 uses `run_left` and `WORD_COUNT` before this value
   counts them down.
9. **17.10.5, "If the image owns the page, it is held as the image's own":** only once the Tracer's copy is made. With
   `tracer_copy`, if the image owns the page but the copy does not, the copy's levels are walked first, and then the
   image's own page is held (ctl 546, 552).
   - The walk also takes one cycle for each (level, file) pair from level 0, including pairs that do nothing (ctl
     553): up to 2 × `depth` cycles.
10. **17.10.5, "it never reads a page that a run will write whole, never writes a page it has not changed": false as
    worded.**
    - **The read rule.** A half's read is decided by the first value that touches it, from that value's run of values
      alone (ctl 459). Measured: on an owned page, two runs of values, (0, 100) then (100, 156), fill half 0. The half
      is read and then written whole: 1 read, 1 wasted.
    - **The write rule.** The Tracer's copy's data page is written unchanged (ctl 588), and a new page's untouched
      halves are written as copied.
    - True wording: "it reads a half only if the value that first touches it, with the rest of its run of values,
      does not fill it in this run; it writes only new pages (whole) and an owned data page's dirty halves."
11. **17.10.6, "one read burst and one write burst at most in flight": in fact one burst at most, read or write.**
    Every state waits for its burst before it issues the next. Measured: never a read and a write in flight together
    in any cycle of the tests.
    - The port reason is right. The simulator also stops if the page memory's port is asked twice in a cycle (ctl
      287); see item 14.
12. **17.10.5, depth 0: `tracer_copy` does nothing per page there.** No look-up is made and the root is held at once
    (ctl 452), so the Tracer's copy gets no pages. With `root` 2, though, its root is still written (ctl 431). Say so,
    or make `tracer_copy` at depth 0 UB.
13. **17.10.8: after a stop the input queue still takes words** until it holds 4: `ready` does not depend on the run
    (ctl 680). Only the decoder stops.
    - Measured: at all 10 stops in the tests, no request, burst or W beat was pending, so "sends nothing more" holds
      with nothing to withdraw.
14. **17.10.11 omits the simulator's other stops** (they mean the controller is wrong, like the reply that cannot
    come):
    - two accesses to the page memory's port in a cycle (ctl 287);
    - an R beat with no read in progress;
    - a B response with no write in progress;
    - a reply with no request outstanding;
    - a refill past the shifter's reach, more than 65 bits held.

## List 2: where the specification is right and the code is wrong or silent

1. **A run of values past index 2³² − 1 wraps to 0, silently** (ctl 656). At depth 3 there is no `beyond` check at
   all (ctl 447, `depth < 3`): a depth-3 file reaches 2³⁰ pages, but the Updater can name only the first 2²³.
   - The spec says neither. I suggest a header word whose start + count passes 2³² be UB (17.10.9), and that 29.4
     state the 2²³-page reach. The code would then need a check in the simulator only.
2. **The two rulings: no objection; their consequences.**
   - **UI-3 (`free_page_credit` to `EVENTS` [2]).** The code posts it on bit 3 (top 61, 163), and so do:
     - the firmware's `host.h` 61 and `master.c`'s `UPD_EVENT_FREE_PAGE_CREDIT`;
     - the black box (`updater.h`);
     - the benches, which take the bit from `Updater::EV_FREE_PAGE_CREDIT`.

     All must move in one change, or the comparison with the black box breaks.
   - **S11 (a root or free page off a page, or `root` 3: UB).** It changes ctl 408 and 576 from setting
     `lpddr_error` to stopping the simulator. The hardware then needs no comparison against LPDDR's end.
3. **Rules of 17.10.9 that the code does not check:**
   - the three roots naming different pages;
   - a free page being new;
   - one writer;
   - `WRITE`s from one source at a time (invariant 36).

   These are UB, as the spec says, and 17.10.11 does not claim the simulator stops on them. The bench checks "new"
   and "one writer" (`tests/ctl_bench.h`'s write rule; `standin_cache.h`), not the chip. Nothing to change unless the
   spec wants the simulator to check them.

Nothing else: every rule 17.10 states that the code should keep, it keeps.
