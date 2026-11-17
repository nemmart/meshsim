# WORKER9 notes: joining the controllers

**Status (2026-11-17): step 1 built (the merge, the shim out); its runner items queued.** Branch `updater-w9`, from
`main` at `1e062236`. Step 1 is checked on the runner and handed to the integrator before step 2 starts (owner).

**The go-ahead (owner, 2026-11-17, 05:26):** "Go ahead with the plan as written."

1. `UpdaterTop::source` and `WordDev` go; the sink methods are the only way in.
2. Joined is `--cache` with `--updater-ctl`; the two together unjoined go.
3. Two proofs on the bench as described. The chip's version waits for a ruling on the firmware; the report says what
   it would take.
4. The xPack toolchain may be downloaded from its official release, verified against the release's published SHA-256
   before unpacking, kept outside the repository, its hash recorded here ("The toolchain", below).
5. **Added:** when step 1 is pushed and its runner items pass, stop and say so. The integrator merges that much into
   `main`, so that `main` has the forward count and the new header, before the joint.

Abbreviations: UI `UPDATER-INTERFACE.md`, AI `AXI-INTERFACE.md`, CI `CACHE-INTERFACE.md`, II `INGRESS-INTERFACE.md`.
"The black box" is `src/units/updater.*`; "the controller" is `updater_ctl.h` inside `updater_top.h`. Paths without a
folder are in `v1.37_wip/`. A dagger (†) marks a choice of mine that the owner may want otherwise; the four that need
an answer are in "Questions", at the end.

## 1. Piece 1: merging `updater-w7`, file by file

A trial merge of `updater-w7` (`4844852a`) into `main` was made and thrown away. Git reports two conflicts; the
real work is in files that merge cleanly and are wrong.

| file | git | how it is resolved |
|---|---|---|
| `tests/proof_bench.cpp` | conflict, twice | WORKER6's view kept (`updater_view::UpdaterView`), with WORKER7's message and checks: the view gains `input_words()` (the black box's 4 words; the controller's FIFO) in place of `segment_entries()`; the link's words are WORKER7's sum (read out by the DMA, forwarded, discarded), and `DMA_FORWARD_COUNT` 0 at the end |
| `tests/proof_run_main.cpp` | conflict, three times | WORKER7's options (`--threshold` gone) with WORKER6's `--updater-ctl` and `--updater-ctl-seed`; "behind the shim" out of the comment |
| `src/units/updater_top.h` | clean, wrong | every line marked `shim` goes, and `EVENTS` [2] reads 0 as in the black box. It gets `port_sink()`, `port_take(t, word)` and `settle(t)` (below). † `source` and `updctl::WordDev` go too: one way in |
| `src/units/segment_shim.h` | clean | deleted |
| `tests/ctl_bench.h` | clean, wrong | `Bench<U>` feeds both Updaters on the port as WORKER7's `updater_bench` does: both outputs of t before the step, the word given after it. `Options::port`, the segment queue's allowance and its credit go. `rest()`, the stream not yet decoded, reads the black box's 4 words and bits against the controller's FIFO and bits |
| `tests/standin_cache.h` | clean | `WordFeeder` goes with `WordDev`; its random `valid` moves into the bench, for both units |
| `tests/updater_view.h` | clean, wrong | `segment_entries()` becomes `input_words()`; the shim out of the comments |
| `tests/test_updater_ctl.cpp` | clean, wrong | the tests "against the black box" run on the port; the error case of a full segment queue goes with the queue. The rest as they are |
| `tests/updater_ctl_teeth.sh` | clean | the 14 bugs kept; each pattern checked to still apply |
| `periph_slot.*`, `peripheral.h`, `updater.*`, `ingress_egress.h`, `host_link.h` | clean | as merged: WORKER6's three dispatch lines beside WORKER7's wiring. The slot finds `UpdaterTop`'s two methods by its `requires` test, with no change |
| the firmware, its images and `MANIFEST`, gold, the vectors | clean | WORKER7's, untouched |
| `run_tests.sh`, `Makefile`, `tests/*.mk`, `tasks/v137-check/` | clean | as merged, read through; the step and item descriptions lose the shim |
| `CHANGES-v1.37.md`, `RUNNING-v1.37.md`, `README.md` | clean | brought up to date at the end |

**The two sink methods.** The slot takes `port_sink()` before any step of t, steps every peripheral, then calls
`port_take(t, word)` if a word moved, then `settle(t)`. The controller's `edge(t, in)` wants the word among its
inputs of t.

- **`UpdaterTop::step(t)`** takes every output of t, posts the events, steps the far side's edges, and keeps the
  controller's inputs. **`port_take`** adds the word. **`settle(t)`** runs the controller's one `edge`.
- **The controller is not changed.** It uses the port's input only as `valid && ready`, so a cycle with no move
  is exactly an edge with `valid` 0.
- The alternative is a second way into the controller, pushing a word after its edge. It breaks "one function a
  clock".

**Checked on the merged tree, before piece 2 starts:**

| check | where |
|---|---|
| the controller alone against gold; against the black box, the stores byte for byte, on gold's fixtures and the 90 random scenarios; the timing varied; the errors | `test_updater_ctl`, here |
| WORKER6's 14 planted bugs | `updater_ctl_teeth.sh`, here |
| WORKER7's tests: `test_ingress_link`, `test_packet_vectors`, `test_updater`, `test_master` | here |
| the whole chip with the controller on its test pieces: `test_master` with `MESHSIM_UPDATER_CTL` 1 and a seed | here |
| WORKER7's 20 planted bugs (`fw_teeth.sh`) | needs the pinned xPack toolchain, which is not on this machine or the runner: Question 4 |
| `./run_tests.sh quick --check` | here, gcc and clang |
| every setting off: `suites` and `check`. **The 37 proof runs' cycles against Quest 220's, to the cycle** | the runner |
| the proof runs with the controller on its test pieces: `suites-ctl`, `suites-ctl-varied` | the runner |

## 2. Piece 2: the wiring

```
 master tile's slot                                 tracer tile's slot
 ┌──────────────────────────┐                      ┌────────────────────────────────────────────────┐
 │ Ingress ──word port──►   │                      │ Tracer ──fill port──┐                          │
 │          UpdaterTop      │                      │                     ▼                          │
 │            lpddr ────────┼──── the joint ───────┼──► arbiter port 1   cache (port 0) ─► arbiter  │
 │            cache ────────┼──── (a cycle's  ─────┼──► look-up port 1 ──┘                   │      │
 └──────────────────────────┘      latch)          │                               the LPDDR model  │
                                                   └─────────── CacheSystem, on the one store ──────┘
```

- **One `CacheSystem`, as today:** the cache on the arbiter's port 0, one LPDDR model on the file store's pages.
  Its `tick(t)` now gives the arbiter client 1's outputs and the cache port 1's request.
- **`UpdaterTop::lpddr` and `UpdaterTop::cache`** get the two halves of a new piece, `src/units/updater_joint.h`:
  an `AxiDev` on the arbiter's client port 1 and a `LookupDev` on the cache's look-up port 1.
- **The two sides are stepped by two tiles.** The Tracer's step ticks the cache system every cycle; the master
  tile's slot steps the Updater when it is not asleep. The machine is one thread with the host link on.
  - **The joint is a latch for one cycle.** Each side, at its step of t, writes its own outputs of t into the
    latch, and reads the other's from the latch if the other has stepped t, and from its registers if not.
  - **So the order of the two tiles in a cycle does not matter.** The bench runs every test in both orders and
    wants the same cycles.
- **The slot stays awake** while anything of the Updater's is in flight: the joint's `quiet()` reads the
  arbiter's lists for client 1 and the cache's port 1. Two small accessors are added to `axi_arbiter.h` and
  `cache_ctl.h`.
- **The test LPDDR and the stand-in cache stay,** for `test_updater_ctl` and for the Updater's controller alone in
  the chip.
- **Not edited:** the documents, `axi_port.h`, `lookup_port.h`, `word_port.h`.

**The joint's checks,** a simulator's and not hardware, as WORKER5's stale-line check is. They are what CI
section 3 asks of a client, checked where the two meet; the test pieces checked the first for WORKER6.

| check | CI, section 3 |
|---|---|
| no update names a page whose write by the Updater is not yet answered | the data is in LPDDR before the index points to it |
| the Updater writes no page of which the cache holds a line | an owned index page changes only by `update`; a page the Tracer reads is not written directly |
| the Updater reads no page of which the cache holds a dirty line or a line in flight | a page the Tracer has written is not read directly |
| at an update's reply, LPDDR holds the new entry | write through |
| one request outstanding, held until taken; a reply only for a client that has one | the handshake |

A check that fires is counted, and a run fails on any. WORKER5's stale-line check and the count of reads of LPDDR
that nothing wrote stay on in every run.

## 3. The settings

| setting | how it is asked for | the Tracer's memory | the Updater |
|---|---|---|---|
| none (the default) | | v1.37's model | the black box, on the store |
| the cache's controller alone | `MachConfig::cache_ctl`; `proof_run --cache`; `CACHE_CTL=1` | the cache, the arbiter, the LPDDR model | the black box, on the store |
| the Updater's alone | `proof_run --updater-ctl [--updater-ctl-seed S]`; `MESHSIM_UPDATER_CTL` | v1.37's model | the controller on its test LPDDR and stand-in |
| **both, joined** | `proof_run --joined [--joined-seed S]`; `MESHSIM_JOINED` (1, or a seed) | the cache, the arbiter, the LPDDR model | the controller on the arbiter's port 1 and the cache's port 1 |

- **† Joined is both settings on.** `--joined` is `--cache --updater-ctl`. Today those two together run the
  controller on a test LPDDR of its own beside the real one, two models on one store. That combination goes.
- **The seed** is the LPDDR model's jitter mode: a new field of `MachConfig`, 0 for fixed.
- All off by default.

## 4. The checks

**On a bench, with no chip** (`tests/test_joined.cpp`, a step `joined` in `run_tests.sh`): `ctl_bench`'s bench
with the controller on a `CacheSystem` through the joint.

| check | what |
|---|---|
| gold's five fixtures, at *s* 0, 20, 60, 63 | every image against gold's; against the black box, **LPDDR byte for byte** (with no Tracer the cache holds no dirty line) |
| the 90 random scenarios; frames back to back; runs cut inside a run of values | against the black box, run by run |
| the timing | fixed, and the model's jitter at several seeds; both orders of the two sides |
| **a second client at once** | a driver on the fill port reads a frozen file and writes a copy of its own while the Updater builds. Every reply against the stand-in (`cache_ref.h`); LPDDR equal at the end once the dirty lines are swept out |
| errors | the model's faults on the Updater's read and write, on a look-up's fetch, on an update's write: the bit set, the run stopped, what is in flight drained |
| the checks | the joint's, the stale-line check, AXI's rules: none fires |

**The whole chip:**

| check | where |
|---|---|
| `test_master`, joined, fixed and at a seed | here |
| **the 37 proof runs, joined:** every image as its run ended and at the end; `PAGE_COUNT` 0 at every `done`; the last record equal to record *s* + 1; no error bit; the cache's flag clear; no stale line; no unwritten read; no check of the joint | the runner: a new item `suites-joined` |
| the same with the jitter, three seeds † | the runner: `suites-joined-varied` |
| every setting off: `run_tests.sh all --check`, `make check` | the runner: `suites`, `check` |

**The Updater and the Tracer on the cache at once.** In the chip the Updater's third run overlaps the trace.
Counted, and printed with the cache's figures for each run:

- the cycles, and the requests, with a look-up port's request and a fill request both outstanding;
- the requests served while a miss waited, and the replays (WORKER5's counters; 0 in every chip run so far);
- the cache's writes that waited behind a page's pieces, and the reverse.

**Two proofs** (Question 3):

- **† On a bench, made:** the real Tracer traces a synthetic guest on the cache (WORKER5's `cache-synth`) while
  the Updater's controller builds a fixture's images on the same cache, arbiter and LPDDR, driven by the bench in
  the firmware's place. The Tracer's rows against its interpreter; the images against gold's.
- **In the chip, not made: it is not cheap.** The firmware starts the third run, hands over, and then waits in
  the barrier and for the tiles' results. A second proof's frames would have to be taken inside those waits: the
  frame loop and the barrier made one loop. With it go a packet file of two proofs, a second set of roots and
  images in the proof bench, and a rebuilt image. That is a change to WORKER7's firmware, so it waits for a ruling.

## 5. The planted bugs (`tests/joined_teeth.sh`)

| | the bug | where planted | what should catch it |
|---|---|---|---|
| J1 | the Updater on the wrong client port | the joint | the run's check that the arbiter's port 1 carried the Updater's bursts and ports 2 and 3 none |
| J2 | the Updater on the wrong look-up port | the joint | a reply that is not client 1's: the joint's check, and a wait that never ends |
| J3 | an update sent before its page's write response | the controller (WORKER6's first tooth) | the joint's check |
| J4 | the cache answers a look-up from a line an update has since changed | the cache: an update that changes LPDDR's line and not its own | the stale-line check |
| J5 | the arbiter grants two writes at once | the arbiter | the model's `wlast` rule, or an image |
| J6 | an update's reply before its write's response | the cache | the joint's check |
| J7 | the joint a cycle out: one side given the other's outputs of t + 1 | the joint | a handshake seen twice or missed: the model's checks, or a hang |
| J8… | one for each disagreement found | | |

Each must be caught on the bench and in the chip (`test_master`, joined).

## 6. What I expect to disagree

Each is a place where a piece has only ever met its own stand-in.

| | the case | why |
|---|---|---|
| E1 | **gaps inside the Updater's read burst** | the test LPDDR sent a burst's beats back to back, with jitter too. The arbiter cuts a page into 8 pieces, with a line of the cache's between them, and in jitter mode each piece has its own latency. AI section 2 says a client may not count on it; nothing has tested it |
| E2 | `wready` falling inside the Updater's write, between pieces | the same; the test LPDDR dropped it at random, so this one is likelier to hold |
| E3 | the Updater's W beats offered with, or before, its address | AI allows it. The arbiter has met only its own test clients and the RW engine |
| E4 | a cache write-back and a page write at once | "one write piece at a time" has never had two real clients |
| E5 | replays and the front end's turns with two real clients | `test_cache` covered them with random traffic; the Updater's look-up, update, look-up rhythm is new |
| E6 | the stale-line check | section 7 of WORKER5's notes argued that the black box cannot make a line stale. The same argument must hold for the controller's order of writes, and now for index lines the two share |
| E7 | a wait that never ends | the slot asleep with something of the Updater's in flight, or the cache system not ticked. The joint's `quiet()` is new |
| E8 | the `done` of segment *s*'s run against the Tracer's first fill | the Tracer's copy must be whole in LPDDR at that `done`. By the sequence it is: every write answered and every update replied before `done` |

For each one found: the case, which piece was wrong against which document or which document was unclear, the
fix, and a planted bug.

## 7. The figures

| figure | from |
|---|---|
| the Updater's cycles a page by kind, on the real cache and arbiter, beside the test pieces' and the black box's | `ctl_figures_a_page`, on the joined bench |
| a cache line's wait at the arbiter behind the Updater's bursts: the mean and the longest | new counters in the arbiter: a client's burst from its address to its last beat or response, less the 80 cycles of an idle line; apart for those raised with a burst of client 1 outstanding |
| the Tracer's cycles a segment beside the Updater's last run, against WORKER5's | the master's stamps, against Quest 210's |
| the whole run to the hand-over, and the master's time awake | against Quest 220's |

## 8. The order of the work

A commit and a push after each, `main` merged in before each, `git status` clean at each turn's end.

1. The merge, the shim out, the bench on the port, the tests and both sets of planted bugs. The runner: `suites`,
   `check`, `suites-ctl`.
2. The joint, the cache system's port 1, `test_joined` on the bench.
3. The setting in the chip; `test_master` joined; the overlap's counters. The runner: `suites-joined`,
   `suites-joined-varied`.
4. `joined_teeth.sh`; the two proofs on the bench; the figures.
5. `RUNNING-v1.37.md`, `CHANGES-v1.37.md`, `REPORT.md` with the disagreements and the lists for the documents.

**The runner.** The Quest repository attaches to this session with the token in my preferences. I will claim
numbers in `NUMBERS.md` as `PIPELINE.md` says and queue the items myself, unless the owner would rather the
integrator did.

## Step 1 as built

- **The merge** is `updater-w7` at `4844852a` into `updater-w9`, `--no-ff`. The two conflicts were resolved as section
  1 has it: `proof_run_main.cpp` takes WORKER7's options (`--threshold` gone) with WORKER6's two flags;
  `proof_bench.cpp` keeps WORKER6's view and takes WORKER7's message and checks, the view reading `input_words()`.
- **`updater_top.h`:** every `shim` line gone, and `source` with `WordDev`. `EVENTS` [2] reads 0, with the black box's
  depth 0 and no fault flag; region part 1 is undefined, as on the black box. `port_sink()` is the controller's
  `ready` register. **The cycle** is `step(t)` (the outputs of t taken, the events posted, the far side's edges
  stepped, the controller's inputs kept), `port_take(t, word)` (the word added to those inputs), `settle(t)` (the
  controller's one edge). Each checks the order and stops the simulator if it is broken: a word outside its cycle, two
  words, a step with the last edge not taken. **`idle()`** no longer looks at the port; the slot adds it.
- **The controller is unchanged** but for one comment, about the shim's overrun bit.
- **`ctl_bench.h`:** `Bench<U>::cycle()` is the slot's order for either unit (the port's outputs, `step`, the move,
  then `settle` if the unit has one). The port's `valid` comes and goes at random from `Options::valid_seed`, for the
  black box too. The stream not yet decoded (`rest()`) reads the black box's 4 words behind the port in place of its
  segment queue.
- **`test_updater_ctl.cpp`:** `port(seed)` became `timing(seed)`, which varies the test LPDDR, the stand-in and the
  port's `valid`. The comparisons against the black box vary `valid` too, now that both are on the port.
  `ctl_error_full_queues` became `ctl_error_full_queue` (the free page queue alone). Two tests are new:
  `ctl_port_holds_four_words` (both units take four words while idle, `ready` then 0, the reset empties them,
  `EVENTS` [2] reads 0) and `ctl_region_part_1_is_unmapped`. 24 tests.
- **`standin_cache.h`:** `WordFeeder` gone. **`updater_view.h`:** `input_words()` for `segment_entries()`.
- **The documents:** `CHANGES-v1.37.md` (the shim's row replaced by the sink's), `RUNNING-v1.37.md`, the task's
  comment.

**Checked here** (gcc release, on the merged tree):

| check | result |
|---|---|
| `test_updater_ctl` | 24 pass: gold's fixtures alone, fixed and varied; against the black box on gold's fixtures and the 90 random scenarios, the stores byte for byte; the cut halves (57); frames back to back; the errors |
| `updater_ctl_teeth.sh`: WORKER6's 14 bugs | **14 of 14 caught** |
| `test_ingress_link`, `test_packet_vectors`, `test_updater`, `test_master` | 43, 6, 23, 34 pass |
| `test_master` with `MESHSIM_UPDATER_CTL` 1, 3 and 7 | 34 pass each |
| `proof_run divs.frames`, *s* 20: off, `--updater-ctl`, `--updater-ctl-seed 9`, `--cache` | pass: 230,400, 231,424, 232,960 and 230,400 cycles |
| `fw/build.sh --check` with the pinned toolchain | 18 images equal to `fw/images/`, 5 to `tests/core-programs/elf/` |
| `fw_teeth.sh`: WORKER7's 20 bugs (`master:` 18, `host.h:` 2), with the pinned toolchain | **20 of 20 caught** (4 of them by the simulator's stop on an undefined case) |

### The toolchain

xPack `riscv-none-elf-gcc` 15.2.0-1, linux-x64, from the release's assets on GitHub
(`xpack-dev-tools/riscv-none-elf-gcc-xpack`, tag `v15.2.0-1`), 433,494,794 bytes. Checked against the release's own
`.sha` asset before unpacking: **SHA-256
`aaaa8060c914851a3e5ee1ba82cc3d6f80972f90638a05c6e823a37557a33758`**, `xpack-riscv-none-elf-gcc-15.2.0-1-linux-x64.tar.gz`:
OK. Unpacked in `~/toolchains/`, outside the repository; `riscv-none-elf-gcc --version` says 15.2.0.

## Choices

| | choice | why; the alternative |
|---|---|---|
| C1 | the controller's edge runs at the slot's `settle`, with the word that moved | the controller keeps one `edge` and is not changed; a second way in would push a word after the edge |
| C2 † | `UpdaterTop::source` and `WordDev` go | one way into the controller, the one the chip uses; keeping `source` for tests would leave a path the chip never takes |
| C3 | the joint is a one-cycle latch, written and read by both sides | the two tiles' order in a cycle then does not matter; stepping the Updater from the cache system's tick would take it out of its slot |
| C4 † | joined is both settings on | four combinations, as the brief lists; the fifth, two LPDDR models on one store, was an accident of two settings |
| C5 | the joint's checks are counted and fail the run, not `FATAL` | a planted bug is then reported with the run's other results; the stale-line check stays `FATAL`, as WORKER5 has it |
| C6 † | two proofs on a bench with the real Tracer; not in the chip | section 4 |

## Questions

1. **`UpdaterTop::source` and `WordDev` removed** (C2), so that the sink methods are the only way in?
2. **Joined is `--cache` with `--updater-ctl`** (C4), and the two together unjoined no longer run?
3. **Two proofs:** the bench run as described, and the chip's left for a ruling on the firmware?
4. **WORKER7's 20 planted bugs rebuild the firmware,** and need xPack `riscv-none-elf-gcc` 15.2.0-1. It is not on
   this machine, and the runner skips `fw-build` for the same reason. May I download it from its GitHub release
   (`xpack-dev-tools/riscv-none-elf-gcc-xpack`, `v15.2.0-1`, the linux-x64 archive; size not checked), or does the
   integrator run the 20 on the merged tree? The firmware's source is not changed either way.
