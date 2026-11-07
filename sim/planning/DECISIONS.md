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
- **D13. The runner, for sim/** (owner). clang and lld installed, `kernel.perf_event_paranoid=1`, `vm.mmap_rnd_bits=28`
  (TSan), confirmed by Quest 122. The runner has 6 performance cores with hyperthreading plus 4 efficiency cores: **benchmark
  threads are pinned to the performance cores** (`taskset`; Quest 122 lists the CPU ids), and any run on the efficiency
  cores is reported separately. Scaling is reported at 1, 6 and 12 threads on the P-cores.
