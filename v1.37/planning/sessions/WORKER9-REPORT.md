# WORKER9 report: M2's G2 (the network, C&C, the chip's assembly)

Branch `sim-w9`, merged into `main`. Day-by-day detail is in `NOTES.md`; this is what W11 (G3) and W12 (G4, the sort end
to end) need. **State at hand-off:** the 64-tile chip (W8's G1 units in every tile, my network and C&C) equals
`golden-m2` every cycle on `wfi` (to its end, cycle 1616) and on `regs` through 824 (where G3's mode-1 path begins). That
holds for every unit of tile 0 and the chip-level units every cycle, and for every tile every 10 cycles. It holds on one
thread, under the debug options, and split over 2, 4 and 8 domains (TSan clean). `make check` is clean on gcc and clang;
run it build by build, since one call exceeds the 5-minute tool limit.

## Files

| file | what |
|---|---|
| `src/units/net.h` | the link word (`units::lw`: ref's `LinkWord` layout, D17), `NetConfig`, `Hops`, `start_skews`, `neighbour`, `arrival_port` |
| `src/units/router.h/.cpp` | the router (step inline in the header) |
| `src/units/inject.h/.cpp` | the injection unit, the `Tokens` pool, the read-side slot `InjReadSide` (W11's) |
| `src/units/cnc.h/.cpp` | `CncProgram`, C&C 1's model (`Cnc`), `RowCollector`, `Endpoint` (C&C 2 and 3) |
| `src/units/chip.h/.cpp` | `ControlIO`, `TileIO`, `ChipSpec`, `tile_cycle`, `build_chip`, `row_bands`, `tile_unit` |
| `src/units/net.mk` | my build rules (included by `sim/Makefile`), `tsan-chip` (in make check) |
| `tests/statefp_g2.h/.cpp` | fingerprint writers: "router", "inj", "srq" (stub), "cnc", "cc1p", "reqbus", "ep" |
| `tests/test_router.cpp`, `tests/ref/RouterConf.java`, `routerconf.sh`, `tests/routerconf/` | router and injection conformance |
| `tests/test_cnc.cpp`, `tests/ref/CncConf.java`, `cncconf.sh`, `tests/cncconf/` | C&C conformance |
| `tests/test_chip.cpp` | the chip's driver and its checks (wfi, regs, form 2, every tile, the overflow event) |
| `tests/test_net_headers.cpp` | the headers compile and meet the unit rules |

## 1. The chip's assembly (`chip.h`)

**A tile** is built by `tile_cycle(cfg, spec, k)`:
1. It sets the domain (`spec.domain[k]`, if a partition is given).
2. It calls the control builder (`spec.control`, W8's `add_g1`, which adds core, ctl, dir, we, ag, gather, ts, hist). With
   no builder it adds a stand-in that sinks eject and GO and never sends: a network-only run.
3. It adds `t<k>.router` and `t<k>.inj`, then wires them:
   - send Hold, core -> inj;
   - send-busy Reg, inj -> block;
   - inj -> router, `Link(intra_tile_latency)`;
   - `loop_left`, router -> inj, `Link(1)`, width 0;
   - eject, router -> director, `Link(intra_tile_latency)`;
   - the overflow `Count`, plus the overflow **event** `Link(1)`, width 0 (D29), router -> block.
4. It returns `TileIO`: four link ins (by arrival port) and outs (by direction), GO in, line out, and on tiles 0-2 the
   fifth port (`from_cnc` Hold, `to_cnc` Fifo).

**W11 and W12 plug in the same way.** Add your units inside the control builder, or have `tile_cycle` call a second
builder. `ControlIO` is the contract between the network and the rest of a tile; extend it with the integrator if you
need a new crossing.

**The chip** is built by `build_chip(cfg, spec)`:
- **Tiles and mesh:** 64 tiles; the mesh links are `Link(link_latency)` from `link_out[d]` of k to `link_in[arrival_port(d)]`
  of `neighbour(k, d)`.
- **C&C and the collectors:** `cnc` and `row0..7`, in tile 0's domain, because C&C's send is a Hold into tile 0's router.
- **The endpoints:** `ep2` and `ep3` in tiles 1 and 2's domains (their receive Fifos).
- **GO:** `Link(go_latency + skew[k] + 1)` per tile. The skews are Java's `Random(1).nextInt(2)` in tile order
  (`start_skews`). The +1 is the block's shifted end of cycle.
- **READY and error lines:** block -> collector `Link(ready_latency - 2)`, then collector -> C&C `Link(1)`, so C&C sees
  an edge of cycle e at e + ready_latency, as ref does.
  - The collectors exist because a unit has at most 64 ready bits; 64 READY plus 64 error lines can't be one unit's.
  - M3's larger fan-ins will meet the same limit.

**The partition** is `row_bands(parts, domain)`: 1, 2, 4 or 8 bands of rows. Every crossing is then a mesh link of
latency 3 (GO and the lines are longer), so the barrier interval is 3; `test_chip` asserts it from `Sim::partition()`. Any
tile-to-domain assignment works, provided C&C and the collectors stay with tile 0 and `epK` with tile K-1. W10's placement
by load can replace `row_bands`.

## 2. The driver (`tests/test_chip.cpp`, function `check`)

**Booting a program (Mach.boot exactly):**
```
CncProgram prog;
prog.load_words(lw::BROADCAST, 0x01000000, im.code...)      // PMEM_BASE
    .load_words(lw::BROADCAST, im.data_at, im.data...)      // _data_start (an empty image adds no step, as ref)
    .command(lw::BROADCAST, 0x4A << 24, &entry, 1)          // CMD_JUMP, slot 1 = _start
    .run_when_ready(0).call(1);                             // runWhenReady's call, then Mach's own .call
... the program's sequence ...
prog.release();                                             // every program ends with a release point
```
Keep ref's step granularity: the C&C fingerprint has the queue's length. So `command` is an instant mark plus one send;
`go` is a step plus a settle; `at_all_ready(id)` is a wait plus a call (it costs a cycle, as SortTest's `atAllReady`).

**Building and running:**
- **The spec:** `ChipSpec{net, &hops, &prog, add_g1, &g1ctx, domain}`, then `build_chip(cfg, spec)` and `cfg.build()`.
- **Options:** set `Options::observe_arrivals` (the fingerprint writers need the Links' arrival cycles).
- **The loop:** run cycle by cycle with `m.run(1)`. At c = 0 compare before running; the golden has a point at cycle 0.

**Stopping.** The driver checks Mach's `allStopped` between cycles (every core's `halt != RUN`) and stops at that cycle.
- That is what ref's `runUntil` does; the golden's last point is that cycle, and `wfi` stops at 1616 as ref does.
- W10's stop request (`Ctx::stop`, `Sim::stop_cycle`) is on main, for the sort's timing runs, where a per-cycle `run(1)`
  costs.
- To use it, a unit must know "every core stopped": an event from each core's halt (a fan-in, so collectors again).
- I did not build that; the polling driver is exact and fine for checking.

**Comparing.**
- **The registry:** `fp::FpRegistry` with `register_g1` (W8) for G1's units, `register_g2_tile` for router, inj and srq,
  and `register_g2` for cnc, cc1p, reqbus and ep. `reg.point(ctx, tiles, true, lines)` gives our point; `fp::compare` it
  with every golden point at that cycle (GO and all-READY cycles have two, identical).
- **D12 first:** `check` refuses a golden whose configuration keys differ from what it builds.
- **Diagnosis:** `CHIP_DUMP=1` dumps the first differing unit's stream.
- **The hash-only goldens:** `CHIP_FP_DIR=DIR test_chip every_tile` compares every tile every 10 cycles against them
  (`regen.sh --check` with `KEEP=DIR` makes them; they are not committed).
- **D29's count:** every run prints `ready_repulses` and fails if it isn't 0.

**Releasing (SortTest queues the sort's commands after the boot has run):**
- `CncProgram::release()` marks a release point; steps after it are not in C&C's queue (nor its fingerprint) until the
  driver, between runs, calls `m.unit<Cnc>("cnc").release_next()` then `m.wake("cnc")`.
- This is exact against ref: `test_cnc` has a release point in its 3 fixtures and matches every cycle.
- Each release point carries the command parity as queued (`commands_to`), as ref's `commandsTo` counts at queue time.

### How far the M2Sort sequence goes: not built. W12's to do

The driver runs M2Prog's `regs` and `wfi` only. For the sort, as `M2Sort` runs it:
1. **Boot.** SortTest's `Machine.boot` queues `load_words(code)`, `load_words(data)`, `command(JUMP, _start)` and
   `run_when_ready()`, **without Mach's extra `.call`** (so `run_when_ready(id)` alone), then `release()`.
2. **Run until the boot ends.** Ref runs until `!cc.busy() && allWaiting()`, where `allWaiting` means every core is
   `inWfi` with `halt == RUN`. The driver checks that between cycles (`Cnc::busy()`; the core's `in_wfi` and `halt` in W8's
   `Rv32`). Call that cycle B. M2Sort's fingerprints count from there ("cycle 0 of the sort's sequence: the boot's last
   cycle", its header comment); check how FpHooks numbers them before comparing.
3. **Load the input between cycles** (SortTest.loadInput): pokes into the big SRAM (W11's) and the cores' data memory (`S`
   fields). No wake is needed for memory.
4. **Queue the sort's sequence**, as M2Sort builds it:
   - Command L1 then `go()`.
   - Then for each next command: after SPLIT, 66 times `command(BARRIER)` + `at_all_ready(id)` + `go()`.
   - Then `command(next or EXIT)` + `at_all_ready(id)` + `go()`.
   - `release()`, then at B: `release_next()` + `wake("cnc")`.
   - Every `atAllReady` callback in M2Sort is `() -> {}`: a call id with nothing behind it.
5. **Stop** when `!busy && allStopped`, then verify as M2Sort does.
6. **Points.** The fingerprint points of the 16K golden are at intervals from B, at the sort's GOs and all-READYs, at the
   end, and in windows.

C&C's model supports all of this (`at_all_ready`, the release, many GOs); none of it is run against the sort yet.

## 3. The injection unit's read side, for W11 (`inject.h`)

ref runs the SRAM read queue and the injection point as one component because they share the token pool (AUDIT
section 2, item 2). Here they are one unit, `Injection`; my send side is built, and the read side is a member,
`InjReadSide rs`, empty, that W11 fills **without restructuring**.

**The step at t:**
1. `loop_left`'s free of t - 1 is applied (`tok.loop_held--`).
2. `tok.start = tok.reads + tok.loop_held` is fixed; `tok.taken = 0`.
3. `rs.step(c, tok)`: **W11.** Area returns placed, one release into the read return, one issue. An issue takes a token:
   `tok.free()` (`start + taken < cap`), then `tok.taken++`, `tok.reads++`.
4. The injection, a read return first:
   - `rs.has_return(c)`, `rs.peek_return(c)` (3 words), and on injection `rs.injected(c, hdr, tok)`. **W11:** free the
     read's token (`tok.reads--`) or, for a packet that loops, move it to `tok.loop_held`; tell the read port
     (`injectedAt`).
   - Else the send hold's packet. A send for the tile itself or a broadcast takes a token (`TAG_TOKEN` set) or waits.
   - A send that loses to a read return is judged again next cycle (`next()`).
5. The send-busy Reg is put if it changed.

**`Tokens`** is `{cap, reads, loop_held, start, taken}`, shared by both sides as ref's one pool; `free()` is ref's
`tokenFree`. Frees take effect the next cycle.

**What W11 adds:**
- **Ports:** `InjReadSide::declare(Ports&)`, called from the constructor: the clients' Fifos, the SRAM's return Links,
  the request Hold to the SRAM.
- **Wakes:** the read side's own; `Injection::step` returns `none()` today, so combine the read side's wake with the
  step's return.
- **State:** your state in `rs`, and `rs.digest()`.

**The token wait:** a send waiting for a token sleeps; it is woken by the router's `loop_left`. The tile builder gives
`t<k>.inj` a missed-wake threshold of 2^20, because the free may not be on its way yet. Keep that in mind when you add
wakes.

**The statistic** `token_waits(t)` reconstructs ref's per-cycle count across the sleep (`wait_from`). A read return ends
a wait interval, so with your read side it stays exact.

## 4. The fingerprint writers (`tests/statefp_g2.*`)

- **"router":** `Router::fp_own` (rr, then every FIFO as StateFp writes it: the put of cycle c listed apart, which
  `Fifo::put_at/put_w` keep), then the four links and the injection link (observer, arrival cycles), then C&C's Hold on
  tiles 0-2.
- **"inj":** the read-return hold (written empty: **W11 replaces it with the real one**), then the send hold (observer).
- **"srq": `w_srq_stub`.** The free list 0..63, no tokens, empty orders and rpReady. `held` is the loopback's tokens
  minus a free on its way (the `loop_left` entry arriving at c + 1). Then three empty clients and 16 empty return links.
  It aborts if `tok.reads != 0`. **W11 replaces it.** Ref's free list order matters once reads happen (AUDIT).
- **"cnc", "cc1p":** C&C's state, plus ref's pending queues rebuilt from the line words in flight: the collectors' and
  C&C's input Links, through the observer, and for the end of cycle c itself, `G2Chip::edge`, which wraps W8's
  `units::line_edge` (the block's endCycle(c) preview).
  - Covered: `readyAt` pending, `errChanges`, C&C's OR'd `errLine` changes.
  - Ref's readyAt is one cycle; the model keeps the spec's sticky bit (D29).
- **"reqbus":** the request bus at rest (constant). Nothing in M2 sends a request; a REQUEST_SEND is W8's to refuse.
- **"ep":** C&C 2 and 3's receive Fifos.

## 5. RouterConf and CncConf: ref's own classes, cycle by cycle

**Both drivers:**
- Both compile into ref's default package from `tests/ref/` (nothing in `sim/ref` changes).
- Each has a `gen` mode (a seeded script) and a `run` mode (ref's observations). The C++ test drives the same script and
  must print identical lines.
- `routerconf.sh` / `cncconf.sh` take `--fixtures` (rewrite) and `--check` (regenerate from ref and compare). They need a
  JDK: `apt install openjdk-21-jdk-headless` works here.

**RouterConf.** One ref router alone (and with `send`, ref's InjectionPath plus SRAMReadQueue's tokens behind it, a core
retrying refused sends). It observes every output word, every `loopbackLeft`, the router's canonical stream every cycle,
overflows and errors, and with `send`: STATUS's send-busy, the "inj" stream, the tokens held, waits, refusals.
- **In make check:** 8 fixtures of 2,000 cycles, also under shuffle, instance-major and step-all.
- **Long runs:** 24 runs of 10^5 cycles (`routerconf.sh 100000 DIR`, then `ROUTERCONF_LONG=DIR test_router long`).
- **For W11:** extending RouterConf's `send` mode with SRAMReadQueue clients is the natural way to check your read side
  against ref before the chip.

**CncConf.** Ref's CnC, Cnc1Peripheral and C&C 1's endpoint in a real `Chip` whose tiles the driver plays: READY at boot
and answering every GO with seeded delays, error lines up and down, packets into C&C's receive FIFO, tile 0's router
stalling one cycle in six, a release point.
- **Observations:** the packet taken, GO, calls, and the "cnc" and "cc1p" streams every cycle.
- **In make check:** 3 fixtures of 6,000 cycles.
- **Not in the scripts:** a second READY pulse, by design (D29).

## 6. What's left (now W11's and W12's checks)

- **a2a's network part.** `a2a` needs G3 (the mode-1 path, the SRAM, the read queue) and drives real traffic through
  every router.
  - With G3 in, run it like `wfi` in `test_chip`: `M2Prog a2a` is `Mach.boot` plus three broadcast commands with GOs; the
    input is poked into the big SRAM first.
  - Compare against golden-m2's `a2a` points.
  - The router, injection and C&C streams come for free from my writers; replace the srq and inj stubs first.
- **NetTest** (owner, Q2): ref's per-chip digests can't match by construction (they hash ref's objects, AUDIT 6.1).
  - Compare its log facts exactly (latencies, delivery counts, queue peaks), plus the canonical fingerprint where it
    applies.
  - Its stimulus (coreless chips driven from Java) would have to be ported; RouterConf's approach is a model.
  - The router keeps `Fifo::peak` (ref's `highWater`, counted at the same moment) for the queue peaks.
  - Optional if a2a and the sort cover the network.

## 7. Surprises worth knowing

- **ref judges a FIFO's put lazily (D28).** A put is judged when the FIFO is next touched: a selector reaching it (they
  stop at the first ready FIFO), a put into it, or idle-skip's quiescence scan. So `overflowCount` can rise several
  cycles late, differently with idle-skip on and off.
  - The router counts at t + 1 (PORTS.md's rule) and puts the overflow event then.
  - References avoid the case. Expect the same with any FIFO you model whose overflow is observable.
- **A second READY pulse (D29).** Ref keeps one `readyAt` per tile (`max(old, edge + 8)`), so a second pulse while the
  first is visible makes the bit read 0 until the second lands.
  - Unreproducible without a link shorter than 3. The model keeps the spec's sticky bit and counts `ready_repulses`.
  - 0 in wfi and regs; the sort must show 0 too.
- **`atAllReady` costs a cycle.** It is a wait step, so the `go` after it sees all READY one cycle later. The queue's
  length is in the fingerprint, so keep ref's step granularity.
- **A port handle's `idx` is only assigned at build.** Don't test it at configuration to decide wiring: that is how the
  overflow event went unwired at the first merge. Decide with explicit flags.
- **A unit reference from `cfg.add` dangles after `build()`** (units move into arenas): use `m.unit<T>(name)`
  afterwards. ASan catches it.
- **STATUS's send-busy is two halves.** Ref's bit is `!sendHold.isEmpty(t)` = occupied at the end of t **or a put made at
  t**. My Reg carries the occupied half; W8's block ORs in the core's own put.
- **A send that loses to a read return must ask for the next cycle**, or nobody steps the unit. ref's inj runs every
  cycle; this one doesn't.
- **C&C is event-driven but not rare.** In the conformance runs it steps on ~1,500 of 6,000 cycles, mostly READY
  arrivals; in wfi, 74 of 1616. A READY arrival is state (ref's readyAt), so it has to be applied when it arrives.
- **Every unit is stepped at the first cycle.** The test stand-ins rely on it to schedule their first wake.
