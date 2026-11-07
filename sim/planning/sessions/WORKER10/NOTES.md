# WORKER10 (sim/, M2): notes

Branch `sim-w10`. Framework speed (D26's two levers). The plan below waits for the go-ahead; no code before it.

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

## Questions for the go-ahead

1. **The Link's declaration**: a depth (visible words held, capacity L + depth) as above, rather than `ref`'s raw
   capacity? Depth reads naturally on the input and doesn't depend on the wiring's latency.
2. **Partitioning**: additive `Options` (`partition = as declared | auto`, the load profile, the target interval) and
   `Config::domain` kept as a constraint. Fine, or should the integrator see the exact signatures first?
3. **Wheels**: decided on the touched/allocated numbers in piece 4, as W3 left it?

## Log

- 2026-11-07: read the prompt and the framework; plan proposed (this file).
