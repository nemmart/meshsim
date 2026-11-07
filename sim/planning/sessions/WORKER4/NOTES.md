# WORKER4 (sim/): notes

Branch `sim-w4`. PLAN-M1 item 7: threads, in two forms (independent chips; one chip over several threads). **This file is
the plan, agreed with the owner 2026-11-07** (his decisions below); read against `main` at 078d29e.

## The owner's decisions (2026-11-07)

1. **E1 and E2: W3 does them**, now, ahead of its increment 5. E1 changes: **each Config gets its own registry**, ids in
   first-add order (a global registry hands out ids in a thread-dependent order). I start with what doesn't need E2 and
   merge main when W3's commit lands.
2. **E3**: approved, in my files.
3. **E4**: approved, in a separate include (`src/threads.mk`, one include line in the Makefile), as W5 did.
4. **E5**: approved and preferred (unit by unit, wide payloads by contents). **Digests compared only at barrier points,
   after the merge.**
5. **E6**: approved, once the detector lands.
6. **A1, A2**: approved. **A3**: the debug-only check for M1; **for M2**: a declared `now` read lets build() refuse it.
7. **R1**: approved; one line in sim.h (W3, or in my E3 commit, telling the owner).
8. **A crossing's arrival cycle and ready mark are exactly the single-domain build's**: the boundary units are
   build-time placeholders, never an extra hop. Tested (a Link across delivers at t + L, as locally).
9. **The configuration reports the barrier interval and which crossing set it**, so a slow partition explains itself.

## For the owner: what needs a decision

1. **Engine changes** (W3's files, all outside the cycle loop; `engine.h`/`engine.cpp` untouched): E1-E4 below are needed,
   E5 is preferred, E6 waits for W3's increment 5. Who makes E2/E3 (a mechanical move in `config.cpp` while W3 edits it):
   me in one commit, merged quickly, or W3.
2. **Two small API additions** (A1, A2) and one choice (A3: how a Count's `now` read across domains is caught).
3. **One rule for unit authors** with by-reference payloads (R1): a slot index is opaque; partitions change its value.

## The two forms

**Form 1, independent chips.** N `Sim`s, built and run one per thread, sharing nothing mutable. The framework's only
hidden global is **the type registry** (`config.cpp`): a `std::vector` pushed to by whichever thread first uses a type,
read by reference (`type_info`) from every build; two threads registering different types race, and a push can move the
elements under a reader. Fix E1. Everything else checked: the allocation hook is `thread_local`; `kind_name` is a const
table; FATAL/DCHECK write to stderr and abort; W1's structures, `Engine` and `Sim::Impl` are per instance; `type_id<T>`'s
static is a magic static (thread-safe); `alloc_big` has no state; W5's units (sim-w5, fa3e3dd) have none either. A
helper runs them (A1): each chip built **on its own thread** (first touch, and the registry exercised concurrently), run
pinned, joined.

**Form 2, one chip over several threads.** The partition is `Config::domain(d)` (W3's hook). `build()` with more than one
domain builds **one ordinary engine per domain with W3's own build**, unchanged, and joins them with outboxes:

- **The split.** Before building, every cross-domain connection is replaced by a pair of **boundary units** (a framework
  unit type per kind, no logic, `step` returns `none()`): in the producer's domain the producer's output is wired to a
  boundary **input** (the *proxy*: a real port in that domain's pools), in the consumer's domain the consumer's input is
  wired from a boundary **output**. Each domain is then a complete, closed machine that W3's build sizes, numbers and
  levels as it does any other (the boundary output counts on the consumer's wheel, so the wheel's buckets have room for
  the merged entries: W3's sizing rule holds unchanged).
- **The route.** After the build, the producer's route is rewritten to point at an **outbox** for the destination domain:
  `Route::wheel`, `mask` (data values, config.h). `Ctx`'s inline code is the same either way: a put appends to "a
  bucket", the producer-side reads (`credits`, `can_put`, the Link's capacity check) read the proxy in its own pools.
- **Outboxes**: one per (source, destination, kind, width, latency), a ring of `W >= 2I` buckets indexed like a wheel
  (`(t + L) & mask`), each bucket's capacity the number of routes on it (one put per output per cycle: never exceeded;
  `Bucket::append` is fatal if it is). One latency per outbox means interval k's entries and interval k + 1's are in
  disjoint buckets, so the destination drains k while the source writes k + 1: **one barrier per interval**.
- **The cycle per thread** (a thread may run several domains, in order): run I cycles of each of its domains (W3's
  `Engine::cycle`, untouched); publish (below); **barrier**; drain every outbox addressed to its domains (interval k's I
  buckets) into its own wheels, translating word 0 where needed, adjusting `pending` and Links' `inflight`; continue. No
  locks in the cycle: the barrier is between cycles.
- **The barrier interval** I = the smallest latency over cross-domain connections (Link L; Fifo, credit, Hold, Reg 1; a
  credit's return 1; a Count read across, 1), at least 1. No crossing at all: no barrier until the end of `run`.

### Each kind across domains

| kind | producer's domain (proxy) | the drain into the consumer's domain | what flows back |
|---|---|---|---|
| Link | boundary LinkIn: `link_put_check` counts on it; its `inflight` reset at each barrier (at most I puts in an interval, I <= L < L + 2) | word 0 proxy -> consumer port; `inflight++`; the capacity check (count + inflight > L + 2: fatal), there instead of at the put | nothing |
| Fifo | boundary FifoIn of depth 1, never read | route's `dest` is already the consumer's index (a Fifo put reads nothing producer-side): no translation; overflow judged at delivery as now | nothing |
| credit Fifo | boundary credit FifoIn: **the producer's credits live there** (`credits`, `credit_spend` unchanged) | word 0 proxy -> consumer port | the consumer's credit return: its `ret` route rewritten to the outbox back, `dest` = the proxy; `deliver_cret` in the producer's domain increments the proxy and wakes the producer, as locally |
| Hold | boundary HoldIn: `can_put`, `held_at_start`, `refused` read it | word 0 proxy -> consumer port; the consumer's delivery accepts or refuses as now | **a status word per hold per barrier**: the consumer's `occ` at the end of t - 1. The producer's domain sets the proxy: occupied at the start of t = `occ` or a put at t - 1; refused = a put at t - 1 and `occ`, which also sets `refused_at` and wakes the producer (an edge bit set between cycles survives to its step: `mark` before `cycle()`). The same inputs give both sides the same decision |
| Reg | boundary RegIn | one boundary RegOut per reader domain; **all** readers, the producer's domain's too, are fed from them (one put reaches every domain through the drain; a route has one wheel) | nothing |
| Count `through_last` | none: each domain's build makes its own replica of the connected set | — | a delta per replica per barrier; each domain adds the others' (a sum: order-free). `through_last` at t is then every domain's increments through t - 1 |
| Wire, RegFile | **fatal at build**, naming both units, the port and the domains | | |
| Count `now` | A3 | | |

A crossing Fifo, credit Fifo, Hold, Reg or Count makes I = 1. On the mesh every cross-group coupling is a Link of 3 or a
line of 8 (width-0 Link), so I = 3.

**Wide payloads across** (by-reference ports): the drain allocates a slot in the destination's pool of the same class,
copies it and rewrites word 0. The source frees its own slot when it reuses that outbox bucket (two intervals later, when
the drain is certainly done): a thread never touches another's pool. The source's pool gets `2I` extra slots per crossing
port (declared as `held` on the boundary input, so W3's sizing counts them). Hold: a put the drain can see will be refused
(the consumer's hold occupied, I = 1) isn't copied, and the producer keeps its slot for the retry, as locally.

### The engine changes (each small, said here before it's made)

- **E1** (`config.cpp`): the type registry made thread-safe: a fixed array (fatal past its size) and a mutex around
  registration; reads lock-free (an id is published by the magic static after its entry is written).
- **E2** (`config.cpp` -> new `config_impl.h`): `Config::Impl` and `Sim::Impl` moved, verbatim, into an internal header,
  so `threads.cpp` can split the staged units and declarations. No behaviour change.
- **E3** (`config.cpp`): `build()` hands a configuration with more than one domain to `build_threads` (mine);
  `Sim::Impl` gets one pointer to the partitioned machine, and each `Sim` method (`run`, `run_until`, `now`, `unit`,
  `digest`, `steps`, `sizes`, `sleepers`) forwards to it when set: about a dozen lines.
- **E4** (`Makefile`): `MODE=tsan` (`-O1 -g -fsanitize=thread`, both compilers), `-pthread`; `make check` adds the
  TSan pass for both compilers.
- **E5** (preferred, `config.cpp`): `Sim::digest` unit-major in add order (each unit's state, then its inputs), through
  per-port hash helpers both builds share; by-reference ports and pools hashed by **slot content, not index**. Then a
  1-domain `Sim` and any partition give the same digest by construction. Without E5, the partitioned machine has its own
  canonical digest (same definition, my code), and the equality tests are among partitions only.
- **E6** (with W3's increment 5): the missed-wake detector as a function the drain can call. Across domains a word put
  this interval is in an outbox, not yet in the consumer's `inflight`, so a per-cycle check could flag a unit whose word
  is on its way; in a partitioned machine the check runs after each drain.

### API

- **A1**: `run_independent(sims or builders, cycles, cpus)` (form 1, `parallel.h`; the configuration regime, STL fine).
- **A2**: pinning for form 2: `Options::cpus` (a list; thread k on `cpus[k]`), default unpinned.
  `Options::threads` = OS threads; domains are spread over them in order (threads <= domains), so one partition can run
  on 1, 2, 4 or 8 threads.
- **A3**: a CountIn doesn't say whether it is read with `now` or `through_last`, so "fatal at build" needs a
  declaration. (a) no API change: a replica with producers in other domains gets a producer level above every level, so
  the existing debug check in `Ctx::now` fires (debug builds only); (b) `Ports::in(CountIn&, name, Count::now)`, checked at
  build in every build. I propose (a) for M1.
- **R1** (sim.h, by-reference payloads): a slot index is opaque; a partition changes which index a payload gets, so a
  unit's behaviour and its `digest()` must not depend on the value (hash the slot's contents).

### Determinism: why the partition can't change a result

1. **Within a domain the order is W3's**: a domain's instances are a subsequence of the single-threaded order (same rule:
   level, type, form, add order), and units interact within a cycle only through Wires, RegFiles and Count `now`, none of
   which cross. Shuffle and both orders already prove a level's order doesn't matter.
2. **Delivery order within a bucket can't matter.** Every Link, Fifo, credit Fifo and Hold has one producer, a Reg one
   output, and each output puts at most once a cycle with a fixed latency, so a bucket holds **at most one entry per
   destination port** (a credit return likewise: one take per cycle). Entries to different ports touch disjoint state
   except: the ready mark (an OR), a Fifo overflow's Count (a sum) and a dropped slot's free (a set of free bits: lowest-free
   allocation after it doesn't depend on the order of frees). So merged entries may land before or after local ones.
3. **Timing is exact**: every crossing has latency >= I, so a word put in interval k arrives at or after its first cycle
   after the barrier, and the drain happens before it. Producer-side state that is "as of the start of t" (a Hold's
   occupancy, credits) depends only on the consumer's state at the end of t - 1 and the producer's own puts, which the
   barrier (I = 1 for these kinds) carries.
4. **Counts** are sums; replicas agree at every barrier.
5. **Idle jumps**: a thread never jumps past its next barrier; the whole machine jumps only when every domain is idle and
   every outbox empty, to the smallest next wake, each thread deciding from the same published values (no extra barrier).
   Skipped cycles are cycles where nothing happens, as in W3's `run_until`.
6. **What can differ** is only what is invisible by R1 (slot indices) and the step counts of boundary units; the digest
   excludes both (E5: contents, not indices; boundary units skipped).

### Debug checks per thread

Shuffle (each domain's seed derived from the seed and the domain), both orders, step-all, wire discipline: per domain,
unchanged. The missed-wake detector: E6. "No value array shared": each domain's pools are its own block (checked at
build), and TSan runs the by-reference crossing tests.

## Files

`src/threads.h`, `src/threads.cpp` (the split, boundary units, routes, outboxes, drain, status words, the run loop),
`src/barrier.h` (spinning and blocking, chosen by measurement), `src/parallel.h` (form 1, A1);
`tests/test_threads.cpp`, `tests/synth.h` (a seeded synthetic graph with every kind, rates and latencies, and seeded
partitions); `bench/threads/` (the mesh on the framework, the form-1 and form-2 runners, Quest kits). Engine changes: E1-E6
only.

## Tests

- **Form 1**: N synthetic machines (different seeds) on N threads give the digests they give one at a time; built
  concurrently; TSan clean (gcc, clang).
- **Form 2**: the synthetic graph on 1, 2, 4, 8 threads over several seeded partitions (random, by chip, worst case: every
  unit its own domain): **identical digests** at checkpoints through the run, and identical per-unit observable logs;
  shuffle, both orders and step-all per domain also identical. Every kind crossing on its own (a Link at several
  latencies, credits, Hold refusals and retries, Reg fan-out to 3 domains, a Count summed across 4); a 16-word class
  crossing (copied, freed, digest by content); a partition with no crossings (no barriers).
- **Death tests**: a Wire, a RegFile across (named); a Link not taken across a boundary (capacity, named); a Count `now`
  across (A3, debug).
- No allocation in the loop with threads (the hook is thread-local: each thread's runs are guarded).
- `make check` clean, gcc and clang, debug, release and TSan.

## Measurements (Quest, clang release, D18; pinned, D13; numbers claimed in NUMBERS.md first)

1. Form 1: the mesh on the framework (W6's 8 × 8, rebuilt as units) and the synthetic chip, 1, 6 and 12 threads on the
   P-cores (CPUs 0, 2, .., 10 for one per core), E-cores separately, as W6's N1; footprint per chip (D15) from `Sizes`.
2. Form 2: one busy mesh over 2, 4 and 6 threads against one thread; the barrier's cost (an empty machine's barrier rate;
   time in the barrier per interval); spinning against blocking (spin-then-futex) on the P-cores, with hyperthread
   siblings busy and idle, and with an E-core in the set.
3. Footprint of the split: boundary units, proxies and outboxes per crossing, from `Sizes`.

## The order of work (each pushed with `make check` clean)

1. E1, E4; `parallel.h`; `synth.h`; form 1's tests. **Quest**: form 1 scaling on the synthetic chip.
2. E2, E3; the split with Link and Fifo; the outboxes, drain, barrier (spinning first); the determinism tests on them.
3. Credit Fifo, Hold (status words), Reg (fan-out), Count (replicas), by-reference crossings; the fatal kinds.
4. Idle jumps; debug checks per domain; the canonical digest (or E5); the full matrix of threads × partitions.
5. The mesh on the framework; **Quest**: form 1 on the mesh, form 2 against one thread, the barrier choice, footprint.
6. With W5: N controllers on N threads, different programs, each matching its golden (M1's last acceptance test).
7. `REPORT.md`.

## Footprint of the framework mesh against W6's (for W3; the owner's question 2026-11-07)

`bench_threads --sizes --chip mesh` (Sizes, one 8 × 8 mesh, 64 Routers, 256 data Links w3 and 256 credit lines w0, all of
latency 3). W6's hand-rolled mesh (Quest 126, F): **127 KB**, of which rings 58%, wheels 32%, ring headers 8%.

| part | as first built (Quest 128) | now | what it is |
|---|---|---|---|
| Router unit state | 134.1 KB | **85.0 KB** | 64 × 1,328 B: the five input queues **inside the unit** (4 × 8 + 16 entries × 24 B = 1,152 B), 96 B of port handles (8 LinkIn × 8 B, 8 LinkOut × 4 B), 80 B of counters, credits, round-robin. As first built every queue had 16 entries (my bench's waste: 49 KB); fixed |
| Link input rings, w3 | 32.8 KB | 32.8 KB | 256 × 128 B: L + 2 = 5 entries × 24 B = 120 B, rounded to a cache line |
| Link input rings, w0 | 16.4 KB | 16.4 KB | 256 × 64 B: an event carries no word, but its ring holds 5 one-word entries (40 B), rounded to a line |
| LinkPort headers | 28.7 KB | 28.7 KB | 512 × 56 B (the ring's header, owner, in flight, capacity, class, bit, width) |
| Routes | 20.5 KB | 20.5 KB | 512 × 40 B (one per output) |
| Wheels | 41.0 KB | 41.0 KB | w3: 4 buckets × 256 entries × 4 words; w0: 4 × 256 × 1 word: the worst-case rule (config.h), as in W6 |
| instances, runs, heap | 3.3 KB | 3.3 KB | per-instance arrays, padded to lines |
| **total** | **276.8 KB** | **227.6 KB** | |

**Where W3 might look** (the framework's side, about 140 KB of the 228):
1. **Data held twice.** The Router copies each word from the Link's ring into its own queue, because a Link's consumer
   must take a word the cycle it arrives (capacity L + 2) and the credit Fifo's latency is 1. W6's router kept its words
   in the transport's ring (depth 8, credits). A Link whose consumer may hold up to a declared depth (the credit protocol
   on a Link of latency L: `ref`'s mesh links) would let the queues live in the framework's rings: about 74 KB of unit
   state (32 + 16 + ... per router) and the copy per hop.
2. **Event lines as rings.** A width-0 Link stores five dummy words per input, a line each: 16.4 KB for 256 credit lines.
   A count of arrivals (one word) would do: about 14 KB.
3. **Headers.** 96 B per Link (LinkPort 56 + Route 40) against W6's 8% for ring headers: 49 KB, as much as the data rings.
4. **Wheels**: 41 KB, the worst-case rule already flagged for M2.

## Log

- 2026-11-07: read the brief, PLAN-M1, DECISIONS, the engine (main 078d29e), W3's notes, W6's and W2's reports; plan
  written, waiting for the go-ahead.
- 2026-11-07: the owner's go-ahead and decisions (above). Main merged (W3 increment 4).
- 2026-11-07: **form 1** (no E2 needed). `src/parallel.h/.cpp` (A1: `run_independent` over Sims or over builders, each
  chip built, run and destroyed on its own thread; `pin_to_cpu`); `tests/synth.h` (the seeded synthetic chip: every kind,
  wires and counts inside clusters, step-all safe; busy: Hold refusals, exhausted credits, events, overflows);
  `tests/test_parallel.cpp` (5: deterministic and busy; N chips on N threads equal to one at a time at 3 checkpoints;
  built and run on their own threads, the allocation hook on in each; the same chip twice; a Sim given twice refused).
  `src/threads.mk` (E4): `make tsan` (gcc and clang) runs the thread tests under TSan, and `make check` runs it first.
  **TSan and W1's hook**: clang links TSan's operator new/delete statically beside the hook's, so the TSan link keeps the
  first (the hook's, `--allow-multiple-definition`); it allocates through malloc, which TSan intercepts. **TSan sees the
  registry race** (two threads building machines of different unit types: a probe, not committed): until W3's E1,
  `test_parallel` registers its types on one thread first; the concurrent-registration test comes with E1.
  `make check` clean (gcc, clang; debug, release, tsan).
