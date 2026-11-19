# WORKER3 (sim/): report

Branch `sim-w3`, merged into main (last: 9e126a1, the idle jump's cap); the final re-run on main a77ac03 (W4's form 2
and E5 digest). PLAN-M1 items 3-6: configuration, the engine, the port kinds (local forms), the debug checks; the unit
author's API first. Every detail, finding and decision is in `NOTES.md`; this is the summary.

## What was built

| file | what |
|---|---|
| `src/PORTS.md` | each port kind's exact rule from `ref`, with the class and lines it comes from; the conformance script format |
| `src/sim.h` | the unit author's API: the cycle, levels, wakes, payloads and widths, unit state, the debug checks; port handles, `Wake`, `Ctx`'s operations, `Ports` |
| `src/config.h` | building a machine: `Config` (add, wire, build), `Options`, `Sizes`, `Sim`; the wheel's sizing rule; mixed fidelity |
| `src/engine.h`, `engine.cpp` | the cycle loop: port pools, routes, the (kind, width) wheels and their delivery loops, run sets, wakes (next, the near wheel, the heap), fast-forward, the debug orders, step-all, the missed-wake detector, the leak check |
| `src/config.cpp`, `config_impl.h` | the build (levels, numbering, sizing carved from one block per thread, unit arenas and handle patching, `Sizes`), `Sim`; the internals in a header for W4 |
| `src/base/value_pool.h` | W1's ValuePool generalised to `SlotPool` (N-word slots) for by-reference payloads; `ValuePool` its 2-word view |
| `src/base/words.h` | the packing rule: line rounding between threads' structures, packing within one thread's block |
| `src/sketch/` | the three stage-1 sketches (a FIFO consumer, the controller's surroundings, a tile cycle- or high-level), compiled in every build |
| `tests/` | `test_engine` (16), `test_kinds` (12), `test_wide` (7), `test_registry` (2), `test_portconf` (17), `test_debug` (20), `test_forms` (2), two `SlotPool` tests in `test_value_pool`; `portconf.h`, the `portconf` tool; `tests/ref/PortConfGen.java`, `PortConfDriver.java`, `portconf.sh` (`--fixtures`, `--check`); the fixtures in `tests/portconf/` |
| `Makefile` | D18: `make` and timing builds clang release; `make check` gcc and clang, debug and release; `make conformance` |

## Stage 2's acceptance (PLAN-M1, W3; D20's additions)

| item | where it is tested | status |
|---|---|---|
| Port conformance against `ref`: one seeded script per kind drives `ref`'s bus classes (a Java driver in `ref`'s default package) and the C++ port; per-cycle observations identical over at least 10^5 cycles at several depths and latencies | `make check`: `test_portconf` over the 17 committed fixtures (seed 1, 2,000 cycles; `portconf.sh --check` regenerates them from `ref`); `make conformance`: 14 configurations x 3 seeds x 10^5 cycles, file by file | **pass** (42 scripts equal line for line; the comparison has teeth: a planted after-pop Fifo rule fails at its first differing cycle). The reserved Link (`WordBackpressureDelayLine`, 3 configurations) is M2's |
| Sizes equal to hand-derived ones | `test_engine` sizes_equal_hand_derived_ones (a ring, a wheel, the total as the sum of its lines); `test_wide` (a size class: 5 slots for a depth-4 Fifo, 7 for a latency-3 Link with 2 held) | pass |
| Overflow fatal | death tests: two puts in a cycle, a Link not taken, a put without a credit, a wake in the past (`test_engine`, `test_kinds`); a size class exceeded (`test_wide`); a slot leaked (`test_wide`) | pass |
| Loops rejected, with names | `test_kinds` a_combinational_loop_is_rejected_with_its_units (`x -> y -> x`), one_level_per_type_must_order_the_wires | pass |
| Digests stable across shuffle seeds and both orders | `test_debug`: W4's synthetic chip under seven option sets; W5's `golden_engine_debug_checks`: 67 goldens x six option sets | pass |
| Wire-discipline violations fatal | `test_kinds` a_wire_written_twice_in_a_cycle_is_fatal (debug); written after a read is checked too, but levels make it unreachable in a correct build | pass |
| An idle unit is never stepped | `test_engine` an_idle_unit_is_never_stepped (stepped once, at cycle 1) | pass |
| A 10^9-cycle sleep costs one wake | `test_engine` a_long_sleep_costs_one_wake (two steps; fast-forward) | pass |
| No allocation in the loop | every kind run under W1's hook (`test_engine`, `test_kinds`, `test_wide`); D24: every engine diagnostic fires under the hook with its own message (`test_debug`, 8 tests) | pass |
| A toy unit's two forms agree | `test_forms`: a 4-stage multiplier's cycle and result-only forms give the same results in the same order, also under the debug orders | pass |
| Step-all's digest equals the ready-driven run's (D20) | `test_debug` (the synthetic chip; a forgotten `next()` caught, the corrected unit agrees); W5's goldens under step-all | pass |
| The missed-wake detector: fatal on a forgotten `next()`, silent on units blocked on a credit or a refusal (D20) | `test_debug`: the death test naming the unit and port; silent on a credit-blocked and a refusal-blocked relay; a per-unit threshold for a wait on a Wire; report-and-go-on; W5's lazy block, under the hook | pass |
| The detector judges a suspect even when the machine goes idle (W4's finding) | `test_debug` an_idle_jump_does_not_skip_a_suspects_judgement (threshold 5, reported at its deadline; fails with the cap removed), an_idle_jump_still_jumps_when_no_suspect_is_due | pass (fixed: the idle jump stops at the earliest suspect's deadline) |
| The end-of-run report lists a planted sleeper (D20) | `test_engine` the_end_of_run_report_names_a_sleeper; `test_debug` report-and-go-on | pass |
| Options never silently ignored | `test_debug` an_option_not_implemented_is_refused (`threads`) | pass |
| Type ids per configuration (E1, for W4) | `test_registry`: first-add order per Config; three threads building at once (TSan-clean) | pass |

**Re-run with W4's E5 digest** (main a77ac03: W4's form 2 and E5, unit by unit in add order, wide payloads by their
contents, merged into `sim-w3`; no conflict): `make check` passes in every build (gcc and clang, debug and release; both
TSan builds), 21 test binaries each. The step-all comparisons: W4's synthetic chip digests the same under all seven option
sets (ready-driven, shuffle seeds 1-3, instance-major alone and with a seed, step-all); step-all still catches a
forgotten `next()` (`test_debug`) and W5's two broken blocks (CountingBlock: ready-driven e57594832d78cfaf against
step-all 739d8e39d4462446; LazyBlock: abc0c646391598ff against 10ccadefe6731367), and the detector still reports the lazy
block under the allocation hook; W4's `test_digest` passes. **W5's 67 goldens under the six option sets: 67 runs, 0
differ** (`golden --engine`, clang release).

## The performance pass (Quest 132)

Before 0c7df79, after 057a9d5, built and run in the same task, interleaved, 5 runs each, pinned to CPU 0. **Machine**:
Intel Core i5-14400F, 6 P cores (CPUs 0-11 with hyperthreads, max 4.7 GHz) and 4 E cores (CPUs 12-15, max 3.5 GHz), L2
9.5 MiB, L3 20 MiB, 64 GB, Linux 6.8, governor powersave, clang 18.1.3 (release -O2). All verdicts PASS (both builds and
their checks, the 67 goldens on the after engine under six option sets, the mesh digest 4a116005d9095712 in all ten runs).

| | before | after | change |
|---|---|---|---|
| controller (W5's bench_controller): ns per unit step | 20.43 | 20.37 | -0.3% |
| controller: instructions / CPU cycles | 54.7 G / 9.62 G | 43.7 G / 9.58 G | -20% / -0.4% |
| controller: IPC | 5.69 | 4.57 | |
| controller: engine symbols' share (perf, self) | 64.1% | 53.6% | |
| mesh (W4's framework mesh): us per mesh-cycle | 5.643 | 5.444 | -3.5% |
| mesh: L1d load misses | 165 M | 119 M | -28% |
| mesh: engine symbols' share | 18.3% | 14.4% | |
| mesh: footprint per chip (Sizes) | 286,848 B | 242,240 B | -15.6% |

The changes: delivery by a due mask, runs by mask, a byte flip per touched run at the end of a cycle, a near-wake wheel for
2-15-cycle wakes, the hot paths forced inline; slim rings and port headers (LinkPort 56 -> 40 B, Route 40 -> 24 B),
width-0 Links count-only, rings packed in the thread's block, debug arrays only when used. One regression found locally
and fixed before the runner: 16-bit ring fields made clang compile the wraps to branches (+13% mispredicts on the mesh).

## The state of PORTS.md and sim.h

- **PORTS.md** is complete for M1: the three classes the machine runs (`WordDelayLine`, `WordFIFO`, `WordHold`) and the
  rules `TileControl` and the others implement by hand (Reg, RegFile, Wire, Count), each with its `ref` lines; the credit
  Fifo; the reserved Link (documented, M2's); for authors, that an accepted Hold put wakes no one and a Count whose change
  must step a unit is an event; the conformance script format.
- **sim.h** holds the agreed operations, unchanged since stage 1 apart from additions approved as they came: `hash_fields`
  and `digest()` (a digest by unique object representation or `digest()`), `Ctx::alloc/slot/free` and `Ports::by_ref` (wide
  payloads; `values()` replaced), `put(RegOut, a, b, c)`, `credits()` meaning room as of the start of the cycle,
  `Config::missed_wake_after`, `Config::chip`, `Sim::type_of`, `Sim::sleepers`; `run_until(t)` inclusive. Every option is
  implemented or refused.

## What M2 should know

1. **The wheel's worst-case sizing** (config.h): every bucket holds one put per output, `W x outputs x (width + 1)` words,
   a quarter of W6's mesh. Two options named there: a wheel per (kind, width, latency) (Σ L per output, still nothing can
   overflow), or capacities from a declared peak rate (fatal when exceeded). Decide on M2's numbers; `Sizes` reports the
   wheels apart.
2. **A Link that can hold up to a declared depth.** A Link's consumer must take each word as it arrives (capacity L + 2,
   as `WordDelayLine`), so a router copies every arrival into its own queue: words held twice (W4's footprint breakdown).
   A Link declared with a depth (visible words held until taken, up to it) would let the queue be the Link's ring. PLAN-M1's
   "Carried to M2".
3. **One level per (type, form).** A Wire chain through units of the same type is refused at build (named). If M2 has one
   (identical pipeline stages joined by Wires), split a type's run by level; runs are already keyed (level, type, form).
4. **The per-cycle dependency chain.** The controller's time held while its instructions fell 20% (IPC 5.7 -> 4.6): a
   simulated cycle is a serial chain of about 74 CPU cycles through the engine's bookkeeping in memory (the ready word, the
   next-cycle bit, the run masks, a stall's near slot). If it matters on M2's busy workloads, the fix to look for is
   general, in the common cycle's bookkeeping.
5. **The solo loop: set aside.** Stepping a lone busy unit directly would speed up only a one-busy-unit machine (a
   microbenchmark case) and add a second engine path to keep exact. Not to revisit unless M2's workloads show that case.
6. Also carried: the reserved Link (`WordBackpressureDelayLine`, for the memory port's landing); the cross-domain forms of
   every kind are W4's (the route already carries its class for a by-reference payload's copy).
