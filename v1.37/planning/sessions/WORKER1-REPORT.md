# WORKER1 (sim/): report

Branch `sim-w1`. Items 1 and 2 of `PLAN-M1.md`: the build, the discipline, the cycle loop's structures, their tests, and the
runner probe (Quest 121). The plan and every detail are in `NOTES.md`.

## What was built

| file | what |
|---|---|
| `sim/Makefile` | plain make; gcc and clang; C++20, `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`; debug (`-O1 -g`, ASan + UBSan, no recovery) and release (`-O2 -DNDEBUG`) into `build/<cc>-<mode>/`; `make check` runs every test in both builds with every compiler present |
| `src/base/fatal.h/.cpp` | `FATAL(fmt, ...)` (always) and `DCHECK(cond, fmt, ...)` (debug only, compiled but not evaluated in release) |
| `src/base/alloc_guard.h/.cpp` | `begin_run()` / `end_run()` / `in_run()`: allocation or deallocation in a run is fatal |
| `src/base/words.h` | the storage convention: `round_to_lines`, `line_aligned`, the alignment DCHECK |
| `src/base/ring.h`, `bitmap.h`, `value_pool.h`, `bucket.h` | the cycle-loop structures, header-only |
| `src/base/arena.h/.cpp` | `alloc_big`, `Block`, `TypeArena<T>` (configuration time) |
| `tests/harness.h/.cpp` | `TEST`, `DEATH_TEST`, `DEBUG_DEATH_TEST`, `CHECK`, `CHECK_EQ`, `test::Rng` (splitmix64) |
| `tests/test_*.cpp` | one binary per structure, plus `test_base` (FATAL, DCHECK, the hook, the harness) |

`make check` is clean on gcc 13.3 and clang 18.1, debug and release (29 s locally on one core); on the runner, gcc only,
15 s. Per structure: unit tests, a seeded differential test (seeds 1-3, 10⁶ operations each, against an STL model), a death
test for every full condition (both builds) and every misuse DCHECK (debug), and a test that runs its cycle-loop operations
inside `begin_run()`/`end_run()`. 76 tests in debug.

## The interfaces

**The shared shape.** Every cycle-loop structure is a small header object over storage it doesn't own:
`static words_needed(...)` (rounded up to whole 64-byte lines) and `init(uint64_t* storage, ..., const char* name)`, which
DCHECKs that the storage is line-aligned. So a sizing pass adds up `words_needed`, allocates one block per thread
(`alloc_big` or a `Block`) and carves it, every structure on its own lines. Full is fatal in every build, the message naming
the kind, the name and the capacity (`ring 'north_in' full: capacity 4 (width 3)`); misuse is DCHECK. No STL, no allocation,
no virtual calls; words are untyped `uint64_t`. Widths are runtime values; a fixed-width form, if W6 justifies it and the
owner agrees, would keep the same method set so callers don't change.

**Ring** (`init(storage, width, depth, name)`): a FIFO of `depth` slots of `width` words; depth need not be a power of two
(wrap by compare). `uint64_t* push()` returns the newest slot to fill, `push(const uint64_t*)` copies one in; `peek(i)` the
i-th oldest (0 = head), `peek_mut(i)`; `pop()`; `count()`, `empty()`, `full()`, `width()`, `depth()`.

**Bitmap** (`init(storage, nbits, name)`, starts clear): `set`, `clear`, `test`; `find_first(from)` (the lowest set bit at or
after `from`, or `size()` if none, `__builtin_ctzll`); `for_each_set(f)` ascending (a template, inlined); `any()`,
`popcount()`, `set_all()`, `clear_all()`; `word(w)`, `word_count()` for structures built on it. Bits at or past `size()` are
never set, so the scans need no mask.

**ValuePool** (`init(storage, capacity, name)`, all free): the thread's `unsigned __int128` array (`sim::u128`, 16-byte
aligned at the start of the storage) and its free bitmap (1 = free) on the following lines. `uint32_t alloc()` returns the
**lowest free index** (deterministic), using a hint word so it doesn't rescan from 0; `free(i)`; `pool[i]` (DCHECK that `i`
is allocated); `allocated(i)`, `in_use()`, `capacity()`.

**Bucket** (`init(storage, width, capacity, name)`): an append-only array of `width`-word entries. `uint64_t* append()`,
`append(const uint64_t*)`; `entry(i)`; `begin()` (all entries as one array, front to back, for the delivery walk); `clear()`
resets the count; `count()`, `empty()`, `width()`, `capacity()`.

**Permanent state.** `alloc_big(bytes, name)`: zeroed, 64-byte aligned; from 2 MB up, 2 MB aligned with
`madvise(MADV_HUGEPAGE)`; `free_big(p)`. `Block(words, name)`: RAII over `alloc_big`. `TypeArena<T>(capacity, name)`: all
objects of one type contiguous, `make(args...)` constructs the next in place (fatal when full), `ar[i]`, `data()`, `size()`;
destructors run when the arena goes. Big arrays go through `alloc_big`, apart from the arena.

**The allocation hook.** A thread-local flag. Every form of `operator new` and `delete` is replaced; `malloc`, `calloc`,
`realloc`, `free`, `aligned_alloc` and `posix_memalign` from our own object files go through `-Wl,--wrap=...` (the
Makefile's `WRAP`, needed on every link). Proven by death tests in debug (under ASan) and release on both compilers; the STL
in a run is caught too. Not caught: allocations inside libc itself. FATAL clears the flag before reporting.

## The runner (Quest 121)

16 threads but **hybrid**: an i5-14400F, 6 P-cores with HT + 4 E-cores, L3 20 MB, 62 GB, one NUMA node, no cgroup limits
(the runner was started from a login session). gcc 13.3 only (**no clang**); **no riscv toolchain**; Java 21 with javac;
perf installed but `perf_event_paranoid` = 4; THP madvise; gcc's TSan fails on the kernel's mmap randomisation. Details in
`NOTES.md`.

**D4:** commit prebuilt firmware images in `sim/fw/` with their sources and build script; ship the C++ framework in kits
as source (it builds there). Optional for the owner: `apt install clang lld`, `perf_event_paranoid` = 1,
`vm.mmap_rnd_bits=28`.

## For W3 and W6

- **Carve, don't allocate.** Size with `words_needed`, allocate one `Block`/`alloc_big` per thread, `init` each structure on
  its slice. Each structure starts on a line boundary; adjacent structures never share a line.
- **Names are `const char*` kept by pointer:** they must outlive the structure (strings owned by the configuration).
- **Full is always fatal; misuse only in debug.** Release keeps one predicted branch per push/append/alloc.
- **Ring and Bucket width is a runtime parameter**; W6's two-against-three-words measurement can use them directly. If a fixed
  width wins measurably, that goes to the owner (it edges toward templated transports).
- **The hook in tests:** keep allocations visible to the compiler from being elided (a `volatile` sink), or a "no allocation"
  proof proves nothing. Link every binary with the Makefile's `WRAP`.
- **On the runner:** gcc only for now; plan scaling runs as 1, 6 (P-cores), 12 (P + HT) and 16 threads, since the cores are
  unequal; THP needs `madvise`, which `alloc_big` does; perf and TSan need the owner's sysctls (or `setarch -R` for TSan).
