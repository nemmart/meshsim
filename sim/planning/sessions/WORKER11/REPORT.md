# WORKER11 report: M2's G3 (the memory path)

Branch `sim-w11`, merged into `main` piece by piece. Day-by-day detail is in `NOTES.md`; this is what the integrator,
W12 and M3 need.

**State at hand-off.** The memory path is built and equal to ref:
- **`regs`** equals `golden-m2` to its end (869): every unit of tile 0 every cycle, every tile every 10 cycles, on one
  thread, under the debug options, and split over 2, 4 and 8 domains.
- **`a2a`** equals its golden to its end (7698): tile 0 every 10 cycles and every tile every 100, one thread and split
  over 4. **It runs in `make check`** (since main had W12's PULSE_DMA, merged at 808b72a).
- **MemConf** shows my units equal to ref every cycle, on fixtures and long runs (section 4).
- **The 16K and full-size sorts** equal ref with every one of my units in them: W12 ran them on the runner (Quest
  154), with 248 and 321 points, each on one thread and on 8 domains, covering every unit of every tile. I did not
  regenerate the sort golden locally.
- **`make check`** is clean at 6aef29b: gcc and clang, debug and release, 42 binaries each; tsan, tsan-units and
  tsan-chip on both compilers.
- **No D30 difference** was needed.

## 1. Files

| file | what |
|---|---|
| `src/units/mem.h` | the crossing with G4: the read-queue clients, the request words, the table-store words, `MemIO` |
| `src/units/big_sram.h` | `BigSram` (16 area queues, contents, incremental sum) and `SramBlocks` (one mapping per tile) |
| `src/units/write_engine.h` | the write engine's behaviour (W8 built the configuration part) |
| `src/units/table_store.h` | `ReqFifo`, `Rmw`, `TableStore` (PTR, STREAM, GROUP, the PTR view), `HistStore` |
| `src/units/inject.h/.cpp` | `InjReadSide`: the SRAM read queue inside W9's injection unit |
| `src/units/tile_ctl.*`, `rv32.h`, `regmap.h`, `regblock.h`, `director.h` | my bits of W8's files (section 3) |
| `src/units/chip.*` | `ControlIO::mem`, wired by `tile_cycle` |
| `src/units/mem.mk` | build rules for `test_mem` and `test_memconf` |
| `tests/statefp_g1.*`, `statefp_g2.*` | writers for we, ts, hist, sram, sram.data, rdport, srq and inj's read-return hold |
| `tests/test_mem.cpp` | hand-derived unit tests (8) |
| `tests/test_memconf.cpp`, `tests/ref/MemConf.java`, `memconf.sh`, `tests/memconf/` | the memory path against ref |
| `tests/test_chip.cpp` | additions: regs to its end, a2a (input and sequence), the read port in one tile, the footprint line |
| `tests/mem_stand.h` | the read queue's ends for tests that build `add_g1` without the chip |

## 2. The units

**Big SRAM (`BigSram`).** Each cycle it does three things, in order:
1. Every non-empty area (found by a mask) serves its head.
2. The write Hold arrives, then the read Hold. Room is judged by the count before the pop (13a); the write counts in
   first. A request without room stays in its Hold, and the producer's next put is refused.
3. A write may merge into its area's last queued write if they share a row and have disjoint chunks (write
   combining, configurable).

Reads return on 16 `Link(sram_read_latency)`s. Writes at or past `mem_chunks` are dropped.
- **Storage:** one zero-filled anonymous mapping per tile, made at configuration (`SramBlocks`), so nothing is
  allocated in the loop and the OS backs only touched pages.
- **Footprint (for W10):** 256 MB is allocated. `regs` touches 256 KB, `a2a` 2.3 MB, `wfi` none. `test_chip` prints
  both figures.
- **The fingerprint's sum** is kept at every store; `poke`/`peek` (32 and 64 bits) keep it too.

**Write engine.**
- **The pointer stage** has 2L + 2 entries in order. PTR read-modify-writes and their in-order returns go through Links
  to the table store, and count-on-arrival to HIST.
- **SRAM writes** go through a Hold: a refused write is put again and the stage fills.
- **Modes:** 0 (region 0x03), 1, 2 (bucket digit) and 3 (the augmented record). A misaligned PTR is undefined behaviour
  and fatal, named.
- **The we-in overflow** reaches ERRORS [4] as an event through W10's `overflow_steps`.
- **Missed-wake threshold:** 1,024 cycles. A full stage waits on a PTR return that sits in the table store's request
  FIFO, which is unit state; the wait is bounded by that FIFO and the two latencies.

**Table store and HIST.**
- **Request FIFOs:** each Link feeds an internal 16-entry `ReqFifo` that judges a put at t + 1 against the occupancy at
  the start of t (PORTS.md 3). An overflow reaches ERRORS [5] (TABLE_OVF) as an event.
- **Service:** one STREAM read, one GROUP read and one PTR RMW a cycle. HIST has its own RMW.
- **The RMW pipe:** at most one write-back is pending, and the store steps at its due cycle.
- **The window's figure-12 rule:**
  - A controller store on a write-back cycle is lost.
  - A HIST load on one returns the engine's value. HIST's RegFile carries the write-back's cycle and value in two extra
    registers, so the core decides with no ordering dependence.
- **The PTR view for W12's AG:** PTR as committed at the end of t − 1. Ref runs the AG before the store's write-back,
  so a write-back of t is published at t + 1.

**The read queue (`InjReadSide`, inside the injection unit, AUDIT section 2 item 2).**
- **Tokens:** 64 slots, shared with W9's send side through `Tokens`.
- **The free list** is a ring in ref's `ArrayDeque` order: taken at the head, returned at the tail.
- **Returns:** the gather and the AG each have an in-order return queue. Read-port entries are ordered by (ready cycle,
  token), as ref's TreeSet orders them.
- **One release a cycle** goes into the read-return Hold. That Hold lives inside the unit: put at t, injected at t + 1,
  never refused.
- **One issue a cycle:** a refused SRAM Hold is re-put and blocks the issue.
- **Zero-width reads** (past the end, or size 11) are ready at t + 2 + latency, with a timed wake.
- **Injection frees the token,** or moves it to the loopback count if the packet loops.
- **Configuration:** clients by flag (W12's gather and AG). `gather_srq_busy` goes to the block; the read port's
  injected count goes to the core.

**The read port.**
- **A store to SRAM_RD_ADDR_QUEUE** queues the read with SRAM_RD_DEST_ADDR as it stands at t. Free entries are
  computed as depth − (stores − injections before t). A store with no free entry is dropped.
- **The block** advances the destination either way, and counts READ_PORT (ERRORS [7]) from a bit in the store's bus
  word.
- **A load** returns the free entries at its sampling cycle.
- **The director** counts results accepted, and packets accepted while results are outstanding (counted only).

## 3. Shared files and the crossings

**With W12 (agreed, `mem.h`):**
- the gather's and AG's read Holds (2 words) into clients 0 and 1;
- `gather_srq_busy` (Reg) into the block;
- STREAM/GROUP requests and returns as `Link(ts_engine_latency)` (3 words);
- the PTR view (RegFile).

The register block and director were split by bit:
- **Mine:** SRAM_RD_*, READ_PORT, WE_IN_OVF, TABLE_OVF, the read-port results.
- **W12's:** PULSE_DMA, STATUS's rd and gather bits, EVENTS [10], ERRORS [3] and [6], mode 4.

**`ControlIO::mem` (`MemIO`).** The read queue's ports live in W9's injection unit. `add_g1` fills them and
`tile_cycle` wires them when `mem.present`, so network-only tiles, RouterConf and `test_router` keep their ports.

**W10's framework.** I used W10's Fifo overflow event (`overflow_steps`, `Ctx::overflows`) for WE_IN_OVF.

## 4. How it is checked

- **`test_chip`:**
  - `regs` and `wfi`: every unit of tile 0 every cycle; one thread, step-all, shuffle, instance-major; 2, 4 and 8
    domains.
  - Every tile every 10 cycles (`CHIP_FP_DIR`, the hash-only goldens regenerated from ref).
  - `a2a`, in `make check`: tile 0 every 10 cycles to its end, on one thread and on 4 domains; every tile every 100
    with `CHIP_FP_DIR`.
  - The read port end to end in one tile, from a hand-assembled boot ROM.
  - The router overflow test (W9's).
- **`test_mem`** (hand-derived, each passed as derived):
  - the overflow events' timing;
  - the PTR path and the SRAM Hold's refusal;
  - `ReqFifo`'s judgement;
  - the block's ERRORS [4] and [5];
  - big SRAM writes, the sum, room by the count before the pop;
  - the PTR view against a same-cycle write-back and a lost window store;
  - read-port returns, including a same-cycle tie broken by token order.
- **MemConf** (`test_memconf`): ref's own Tile with only G3's components running in Tile's order. A seeded script
  plays everything around them:
  - the gather's and AG's read Holds, with refusals retried;
  - the read port's queue and we-in;
  - the controller's PTR and HIST window stores and the AG's STREAM/GROUP requests;
  - the core's send Hold;
  - the router, which frees each loopback token 0–127 cycles later.

  Each cycle compares the packet injected, the table returns, the free, and the streams of srq, inj, sram, sram.data,
  we, ts and hist.
  - **Fixtures:** 5 × 3,000 cycles, two with combining. All equal, including under the debug options; a corrupted line
    fails.
  - **Long runs:** 15 × 20,000 cycles, equal.
  - **What a fixture reaches (`MEMCONF_STATS`):**
    - 2,000–2,800 reads over the three clients, and 150–240 zero-width reads;
    - the token window full for 450–650 cycles;
    - SRAM Hold refusals and full areas;
    - write-engine refusals and we-in overflows;
    - 30–60 window stores lost per table;
    - about 300 augmented writes;
    - 27–46 write-combining merges per combining fixture;
    - 7–32 same-cycle read-port ties.
  - **Regenerate:** `tests/ref/memconf.sh --check` (needs a JDK).
- **The sorts:** W12's `test_sort` covers SPLIT's read port and every G3 unit on every tile. The 16K sort (248 points)
  and the full-size sort (321 points) are equal on the runner, one thread and 8 domains (Quest 154).

## 5. Surprises worth knowing

- **Cycles start at 1.** Half my first hand-written expectations in `test_mem` were one cycle off for that reason.
- **Ref's AG reads PTR by a host peek,** which flushes pending macro writes but runs before the table store's step.
  Hence the PTR view's lag: the write-back of t is visible from t + 1.
- **A table-window store or HIST load on a write-back cycle** is ref's figure 12, not a bug. It happens: MemConf loses
  30–60 stores per fixture.
- **Write combining needs a write queued behind a read in its area.** With one write Hold, writes never queue behind
  each other, which is why `regs` and the plain MemConf fixtures never merged.
- **The read port's free count uses injections before t,** a Count read through t − 1. The core's own stores are its
  own state.
- **Missed-wake thresholds:** both the write engine and the injection unit legitimately wait on state held inside
  another unit. Their thresholds are raised and documented.
- **Container:** clang's sanitizer runtime needed `libclang-rt-18-dev` (`--no-install-recommends` leaves it out).

## 6. What's left

- **Performance (next, through the integrator).** W10's profile of the sort (Quest 156) puts BigSram, the read side and
  the table store among the top costs. Its hot-lines write-up for these units comes next.
- **Footprint (W10's study, for awareness).** BigSram, TableStore and Injection each hold 3-7 KB per tile that is
  barely touched outside `regs`. This is not a problem today, and W10 checks again in the sort's traces. If it ever
  matters, the cheap fixes are these, neither changing behaviour:
  - size BigSram's area queues to the configured depth instead of a fixed 16 x 16 entries;
  - keep the read side's slot arrays out of tiles without it.
- **M3, by D30:** nothing in G3 needed a difference from ref. The big SRAM costs only its touched pages (256 MB
  allocated; `a2a` touches 2.3 MB).
