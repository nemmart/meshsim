# Review request: WORKER6, the Updater

*From the specification session (`sessions/SPEC/`), branch `v137-spec`, at the commit that adds this file. Your
controller is the authority on what the Updater does (the owner: "it's actually the spec portion of those codes").
The specification was written from `updater_ctl.h`, `updater_top.h` and `word_port.h` quickly, at cycle-level detail:
that is where mistakes will be. Read it against your code, not against `UPDATER-INTERFACE.md`.*

## What to read

All in `v1.37_wip/spec/`:

| Section | What it states |
| --- | --- |
| `machine-spec.md` 17.10, 17.10.1–17.10.11 | the Updater: words and bits, the decoder, the word port, the free page queue, **a run's states and a fault's walk (17.10.5)**, its memory side, **its registers (17.10.7)**, errors, what software keeps, reset, the simulator's checks |
| `host-frames.md` 29.3 | the update words and the LRU code |
| `master-firmware.md` 30.4 | the runs the master tile makes, with each run's settings |
| `machine-spec.md` 3 (`word_port`, `lookup`), 20 (invariant 36), 21.5 (the Updater's row), 24 (its row) | the gathered interfaces, invariant, timing and budget |

**Two rulings change your code** (the owner, 2026-11-18), so the specification differs from it on purpose; they
belong in your second list only if you see a problem with them:
- `free_page_credit` is packed to `EVENTS` [2] (UI-3);
- a root or free page that is not a page's address in LPDDR, and `RUN_CONFIG.root` 3, are UB: the simulator stops
  on them, and `ERRORS.lpddr_error` is not set for them (S11).

## What I derived and did not run, or could not fully check from the code

1. **The register table of 17.10.7 and its total, 4,204 flip-flops,** at the signals' widths: among them the bit
   buffer's count as 8 bits, the catch index as 10 bits and a valid bit, the free page queue's pointers as 8 + 9 +
   7, the three roots as 31 bits each.
2. That `EVENTS.done` is posted, and `STATUS.busy` reads 0 from the next cycle (from `UpdaterTop::running`).
3. The statistics' definitions as 17.10.1 states them, in particular which states' waits count in
   `STAT_STALL_MEMORY`, and that the counters count only while a run is in progress and not stopped.
4. **The decoder (17.10.2):** a refill only when the next code's bits are not all in, judged from its first two bits,
   and only while `WORD_COUNT`, less a word being decoded, is not 0; a header word goes into the LRU table as a
   value does; a value's type from *n* = min(the run of values' remainder, `WORD_COUNT`).
5. `ready` as "the input queue, after the cycle's take and refill, holds fewer than 4".
6. **The fault's walk (17.10.5):** level by level from the top, the Tracer's copy before the image at each level; a
   source shared by both files at a level read once; the image's data page taken but held, the Tracer's copy's
   written from the source as it stands, before the image's is changed; below a hole, holes for an index page and
   zeros for a data page.
7. The pages a fault needs counted as depth − *L* for each file that must, before any is taken.
8. A flush: an owned page by its dirty halves (both, one, or none); a new page whole, its missing halves read first
   or zeros for a hole, then the update after the write's response.
9. "It never reads a page that a run will write whole, never writes a page it has not changed."
10. 17.10.6: one read burst and one write burst at most in flight; the page memory's one port never asked twice in a
    cycle because a run waits for each burst before it patches.
11. Depth 0: the root is the data page, owned, held at once; a page number ≥ 1 is `beyond`.
12. An error: the run stops with `busy` 1, takes in what is in flight, and sends nothing more; the decoder stops.
13. The reset, as 17.10.10 lists it.
14. 30.4's table of the runs, and 29.7's pages each of frame 9's runs takes (twice the Tracer's copy for segment *s*;
    the final image's less the copy's after it).

## What I ask of you

Two lists, each item with the section and the line of code:

1. **Where the specification misdescribes what your code does:** a state, a transition, a width, a cycle, an error,
   a count, an omission that matters to a builder.
2. **Where the specification is right and your code is wrong or silent:** a rule it states that your code does not
   keep or check, or a case your code does not handle.

Please do not edit `spec/`: put both lists in `sessions/SPEC/REVIEW-WORKER6-ANSWER.md`, and the specification session
applies them.
