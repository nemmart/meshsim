# WORKER6 notes: the plan

**Agreed 2026-11-07 (owner's go-ahead), with these changes:** Fp128 reduces by folding (D14), no Montgomery: three forms
(`unsigned __int128` values; `__int128` column accumulators; explicit `_addcarry_u64` limbs; the third added because gcc
compiles the first form's 128-bit carry compares to branches). Credits agreed; the credit wheel's share reported apart.
Rates reported as offered and accepted; 0.35 is below saturation (~0.365, measured: below). The full matrix on one mesh
only; the scale runs on the (a)/(b) pair at the best layout and width, both mixes. Three repeats, median and spread.
128 meshes over 6 or 12 threads split unevenly (21-22, 10-11 per thread): the slowest thread sets the wall time.

**Saturation (local, deterministic):** offered 0.30/0.35/0.36/0.40 → accepted 0.300/0.350/0.359/0.368; with a 64-deep
injection queue, 0.35 has no refusals and a mean queue of 0.43-0.50 over 100 K and 400 K cycles (bounded); 0.36 starts
refusing. Quest task A repeats the sweep on the runner.

**Quest tasks:** 124 (the mesh), 125 (Fp128), 126 (D15's addition: throughput against N, footprint, ready marks by
load); bodies in `sim/bench/quest/`, assembled with `mk_task.sh`; `summarize.py` gives medians and spreads of a raw.txt.

---


Measurements to decide W3's API (D10), not a product. Everything in `sim/bench/`, on W1's structures, `sim/src/base/`
untouched (bench-only copies where a variant needs a different structure).

## Files

```
sim/bench/Makefile        plain make, reusing sim/'s flags and WRAP; targets: bench (gcc -O2), bench-all (the flag set
                          below), check (the debug build with ASan/UBSan, small sizes: digests equal, no allocation)
sim/bench/fixed.h         FixedRing<W>, FixedBucket<W>: W1's Ring and Bucket with the width a template parameter, same
                          method set (W1's question 2); and SoaBucket: dest ids in a uint32 array beside the words
sim/bench/mesh.cpp        the busy mesh, templated on the variant (one binary, every variant compiled in)
sim/bench/fp128.h/.cpp    Montgomery multiply (two forms) and the toy multiplier unit
sim/bench/common.h        timing, pinning (pthread_setaffinity_np from a CPU list), splitmix64, the digest
```

## 1. The mesh

8 × 8 tiles, one router type, XY routing, all as plain arrays carved from one `Block` per thread (allocated by that
thread, first touch).

- **Router:** five input `Ring`s (N, E, S, W, local injection), depth D (default 8, so the credit round trip doesn't
  throttle; D = 4 as a back-pressure check); five outputs (four links, eject), **one word per output per cycle**, each input
  at most one pop per cycle, round-robin per output over the inputs whose head routes there.
- **Flow control: credits** (the credit `Fifo` kind). An output holds D credits for the downstream input; a send takes one;
  the downstream pop returns it as a 1-word entry on a **separate credit wheel**, latency 3. So no ring is ever full (full
  stays fatal), and narrow deliveries get measured too.
- **Links:** latency 3; a send appends to the wheel bucket for t + 3. Wheel of 4 buckets (largest latency + 1), each sized
  by the rule (one put per output per cycle: 4 × routers).
- **Injection:** per tile, Bernoulli at rate r (per-tile splitmix64), random destination ≠ self, payload size by the mix;
  into the local ring if not full, else refused and counted. Rates: 0.1 and 0.35 words/tile/cycle (moderate, near XY
  saturation on 8 × 8 uniform traffic).
- **Mixes:** all 128-bit; and 32/64/128 in equal thirds.
- **Ejection:** the eject output folds (src, size, payload) into the destination tile's digest; in (a) it reads the pool
  and frees the index. The mesh digest combines the 64 tiles' digests; **every variant must print the same digest** for
  the same seed, rate and mix (checked in `make check` and in the tasks), and 128 meshes give the same digests on 1, 6 and
  12 threads.
- **The cycle (README's core loop), per thread:** (1) deliver: walk credit bucket t, then link bucket t: push the words onto
  the destination ring, set the router's ready bit (a `Bitmap` over the thread's routers); (2) inject (all tiles); (3) run:
  `for_each_set` over the ready bitmap, each router steps; (4) commit: clear bucket t, re-mark routers with words left.
  The measured loop runs inside `begin_run()`/`end_run()` (W1's hook, linked with WRAP): any allocation aborts.

## 2. The variants

| axis | values |
|---|---|
| link word | **(a)** 2 words: header, then the 32/64-bit data or a `ValuePool` index (alloc at injection, free at ejection; routers read only the header) · **(b)** 3 words inline, no pool |
| where the destination id goes | **E1** a word of the entry (W1's `Bucket`, width + 1) · **E2** a parallel `uint32_t` array (`SoaBucket`) |
| width | runtime (W1's `Ring`/`Bucket`) · compile-time (`FixedRing<W>`/`FixedBucket<W>`) |
| ready marks | bitmap set per delivery (the design) · step every router every cycle (polling), once, to price the mark |

(a)/(b) × E1/E2 × runtime/fixed × 2 mixes × 2 rates = 32 single-mesh runs; the ready-mark comparison on the best layout.
The header is packed only from what the bench needs (src, dst, size); the destination port id is never packed into the
header, since a transport must not own payload bits.

## 3. Runs and Quest tasks

Timing on the runner only, nothing else running; each task's header says "timing task, keep the box quiet", expected
minutes, and is safe to re-run. Embedded kit (`sim/src/base` + `sim/bench`), as Quest 122. Plain numbers claimed in
`NUMBERS.md` just before each push.

- **Task A, the mesh** (~10 min): build at gcc -O2; every variant, one mesh, one thread on CPU 0, 3 repeats (median),
  under `perf stat` (`cpu_core/` cycles, instructions, cache-misses, L1-dcache-load-misses); the best two variants
  also at -O3, -O2 -march=native, and clang -O2. Then **128 independent meshes** (each thread a contiguous share, one
  wheel and one ready bitmap per thread) for (a) and (b) on the best layout: 1 thread (CPU 0), 6 (CPUs 0,2,4,6,8,10: one per
  P-core), 12 (0-11, with HT); the E-cores separately (1 thread on 12, 4 on 12-15). `perf record -g` on the busiest
  (128 meshes, 128-bit mix, rate 0.35, 1 thread). Reported: ns per mesh-cycle, ns per hop (router traversals, ejection
  included), and aggregate mesh-cycles/s.
- **Task B, Fp128** (~5 min): see 4, same pinning and perf stat.
- A third only if the numbers raise a question.

Sizes: each run ~1-2 s after a warm-up (one mesh ~10⁶ cycles; 128 meshes ~10⁴); the counts come from a quick local
calibration.

## 4. Fp128

- **Modulus:** p = 2¹²⁸ − 159 (prime, checked) unless the owner names one (`QUESTIONS.md` Q1). Montgomery with R = 2¹²⁸,
  values kept in Montgomery form, inputs and outputs in [0, p).
- **Forms:** (M1) `unsigned __int128` throughout (four 64 × 64 → 128 products for the 256-bit product, then REDC); (M2)
  two 64-bit limbs, CIOS with explicit 64 × 64 → 128 multiplies (`__int128` or `_mulx_u64` + `adcx`/`adox` with
  -march=native). Each checked against a slow reference (bit-by-bit modmul) on 10⁶ random pairs. **Plus one line for
  reference:** the special reduction a 2¹²⁸ − c modulus allows (fold the high half × c), since if the real modulus is
  special that changes the answer; it doesn't replace Montgomery in the recommendation unless Q1 says so.
- **Per operation:** latency (one dependent chain) and throughput (8 independent chains), ns and cycles per multiply.
- **Unit-like loop:** K toy multiplier units per thread, each with an input `Ring` of operand pairs (two pool indices,
  width 2) and an output `Ring` of result indices (width 1). Per cycle: a unit with input pops a pair, multiplies
  pool[x] · pool[y], allocs the result's index, frees x, pushes the result; a feeder phase pairs each unit's results back
  into its input (result, y), so the values keep changing and the work can't be hoisted; the ready bitmap as in the mesh.
  The digest of the final values checks every form agrees. Reported: ns per multiply in the loop against the raw
  throughput, i.e. what the data movement costs. K = 64 × 128 units total (a tile's worth on 128 chips), on 1, 6 and 12
  threads, the E-cores separately.

## 5. The report

`WORKER6/REPORT.md`: the tables, and short answers for W3: the link word (a or b, by how much on each mix), fixed or
runtime width, the delivery entry's layout and where the destination id goes, what a ready mark costs against polling, how
a unit should hold and compute on 128-bit values (pool indices, which multiply form), and the -O3 / -march=native / clang
verdict.

## Open points (for the go-ahead)

- Credits as the flow control (rather than ref's per-feeder overflow FIFOs, which would need drops in a benchmark): OK?
- Any change to `sim/src/base/` I find useful goes here as a proposal; none so far.
