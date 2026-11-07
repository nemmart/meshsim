# WORKER1 notes: the plan (for the go-ahead)

## Files

```
sim/Makefile
sim/src/base/fatal.h        FATAL(fmt, ...) always on; DCHECK(cond, fmt, ...) debug only; both fprintf + std::abort()
sim/src/base/alloc_guard.h  begin_run() / end_run() / in_run(); the hook itself in alloc_guard.cpp
sim/src/base/ring.h         Ring
sim/src/base/bitmap.h       Bitmap
sim/src/base/value_pool.h   ValuePool (the index pool over unsigned __int128)
sim/src/base/bucket.h       Bucket (one delivery bucket)
sim/src/base/arena.h        TypeArena<T>, alloc_big()
sim/tests/harness.h/.cpp    TEST, DEATH_TEST, CHECK*, a seeded PRNG (splitmix64: our own, identical on both compilers)
sim/tests/test_base.cpp     FATAL, DCHECK, the allocation hook, the harness's own death tests
sim/tests/test_<structure>.cpp   unit, differential and death tests, one binary per structure
```

## Shared shape of every structure

- **Storage from outside.** Each has `static size_t words_needed(...)` and `init(uint64_t* storage, ..., const char* name)`, so W3's
  sizing pass adds them up and carves one block per thread; an owning `create(...)` for tests and simple uses.
- **Fatal when full, in both builds** (one predicted branch), message `"<kind> '<name>' full: capacity N (width W)"`.
  **Misuse** (pop/peek empty, index out of range, double free, bit out of range) is `DCHECK`: debug only.
- Header-only, no STL, no virtuals, no allocation after `init`; payloads are `uint64_t` words; `name` is a pointer kept in a
  cold field, never copied.

## Interfaces

- **Ring** (N-word slots, depth D, both runtime): `uint64_t* push()` (slot to fill), `void push(const uint64_t* w)`,
  `const uint64_t* peek(unsigned i = 0)` (i-th oldest), `void pop()`, `count()`, `empty()`, `full()`, `width()`, `depth()`.
  Head and count, wrap by compare (D need not be a power of two).
- **Bitmap** (n bits): `set(i)`, `clear(i)`, `test(i)`, `find_first(from = 0)` (returns n if none, `__builtin_ctzll`),
  `for_each_set(f)` (template, inlined), `any()`, `popcount()`, `clear_all()`.
- **ValuePool** (capacity C over the thread's `unsigned __int128[C]`, free slots a Bitmap, 1 = free): `uint32_t alloc()`
  (lowest free index, so deterministic; keeps a hint word so find-first doesn't rescan from 0), `void free(uint32_t)`,
  `unsigned __int128& operator[](uint32_t)`, `in_use()`, `capacity()`.
- **Bucket** (entries of E words, E a runtime parameter, capacity C): `uint64_t* append()`, `void append(const uint64_t*)`,
  `const uint64_t* entry(unsigned i)`, `count()`, `clear()` (count = 0). The wheel of buckets is W3's.
- **TypeArena<T>** (permanent state, configuration time): `TypeArena<T>(capacity, name)`, `T* make(args...)` (placement new,
  contiguous, fatal when full), `T& operator[](i)`, `size()`; destructors run at teardown. **`alloc_big(bytes, name)`**: a
  separate allocation for big arrays (aligned to 2 MB with `madvise(MADV_HUGEPAGE)` above 2 MB, since huge pages measured ~10%),
  so they never sit between a type's objects.

## The allocation hook

A thread-local "in run" flag (W4's threads each get their own). Replaced `operator new`/`delete` (every form) die with
`"allocation of N bytes inside a run"` while it is set; our own code's `malloc`/`calloc`/`realloc` go through
`-Wl,--wrap=...` and do the same. Not caught: allocations inside libc itself (rare, and none in the cycle loop by
construction). Proven by death tests (new, new[], malloc each fire; outside a run they don't), and every structure's
cycle-loop operations run inside `begin_run()`/`end_run()` in their tests.

## Build

`make` (release, gcc), `make debug`, `make check`: every test binary in debug and release, with gcc and with clang where
present (`command -v clang++`), into `build/<cc>-<mode>/`. Common: `-std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror`.
Debug: `-O1 -g -fsanitize=address,undefined -fno-sanitize=vptr -fno-sanitize-recover=all -fno-omit-frame-pointer`. Release:
`-O2 -DNDEBUG` (no `-march=native`: W6 measures -O2/-O3/native). The container has gcc 13 only; I'll apt-install clang here.

## Tests

- Unit tests per structure (edges: depth 1, width 1 and 3, wrap-around, word boundaries 63/64/65 bits, capacity exactly hit).
- **Differential**, per structure: 10⁶ random operations against an STL model (`std::deque` of word arrays, `std::vector<bool>`,
  `std::set` of free indices, `std::vector`), seeds 1, 2, 3 fixed, all results and contents compared, under ASan/UBSan.
- **Death tests** (fork, child's stderr through a pipe, expect SIGABRT and a POSIX regex match): every full condition, every
  `DCHECK` misuse (debug only), the hook.

## The runner probe

Task: next free plain number in Quest's `NUMBERS.md` (121 today), `121-w1-sim-probe.sh`: `nproc`, `lscpu`, `free`, `numactl -H`,
cgroup limits (memory, tasks), gcc/clang/make versions, riscv toolchains (`riscv64-unknown-elf-gcc`, `riscv32-*`,
`riscv64-linux-gnu-gcc`, clang's riscv32 target), `java -version`, `perf` and `perf_event_paranoid`, THP `enabled`/`defrag`;
then `make check` from an embedded kit (sim/Makefile, src, tests; `exit` before the kit). Facts and the D4 recommendation go here.

## Questions

1. Full checks always on, misuse checks debug only: agreed?
2. Ring and Bucket width as a runtime value (one code path, W3-friendly), or a template parameter? I'd start runtime and have W6
   measure a fixed-width form against it.
