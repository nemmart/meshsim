# Worker 1 (sim/): the build, the discipline, and the cycle loop's structures

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`): very fast, parallel, and easy to
build units in, with v1.3's Java simulator (`sim/ref/`) as the exactness oracle. Milestone 1 is the primitives plus one RV32
controller running real firmware, matching `ref` instruction for instruction. You lay the foundation everything else stands
on: the build, the rules that keep C++ honest, and the low-level structures the cycle loop is made of. W2 works beside you
on the reference side (Java); W6 (benchmarks) and W3 (configuration, engine, ports) start on your structures when you finish,
so their shape matters more than their number of features.

READ FIRST: `sim/README.md` (the owner's direction: the core loop, memory, payloads as untyped 64-bit words, sizing at
startup, the two coding regimes, the build rules), `wip/planning/SIMULATOR-NEXT.md` (why, what was measured),
`sim/planning/PLAN-M1.md` (the plan; you are items 1 and 2), `sim/planning/DECISIONS.md`. For background,
`wip/planning/sessions/WORKER29-DESIGN.md` sections 1, 5 and 6 (the measurements, the wheel, the layout). For the runner:
the Quest repository's `PIPELINE.md` and `NUMBERS.md`, and `wip/planning/INTEGRATOR-HANDOVER.md`'s "How we work" (task
numbers, embedded kits, the meshsim repository being private).

THE WORK
1. **The build** (`sim/Makefile`, plain make): gcc and clang, C++20, `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`; a
   debug build with ASan and UBSan, a release build (`-O2` or `-O3`, your call, measured later); `make check` builds and runs
   every test in both, on both compilers where present. `FATAL(fmt, ...)`: the context to stderr with `fprintf`, then
   `std::abort()`. A tiny test harness of our own (no test framework), with **death tests** (fork, expect the abort and a
   message matching a pattern). **The allocation hook:** any allocation between `begin_run()` and `end_run()` is fatal,
   proven to fire, for every later test to use.
2. **The structures** (cycle-loop code: no STL, no allocation, no virtual calls, inlinable; untyped `uint64_t` words):
   - a **ring of N-word slots**, N and the depth fixed at construction (from a declaration), push, peek, pop, count;
   - a **bitmap** with set, clear, test and find-first / iterate set bits (`__builtin_ctzll`);
   - an **index pool** over the thread's array of `unsigned __int128` values, its free slots a bitmap: allocate returns an
     index, free returns it; the array never grows;
   - **delivery buckets**: append-only arrays of fixed-width entries (the entry width a parameter: W6 measures two words
     against three for a link word, so don't bake one in), cleared by resetting a count;
   - a **per-type arena** for permanent state (allocated once with `new`/`malloc` at build time, objects of one type
     contiguous, big arrays allocated apart).
   Every one sized once, **fatal when full**, its message naming the structure and its capacity. Keep the interfaces small;
   W3 will build ports and the wheel from them.
3. **Tests:** unit tests per structure; a **seeded differential test** per structure (10⁶ operations against an STL model,
   fixed seeds, deterministic); death tests for every overflow and misuse you can detect in debug; all under ASan and UBSan.
4. **The runner probe** (one Quest task): `nproc`, `lscpu`, memory, gcc and clang versions, `make`, the riscv toolchain
   (`riscv64-unknown-elf-gcc` or similar), Java, `perf`, transparent huge pages; then `make check` from an embedded kit. Write
   what you find in your NOTES, and from it the recommendation on prebuilt firmware images (DECISIONS D4).

RULES
- **Propose your plan first** (the files, each structure's interface, the tests) in `WORKER1/NOTES.md`, and wait for the
  go-ahead before writing code.
- Work on branch `sim-w1`; the integrator merges. Keep your notes, questions and report in `sim/planning/sessions/WORKER1/`.
- No STL, allocation or exceptions in cycle-loop code; anything goes in tests and configuration (DECISIONS D6).
- Long runs go to the runner (claim plain Quest numbers in `NUMBERS.md` first); background jobs don't survive a turn.
- Never write a token into a file. Never `pkill -f` a pattern that matches your own command line.
- Commit and push before anything long in a turn, keep the last tool calls for committing, end each turn with a clean
  `git status`.
- Keep replies to the owner short.

DELIVERABLES: the branch with the build, the structures and their tests (`make check` clean on both compilers, both builds);
the runner probe's results; `WORKER1/REPORT.md` (what was built, each structure's interface in a paragraph, the runner's
facts, anything W3 and W6 should know).
