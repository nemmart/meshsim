# WORKER10 report (sim/, M2): framework speed

Branch `sim-w10`, merged into main throughout. Measurements on the runner (i5-14400F: 6 P-cores with 2 threads each,
4 E-cores; a P-core's L1d 48 KB, L2 1.25 MB; L3 20 MB; D21), clang release (D18), in-process repeats for every chip
measurement. The day-by-day record, with every Quest's numbers, is `NOTES.md` in this folder.

## The result: the full-size sort

M2Sort's counted span (L1's GO to L2's all-READY, **1,229,687 cycles**, setup excluded as in W7's baseline), the 64 x
65,536 uniform keys, the whole chip with W8's, W9's, W11's and W12's units, equal to `ref` at every fingerprint point
(W12). Seconds over the span; the median machine of each of 3 processes (min-max over the processes):

| revision (Quest 158, interleaved, 3 processes) | one thread | 6 threads by tiles | x ref |
|---|---|---|---|
| before D31 (ef9cd71, Quest 157's) | 28.74 s (28.56-29.06), 23.37 us/cycle | 5.26 s (4.83-5.28), 4.27 us | 18.2 |
| + W11: BigSram's running sum off when not fingerprinting (14195b9) | 28.27 s (28.01-28.65), 22.99 us | 5.30 s (5.29-5.33), 4.31 us | 18.1 |
| **+ W12: the register block writes only changed registers (d51b6e1)** | **24.89 s (24.76-25.63), 20.24 us** | **4.60 s (4.59-4.60), 3.74 us** | **20.9** |

**4.60 s at 6 threads, 20.9x `ref`; 12.5% off Quest 157's 5.26 s here (W12's change most of it, W11's ~1.6% on one
thread and none measurable at 6).** One thread 24.9 s (3.9x `ref`). The breakdown at 6 threads after D31 (us a cycle,
the `--time` run, 3.98): compute 3.662 (ideal 20.24 / 6 = 3.37: inflation 1.09x), imbalance 0.188, barrier 0.067,
drain 0.046, publish and clear 0.018. Resident set 241 MB (208 MB of it the big SRAM's touched pages). Digests equal
over every configuration within a revision; before and after D31 they differ by construction: BigSram's digest folds
its running sum only when it keeps one (W11; the 16K sort equal to `ref` at all 248 points with fingerprints on).
Against the ~4 s aim: **15% over**.

`ref` (W7, Quest 134): 95.80-96.01 s, 78 us a cycle, one P-core.

## What was built (framework; all additive, `config.h` / `sim.h`)

- **A Link that holds** (`Ports::in(LinkIn&, name, width, depth)`; capacity L + depth = `ref`'s
  `WordDelayLine(name, L, L + depth)`), `peek(LinkIn, k)`, `size(LinkIn)`; conformance against `ref` (57 scripts at
  10^5 cycles); PORTS.md 2b says exactly what the consumer sees.
- **The port observer** (`Sim::observe`, W8's request): a port's entries between runs, in flight with their arrival
  cycles, one domain or split; allocation-free.
- **The stop request** (`Ctx::stop`, `Sim::stop_cycle`) and **the between-cycles wake** (`Sim::wake`, W9's driver).
- **A Fifo overflow as an event** (`Ports::overflow_steps`, `Ctx::overflows`; W11, W12, D29).
- **The cheaper split**: declared domains grouped into one engine per thread (only connections between threads cross);
  `Options::engines` (measurement only) keeps W4's layout. 64 tile-domains on one thread now cost what one domain does
  (Quest 137: 15.82 -> 5.42 us, 103,552 -> 24,136 words).
- **Placement by load** (`Options::placement = by_load`, `Options::load`, `calibrate`, `Sim::load_profile`,
  `Config::weight`; `partition.cpp`): granules from what can't cross, recursive bisection with a balance phase, cut
  refinement and a k-way polish; crossings priced by `Options::crossing_ns` (0 by default: no gain measured).
- **The drain**: prefetched (Quest 140), then **direct delivery** of crossing Links from the source's outbox (Quest
  141; the exact capacity check kept in debug builds).
- **The flags barrier** (`Options::barrier`: automatic, the flags barrier from 3 threads; Quests 144-146; the
  crossover measured on this machine, to re-check on others).
- **Measurement**: `Options::time_threads` (each phase of a split run: compute, imbalance, barrier, drain, publish,
  clear), `Internals::regions` (each structure's memory and owning thread), and the benches and tools in
  `bench/w10/`: `bench_chip` (the boot, wfi, regs; --units, --windows, --trace-map), `bench_sort` (the sort over its
  counted span; placements, phases, units, windows, traces), `trace_filter` + `trace.sh` (footprints from a lackey
  trace), `annotate-body.sh` (perf's hot lines), `mk_task.sh` (the Quest kit, carrying every input the tests read).

## What the measurements say

- **The split costs little now**: at 6 threads on the 16K sort, compute inflation 1.02x, imbalance 0.19, barrier 0.08,
  drain 0.04 us a cycle against 2.92 (Quest 155); on the full size the framework's overheads are ~8% (Quest 157).
- **The barrier** matters only on light work (the boot: 27% at 6 threads; the flags barrier took it to ~15%); on the
  sort it is under 3%. A neighbour-only barrier was considered and not built (little gain on the target).
- **Placement** barely matters on the sort: tiles, rows and by-load are within a few percent; 6 threads (one per
  P-core) is the best count on this machine; 8 and 12 put two threads on a core.
- **The quiet case**: cost follows the steps taken, not the chip's size (Quest 148); the sort spends most of its time
  in four busy phases (93% of one thread's time at 16K) that parallelise ~5.5-6x at 6 threads.
- **Footprint**: per thread per interval at 6 threads the chip touches 8-47 KB (a P-core's L1d is 48 KB), the whole
  chip a few hundred KB; the wheels' worst-case sizing costs address space, not cache (left as they are); the big SRAM
  is backed only where touched (0 after the boot, 208 MB in the full-size sort).
- **Per-thread speed is the units'**: the framework's share of a step is ~15% on the boot and ~11% on the sort; two
  cuts to it were measured, gained nothing, and were reverted (Quest 149). The full size costs 1.56x a cycle more than
  16K on one thread: the units' memory traffic grows with the data. The units' hot lines (Quest 156) went to their
  owners: TileCtl's register writes and BigSram's running sum became D31, measured above.

## Left open

- **The ~4 s aim: 4.60 s, 15% over.** What remains is the units' own time at full size (BigSram's memory traffic, the
  router's queue store, TableStore and the read side; Quest 156's notes for their owners); the framework's overheads
  are ~0.32 of 3.98 us a cycle (8%), and 6 threads is this machine's best count.
- Huge pages for the big SRAM's blocks (TLB misses over ~200 MB of 4 KB pages): not measured; W11's code.
- The sort's footprint traces (`bench_sort --trace-map`, `trace.sh` with BENCH=bench_sort): the tool is ready, the
  traces not run.
- The flags barrier's crossover and every timing here are this machine's (D21): re-check on any other.
