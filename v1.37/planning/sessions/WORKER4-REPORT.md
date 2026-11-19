# WORKER4 (sim/): report

PLAN-M1 item 7, threads, in both forms, on branch `sim-w4` (merged into main as it went). The plan, the owner's decisions
and the details are in `NOTES.md`; this is the summary.

## What was built

**Form 1, independent chips** (`src/parallel.h/.cpp`). `run_independent` runs N `Sim`s, or N chips each built, run and
destroyed on its own thread, pinned. The one hidden global, the type registry, was fixed by W3 (E1: a registry per
`Config`, ids in first-add order). W5's `test_controllers` (N controllers on N threads, Quest 131) is M1's last
acceptance test on it.

**Form 2, one chip over several threads** (`src/threads.h/.cpp`, `src/barrier.h/.cpp`, `src/internals.h`). The partition
is `Config::domain(d)`. A configuration with more than one domain is split before anything is built: each crossing is cut
and its ends wired to boundary units (build-time placeholders, never a hop), so each domain is a closed machine that W3's
`build()` builds unchanged. Then the domains are joined:
- each crossing route points at an **outbox** (one per source, destination, kind, width and latency), appended after the
  domain's own wheels in its wheel table, so a put runs the same code either way;
- every **interval** (the smallest crossing latency: 3 on the mesh; 1 when a Fifo, credit Fifo, Hold, Reg or Count
  crosses) the threads meet at a **barrier**, and each domain **drains** the outboxes addressed to it into its own wheels at
  the bucket each entry was put for, so a word arrives at t + L exactly as in one domain;
- what a producer reads "as of the start of t" and can't see across (a Hold's occupancy, a Count's other increments, each
  domain's next event) crosses as **status words**, one cache line each, double-banked by interval;
- a credit's return travels back through an outbox; a Reg read in several domains is fanned out by the drain; payloads
  carried by reference are copied into the destination's pool and freed at the source two intervals later;
- a Wire or RegFile across domains is fatal at build, naming both units; a Count read with `now` across is caught in
  debug builds (A3; M2: a declared `now` read would let `build()` refuse it);
- the whole machine **jumps** idle cycles only when every domain is idle and nothing was sent, to the earliest next wake
  or missed-wake deadline (W3's cap), each thread deciding from the same status words;
- `Sim::partition()` reports the domains, threads, crossings by kind, and the interval with the crossing that set it.

**The digest unit by unit** (E5, `src/digest.h/.cpp`): add order, each unit's state then its ports; payloads by reference
hashed by their contents; port handles left out of the default (bytes) digest. So a partition can't change it. The
`sim.h` rule: a unit's own `digest()` leaves out its port handles, and a slot index is opaque (R1).

**D21** (`sim/tools/machine.sh`, `src/topology.h/.cpp`): the machine's record and CPU lists, detected (hybrid P/E chips,
sockets, the process's allowed CPUs) or overridden; the default placement (one per P core, then their hyperthreads, then
E cores), used by the framework when no CPU list is given (-1: unpinned). W5's `machine.inc` is a shim; W2's
`baseline.sh`, W6's Quest bodies and mine take the machine from it, with no CPU numbers left.

## Engine changes (each approved, all outside the cycle loop)

E1 and E2 (W3: the registry per `Config`; `config_impl.h`); E3 (the split's hooks: `detail::Internals` a friend of
`Config` and `Sim`, `Sim::Impl::threads` and the forwards, `Options::cpus`, `barrier_block`, `barrier_spins`,
`Sim::partition()`); E4 (`src/threads.mk`: `make tsan` for gcc and clang in `make check`, `machine-check`); E5 (the
digest view in `Sim::Impl`, `TypeInfo::own_digest`); E6 (the missed-wake detector across domains: a drained word marks its
consumer as having something on its way, and a crossing's consumer waits one interval longer; debug builds only).

## Determinism

`NOTES.md`, "Determinism": each domain keeps W3's order (a subsequence of the single-domain one); a bucket holds at most
one entry per destination port, so the order of merged and local entries can't matter; every crossing's latency is at
least the interval; producer-side state "at the start of t" depends only on the consumer at the end of t - 1 and the
producer's own puts; Counts are sums; idle jumps are agreed; the digest leaves out exactly what a partition changes.

## Tests

`test_parallel` (5: N chips on N threads equal to one at a time, built on their own threads), `test_threads` (19: a Link
across arrives at t + L with the same step count; the partition report; no crossing, no barrier; the synthetic chip split
2-8 ways on 1-8 threads; debug options per domain; no allocation with workers; every barrier mode; a 10^9-cycle sleep;
wide payloads; E6 both ways; the idle-jump cap across domains; the watchdog; Wire, RegFile and Count `now` across fatal),
`test_matrix` (8: each kind crossing on its own, 16 units, 2/4/8 domains x 2 partitions x 1/2/4/8 threads, 5
checkpoints; mutations of the Hold and Count crossings are caught), `test_digest` (3), `test_topology` (7, fake /sys
trees), `tools/test_machine.sh`. `make check` clean on gcc and clang, debug, release and TSan; `make conformance` clean.

Two races and one livelock found and fixed on the way (a thread reading another's cycle at a run's start; an outbox cleared
or read after an idle jump while aliased with the next interval's; a pure spinning barrier starving the last thread on a box
with fewer CPUs than threads), and one release-only crash (E6 writing through the detector's empty arrays). The barrier's
debug watchdog (a wait over 10 s prints every thread's state and is fatal) makes a hang explain itself.

## Measurements (clang release, pinned; the machine recorded with each)

**Quest 128, form 1** (the framework mesh and the synthetic chip, i5-14400F): 5.6 µs per mesh-cycle on one P core
(178 K mesh-cycles/s); 6 P cores 4.9x, 12 threads 7.2x (1.29 M/s); the 4 E cores 0.37 M/s more; 16 chips per thread cost
nothing more. Identical digests on 1, 6 and 12 threads.

**Quest 133, form 2** (one mesh over threads, a barrier every 3 cycles): 2 threads 1.90x, 4 threads 3.05x, 6 threads 2.80x
(8 rows don't divide over 6); the split's own cost on one thread 3% at 2 domains, 18% at 6; the barrier ~0.5 µs spinning;
spinning, yielding after 200-20,000 pauses within the spread of each other on real work, blocking 3-7% slower (default:
yield after 2,000, which can't livelock); the due mask off in split domains costs nothing measurable (≤2% on a quiet mesh);
footprint +17% at 2 domains, +52% at 6. Identical digests over every run.

**Footprint** of the framework mesh (for W3, in `NOTES.md`): 228 KB per mesh against W6's hand-rolled 127 KB; the
framework's share ~140 KB (words held twice in the routers' own queues, ~74 KB: an M2 design item; event lines stored as
rings; 96 B of headers per Link; worst-case wheels).

## For M2

- **Words held twice**: a Link whose consumer may hold up to a declared depth (the mesh's credit protocol on a Link of 3)
  would let routers keep their queues in the framework's rings (owner: an M2 design item).
- **Partition by load**: the mesh's bands of rows don't divide over 6 threads; M2's chip may want a partition helper (by
  tile count or measured load), and the report already names each domain's crossings.
- **The split's own cost** (18% at 6 domains on one thread): per-domain bookkeeping per interval; worth a look if M2 runs
  many domains per thread.
- **A3**: a declared `now` read on a `CountIn` would make a Count `now` across domains fatal at build in every build.
- **The barrier** at rate 0 is most of the work (~0.5 µs per barrier): for long quiet stretches the agreed idle jump does
  the work; a chip that is quiet but never idle (units ticking) would pay it every interval.

## Files

`src/parallel.h/.cpp`, `threads.h/.cpp`, `barrier.h/.cpp`, `internals.h`, `digest.h/.cpp`, `digest_view.h`,
`topology.h/.cpp`, `threads.mk`; `tests/synth.h`, `test_parallel.cpp`, `test_threads.cpp`, `test_matrix.cpp`,
`test_digest.cpp`, `test_topology.cpp`; `bench/threads/` (`mesh_fw.h`, `bench_threads.cpp`, `quest/`); `tools/machine.sh`,
`fake_sysfs.sh`, `test_machine.sh`.
