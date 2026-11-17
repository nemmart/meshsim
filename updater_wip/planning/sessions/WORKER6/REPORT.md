# WORKER6 report: the Updater's controller

**Status (2026-11-17): built and checked here; the long runs are ready for the runner, not yet run** (the Quest
repository could not be attached to this session: "The long runs", below).

The plan, the state diagram, the order of the traffic and the choices are `NOTES.md`. Abbreviations as there: UI, AXI,
CI, the black box.

## What was built

All in `v1.37_wip/`, on branch `updater-w6`, `main` merged in (WORKER5's work included).

| piece | file | |
|---|---|---|
| the controller | `src/units/updater_ctl.h` | every register in one struct (`Regs`), the state an enumeration among them; `out()` from the registers alone; one `edge(t, in)`; the page memory a unit with one port. Header-only, as the RW engine |
| the shim | `src/units/segment_shim.h` | v1.37's segment queue as a word-port source. The controller knows nothing of it |
| the peripheral | `src/units/updater_top.h` | the control block's events and errors, the region, the window's checks, the ports wired. Every line that knows the shim is marked `shim` |
| the test pieces | `tests/test_lpddr.h`, `tests/standin_cache.h` | a test LPDDR (fixed, or jittered; faults; AXI's rules checked) and CI section 8's stand-in, with the checks of what CI asks of a client; the word feeder |
| the bench | `tests/ctl_bench.h` | one bench for both units; a frame file's plan of runs; the comparison |
| the tests | `tests/test_updater_ctl.cpp` | 22 tests, the step `updater-ctl` |
| the teeth | `tests/updater_ctl_teeth.sh` | 14 planted bugs |
| the setting | `tests/updater_view.h`; `proof::Case::updater_ctl`; `proof_run --updater-ctl [--updater-ctl-seed S]`; `MESHSIM_UPDATER_CTL` | off by default. The proof bench puts the controller in the black box's place when the machine is placed |
| the runner's items | `updater_wip/tasks/v137-check/`: `suites-ctl`, `suites-ctl-varied` | `run_tests.sh all --check` with the controller in the chip |
| the documents | `RUNNING-v1.37.md` ("On the Updater's controller"), `CHANGES-v1.37.md` (six rows) | |

**The shared files, kept small** (the owner's note): `peripheral.h` one line (the tag `PT_UPDATER_CTL`);
`periph_slot.cpp` three (an include, two dispatch cases); `run_tests.sh` two; the `Makefile` one (an include);
`proof_bench.h` two; `proof_bench.cpp` the view in place of the black box's members (about 15 lines, all in
`ChipProof`); `proof_run_main.cpp` two flags. **`MachConfig`, `mach.cpp`, `segment_bench` and `tile_ctl` are not
touched:** the controller goes in at the proof's `place()`.

**What the integrator does to join it to WORKER5's work:** give `UpdaterTop::lpddr` the arbiter's client port 1 and
`UpdaterTop::cache` the cache's look-up port 1 (the two interfaces in `updater_top.h`, `AxiDev` and `LookupDev`, are a
cycle's outputs and an edge); and for WORKER7's, delete the `shim` lines and give `UpdaterTop::source` Ingress's
forwarder.

## The checks and their results

| check | what | result |
|---|---|---|
| **alone against gold** | gold's five fixtures (`kin-raw`, `kin-lru`, `jumps-base`, `divs`, `kin-big`) on the word port, no shim, at *s* 0, 20, 60, 63: every image against gold's `frames-images`; `PAGE_COUNT` 0 at every done; no free page left | **pass** |
| **against the black box** | the same runs on both, the controller behind the shim, compared after every run: block 2, `ERRORS`, busy, the dones and the free page credits, the free page queue, **the stores byte for byte**, the stream not yet decoded, `STAT_PAGES_WRITTEN`, `STAT_PAGES_READ` (below) | **pass**, on the five fixtures at *s* 0, 20, 63 |
| **random streams** | 90 scenarios of four files each: depths 1, 2, 3; holes and shared pages at every level (a look-up stopped at a hole 1,425 times and at a shared page 1,646, at each of levels 0, 1, 2); runs of the Updater cut at random points, inside runs of values; `continue` across runs; raw and LRU; the Tracer's copy (1,707 pages written in its runs). Against the black box run by run, and both against a model of the files | **pass** |
| **the run ending inside a covered half** (the owner's note) | 51 such halves in the random scenarios; and a fixed case: cuts 1, 255, 256, 257, 511 into a page-aligned run of 512, and 1, 255 into a half-aligned 256, on an owned and a shared page, raw and LRU | **pass** |
| **frames back to back** | four frames in one stream on the port, as the forward count may offer the next frame early | **pass** |
| **the timing varied** | the fixtures, the random scenarios and the back-to-back frames again with the test LPDDR jittered, the stand-in at random and the port's `valid` at random, several seeds | **pass**, the same results |
| **errors** | an error answer to a read (the root's; a half's) and to a write (the root's; a new page's); `lpddr_error` and `not_owned` to an update; `lpddr_error` to a look-up; a word beyond; a page with `PAGE_COUNT` 0; a root off a page; a full free page queue; through the shim a full segment queue. Each sets its bit; the run stops (busy 1, no done); against the black box where it has the case: the same bits, `WORD_COUNT`, `PAGE_COUNT`, dones and store | **pass** |
| **the test pieces' checks**, in every run above | no read of a page never written; no update naming a page whose write is unanswered; no write to a page that is neither new, nor a root being made, nor an owned data page of the image; AXI's rules (4 KB, alignment, `wlast`, a request held until it moves) | **none fired** |
| **the whole chip**, setting on, shim in | `test_master` (35 tests: the fixtures, the thresholds, the links, the error path, the teeth, and WORKER5's cache runs) with `MESHSIM_UPDATER_CTL=1` and `=3` (varied) after `main`'s merge, and `=7` (varied; its 29 tests then) before it; `proof_run --updater-ctl` and `--updater-ctl-seed 9` on `divs` | **pass**. The firmware unchanged |
| `proof-real`, `proof-matrix`, `proof-29` with the setting | the runner's item `suites-ctl` (and `suites-ctl-varied`) | **not yet run** |
| **the setting off** | `test_master` 35 pass; `./run_tests.sh quick --check` here | see "The long runs" |
| `make check`, `run_tests.sh all --check` | the runner's items `check` and `suites` | **not yet run** |
| **debug builds** | `test_updater_ctl` with gcc's ASan and UBSan; gcc and clang release | **pass** (clang's sanitizer runtime is not in this sandbox) |
| **teeth** | 14 bugs (`tests/updater_ctl_teeth.sh`), each caught: an update before its B; owned bits kept; a half taken as covered; the type from `run_left` alone; the decoder taking a word early; the image before the Tracer's copy; the entry caught from the wrong beat; an LRU hit not moved; `WLAST` early; one half written of two dirty; `PAGE_COUNT` not counted; the credit every 64; a hole's page of ones; the FIFO taking a word when full | **14 of 14.** The early-taking decoder was caught only once the back-to-back test was added: before it, no test offered the next frame's words early, as no bench of v1.37 does |

### The free pages are taken in the black box's order

So the comparison is of the stores whole, byte for byte, and not only of the files. It holds in every run above.

### `STAT_PAGES_READ`, and a finding in the black box

The black box's "covered" test is `run_left` ≥ 256 alone (`updater.cpp`, `patch`). When a run of the Updater ends
inside a run of values, it takes a half as covered that this run does not fill, and skips its read. Its images are
still right (its page memory always holds the true page), but its `STAT_PAGES_READ` is one low for each such half.
The comparison adds them (the controller counts them as it reads, in a shadow that is not a register); in gold's
frames the case never arises, since a run ends where its runs of values end. The black box is not changed.

## The figures

The test pieces' timing fixed: the test LPDDR at AXI's defaults (70 and 70), the stand-in at CI's suggestion (80 a
line, 80 a write). **These are the test pieces' figures, not a cache's or an arbiter's:** the stand-in serves one
request at a time and caches nothing, so every look-up is a walk of 80 cycles a line.

**A page, by kind** (`test_updater_ctl ctl_figures_a_page`; a depth-2 file; the data and the free pages already there;
with `root` 2 the run's figures include the root's copy: a read and one or two writes):

| the page | cycles | the black box's | look-ups | updates | reads | writes | waiting on memory |
|---|---|---|---|---|---|---|---|
| owned: 1 value (a half read, written in place) | 571 | 437 | 1 | 0 | 1 | 1 | 559 |
| owned: a whole page (nothing read) | 1,010 | 868 | 1 | 0 | 0 | 1 | 489 |
| new, its index page owned: a whole page | 1,258 | 948 | 1 | 1 | 0 | 1 | 731 |
| new, its index page owned: 1 value (a half filled) | 876 | 437 | 1 | 1 | 0 | 1 | 731 |
| new, a hole at level 0: 1 value (a new index page too) | 1,288 | 853 | 1 | 2 | 0 | 2 | 1,140 |
| copied, shared at level 0: 1 value | 2,539 | 2,273 | 1 | 2 | 4 | 3 | 2,515 |
| copied, shared at level 0: a whole page | 2,650 | 2,368 | 1 | 2 | 2 | 3 | 2,119 |
| with the Tracer's copy, shared at level 0: 1 value | 3,941 | 3,361 | 2 | 4 | 3 | 6 | 3,912 |
| with the Tracer's copy, shared at level 0: a whole page | 4,452 | 3,872 | 2 | 4 | 3 | 6 | 3,912 |

- **A whole page of values costs 512 cycles of patches,** one a cycle; the rest is memory. The decoder kept up in
  every case: no cycle waited on data with the data there.
- **A burst a page:** a page read is one 256-beat burst, a half 128; the Tracer's copy's page is read once and
  written twice, its index pages likewise (N15). **A look-up a page,** two with the Tracer's copy; **an update for
  each page made,** after its B.
- **Where it waits: memory, almost entirely.** Each update is a walk and a write, about 240 cycles on the stand-in;
  each burst is the LPDDR's 70 plus its beats. A real cache that holds the index lines would cut the look-ups and
  updates to about a third.

**The runs of gold's fixtures** (`ctl_figures_runs`; the controller behind the shim, its cycles against the black
box's): the program image (513 words, two pages) 1,422 against 1,274; `kin-big`'s data image (3,167 words, eight
pages) 8,141 against 6,796; `divs`' segment *s* (four words, a page and its index page copied twice) 2,470 against
2,189. The controller is about 10 to 35% slower than the black box's plain costs, and up to twice on a single value
in a new page, where it fills a half (128 cycles) that the black box treats as free. **No cycle count here is a figure
of the design:** both sides' memories are stand-ins.

**On the chip** (`test_master`, the setting on): the guests' whole runs take about 0.5% more cycles than with the
black box (230,400 to 231,424 for `kin-raw` at *s* 0; 226,304 to 227,840 for `divs` at *s* 20), because tracing dominates and the link sets the images' pace.
The 2²⁹ figures come with the runner's `suites-ctl`.

## The long runs

**Not yet run.** The Quest repository could not be attached to this session (the session's permission check refused
it), and the brief says to claim a number in `NUMBERS.md` before writing a task. The tasks are ready to make, from
this branch's HEAD:

```
updater_wip/tasks/v137-check/make_task.sh NNN-w6-suites-ctl suites-ctl                # the chip on the controller, fixed timing
updater_wip/tasks/v137-check/make_task.sh NNN-w6-suites-ctl-varied suites-ctl-varied  # the same, its test pieces' timing varied
updater_wip/tasks/v137-check/make_task.sh NNN-w6-suites suites                        # the setting off: run_tests.sh all --check
updater_wip/tasks/v137-check/make_task.sh NNN-w6-check check                          # the setting off: make check
```

A kit made here unpacks and runs on this machine (`DRY=1`). `./run_tests.sh quick --check` with the setting off is
being run here as well (the result in `NOTES.md` when it is in).

## What a hardware designer must still choose

1. **The FIFO's depth** (4 words) and **the bit buffer's width** (194 bits; the shifter puts a word in at 0 to 65).
2. **The LRU table's move to front in one cycle:** a 32-entry shift of 64-bit words, about 2,000 flip-flops. A
   slower table would make an LRU hit take more than a cycle.
3. **The page memory's shape:** 256 × 128 with two 64-bit write enables and one port (here), or two banks; its read
   latency of 1, and the W path's prefetch (the RW engine's).
4. **Holes and empty roots from a constant on the W path,** and a hole's half filled in 128 cycles when a value
   needs it. A bit for each 64-bit word of the page (512 flip-flops) would make the fill free.
5. **Halves read lazily,** when first touched and not covered, and **a look-up for every page brought in.** Keeping
   the last index page's address would save look-ups; a real cache makes them cheap anyway.
6. **One burst at a time each way, and a look-up port with one request,** as the owner's sequence requires: nothing
   overlaps a page's write with the next page's look-up. Overlapping them is the obvious speed-up, and it needs a
   second page memory (owner: one).
7. **The type's comparator,** min(`run_left`, `WORD_COUNT`) against 256 and 512, both 32 bits.
8. **What a stopped run does with what is in flight:** here it sinks every beat and response and sends nothing more.
9. **The statistics' exact definitions** (UI marks them †): here `STAT_STALL_MEMORY` counts every cycle waiting on a
   burst or a reply, `STAT_STALL_DATA` the cycles a value was wanted and none was decoded.
10. **The address checks** (a root or a free page off a page, or past LPDDR's end, is `lpddr_error` before any burst):
    whether the hardware checks at all.
11. **Who discards a page** before it is reused (CI Open 3): not needed in v1.37, where nothing is freed.

## What I would change in the documents

**Do not edit `v1.4_wip/`** (the brief): these are for the owner.

**UPDATER-INTERFACE.md**
1. The decoder's type (N15 section 4): say that it counts only the values this run will deliver, min(`run_left`,
   `WORD_COUNT`), so that a run ending inside a run of values never leaves a half unread and part-written (the
   owner's note at the go-ahead).
2. Say that the decoder takes no word before a code needs it. Otherwise `continue` 0 loses the next frame's first
   word when the forward count offers it early.
3. What a run does after an error: it stops, `busy` stays 1, no `done` (WORKER3's report asked for this too); here it
   also sinks what is in flight and sends nothing more.
4. `beyond` is the Updater's own check, from the depth, before it writes the page it holds; the cache never sees such
   a request.
5. The order of a fault's pages: counted from the look-ups before any is taken, so `out_of_pages` takes none.
6. A free page queue entry's unit: the page's address in 128-byte units (WORKER3's report, still open).
7. `STAT_PAGES_READ` and `_WRITTEN` count bursts: pages and half pages. Say whether a root's copy counts, and that a
   half filled (a hole) is not a read.
8. "What the Updater keeps behind the port is its own": four words here, and the decoder's bits.

**CACHE-INTERFACE.md** (section 3, "What the cache asks of its clients"; the owner accepted both as document changes)
1. **The roots of v1.37:** the Updater writes a root whole (`root` 1 or 2) though the page did not come from the free
   page queue. It is new and named by no entry, which is what the ask is for: word the ask that way.
2. **Discards:** the Updater sends none in v1.37, since nothing is freed. For v1.4: who discards (Open 3).
3. An update answered `hole` or `beyond` cannot come to a client that looked up first: say a client may treat it as
   fatal. The controller stops the simulator on one.

**AXI-INTERFACE.md:** nothing failed. The Updater raises one burst at a time each way (pages and half pages) and
sends a write's beats back to back, as section 2 asks.

**INGRESS-INTERFACE.md and `word_port.h`:** say whether `valid` may fall without a word moving. Nothing forbids it, and
the controller does not depend on it (its tests drop `valid` at random); Ingress's forwarder will hold it while it has
words.

**Questions (`QUESTIONS.md`):** none. Nothing in the documents failed to work.
