# sim/: decisions

The decision record of the next-simulator project (the owner's rulings, with the date). The brief and its standing direction
are in `sim/README.md` and `wip/planning/SIMULATOR-NEXT.md` and are not repeated here; this file records what was decided
after them. The plan for the first milestone is `PLAN-M1.md`.

## 2026-11-06: milestone 1's scope and the working rules

- **D1. Milestone 1's scope.** The controller with its memories, boot ROM, CSRs and traps, and enough of the register block
  for SIM_EXIT, the timers and WFI. Sends, requests and the recipes come with later units.
- **D2. The programs.** The 48 riscv-tests programs (as `RiscvTest` runs them), and `sw/tests`' single-tile programs: `hello`,
  `faults`, `misalign_ld`, `misalign_st`. **`regs` and `wfi` move to M2** (they send through the router and use the counters,
  GO and commands); W2 writes small single-tile replacements covering CSRs, `mcycle`/`mtime`, and WFI woken by a command and
  by GO.
- **D3. Exactness is a retire trace.** Both sides write a per-instruction retire trace (cycle, pc, rd and its value, any memory
  operation); M1 is accepted when they are equal, line for line.
- **D4. Toolchain.** C++20, gcc and clang, plain `make` (no CMake), no RTTI and no exceptions anywhere; a fatal condition
  prints its context to stderr with `fprintf` and calls `std::abort()`. If the Quest runner lacks a C++ compiler or the riscv
  toolchain, prebuilt firmware images are committed and the build ships in the kit.
- **D5. `sim/ref` stays frozen, with one exception:** one minimal observer, opt-in and off by default, proven to change
  nothing (identical logs with it off), recorded in `sim/ref/README.md`. It logs retirements and the test-bench events
  (command landed, GO, send accepted or refused). Drivers that use `ref`'s classes (the trace runner, the port conformance
  driver) live outside `sim/ref`, compiled into its default package, since its classes are package-private.
- **D6. Minimal STL.** The configuration phase (declaring, wiring, sizing, loading) may use whatever C++ makes it clear; the
  cycle loop uses none of it: plain arrays, our own structures, untyped 64-bit words, no allocation.
- **D7. The field engine is the coming load.** The ~50 replays' control flow is unknown: the sumchecks over Fp128, and the
  field engine that computes them, don't exist yet; runs will likely be one sumcheck at a time, and much heavier than today's.
  So units must do real arithmetic cheaply (native 128-bit multiplies), not only move data, and a unit type may have a fast
  result-only form beside its cycle-accurate one.
- **D8. The baseline.** Today's engine as 16 independent JVMs at once, measured early, is the bar to beat. `sim-step-cost`
  stays unmerged; it doesn't affect `sim/`.
- **D9. Threads in M1.** Thread-local by design, single-threaded first, plus one test of N independent controllers on N
  threads. Local and cross-domain port forms are chosen at configuration from the partition.
- **D10. The order of work.** W6's benchmarks (two words against three for a link word; the Fp128 multiply's cost) start right
  after W1, on a hand-rolled loop over W1's structures, in parallel with W3, and feed W3 before its API header is agreed. The
  owner reviews W3's API header after the integrator: it decides whether units are easy to build, half the goal.
- **D11. Files.** The new series' workers live in `sim/planning/sessions/` (never colliding with the old WORKER1-30); this
  project's decisions in `sim/planning/`.

## 2026-11-07

- **D12. A golden trace carries its configuration.** Every golden trace starts with a header: the latencies (GO and READY 8,
  the register load, the rest of `Config`'s relevant values), `stage_delay`, the shuffle seed, the tile, and the image's
  sha256. The C++ test bench checks its own configuration against the header before comparing and refuses on any difference,
  so a parameter mismatch shows as one clear error rather than every cycle disagreeing.
- **D13. The runner, for sim/** (owner; *a note on pinning the owner's runner, not a project rule: D21*). clang and lld installed, `kernel.perf_event_paranoid=1`, `vm.mmap_rnd_bits=28`
  (TSan), confirmed by Quest 122. The runner has 6 performance cores with hyperthreading plus 4 efficiency cores: **benchmark
  threads are pinned to the performance cores** (`taskset -c 0-11`: CPUs 0-11 are the six P-cores and their hyperthreads, 12-15 the E-cores; Quest 122), and any run on the efficiency
  cores is reported separately. Scaling is reported at 1, 6 and 12 threads on the P-cores.
  Quest 122 confirmed clang and lld, perf (`perf record` and `report` work, IPC visible per core type) and TSan on gcc and clang;
  its `mmap_rnd_bits` check failed only because the runner's user can't read the sysctl, while TSan itself now works.
- **D14. Fp128 is a Crandall prime** (owner): 2^128 - 159 for the benchmarks. Reduce by folding, not Montgomery: multiply the
  high half of the 256-bit product by 159, combine it with the low half in ~160-bit arithmetic, then normalise.
- **D15. The bar** (D8, measured by W2, Quest 123: ReplayBench on segment 0 of evm-zone-29, `sim-step-cost`, repeat 2):
  **36.8 µs per chip-cycle for one replay alone, and about 100,000-110,000 chip-cycles/s for the whole box.** Today's engine
  is memory-bound across the box (12 JVMs give 3.6× one; two sharing a P-core each run 3.4× slower), so the new framework is
  judged on **memory footprint per chip** as well as speed per thread: *(amended by D26: we simulate one chip at a time, so footprint matters for how well one chip's state fits the caches of
  the cores it is split over, not for running chips side by side.)*
- **D16. Word width** (owner, on W6's T1): runtime in declarations, specialised inside the framework, never visible to unit
  authors. Internal templates on the word count (1, 2, 3) are fine: at most three instantiations, each genuinely different
  code. What to avoid is templating on payload types, which stamps out identical copies of the same code for same-sized
  types; payloads stay untyped words.
- **D17. The link word is (b), three words inline** (owner, on W6's report; reverses the owner's proposal (a)): no
  allocation or free per 128-bit payload, no copy when crossing threads, one kind of payload; 12% less memory per mesh; (a)'s
  edge under memory pressure (up to 5.5%) doesn't pay for that machinery. Units hold 128-bit values inline as two words. The
  thread's 128-bit value array remains only for payloads larger than 128 bits.
- **D18. Release and timing builds use clang** (owner, on W6's report: ~10% on the mesh; an Fp128 multiply 2.4 ns against
  gcc's best 4.3). gcc stays in `make check`, both compilers in both builds.
- **D19. API reviews** (owner, 2026-11-07; amends D10): the integrator reviews APIs; the owner looks only at points the
  integrator flags for his judgement. Stage 2 and later implementation still start on the owner's go-ahead.
- **D20. Explicit wakes, with tools to catch a missed one** (owner, 2026-11-07): a unit that leaves data in an input is not
  stepped again unless it asks; accepted provided development has tools that catch the mistake: the step-all check
  (digest equal to the ready-driven run), a debug detector naming any unit asleep with data waiting in an input and no wake
  pending, and an end-of-run report of the same. Stage 2 of W3 and W5 start (owner's go-ahead).
- **D21. Nothing machine-specific is baked in** (owner, 2026-11-07; the owner also runs on other machines, e.g. Snarkify's
  servers). CPU sets, core counts and core types are arguments or environment settings, or detected at run time (`lscpu`,
  `/sys/devices/system/cpu`, including P/E core types on hybrid chips: `/sys/devices/cpu_core/cpus`, `cpu_atom/cpus`),
  never hardcoded. The framework's thread placement takes a CPU list from configuration, with a sensible detected default.
  **Every benchmark result records its machine**: CPU model, core types and counts, clocks, memory, compiler and versions.
  D13 stays as the note on how to pin on the owner's runner. Existing scripts with hardcoded CPU lists (W2's `baseline.sh`,
  W6's `sim/bench/quest/*`, W4's and W5's task scripts) are made to take them as arguments, defaulting to detection.
- **D22. The honest speed figure** (2026-11-07, W5, Quest 130): **unit for unit, the framework is about 4.5× `ref` per
  thread** (Controller plus TileControl: ~23 ns per instruction, engine included, against ~104; identical traces). The 58×
  (idle-skip on) and 2,300× (off) figures mainly measure chip-level work M1 doesn't model yet: **don't quote them as
  speedups.**
- **D23. Chip-level work is event-driven too** (2026-11-07, from `ref`'s profile with idle-skip on: 33% the request bus,
  19.5% scanning overflows, 17% the chip's own loop, 2.3% the controller). When M2 models the whole chip, nothing scans all
  tiles every cycle (overflows, invariants, the request bus): such work is driven by the events that cause it, like every
  unit, or the framework rebuilds `ref`'s bottleneck one level up.
- **D24. Diagnostics don't allocate** (2026-11-07, from W5's finding): every fatal and report path in the engine formats
  into a preallocated buffer (`snprintf`), or records fixed-size facts (cycle, unit, port) in a list sized at startup and
  formats them after the run. No allocation in the cycle loop, diagnostics included.

## M2

- **D25. M2's target** (owner, 2026-11-07): **the full-size sort** (`p4/uniform-k128`) on the new framework, exact against
  `ref`, **multithreaded**, to see how fast it is against today's engine: about 110 s on one `ref` JVM, **aiming at about
  4 s** excluding setup. *(W7, Quest 134: the counted span is ~96 s; the aim is ~24× that.)* The plan is `PLAN-M2.md`.
- **D26. One chip at a time** (owner, 2026-11-07; corrects SIMULATOR-NEXT 4's "throughput first, independent chips"): we
  simulate one chip at a time and don't expect to run separate simulations side by side. **M1's form 1** (N independent
  machines on N threads) **is a thread-safety test only**, not a benchmark or a design goal: don't quote its scaling (W5's
  Quest 129, W4's Quest 128, W6's throughput against N). **The figures that count** are **speed per thread** (~4.5× `ref`
  unit for unit, D22) and **one chip split over threads** (form 2: ~3× on four threads for the mesh so far, Quest 133).
  M2's work pushes those two: a faster thread, and a cheaper, load-balanced split.
- **D27. Model `ref` as it runs** (2026-11-07, W8's finding on `pendingAdd`): where `ref`'s default run and the spec's
  intent differ, the framework reproduces `ref`'s default run (its fixed child order, no shuffle) and the difference is
  recorded for the spec. First case: `Tile.children` steps the director before the core, so `TileControl.pendingAdd` is
  always 0 in `ref`'s run: the director judges a due decrement without the core's same-cycle counter store. The
  framework models that order-free (the core's store reaches the register block registered, for t + 1). No M2 program
  reaches the case (W7's `pendcheck`: 0); `AUDIT-M2` sections 2.5 and 3.3 are corrected accordingly.
- **D28. Where `ref` isn't self-consistent, follow the rule** (2026-11-07, W9's finding on router overflows): `ref` judges
  a router FIFO's put only when the FIFO is next touched (its selector reaching it, a later put, or idle-skip's quiescence
  scan), so an overflow's count, and ERRORS' ROUTER_OVF bit, rise a variable number of cycles late, differently with
  idle-skip on and off. That is an artefact of `ref`'s lazy commit, not a behaviour D27 asks us to reproduce: `ref` has no
  single answer. The framework counts at t + 1 (PORTS.md's rule), and references avoid the case (no M2 program
  overflows a router FIFO; the fingerprint carries no counts). D27 still applies wherever `ref`'s default run is
  self-consistent.
- **D29. Where reproducing `ref` would need news faster than a Link of 3 can carry, follow the spec** (2026-11-07, W9's
  finding on a second READY pulse): `ref` keeps one timestamp per tile, so a READY raised again while the earlier one is
  still visible reads 0 at C&C until the new pulse lands. C&C can't know of that pulse before it arrives without a link
  shorter than 3 cycles, which would break form 2 (every crossing a Link of at least 3, D26). The framework keeps the
  spec's sticky bit, **counts the case** (`ready_repulses`), and references avoid it (0 in every M2 run so far).
  Also from D28's reasoning: **an overflow that feeds ERRORS reaches the register block as an event** (PORTS.md section
  8), counted at t + 1, never seen only at the block's next step: ROUTER_OVF, the write engine's input and the gather
  queue alike.
- **D30. Very close is enough where exact would be costly** (owner, 2026-11-08): the RTL doesn't match `ref` cycle for
  cycle either (jumps take more cycles, and other small things), so `ref` is the oracle for finding bugs, not a timing
  truth worth any price. **We don't rewrite large amounts of code, or restructure, to fix a difference of a cycle or so.**
  Exactness stays the default check: a difference from `ref` is a bug until it's explained. Where reproducing `ref`
  exactly would make the code markedly more complex, a worker proposes a **deliberate difference**; the integrator
  approves it and it's recorded here or in the unit's notes, with what it changes and why it's acceptable. Prefer
  differences no reference program reaches (as D28, D29), so every check stays exact. Where one does show in a program:
  the result must be identical, the cycle count very close (working bound: within 1% of `ref`'s on the sort's counted
  span), and the comparison uses the fingerprints before the difference plus the end result.
- **D31. Enough speed work for M2** (owner, 2026-11-08): the full-size sort is exact and 17.8× `ref` at 6 threads (5.38 s
  against ~96 s; Quest 157). M2's speed work ends with **two cheap, general wins**: BigSram's running memory hash kept only
  when fingerprints are wanted (W11), and the register block writing only the registers that changed (W12). Then M2
  closes and M3 is planned. No further unit-specific tuning (router layout, huge pages, index arithmetic) for this
  workload: the units that matter next (the field engine, D7) don't exist yet, and the framework's own overhead is ~8%.

## M3

- **D32. M3's target** (owner, 2026-11-09): **get the simulator as it exists in v1.3 running on the new framework: the
  Tracer and the replayers.** The defining workload is a segment on the full chip as `ref`'s `TracerT2` runs it (W13
  confirms the run). Exact first (D30 applies), then timed. The plan is `PLAN-M3.md`.
- **D33. A deliberate difference (D30): C&C 1's peripheral stores one cycle later** (integrator, 2026-11-09, W16's
  proposal; AUDIT-M3 3.4's open item). `ref`'s C&C 1 peripheral applies a store within the store's own cycle; the slot's
  rule applies stores at level 0 of t + 1. The framework keeps the slot's rule, so C&C 1's own work (the watchdog's first
  count, a UART byte's start) runs one cycle later than `ref`. Neither reference run reaches either (W13's coverage);
  exactness otherwise would need a special path in the generic slot.
- **D34. The request bus: exact or splittable, chosen explicitly** (integrator, 2026-11-09, W14's question; D29 applied).
  In `ref` three request-bus couplings act within one cycle (a tile's head beat leaving only if its row node's buffer has
  room at that instant; a row node forwarding only if the root's input has room; a REQUEST_SEND accepted when the bus
  freed the head that cycle). Reproducing that needs the senders in the bus's domain. So the bus has **one
  implementation with its credit latency as a configuration**: **0, exact as `ref`** (every sender in the bus's domain;
  a split is fatal at build), used by ReqConf and exactness runs; **3, the spec under D29** (each port's leaf in its tile,
  beats and credits over Link(3), splittable), **the default**. Mode 3 equals `ref` until some node's 8-beat buffer fills;
  it **counts** the cycles where the two would differ, and references avoid them (T2's two boot requests never reach it).
  The mode is never derived from the partition, so results stay identical across thread counts and partitions.
- **D35. M3's speed work: one cheap, general fix first** (owner, 2026-11-10). T2 is exact on one thread and on the
  6-domain split (Quest 170); its segment takes 24.3 s / 26.6 s on one thread and 21.0 s / 24.1 s on the split, against
  `ref`'s 46.2 s / 50.9 s with idle-skip (and v1.3's acceptance run's ~18 min without it). About 59% of one thread's time
  is the slot republishing its whole register window every step: W15 makes it publish only the registers that changed
  (as W12 did for the register block, D31), with a debug guard and every point unchanged. Then measure again; further
  speed work only if the owner asks.
- **D36. M3 closes; the new framework running v1.3 is v1.35's first step** (owner, 2026-11-10). T2 is exact at every
  point on both reference runs and 6.5× `ref` (Quest 171). Further work is v1.35's, as the owner defines it.
- **D37. The name** (owner, 2026-11-10): **meshsim is the simulator, whatever the implementation underneath.** The C++
  framework in `sim/` is **meshsim v1.35**; the frozen Java copy in `sim/ref` is meshsim v1.3, the exactness oracle. No
  new product name.
- **D38. v1.35's scope** (owner, 2026-11-10): **v1.35 freezes the fast simulator** (`sim/`), with no other improvements
  than updating all its docs and build scripts and cleanup. **No Java in v1.35**, provided it has an equivalent for
  everything v1.3's Java meshsim had (the Java stays available as v1.3). `sim/` is v1.35's work-in-progress folder; at
  the freeze it is copied, without `sim/ref`, to a frozen `v1.35/`. The plan is `PLAN-V135.md`.
