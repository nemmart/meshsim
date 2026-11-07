# WORKER6 (sim/): report — the link word, widths, ready marks, and the cost of Fp128

Branch `sim-w6`. Harness in `sim/bench/` (`make`, `make all-builds`, `make check`); the plan and its changes in `NOTES.md`.
Runner results: Quest **124** (the mesh), **125** (Fp128), **126** (D15: throughput against N, footprint; pending).
All numbers are medians of 3 runs with the spread (max − min, % of the median); P-core runs pinned to CPU 0 unless stated.
Every variant's digests agreed in every task (verdicts in each task's `VERDICTS.txt`), so the variants compared do the same
work; `make check` (gcc and clang, ASan/UBSan) proves it locally and proves the loop allocates nothing (W1's hook fires).

## Recommendations for W3

1. **Link word: (b), three words inline.** On speed it is a wash: on one mesh (b) is faster (median 5.7% over the 16
   cells of T1, range 0.4-9%, 0-4.6% fewer instructions); with 128 meshes on a thread (an 18 MB working set) (a) is 1-2%
   faster on the P-cores and 2-5% on the E-cores, its rings being two-thirds the size. On allocation (b) wins: (a)'s value
   pool must be sized for every slot that could hold a 128-bit value, which costs more than the word it saves per slot
   (143.5 KB per mesh for (a) against 126.7 KB for (b)). (a) also adds alloc/free at every injection and ejection and a
   copy at every thread crossing, and the toy multiplier says the same for units (item 4). *Provisional on 126, which
   measures both under memory pressure (16 meshes per thread, N = 1..12).*
2. **Width: compile-time for the hot transports**, if the owner accepts it: 11-13% fewer instructions and ~5% faster on
   one mesh (runtime width copies through a `memcpy` call). Not measured but likely to recover most of it without
   templates: keep runtime widths and dispatch the copy on width 1-4 with inline fixed copies.
3. **The destination id: a word of the delivery entry** (E1, W1's `Bucket` as is). A parallel `uint32_t` array (E2) was no
   better beyond noise.
4. **128-bit values in units: inline, two words, computed with explicit 64-bit limbs** (`_addcarry_u64`, the `limbs`
   form). Inline operands beat pool indices by 2 ns per multiply on one thread and by 25% in total at 12 threads; `limbs`
   is the best or equal-best form on every build and core type. **Build with clang**: it makes every form ~2.4 ns per
   multiply in throughput against gcc's best 4.3 ns (and gcc's plain `__int128` form 11 ns).
5. **Ready marks: don't mark per delivery inside a busy group.** On a busy mesh (rate 0.35) stepping all 64 routers
   without marks is 15-19% faster than a bitmap mark per delivered word; at rate 0.1 they tie. Task 126 measures marks on transitions
   only (empty → non-empty) and a quiet mesh (rate 0.01). *(Filled in when 126 lands.)*
6. **The credit wheel** costs 1.1-1.8% of the time in its delivery phase (119 credits per mesh-cycle against 142 hops), and
   is identical in (a) and (b), so none of the link-word difference is the credit wheel's.
7. **Footprint per mesh**: (b) 126.7 KB, (a) 143.5 KB, as the sizing allocates them; 128 meshes are 16.2 / 18.4 MB against
   the runner's 20 MB L3. *(Bytes touched per mesh-cycle and the N-curve: 126.)*

## The mesh (Quest 124)

8 × 8 XY routers, five input rings (depth 8, injection 16), one word per output and one pop per input per cycle,
round-robin, credits returned through a width-1 wheel at latency 3, links of latency 3, a wheel of 4 buckets. Injection is
Bernoulli per tile to a uniform random other tile. Not exact to `ref`; deterministic.

**Load.** Saturation is about 0.365 words per tile per cycle: offered 0.30 / 0.33 / **0.35** / 0.36 / 0.37 / 0.40 / 0.50 →
accepted 0.300 / 0.330 / **0.350** / 0.359 / 0.365 / 0.369 / 0.366, mean injection queue (64 deep) 0.05 / 0.09 / **0.42** /
2.9 / 8.9 / 29 / 46. So 0.35 is busy (96% of saturation, 142 hops per mesh-cycle) but bounded, with no refusals.

**Routing by table.** The router's XY decision is a lookup in a 64 × 64 table (as `ref`'s `Router` does) rather than four
compares: 45 → 30 ns per hop locally, from fewer mispredicted branches. It is the unit's own code, the same in (a) and (b),
so it doesn't bias the link-word comparison; it makes the transports' share of the time larger and their differences
easier to see.

### T1: the matrix — ns per mesh-cycle (ns per hop), one mesh, gcc -O2, ready marks per delivery
| lw | dest | width | 128-bit, 0.35 | mixed, 0.35 | 128-bit, 0.1 | mixed, 0.1 | instr/hop (128, 0.35) |
|---|---|---|---|---|---|---|---|
| (a) | e1 | rt | 3313 ±6% (23.4) | 3343 ±5% (23.6) | 1029 ±5% (25.4) | 1096 ±6% (27.1) | 262 |
| (a) | e1 | fx | 3069 ±5% (21.7) | 3151 ±11% (22.2) | 1000 ±3% (24.7) | 1029 ±7% (25.4) | 227 |
| (a) | e2 | rt | 3128 ±11% (22.1) | 3303 ±8% (23.3) | 1029 ±8% (25.4) | 1089 ±1% (26.9) | 264 |
| (a) | e2 | fx | 3020 ±7% (21.3) | 3141 ±7% (22.2) | 990 ±6% (24.4) | 997 ±9% (24.6) | 231 |
| (b) | e1 | rt | 3020 ±8% (21.3) | 3210 ±8% (22.6) | 966 ±16% (23.8) | 1007 ±8% (24.9) | 250 |
| (b) | e1 | fx | 2970 ±7% (21.0) | 2962 ±10% (20.9) | 907 ±9% (22.4) | 944 ±8% (23.3) | 222 |
| (b) | e2 | rt | 3016 ±11% (21.3) | 3206 ±11% (22.6) | 959 ±5% (23.7) | 990 ±4% (24.4) | 255 |
| (b) | e2 | fx | 3008 ±9% (21.2) | 3076 ±9% (21.7) | 936 ±4% (23.1) | 942 ±5% (23.3) | 222 |

The spread on one mesh is 5-11% (the first repeat ran cooler), so time differences below ~5% are noise; the
instruction counts are exact. Fixed width (`fx`) saves 11-13% of the instructions and ~5% of the time; E1 and E2 tie; (b)
is faster than (a) in every cell (median 5.7%). On the mixed load (a) carries 32/64-bit payloads inline, and still doesn't win.

### T2: builds — ns per mesh-cycle (instructions, 10⁹), one mesh, rate 0.35, E1, fixed width

| lw | mix | gcc -O2 | gcc -O3 | gcc -O2 -march=native | clang -O2 |
|---|---|---|---|---|---|
| (a) | 128 | 3069 ±5% (10.60) | 2762 ±3% (9.78) | 2841 ±1% (10.20) | 2896 ±8% (9.31) |
| (a) | mixed | 3151 ±11% (10.30) | 2843 ±10% (9.51) | 2917 ±1% (9.90) | 2814 ±1% (9.02) |
| (b) | 128 | 2970 ±7% (10.40) | 2640 ±2% (9.45) | 2696 ±2% (9.84) | 2672 ±2% (9.18) |
| (b) | mixed | 2962 ±10% (10.38) | 2770 ±1% (9.47) | 2829 ±3% (9.83) | 2735 ±1% (9.11) |

-O3 and clang -O2 are ~8-10% faster than gcc -O2 (8% and 12% fewer instructions); -march=native ~7%. The spreads of
the -O2 column are wide, so treat these as "-O3 or clang helps by up to ~10%", not a ranking.

### T3: ready marks per delivery (bm) against polling every router (poll) — ns per mesh-cycle, E1, fixed width

| lw | mix | rate | bm | poll |
|---|---|---|---|---|
| (a) | 128 | 0.350 | 3069 ±5% | 2493 ±0% |
| (a) | 128 | 0.100 | 1000 ±3% | 957 ±1% |
| (a) | mixed | 0.350 | 3151 ±11% | 2618 ±1% |
| (a) | mixed | 0.100 | 1029 ±7% | 998 ±1% |
| (b) | 128 | 0.350 | 2970 ±7% | 2403 ±0% |
| (b) | 128 | 0.100 | 907 ±9% | 918 ±1% |
| (b) | mixed | 0.350 | 2962 ±10% | 2516 ±11% |
| (b) | mixed | 0.100 | 944 ±8% | 923 ±0% |

Phase shares (rdtsc per phase, E1 fixed width, rate 0.35): one mesh — credit delivery 1.7-1.8%, link delivery 7.5-9.2%,
injection 12-15%, routers 75-77%; 128 meshes on one thread — credits 1.1-1.2%, links 22-25% (the delivery walk now misses
cache: 8,192 routers' rings), injection 8-11%, routers 65-66%. `perf record` on 128 meshes agrees (`step_all` 66%,
`deliver_links` 22-25%, `inject` 8-11%, `deliver_credits` 1%).

### T5: scale — 128 independent meshes, rate 0.35, E1 fixed width: total mesh-cycles/s [mean per-thread ns per mesh-cycle]

| lw | mix | P1 | P6 | P12 | E1 | E4 |
|---|---|---|---|---|---|---|
| (a) | 128 | 2.42e+05 ±0% [4134] | 1.47e+06 ±8% [3799] | 2.23e+06 ±0% [5141] | 8.67e+04 ±2% [11531] | 3.16e+05 ±0% [12564] |
| (a) | mixed | 2.37e+05 ±0% [4215] | 1.54e+06 ±5% [3757] | 2.21e+06 ±1% [5181] | 8.77e+04 ±2% [11407] | 3.21e+05 ±0% [12376] |
| (b) | 128 | 2.37e+05 ±0% [4215] | 1.54e+06 ±0% [3755] | 2.22e+06 ±1% [5157] | 8.54e+04 ±0% [11710] | 3.05e+05 ±1% [13021] |
| (b) | mixed | 2.33e+05 ±0% [4294] | 1.5e+06 ±1% [3856] | 2.18e+06 ±0% [5261] | 8.42e+04 ±1% [11882] | 3.04e+05 ±1% [13054] |

P1, P6, P12 are 1 thread on CPU 0, one per P-core (CPUs 0, 2, .., 10) and all 12 hyperthreads; E1 and E4 are CPU 12
and CPUs 12-15. 128 meshes split 21-22 per thread on 6 threads and 10-11 on 12, so the slowest thread sets the wall time:
on P6 the wall time is 0.82-0.88 s against a mean thread time of 0.81 s (slowest thread ≈ 2.5 × 10⁵ mesh-cycles/s, mean
2.6 × 10⁵); task 126 prints the slowest and mean rates directly. Six P-cores give 6.1-6.5× one; their hyperthreads add another
43-52% (9.2-9.4×); the four E-cores add 3.0-3.2 × 10⁵. One thread with 128 meshes (an 18 MB working set) runs 35% slower per mesh than
one mesh alone (4.1 against 3.07 µs per mesh-cycle): the delivery walk's cache misses. (a) and (b) are within a few percent at
every scale, (a) slightly ahead (P1 +2%, P12 +1%, E +1.5-5.5%; P6 mixed within its 8% spread).

**Against `ref` (D15), for scale only:** a mesh-cycle here is 64 routers and their links, not a chip-cycle (`ref`'s router
was 7.7% of a busy replay's time), so these don't compare directly with `ref`'s 36.8 µs per chip-cycle and
100,000-110,000 chip-cycles/s for the box. What does carry over: a busy router costs ~21 ns per hop here (~220
instructions, the bench's injection and digest included), and the whole box runs ~2.5 × 10⁶ busy mesh-cycles/s.

## Fp128 (Quest 125; D14: p = 2¹²⁸ − 159, folded by 159)

The fold, three forms: **u128** (`unsigned __int128` values, carries by 128-bit compares), **acc** (64 × 64 → 128 products
summed column by column in `__int128` accumulators), **limbs** (`_addcarry_u64` chains). All three agree with a slow
double-and-add reference on 576 edge-case pairs (0, 1, p − 1, p, p + 1, 2¹²⁸ − 1, near 2⁶⁴, ...) and 10⁶ random pairs in
every build, and with Python's `(a * b) % p` on 3 × 10⁵ products.

### F1: per operation — latency / throughput (8 independent chains), ns

| form | gcc -O2 | gcc -O3 | gcc -march=native | clang -O2 | E-core, gcc -O2 |
|---|---|---|---|---|---|
| u128 | 9.56 / 11.17 | 10.05 / 9.21 | 9.83 / 9.59 | 6.26 / 2.57 | 16.00 / 24.84 |
| acc | 7.10 / 4.72 | 8.26 / 4.89 | 7.83 / 4.81 | 6.61 / 2.48 | 13.14 / 14.02 |
| limbs | 6.93 / 4.25 | 7.23 / 4.28 | 6.47 / 4.32 | 6.75 / 2.42 | 12.24 / 7.89 |

**gcc and clang differ more than the forms.** Under gcc the plain u128 form is 2.4× slower in throughput than the
others (its 128-bit carry compares become branches); under clang all three are ~2.4-2.6 ns (≈ 11-12 core cycles at
4.7 GHz), half the cost of gcc's best (4.25 ns), with about half the instructions. Latency (one dependent chain) is
6.3-7 ns for the good forms everywhere. On an E-core, `limbs` is clearly best (7.9 ns against 14 and 25).
**Recommended form: `limbs`** — best or equal on every build and core type, and it doesn't depend on the compiler noticing
carries.

### F2: the toy multiplier unit, 64 units on CPU 0 — ns per multiply

A unit pops an operand pair from its input ring, multiplies, and puts the result paired with its constant operand back
through the delivery wheel (latency 1); every unit multiplies every cycle. `pool`: the pair is two `ValuePool` indices, the
result takes the freed index; `inline`: the pair is the two values, four words. `none`: an addition instead (the data
movement alone).

| form | pool, gcc -O2 | inline, gcc -O2 | pool, gcc -O3 | pool, native | pool, clang -O2 |
|---|---|---|---|---|---|
| u128 | 18.7 ±11% | 15.7 ±7% | 19.0 ±0% | 18.8 ±2% | 9.2 ±0% |
| acc | 12.2 ±1% | 10.2 ±31% | 12.4 ±0% | 12.1 ±0% | 9.8 ±1% |
| limbs | 11.2 ±5% | 9.1 ±9% | 11.6 ±6% | 11.8 ±10% | 8.7 ±7% |
| none | 8.7 ±7% | 4.2 ±5% | - | - | - |

The data movement costs 8.7 ns per operation through the pool and 4.2 ns inline: the pool's alloc, free and two
indirect loads add ~4.5 ns, as much as the multiply itself. With clang the pool form costs 8.7-9.8 ns whatever the form.

### F3: the toy unit at scale, 8192 units (a tile's worth on 128 chips), gcc -O2: total multiplies/s [mean per-thread ns]

| form | hold | P1 | P6 | P12 | E1 | E4 |
|---|---|---|---|---|---|---|
| u128 | pool | 4.84e7 ±1% [20.6] | 2.59e8 ±17% [22.6] | 3.73e8 ±0% [32.1] | 2.42e7 ±3% [41.3] | 9.24e7 ±1% [41.7] |
| u128 | inline | 5.63e7 ±1% [17.8] | 2.67e8 ±1% [20.1] | 4.42e8 ±0% [27.1] | 2.85e7 ±3% [35.1] | 1.12e8 ±4% [35.7] |
| acc | pool | 6.88e7 ±1% [14.5] | 3.24e8 ±10% [16.7] | 4.73e8 ±1% [25.2] | 2.95e7 ±12% [33.9] | 1.08e8 ±8% [35.4] |
| acc | inline | 8.48e7 ±1% [11.8] | 4.19e8 ±32% [12.6] | 5.76e8 ±0% [20.7] | 4.02e7 ±4% [24.9] | 1.45e8 ±10% [27.2] |
| limbs | pool | 7.66e7 ±3% [13.0] | 3.36e8 ±30% [14.2] | 5.3e8 ±1% [22.5] | 3.56e7 ±9% [28.1] | 1.31e8 ±7% [29.6] |
| limbs | inline | 9.78e7 ±2% [10.2] | 4.36e8 ±2% [10.7] | 6.63e8 ±1% [18.0] | 4.61e7 ±11% [21.7] | 1.76e8 ±3% [22.6] |
| none | pool | 1e8 ±2% [10.0] | 5.59e8 ±26% [10.2] | 7.38e8 ±0% [16.2] | 6.5e7 ±8% [15.4] | 1.66e8 ±8% [21.8] |
| none | inline | 1.46e8 ±3% [6.9] | 6.87e8 ±1% [6.8] | 1.14e9 ±0% [10.5] | 1.01e8 ±14% [9.9] | 1.91e8 ±2% [20.8] |


At 12 threads `limbs` inline does 6.6 × 10⁸ multiplies/s against 5.3 × 10⁸ through the pool (+25%); the u128 form
loses a third. Six P-cores give 4.4-5.3× one, their hyperthreads add 44-58%, the four E-cores 0.9-1.9 × 10⁸ more.
