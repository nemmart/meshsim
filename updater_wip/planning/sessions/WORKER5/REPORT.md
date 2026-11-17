# WORKER5 report: the interface documents, and the cache's controller

Branch `updater-w5`. The plan and the choices are in `NOTES.md`; the questions and the owner's answers in
`QUESTIONS.md`. Paths without a folder are in `v1.37_wip/`. AI is `AXI-INTERFACE.md`, CI `CACHE-INTERFACE.md`, UI
`UPDATER-INTERFACE.md`, all in `v1.4_wip/planning/`.

## Part A: the interface documents (2026-11-16)

**Done and agreed** (owner, 2026-11-16; merged into `main` at `e59ae24`). `AI` (a client's AXI port, what a client may
count on, the arbiter, the controller's port with IDs, the LPDDR model) and `CI` (the look-up port, the three
operations and every reply, an update's sequence, the fill port unchanged, the cache from outside, timing, the
stand-in), with their headers `src/units/axi_port.h` and `src/units/lookup_port.h`. The owner's one change, made on
main: a client has one number everywhere, the look-up port's numbers being the AXI IDs. Q1 and Q2 as proposed; Q3
open, nobody discards in v1.37.

## Part B: what was built

| | where | what |
|---|---|---|
| the LPDDR model | `src/units/lpddr_model.h`, `.cpp` | AI sections 4 and 5: the file store's pages as its memory; IDs; a read's first beat 70 cycles after its address, a write's response 70 after its last beat; R round robin among the IDs that are due; 16 bursts each way; the jitter mode; a test's faults and `DECERR` past the end; every rule it relies on checked |
| the AXI arbiter | `src/units/axi_arbiter.h`, `.cpp` | AI section 3: round robin, reads and writes apart, a burst cut into pieces of 32 beats and seen whole by its client (one `rlast`, one response), one write piece at a time, a client's ID, 4 bursts a client each way; every output a register; the RW engine's adapter |
| the cache memory | `src/units/cache_memory.h` | the tag memory (512 rows: 8 ways of valid, dirty and a 22-bit tag, and 8 ages of 3 bits) and the data memory (4,096 rows of 1,024 bits, 8 lane enables), each one read port and one write port, a read's row the next cycle |
| the cache's controller | `src/units/cache_ctl.h`, `.cpp` | CI, in the shape of hardware: the front end, the walker, the miss handler, the AXI writer, the update slot, the page sequencer, the replies; step 1 and step 2 (`hits_under_miss`); the stale-line check |
| the three as one, on the Tracer's fill port | `src/units/cache_system.h`; `src/units/tracer.h`, `.cpp` | `Tracer::FillPort` gains a polled response and a `tick`; unset, the Tracer runs as before |
| the setting | `tests/mach.h`, `mach.cpp`, the harnesses | `MachConfig::cache_ctl` (`cacheCtl`, off by default, with `host_link`); `proof_run --cache`; `tracer_block --fill cache`; `TRACER_CACHE=1` for the synthetic guest; `CACHE_CTL=1` for `run_tests.sh`; the cache's figures (`tests/cache_figures.h`) |
| the tests | `tests/test_lpddr_model.cpp` (11), `test_axi_arbiter.cpp` (10), `test_cache.cpp` (11), `cache_ref.h` (the stand-in), `axi_test_client.h`; `test_master`'s `master_cache_*` (6) | below |
| the documents | `CHANGES-v1.37.md`, `RUNNING-v1.37.md` ("On the cache's controller"); `run_tests.sh`'s steps `cache`, `cache-synth`, `tracer-block-cache`; `updater_wip/tasks/v137-check/`'s item `suites-cache` | |

### How a cycle reads

Each unit keeps its state in registers, among them its state machine's state; its outputs are a function of its
registers alone (`out()`); one function advances a clock (`edge(t, inputs)`), reading the registers as they stood at
the cycle's start and the other units' outputs of the cycle, and writing the next registers. No unit reads another's
insides. A memory is read and written only through its ports, at most one read and one write a cycle each (a second
stops the simulator), a read's row at its output from the next cycle. `CacheCtl::edge` reads top to bottom as the
hardware does in a cycle: the miss handler's state's work, the AXI port's handshakes and beats, the update slot's
response, the walker's one step, the requests taken at the ports, then the next outputs and the memories' edge.
`CacheCtl::trace` prints a line a cycle. A designer should be able to draw each part from its state's case.

## The checks and their results

| check (the brief's) | where | result |
|---|---|---|
| **the cache alone:** every operation and every case of a reply, on a miss and a hit; updates (every case, LPDDR holding the entry when the reply is on the bus, the reply after the write's response however slow LPDDR is, one update in flight, a look-up of the line meanwhile seeing the new entry); the fill port (every status, a write dirty and allocated without a read); a miss, an eviction, a write-back; a discard with clean, dirty and in-flight lines; an update beside a fill of the same line; LPDDR errors of every kind (a look-up's fetch, an update's walk and its write, a fill read's line: `beyond` and the flag, a fill write's walk, a write-back's: the flag); the handshake's rules; an idle cache's figures | `test_cache`, here and on the runner; both steps | **pass,** 11 tests |
| **random traffic against a reference with no cache:** three look-up clients on files of their own, sharing a frozen file, and the fill port reading and writing a file of its own; 24 seeds: both steps, with and without jitter, and half of them with every page in the same 32 sets (evictions, write-backs); a phase of discards of scratch pages | `test_cache` | **every reply equal to the stand-in's throughout, and LPDDR equal at the end** (the cache's dirty lines swept out, the discarded pages left out). About 1,100 look-ups and updates and 600 fill requests a seed; up to 25 write-backs, 120 discards, 700 replays and 1,300 requests served under a miss |
| **the arbiter alone:** a line's latency through it (80 cycles to its last beat); a page cut into 8 pieces and seen whole; writes with one response; fairness (three clients' pieces strictly in turn); the cap (a line passes a page; with no cap it waits longer); a line's write passing a page's; the limit of 4 bursts; an error in one piece; random reads and writes with jitter; the RW engine through its adapter, a write job and a read job | `test_axi_arbiter` | **pass,** 10 tests |
| the LPDDR model alone | `test_lpddr_model` | **pass,** 11 tests |
| **the Tracer on it, alone:** the three real segments (EVM Zone 0 and 1, Cyclone 0), at compression 0 and 7, every run against gold's files; the synthetic guest, eight seeds, against its interpreter | `tracer-block-cache` (Quest 210); `cache-synth` (here, 209, 210) | **6 of 6 and 8 of 8 pass** |
| **the Tracer on it, `segment-real`:** its three segments from packet files, every row and delivery against the recorded files | `proof-real` with `--cache` (Quest 210) | **3 of 3 pass** |
| **the whole chip:** `proof-real`, `proof-matrix` (24) and `proof-29` (10) with the setting on | Quest 210 | **37 of 37 pass:** every image against gold's, the Tracer's last record against record s + 1, the cache's error flag clear, no LPDDR error, no read of LPDDR nothing wrote |
| the guests on the cache | `test_master master_cache` (6 runs), here and on the runner, also with ASan and UBSan (gcc debug) | pass |
| **with the setting off:** `./run_tests.sh all --check` | Quest 209 | **pass:** every v1.36 step against its log, v1.37's steps, the 37 proof runs with the same cycles as WORKER3's Quest 204, to the cycle (e.g. EVM Zone 2²⁹ s 0, 26,784,256); one step SKIPPED, `fw-build` (no xPack toolchain on the runner), as before |
| `./run_tests.sh quick --check` | here | pass; no golden log changed |
| `make check`: every test, debug and release, gcc and clang, the TSan runs | Quest 211 | **pass,** 906 s |

The stale-line check (NOTES section 7) was on in every run above: no clean line in the cache ever differed from LPDDR.

**Quests 209 to 211 ran `updater-w5` at `21197e0`.** Then `main` was merged in (WORKER4's threshold in v1.37's
Ingress and the master tile's firmware; two conflicts, in `proof_run`'s options and `CHANGES-v1.37.md`, resolved in
`336724a`), and **Quest 212 ran `suites-cache` again at `336724a`: all pass,** the 37 proof runs and the six Tracer-alone
runs on the cache, every run's error flag clear. `./run_tests.sh quick --check` passes here on that tree
(`test_master` 35 tests, the six on the cache among them). What else came after `21197e0` is documents and
`test_cache`'s eleventh test, the idle figures.

## The figures

### An idle cache (`test_cache cache_idle_figures`; the fixed LPDDR model)

| | cycles, handshake to answer |
|---|---|
| a look-up of a depth-2 file, both index lines in the cache | 6 |
| the same, both missing | 173 |
| an update whose path is in the cache: the walk, the line's write and its response | 87 |
| a fill read of a depth-3 file, all four lines in the cache | 10 |
| the same, all four missing | 351 |
| a fill write that hits | 9 |
| a fill write that allocates (no LPDDR read) | 17 |
| a line through the arbiter from LPDDR, address to last beat (`test_axi_arbiter`) | 80 |

A hit costs 2 cycles a level (the tag read, then the compare with the data read); v1.37's model charges 6.

### The Tracer's traffic (Quest 210)

| run | memory index hits | memory data hits | map index | map data | evictions | write-backs | fill read, mean (max) | fill write |
|---|---|---|---|---|---|---|---|---|
| EVM Zone 23, s 0 (Tracer alone) | 99.93% | 68.3% | 99.93% | 83.0% | 658 | 295 | 30.6 (261) | 7.0 |
| EVM Zone 23, s 1 (Tracer alone) | 99.91% | 55.7% | 99.95% | 65.4% | 4,659 | 1,271 | 50.0 (261) | 7.0 |
| Cyclone 22, s 0 (Tracer alone) | 99.94% | 64.4% | 99.96% | 83.6% | 6,784 | 4,018 | 47.1 (344) | 7.0 |
| EVM Zone 2²⁹, s 42 (chip) | 99.99% | 98.3% | 99.66% | 43.5% | 6 | 0 | 10.4 (261) | 7.0 |
| Cyclone 2²⁹, s 0 (chip) | 99.96% | 45.9% | 98.5% | 23.4% | 8,862 | 8,574 | 153.3 (344) | 7.0 |

`cache-figures.txt` of Quest 210 has every run's. The walks: two index lines for the memory file, one for the map's
(depth 1). Index lines almost never miss: 16 to 38 misses a segment, the trees' few lines. Nothing was replayed and
nothing served under a miss in a chip run: the Tracer raises a write-back only when no read is outstanding (17.9.5),
so its two channels never overlap, and step 2 earns nothing until a look-up client shares the cache.

### The cycles against v1.37's model

From the tracer tile's load to the windows' landing (the master's stamps; Quests 209 and 210), and from `START` to
the rows' end for the Tracer alone:

| | model | cache | |
|---|---|---|---|
| Tracer alone, EVM Zone 23 s 0 | 20,236,671 | 20,169,482 | −0.33% |
| Tracer alone, EVM Zone 23 s 1 | 18,725,980 | 18,743,353 | +0.09% |
| Tracer alone, Cyclone 22 s 0 | 19,837,346 | 19,976,025 | +0.70% |
| `proof-real`, 3 runs | | | −0.16% to +0.91% |
| `proof-matrix`, 24 runs | | | −0.52% to +4.39% |
| `proof-29`, 10 runs | | | −2.71% to +3.06% |

Why they differ, both ways:

- **Hits are cheaper:** 2 cycles a level against the model's 6, and a line from LPDDR about the same (80 against 80).
  A segment that hits, EVM Zone 2²⁹ s 42 with 98% of its data lines hitting, is 2.7% faster.
- **A dirty victim costs a write first:** the cache writes the victim back in its place and waits for the response
  before the fetch (owner: no list of pending writes), about 80 cycles more on such a miss (344 at most, against 261
  for a clean one). The model kept every set's next victim clean ahead of time, so its fetches never waited. A
  segment that writes much, Cyclone 2²⁹ s 0 with 8,574 write-backs, is 3.1% slower; Cyclone 22's s 3, 4.4%.
- **The cache stays warm:** the model went cold at each state write; the cache keeps its lines across the windows of
  a segment (they are all current), so fewer misses.

## The choices a hardware designer must still make

| | as built here | the alternatives |
|---|---|---|
| the memories' ports | one read and one write port each | one port each, a cycle more on every read-modify-write (the LRU on a hit, the dirty bit) |
| the replacement order | true LRU, 8 ages of 3 bits a set | a tree's 7 bits (pseudo-LRU); the victim then differs, nothing else |
| "write in flight", "being filled" | registers compared with the set and way: the update slot, the miss handler | bits in the tag memory, which would need a write for each response |
| a request that must wait | walked again from the top when it can go on (a replay) | its walk's place kept per port (registers a port), to resume |
| step 2's rule | a request that touches the miss's set waits | a compare per way against the miss handler's way only |
| a dirty victim | written back in place, the fetch waiting for its response (owner) | **written back early, in place:** when a set's least recently used line is dirty, write it back while it stays readable and marked in flight, so that the next miss there finds it clean. It needs no list (the owner's ruling) and wins back the 3 to 4% of the write-heavy segments |
| the line registers | three of 1,024 bits: the walker's, the miss handler's, the AXI writer's | two, if the writer sends from the miss handler's or the walker waits for it |
| the front end | round robin over the four ports and the fill port's two channels | the fill port first |
| the arbiter's outputs | registers: a cycle each way | combinational steering, a cycle less on a line |
| the arbiter's W path | one write piece at a time, a FIFO of two beats | addresses ahead of their beats; a store-and-forward buffer a client |

## What I would change in the documents

| document | change |
|---|---|
| `AXI-INTERFACE.md` | Open: **the controller's own limit of bursts outstanding can hold a line's address behind a client's pieces** (found by `test_axi_arbiter`: a client with more pieces at the controller than its limit holds the arbiter's AR register). The Updater's one page at a time (8 pieces) stays under 16, so it does not arise; say so, or let the arbiter hold a client's pieces back past a share of the limit |
| `AXI-INTERFACE.md` | section 5: the figures as built (a line 80 cycles to its last beat) are the document's; nothing else |
| `CACHE-INTERFACE.md` | section 7: the idle figures above in place of the guess; section 6, "Open 11": the early write-back in place is the cheap fix, and keeps to the ruling |
| `CACHE-INTERFACE.md` | step 2 earns nothing with the Tracer alone (its channels never overlap); it is for when the Updater's look-ups share the cache |
| `UPDATER-INTERFACE.md` | "Its other connections": cite AI and CI; `ERRORS.not_owned` is the cache's `not_owned` reply to an update, and `ERRORS.lpddr_error` also its `lpddr_error` reply (NOTES section 10) |
| `RTL-CHANGE-REQUESTS.md` | comments A and B: point to AI sections 3 and 4 (`ID_BITS` 2, one ID a client; turns of one piece of 32 beats). Comment E (the fill port's read status) is on main |
| the specification, later | 23.6 and 17.9.14 from CI and the figures here |

## What is not done

- **The RW engine is not on the arbiter on the chip** (NOTES section 11, B6): its adapter is tested on the arbiter
  alone. On the chip the memory tile keeps v1.36's slave in its tests and runs nothing in the proof runs.
- **`segment-real` itself is not on the cache;** its three segments are, from their packet files (`proof-real`).
- **`suite_tracer` is not run on the cache** (its logs hold the model's cycles); the Tracer alone is, on the real
  segments and the synthetic guests.
- **The Updater stays a black box** on the store, as the brief has it: the look-up port is exercised by the tests'
  drivers only, until WORKER6's controller joins.
