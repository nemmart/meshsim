# WORKER10 (sim/, M2): notes

Branch `sim-w10`. Framework speed (D26's two levers). **Go-ahead 2026-11-07** (owner), with the answers and the order below.

## What I read, and what decides the plan

- **Today a domain is an engine.** W4's split builds one W3 engine per domain and runs a thread's domains one after the
  other (`threads.cpp`: `doms[d].thread = d * T / D`), each with its own instance arrays, wheels, run sets, boundary
  units, proxies, outboxes, a drain and a `run_until` per interval. That is the +18% time and +52% memory at six domains
  on one thread (Quest 133), and why splitting finer costs.
- **The partition is the author's** (`Config::domain`), by bands of rows on the mesh: 8 rows over 6 threads gave two
  threads twice the work (6 threads 2.80x, slower than 4's 3.05x).
- **A Link's consumer must take each word as it arrives** (capacity L + 2, `WordDelayLine`'s default); `ref` has the
  3-argument form `WordDelayLine(name, L, capacity)`, which is exactly "a consumer that may hold". The framework's
  `LinkPort` already carries `cap`; nothing declares it.
- **The wheels are sized for every output putting every cycle**; allocated, not necessarily touched (a bucket is filled
  from its front), so what costs cache is the touched part. Measure before changing.
- **The aim**: ~3.25 µs per chip-cycle on the sort's chip. W4's framework mesh alone is 5.4 µs per mesh-cycle on one
  thread at rate 0.35, and the sort routes ~0.8 words per tile-cycle (AUDIT-M2 1.1), plus 64 controllers and the
  engines. So both levers are needed: roughly 4-5x from threads on 6 P-cores (+ hyperthreads), and more per thread.

## The plan

Order: baseline, then 3 (W9 needs it soonest), 1, 2, 4, 5. Each piece: its own commits and tests, then one Quest task
with **before and after built in the same task, interleaved, 5 runs, pinned from `machine.sh`, the machine recorded**
(D21); digests compared over every run; W5's 67 goldens on the engine under the six option sets, and `golden-m2`'s
references as W8/W9 make them runnable, unchanged. `make check` (gcc and clang; debug, release, TSan) on every commit.

### 0. The baseline (one Quest task, reused as "before")

W4's mesh (rates 0.35 and 0.05) and synthetic chip on `main`: one thread; split on one thread at 2/4/6/8/64 domains;
form 2 at 2, 4, 6, 8, 12 threads (P-cores first, then hyperthreads); `Sizes`; perf (IPC, L1d/L2/LLC misses). Adds to
`bench_threads`: **the mesh with one domain per tile** (64 granules: the finest split, the partitioner's input) and a
`--partition` choice (rows, tiles, auto).

### 1. A cheaper split: one engine per thread, domains as granules

- **A domain becomes a unit of placement, not an engine.** At build, domains are grouped into thread groups (piece 2
  decides which); each group is built as **one** engine by W3's build, so a crossing between two domains of the same
  group is an ordinary local port. Only crossings between groups get boundary units, proxies and outboxes.
- So any number of domains on one thread *is* the unsplit machine (cost 0 by construction), and the fixed cost is per
  thread, paid once. Determinism is W4's: a group is a coarser partition, already proven equivalent (the matrix).
- **Then the per-thread fixed cost itself**, profiled at rate 0 and busy: the drain walks only outboxes with entries (a
  mask, like the due mask), outbox buckets cleared by count, the status words only when a Hold or Count crosses,
  boundary units out of the instance arrays (they never step after cycle 1). The due mask kept on in split domains.
- Measured: split cost on one thread (now forced via several groups on one OS thread, kept as a bench option), form 2
  at 2-12 threads, footprint per thread and per machine.

### 2. Partitioning by load

- **Granules** are formed by the framework from the declarations: units joined by anything that can't cross (Wire,
  RegFile, Count) or that would shorten the interval (Fifo, credit Fifo, Hold, Reg, a Link below the target interval)
  are one granule (union-find at build). The target interval defaults to the largest latency that still leaves at least
  4 granules per thread (3 on the mesh and, per AUDIT-M2, on the sort's chip); `Config::domain` still works, as a
  constraint (units of one domain stay together).
- **Load per granule**, two sources: **measured** (the default): a calibration run of N cycles on one thread records
  steps per instance and, in calibration mode only, time per run call (rdtsc around each run function, not in the normal
  loop), giving a cost per step per type; load = Σ steps x cost, crossing traffic = puts per route. Or **declared**:
  a weight per unit or type, for a first run with no profile. The profile is a value the caller keeps and passes back.
- **Assignment**: recursive bisection of the granule graph in proportion to the threads on each side (6 = 3 + 3, 12 = 6
  + 6, any count), each cut refined by Fiduccia-Mattheyses to minimise crossing words under a balance bound; seeded,
  deterministic. Contiguous blocks fall out on the mesh. `Sim::partition()` reports each thread's load and crossings.
- Static over the run. The sort's load moves between phases; balanced on the whole run's totals first, and I'll measure
  per-phase imbalance on the sort's chip before proposing anything dynamic.

### 3. A Link whose consumer may hold up to a declared depth

- **Additive**: `Ports::in(LinkIn&, name, width, depth)`: the consumer may leave up to `depth` words visible; the
  capacity (in flight + visible, fatal when exceeded, as now) is L + depth, which is `ref`'s `WordDelayLine(name, L,
  L + depth)`; depth 2 is today's Link. `peek(LinkIn, k)` and `size(LinkIn)` added for a router that looks into its
  queue. The ring holds only visible words, sized to what can be visible. Nothing else in the engine changes (the
  missed-wake detector already sees a level bit).
- **Conformance**: `PortConfGen`/`PortConfDriver` (in `sim/tests/ref`, not `sim/ref`) gain `link L C`, driving
  `WordDelayLine(name, L, C)`, the consumer holding words at random within capacity; committed fixtures in `make check`,
  `make conformance` at 10^5 cycles x several (L, C) x 3 seeds.
- **Then**: W4's mesh rewritten on it (queues in the framework's rings, no copy per hop) as the measurement, and a note
  to W9 through the integrator.

### 4. Footprint (D15 as amended by D26)

- First the number that matters: per thread, **touched** lines over a run against allocated (`Sizes` beside perf's
  cache misses), on the mesh, the synthetic chip, then the sort's chip.
- **The wheels**: on those numbers, W3's option (i), a wheel per (kind, width, latency) with exactly as many buckets as
  needed, keeping "nothing can overflow"; I don't propose (ii) (declared peak rates, fatal) unless (i) isn't enough.
- **Event Links as counts** (width 0: a count, not five dummy words: ~14 KB on the mesh); **headers** (LinkPort, Route)
  slimmed where the measurements say; whatever the sort's chip shows next.

### 5. The per-cycle dependency chain

Only if it shows on busy workloads: perf on the mesh at several rates and on the sort's chip as it assembles (cycles
per simulated cycle against instructions). If it does, a general change to the end-of-cycle bookkeeping (the run masks
and next-cycle sets carried without a store and reload between cycles), never a lone-unit path.

### Reporting

`REPORT.md` with each change's before/after table (one thread and form 2 at 2-12 threads, the machine), the partitioner,
the Link with depth and its conformance.

## The owner's answers and the order (2026-11-07)

- Q1: declare by depth, capacity L + depth; the mapping to `WordDelayLine`'s capacity documented (PORTS.md 2b).
- Q2: new `Options` are fine (additive); exact signatures here (below), no wait for approval.
- Q3: the wheels decided on touched against allocated numbers.
- **Order**: the baseline (Quest 136, the "before" for every change); the Link that holds words; **the port observer**
  (W8's request, `sim-w8` NOTES.md "Request to W10"); **the stop request** (W9's timing runs); the cheaper split;
  partitioning by load; footprint; the chain. Commit after each piece passes its own tests, before the long checks.

## Signatures (the additions; every one additive)

**The Link that holds** (done, 73ec325): `Ports::in(LinkIn&, const char* name, unsigned width, unsigned depth)`
(capacity L + depth; PORTS.md 2b says exactly what a consumer sees); `const Word* Ctx::peek(LinkIn, unsigned k) const`,
`unsigned Ctx::size(LinkIn) const`.

**The port observer** (W8's request), `config.h`:

    struct PortEntry { Cycle at; Word w[3]; };    // at: a Link entry's arrival cycle (0 for other kinds); w: its words
    struct PortView {                             // one input port at the end of cycle c, the last cycle run (now() == c + 1)
        uint32_t n = 0;                           // entries written to the caller's buffer, in order
        bool put = false;                         // Fifo, credit Fifo, Hold, Reg: a put made at c, judged at c + 1
        Word put_w[3] = {};                       //   its words
        bool valid = false;                       // Reg: a value has landed (then n = 1); Hold: occupied (then n = 1)
    };
    // class Sim, between runs only (fatal inside a step or a run); reads only, allocates nothing; fatal if more than max.
    PortView observe(const LinkIn&, PortEntry* out, uint32_t max) const;  // visible words, then those in flight (in the
                                                                           // wheel, wherever the split put it), each with
                                                                           // its arrival cycle: ref's WordDelayLine order
    PortView observe(const FifoIn&, PortEntry* out, uint32_t max) const;  // visible entries; put: the put made at c
    PortView observe(const HoldIn&, PortEntry* out, uint32_t max) const;  // the held word if occupied; put as above
    PortView observe(const RegIn&, PortEntry* out, uint32_t max) const;   // the value if valid; put as above
    // Options: bool observe_arrivals = false;  keep each visible Link word's arrival cycle (a store per delivered Link
    //   word, nothing else; off: observe(LinkIn) is fatal while a word is visible). The handle is the one in the unit
    //   object (sim.unit<T>(name).port): the observer finds its domain from the handle's address.
Wire, RegFile and Count are already readable from their producers' and readers' units. Width-0 Links give `at` only.

**The stop request** (W9's timing runs), `sim.h` and `config.h`:

    void Ctx::stop();                 // the run stops at the end of this cycle (several requests: the earliest cycle)
    Cycle Sim::stop_cycle() const;    // the cycle a stop was asked in during the last run, kNoCycle if none
    // One domain: the run returns after that cycle (now() == stop_cycle() + 1). Split (form 2): every domain runs to the
    // end of the barrier interval holding that cycle, and the run returns there, every thread at the same cycle
    // (now() - 1 >= stop_cycle(), within one interval). The next run starts afresh (no request pending).

**The between-cycles wake** (W9's driver), `config.h`:

    void Sim::wake(const std::string& unit);   // between runs only (fatal during one; fatal for an unknown name): the
                                               // unit is stepped at the next cycle run, now(), exactly, in whichever
                                               // domain it sits; its own wakes are unchanged; twice before a run: once

**Partitioning by load** (piece 2, as built in 4089276), `config.h`:

    // Options:
    enum class Placement : uint8_t { declared, by_load } placement = Placement::declared;   // by_load: the framework
    //   forms granules and assigns them to threads (Config::domain, if used, keeps a domain's units together)
    const LoadProfile* load = nullptr;     // by_load: a calibration run's profile; null: declared weights, else 1 per unit
    unsigned min_interval = 0;             // by_load: the smallest barrier interval allowed; 0: the largest latency that
                                           //   leaves at least 4 granules per thread
    bool calibrate = false;                // time each step (a calibrating run loop, bound only when set)
    // struct LoadProfile { unit names, their loads (ticks), cycles; to_text(), static from_text(text) };
    void Config::weight(const std::string& unit, double w);   // a declared load, for a first run with no profile
    LoadProfile Sim::load_profile() const;                    // after a run with Options::calibrate

## Questions for the go-ahead (answered above)

1. **The Link's declaration**: a depth (visible words held, capacity L + depth) as above, rather than `ref`'s raw
   capacity? Depth reads naturally on the input and doesn't depend on the wiring's latency.
2. **Partitioning**: additive `Options` (`partition = as declared | auto`, the load profile, the target interval) and
   `Config::domain` kept as a constraint. Fine, or should the integrator see the exact signatures first?
3. **Wheels**: decided on the touched/allocated numbers in piece 4, as W3 left it?

## Log

- 2026-11-07: read the prompt and the framework; plan proposed (this file).
- 2026-11-07: go-ahead. Quest 136 (the baseline, main's framework, i5-14400F): one thread 5.39 us/mesh-cycle (0.05:
  1.18; synth 6.42); split on one thread rows 2/4/6/8 +4/+10/+19/+25%, **tiles 64: 2.9x time, 4.3x memory**; form 2 by
  rows 2/4/6/8 threads 1.85/2.96/2.76/3.39x, by tiles 6/8/12 threads 1.29/1.47/1.74x; every digest equal.
- 2026-11-07: the Link that holds (73ec325): conformance 57 scripts x 10^5 equal to ref, old fixtures byte-identical;
  test_link_depth 6/6. PORTS.md 2b: what the consumer sees, for W9 via the owner. Note for W9's judgement: ref's
  `Router` takes every arriving word at once (Router.java:125) into per-(output, feeder) overflow `WordFIFO`s of
  queue_depth (85), visible at t + 1, dropping on overflow; a Link that holds is one in-order queue per input, head-only
  take, fatal when full. Whether a router on it reproduces ref is W9's to judge; I see those two differences.
- 2026-11-07: the port observer and the stop request (c8601be); the cheaper split (f703da9): domains grouped into one
  engine per thread, `Options::engines` a measurement option only. **Both commits clean**, each on a clean checkout,
  build by build (this container, clang 18.1.3, gcc 13.3.0): gcc and clang, debug and release, 25 test binaries each
  (W5's 67 goldens included, test_golden); TSan gcc and clang: test_parallel, test_threads, test_topology,
  test_matrix, test_observe, test_link_depth (the last two now in `TSAN_TESTS`).
- 2026-11-07: sim-w10 merged into main through 2e016ea (owner). Placement by load (4089276): granules, recursive
  bisection with a balance phase and cut refinement, then a k-way polish; calibration times each step by a separate run
  loop (no cost when off). Mesh by profile: load max/mean 1.009 / 1.028 / 1.031 / 1.041 / 1.068 at 2 / 4 / 6 / 8 / 12
  threads, the digest equal at each. test_partition 7/7 (debug, release). The between-cycles wake `Sim::wake` (W9's
  driver), tests beside the stop request: one domain and split, exact cycle, digest equal.
- 2026-11-07: **38f1784 clean** (placement by load, calibration, the wake): clean checkout, build by build, gcc and clang
  debug and release (26 test binaries each) and TSan gcc and clang (7 binaries, test_partition added).
- 2026-11-07: **Quest 137** (the cheaper split, before 45b4cbf / after 2e016ea, interleaved; i5-14400F): one thread
  unchanged (mesh 5.418 -> 5.423 us; 0.05: 1.182 -> 1.197; synth 6.407 -> 6.431; instructions +0.02%): the observer's
  and the stop's branches cost nothing measurable. Split on one thread: rows 2-8 now 0-1% (was 3-20%), tiles 64 now
  5.42 us and 24,136 words (was 15.82 us, 103,552 words: 2.9x, 4.3x). Form 2 by tiles: 2 / 4 / 6 / 8 / 12 threads
  2.91 / 1.85 / 1.60 / 1.57 / 1.66 us (was 8.48 / 5.08 / 4.18 / 3.66 / 3.11): **3.38x at 6 threads** (rows 2.76x);
  synth 1.12 / 1.33 / 1.21 / 1.29x (was 0.62 / 1.06 / 0.98 / 1.29x). W4's layout kept measurable: 64 engines on one
  thread 15.61 us. Every digest equal across both revisions. The plateau (~1.6 us at 6-12 threads against 0.9 ideal) is
  the next target: the per-interval fixed cost (barrier every 3 cycles, the drain's copy, ~11% of instructions at 6
  engines locally) and imbalance (Quest 138 measures placement by load).
- 2026-11-07: **Quest 138** (placement by load against rows and tiles, 2-12 threads, every digest equal): **placement
  by load lost to the plain grouping by tiles** on the mesh: at 6 threads tiles 1.597 us (3.40x), profile 1.791 (3.03x),
  load 1.874, rows 1.964; at 8: tiles 1.566, profile 1.959; at 12: tiles 1.656, profile 1.776. Profile beats load (the
  measured loads help) but not tiles, though its balance by its own measure is 1.025 at 6. So the calibrated step time
  isn't what limits the run: the per-interval costs (drain per crossing word: profile 590 crossing words at 6 threads,
  its irregular blocks) and compute inflation are not in that load. Synth: clusters already the granules (interval 1),
  load/profile ~ tiles. Mesh at 0.05: best 0.61 us at 4-6 threads against 1.17 on one: fixed cost bound. Next: Quest
  139, Options::time_threads (2d19444, a measurement option) for the breakdown.
- 2026-11-07: **Quest 139, the breakdown** (Options::time_threads; the timer costs ~1%; us per chip-cycle, the shares of
  the timed runs times the plain wall). **The flattening point, mesh 0.35 by tiles at 6 threads: wall 1.603** = compute
  1.019 (ideal 5.371/6 = 0.895, so 0.124 of compute inflation, 1.14x) + imbalance 0.192 + barrier wait 0.117 + drain
  0.227 + clear 0.044 + publish 0.006. At 8: compute 0.949 (inflation 1.41x) + imbalance 0.230 + barrier 0.095 + drain
  0.235 + clear 0.053 = 1.571. At 12 (hyperthreads): compute 0.863 (1.93x) + imbalance 0.238 + barrier 0.098 + drain
  0.343 + clear 0.100. By profile at 6: imbalance halves (0.094) but drain + clear grow (0.434) and inflation (1.26x):
  why profile loses to tiles (Quest 138). Mesh 0.05 at 6: compute 0.291 (1.47x) + imbalance 0.083 + barrier 0.094 +
  drain 0.088 + clear 0.039 = 0.599. Synth (interval 1) at 4: compute 2.086 + imbalance 0.207 + barrier 0.338 + drain
  1.608 + clear 0.522 + publish 0.105 = 4.874: the drain is a third.
  **Decision: do the drain.** Drain + clear is the largest overhead after compute (17% at the mesh's flattening point, 44%
  on synth), and it is what makes the cut expensive (so a placement by load can't trade crossings for balance). ~23 ns
  per crossing entry at 6 threads: the lines are another core's, just written, so it is miss-bound, not
  instruction-bound. Step 1, cheap: overlap the misses (prefetch headers, then entries) - Quest 140. Step 2 if the
  drain is still large: deliver straight from the source's outbox (no copy; the destination reads each line once, at
  delivery). Then the placement's objective weighs crossings by the measured drain cost.
- 2026-11-07: **Quest 140, the drain's prefetch** (before d1f86dd / after a58144b, interleaved; digests equal): mesh
  0.35 by tiles at 6 threads: drain 0.223 -> 0.141, wall 1.594 -> 1.479 (3.63x one thread); at 4: 1.796 -> 1.726; at 8:
  1.572 -> 1.533; at 12: 1.655 -> 1.576. By profile at 6: drain 0.339 -> 0.218, wall 1.752 -> 1.615. Mesh 0.05: no
  change (drain ~0.085 at 6 with few entries: the per-interval walk over the incoming outboxes' buckets, not the
  entries). Synth: drain -10%, wall -2 to -5%. Left at the mesh's flattening point (6 threads, 1.479): compute ~1.02,
  imbalance 0.183, barrier wait 0.084, drain 0.141, clear 0.044.
- 2026-11-07: **Quest 141, drain step 2 (direct delivery, 15e1bd7)** against the prefetch (a58144b), interleaved, digests
  equal. Mesh 0.35 by tiles: 6 threads 1.470 -> 1.443 us, 8: 1.536 -> 1.466, 12: 1.576 -> 1.511 (2-5%). Drain + clear
  at 6: 0.183 -> 0.062, but compute 1.006 -> 1.103: **the cost moved into compute rather than vanishing** (the other
  core's lines are now missed at delivery instead of at the barrier), which confirms the drain is miss-bound. What is
  left of the drain (~0.06 at 6, 0.07 at rate 0.05) is the barrier's walk over the incoming buckets' headers.
  At 6 threads now: compute 1.10 (inflation 1.23x, delivery's misses included) + imbalance 0.21 + barrier 0.065 +
  drain 0.06 = 1.443.
- 2026-11-07: TSan on 15e1bd7 found a race in the phase timer (2d19444, a measurement option): fixed in 4bf2f54.
  Merging main (W8's and W9's chip) showed the observer counting a direct outbox's bucket twice when crossing Links of
  different latencies have outboxes of different windows (W8's 't0.dir.in'); fixed in f6a4e2a (each wheel and outbox
  over its own window), with a test that reproduces the exact abort without the fix. **f6a4e2a clean** (main merged):
  clean checkout, build by build, gcc and clang debug and release (36 test binaries each, test_chip and
  test_g1_replay included), TSan gcc and clang (the 7 thread tests, test_controllers, test_chip form2: tsan-chip).

## Rules for measurement (owner, 2026-11-07)

- **Every chip measurement uses in-process repeats** (bench_chip --reps: an untimed machine first, then R timed
  machines in the same process, the median reported with min and max), over several processes. A chip run is
  milliseconds: one run per process measures the clock's ramp, first touches and the workers' start (Quest 142, withdrawn
  by Quest 143). Mesh runs (bench_threads, 60,000 cycles, 60-300 ms) are long enough as they are.

## What carries over to the sort's chip (and what may not)

The benchmark that counts is the sort's chip at 16K (owner, 2026-11-07); until it runs, W9's chip as far as it runs
(the boot: 14,791 cycles of network and port-B traffic; wfi, regs). From then on every change is reported on it first,
the mesh second. Of today's findings (on W4's synthetic mesh and W6's synthetic chip):

- **Expected to carry over.** A crossing word costs a cache miss (~20 ns, moved but not removed by direct delivery), so
  the drain's cost per word and crossings against balance hold: a placement must weigh the words it makes cross
  against the imbalance it removes. Imbalance as the next overhead after compute (0.2 of 1.44 us at 6 threads on the
  mesh). The fixed cost per barrier interval (the barrier, the walk over the incoming buckets): the real chip's
  crossings are Links of at least 3, as on the mesh.
- **May not carry over.** The synthetic chip barriering every cycle and its drain at a third of its time: the real chip
  shouldn't barrier every cycle (every crossing a Link of at least 3). The mesh's uniform random traffic (its loads
  and crossings are symmetric; the sort's are not). Loads from step time alone: the sort's load moves between phases,
  so a calibration should cover the whole boot (and the sort, once it runs), not a window of it.
- 2026-11-07: **The placement objective prices crossings** (Options::crossing_ns, default 20 ns): calibration also
  counts each Link input's words (a branch per bucket, as the observer's), LoadProfile v2 carries them and the clock's
  rate; the k-way polish minimises the largest group cost (its load plus every crossing's price, paid on both sides).
  **bench_chip** (bench/w10/): W9's chip running the sort's boot (14,791 cycles), wfi (to its stop) or regs (to 824),
  one thread or split, by placement.
- 2026-11-07: **Quest 142, W9's chip first** (i5-14400F; digests equal across every placement). One thread: the sort's
  boot 0.948 us/cycle, wfi 0.878, regs 1.449. **On the chip, placement by profile wins clearly**: the boot at 6 threads
  0.328 us (2.89x) by profile, against 0.584 by tiles (1.62x) and 0.502 by rows at 4 (1.89x); at 4 threads profile
  0.358 (2.65x). wfi at 6: profile-20 0.448 (1.96x); regs at 6: profile 0.494 (2.93x). So the mesh's lesson (grouping
  by tiles beats placement by load) does not carry over: the chip's load is uneven (tile 0 with C&C, the boot's
  traffic), the mesh's uniform. **Pricing crossings: no measurable gain on the chip** (the boot at 6: 0.328 / 0.331 /
  0.332 at 0 / 20 / 60 ns; at 8: 0.421 / 0.404 / 0.393, a little), **and a loss on the mesh** (6 threads: 1.582 at 0 ns,
  1.724 at 20). Pathological placements, to look at: tiles at 8 threads on the boot (1.271 us, worse than one thread,
  though rows at 8 is the same grouping and 0.530) and load (1 per unit) at 6 and 8 (1.10, 0.91). The breakdown on the
  boot at 6 by profile-20: compute 0.188 (ideal 0.158) + imbalance 0.035 + barrier wait 0.077 + drain 0.029 + publish
  and clear 0.016 = 0.343: **on the chip the barrier, not imbalance, is the largest overhead** (a light chip: 0.95 us
  of work a cycle over 6 threads is 0.16, against a barrier every 3 cycles).
- 2026-11-07: crossing_ns defaults to 0 (owner). A Fifo overflow as an event (W11, W12; D29): Ports::overflow_steps
  steps the owner in the cycle an overflow is judged, Ctx::overflows reads the count; PORTS.md 3; tests.
- 2026-11-07: **Quest 143, the anomalies: Quest 142's placement differences were measurement artifacts.** A boot is
  14,791 cycles, ~7 ms split: one run per process measured the clock's ramp, first touches and the workers' start as
  much as the machine (142's per-repeat times for one configuration ranged 0.42-0.74 and 0.70-1.48 us). bench_chip now
  builds and runs an untimed machine first, then 9 timed ones in the same process. Measured so, **every placement is
  the same on the boot**: 6 threads tiles 0.329-0.330, load 0.327-0.339, profile 0.332-0.340 us (one thread 0.931-0.945:
  2.8x); 8 threads rows 0.416, tiles 0.415, load 0.410-0.413, profile 0.409-0.432. So the tiles-at-8 and load-1
  anomalies are gone, and **so is Quest 142's "placement by profile wins on the chip"**: withdrawn. The boot's load is
  even enough that grouping by tiles balances it; the sort's may not be, and that is where placement by load gets
  measured next. The breakdown at 6 (tiles, three processes): compute 0.179-0.184 (ideal 0.156: inflation 1.16x) +
  imbalance 0.026-0.029 + **barrier wait 0.082-0.105** + drain 0.025-0.029 + publish and clear 0.016 = 0.338-0.351:
  the barrier is the chip's main overhead (~27%), as planned next. At 8 (two threads share a core) imbalance rises to
  0.094. (Quests 136-141 ran 60,000 cycles a run, 60-300 ms: not affected; their medians stand.)
- 2026-11-07: **ae0d599 clean** (main merged at 4175824; the priced placement, bench_chip, crossing_ns 0, the Fifo
  overflow event, bench_chip --reps): clean checkout, build by build, gcc and clang debug and release (36 test binaries
  each) and TSan gcc and clang (the thread tests, test_controllers, test_chip form2).
