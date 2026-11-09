# WORKER16 (sim/, M3, G7): notes

The C&C cores and the chip's integration, then T2 end to end. Branch `sim-w16` (from main 41eb698). Prompt: `PROMPT.md`.

## Findings before the plan

1. **M2 has no request bus in C++.** `REQUEST_SEND` is fatal in M2's tile (`tile_ctl.h`, "not modelled in M2"), and
   the `reqbus` fingerprint stream is a constant; AUDIT-M3 3.4 speaks of "M2's request bus", which doesn't exist.
2. **The slot isn't on main yet** (W15). C&C 1's body needs ports beyond the slot's own: the row collectors' READY and
   error-line words, C&C 2's and C&C 3's error lines, 64 GO links out.
3. **C&C 1's store timing** (AUDIT-M3 3.4's open item): under the slot's rule a store applies at level 0 of t + 1, so
   C&C 1's own work in the store's cycle (the watchdog's count, the UART's TX start) is one cycle later than `ref`.
4. The boot's references (the unit lines at boot end) and the harness's landings are on the runner only.
5. No clang and no RISC-V toolchain in the session's container.

## Rulings (owner, 2026-11-09)

- **Q1, the request bus:** build `RequestBus` as `ref` has it (row nodes, root, receive queues) with the controllers'
  request ports and receive registers (T2's boot sends through it). Its chip-level work is event-driven (D23; AUDIT-M2
  section 4 named `RequestBus.runCycle`'s per-cycle scan as the first case): nothing scans all tiles each cycle. The
  tiles' `REQUEST_SEND` stays fatal ("not modelled in M3"): T2 sends none.
- **Q2, type bodies declaring their own ports:** the owner asks W15, who publishes the slot's header next (before its
  Stage A). Until it lands, `cc1p` is built to W15's method list (W15 NOTES, "The interface") with its own driver.
- **Q3, C&C 1's store timing:** approved as a deliberate difference, **D33**. The slot's rule is kept: C&C 1's
  watchdog and UART run one cycle later than `ref` after a store; neither reference run reaches them (AUDIT-M3 section
  1: no watchdog, no UART traffic). Recorded in DECISIONS.md and AUDIT-M3 3.4.
- **Q4, the boot-end point:** extracted by a Quest task and committed to `golden-m3` (1,214 lines); the `sram` dump too
  if it is a few MB at most, otherwise the boot check runs on the runner.
- **Q5, toolchains:** clang by apt. The RISC-V images: the committed prebuilt ones if they exist (D4); otherwise
  `gcc-riscv64-unknown-elf` by apt, and the built images committed with a manifest of their hashes.

## The plan (agreed 2026-11-09)

A commit after each step; `make check` clean on gcc and clang; every new unit through the debug checks (shuffle, both
orders, step-all, the missed-wake detector).

1. **The router's controller variant**: `FEEDERS_CC`, `ccRoute` (from the loopback or `CC_DMA_IN`, a packet for this
   controller is ejected, any other leaves on `CC_LINK_OUT`; every other input is ejected), the inputs `CC_LINK_IN`,
   `CC_HOST_IN`, `CC_DMA_IN`, the output `CC_LINK_OUT`. Checked against `ref` by extending RouterConf.
2. **Units 64-66** as framework tiles from M2's units (controller, register block, memories, big SRAM, read queue and
   injection, director, write engine, AG, table store, HIST, read port, gather): the C&C links `Link(1)` both ways, the
   host port (`Fifo(4)`, a host-queue stand-in stepped only while it holds packets), C&C 3's DMA input (`Fifo(4)`, the
   slot's S6).
3. **The request bus**, event-driven, and the controllers' request ports and receive registers.
4. **C&C 1's body (type 1) and C&C 2's stub (type 2)**; the chip lines as events (D23, AUDIT-M3 section 4): READY
   through the row collectors, the error lines as `Link(error_latency)` events, GO from C&C 1's slot. M2's Java `CnC`
   and the endpoints stay built and idle (their fingerprint streams are kept).
5. **The fingerprint** (`cc1p`, `cc.host`, `reqbus`, units 64-66, the M3 chip line) and **the boot exact** (milestone
   1): `coresBooted` at 7,222 and every unit's line at boot end equal to golden-m3; stubs for `tr`, `rp` and the slot if
   theirs haven't landed.
6. **The T2 harness**: the `files` and `sram` dumps loaded, the rows observed and checked against `rows.bin`,
   `CC_SEGMENT`, the checks of AUDIT-M3 section 0 step 6, the partition (C&C c with tile c, C&C 3 with tile 2, the
   replaying tiles spread).
7. **T2 end to end**: evm-zone-29 segment 10, then cyclone-29 segment 10; fingerprints every 100,000 cycles.
8. **The first timing** on the runner, one thread and split, C&C 3's domain on one thread reported separately.
9. `REPORT.md`.

## Log

- 2026-11-09: plan agreed (rulings above); branch `sim-w16` from main 41eb698.
- 2026-11-09: **step 1 done**: the router's controller variant (`Router` with `tile` 64-66: `FEEDERS_CC`, `ccRoute`,
  `step_cc`; ports `in[0]` CC_LINK_IN, `host` CC_HOST_IN Fifo(4), `dma` CC_DMA_IN Fifo(4), `out[0]` CC_LINK_OUT) and
  tiles 0-2's fifth port as the C&C links (`FIFTH_LINK`: `from_cc`/`to_cc`, Link(cc_link_latency)). `NetConfig` gains
  `cc_link_latency`, `cc_host_depth`, `cc_dma_depth` (ref's 1, 4, 4). The feeder table is per router now (27 FIFOs on a
  tile, 28 on a controller); a tile router's stream and digest are unchanged. RouterConf extended (`gen ... link`,
  `gen 64-66 [dma]`): five new fixtures (`t0-s7-link`, `t2-s8-link`, `t64-s1`, `t65-s2`, `t66-s3-dma`; overflows and
  eject errors reached), the old eight byte-identical on regeneration; all equal to ref, under shuffle, both orders and
  step-all (clang and gcc debug). A planted bug (the DMA's packet for the controller sent on the link) fails `t66`.
- 2026-11-09: **the images** (Q5): no prebuilt cc1, cc23, tracer_ctl, replay_ctl in the repo, so built with
  `gcc-riscv64-unknown-elf` (13.2.0) and committed in `tests/m3-images/` with `MANIFEST` (sha256) and `build.sh`
  (builds a scratch copy of `sim/ref`, nothing in it written; checks against W13's `IMAGES`, 110u's: byte-identical;
  `--check` rebuilds and compares). The boot ROM is `ref/meshsim/sw/bootrom.hex`.
- 2026-11-09: `make run` clean on clang release after step 1 (48 test binaries); gcc and clang debug on the router's
  and the chip's tests.
- 2026-11-09: merged main (W15's slot interface: `peripheral.h`, `periph_slot.h`; D33 as the integrator recorded it).
- 2026-11-09: **C&C 1's peripheral and C&C 2's stub** (`units/cnc1_periph.h`: `Cnc1Peripheral`, type 1, and `CcStub`,
  type 2, on `PeripheralBase`), to W15's method list; their `visit` cases wait for `periph_slot.cpp`. C&C 1 owns its
  ports through the hooks: `rows[8]` (M2's row collectors: READY edges and the tiles' error levels), `cc_line[3]` (the
  controllers' register blocks' line words: C&C 1's own for the toast pin, C&C 2's and 3's for ERROR_LINES_2 [1], [2]),
  `go[64]`. READY arrivals apply in `begin(t)` (READY_LINES reads arrivals up to t); error-line arrivals in `step(t)`
  (ref applies errChanges in runCycle, after the reads of t); EVENTS [2] from `step(t)`; PULSE_GO's GO put in `end()`
  with latency go_latency + skew (the block's GO input at t + go_latency + skew + 1, as M2's net). UART RX has no
  source in M3 (no rxByte): it stays empty, so its read-with-pop never arises. C&C 1's own error line reaches the toast
  pin two cycles after ref's (the block's line word; toast_at is ref's); T2 has no error.
  **Cnc1Conf** (`tests/ref/Cnc1Conf.java`, `cnc1conf.sh`, `tests/test_cnc1_periph.cpp`): ref's Cnc1Peripheral driven by a
  seeded script as the chip, C&C 1's core and its slot drive it; the C++ class in a stand-in for the slot (W15's level-0
  order) with the lines on Links. D33 in the driver: ref's stores of words 40-45 one cycle later; no PULSE_GO while a
  watchdog is set; loads at least 2 cycles after a store; D29's re-pulse avoided. Three fixtures of 20,000 cycles (85
  GOs each, all-READY and watchdog events, error lines, the toast, UART bytes): equal to ref, under shuffle, both orders
  and step-all. A planted bug (tiles' error lines applied in `begin`) fails.
- 2026-11-09: **step 2 done** (units 64-66): `SramBlocks` holds 67 units (`kUnits`); `G1Ctx::units` (64 by default, 67
  with C&C as cores) with a check; `unit_prefix(k)` (inline, `chip.h`): `t<k>.` or `cc1.`-`cc3.`, used by `add_g1` and
  `tile_unit`; `Injection::loops` as ref's (a controller's broadcast never reaches itself); the router's DMA input a
  credit Fifo (the slot's S6 is a CreditOut); `host_port.h` (the host's queue, ref's hostWrite/hostLoad/hostCommand,
  stepped while it holds packets); `cc_cycle` and `build_chip` with `cc_cores` (C&C c in tile c's domain, the C&C links
  Link(1) both ways, tiles 0-2's fifth port FIFTH_LINK); until C&C 1's slot is wired, `cc1.lines` (a stand-in) takes
  the row collectors' and the controllers' line words and drives no GO (also the controllers' GO inputs, which nothing
  drives in ref). `test_cc_units`: C&C 1 booted through the host port (`tests/m3-images/cc_ping.c`) sends to tile 5, to
  itself and a broadcast; one domain, 4 row bands on 4 threads, the debug options.
- 2026-11-09: **an edit to W8's file** (`tile_ctl.h`, TileCtl::step; approved by the owner): the block steps again the
  cycle after the core's own send (`send_put ? c.next()`), so STATUS's send-busy is fresh for a polling core. A missed
  wake M2's programs never exposed (a landing or a K line always stepped the block); the detector can't see it (no data
  waits, only stale published state). Found by C&C 1's first send in `test_cc_units`.
- 2026-11-09: make check, build by build (one `make` exceeds the tool's 300 s: each build in a timed call, the tests from
  a resumable script): 49 test binaries pass on clang debug, clang release, gcc debug and gcc release (wfi, regs, a2a
  against their goldens in `test_chip`; the 16K sort in `test_sort`: unchanged); TSan `test_chip form2` on clang passes.
- 2026-11-09: **Quest 165** (DONE): `$DATA/w13-golden-m3` equal to `HASHES`; both runs' boot-end point (cycle 7,222,
  1,214 lines each) committed as `tests/golden-m3/boot/` with `SHA256`. The two differ only in `cc3.cmem` and
  `cc3.sram.data`. The `sram` dumps are 8.3 MB each (16.97 MB raw; they differ only from byte 16,707,085, C&C 3's part):
  over the owner's "a few MB", so not committed; the boot's check with the real landings runs on the runner.
- 2026-11-09: TSan `test_chip form2` on gcc passes (the make check is complete: both compilers, debug, release, TSan).
- 2026-11-09: merged main (W15's slot body, W14's Tracer in it). **C&C 1's peripheral and C&C 2's stub in the slot**,
  W14's four edits in `periph_slot.cpp` (include, `visit` cases, delete, install; the stub on C&C 3 reports type 3, as
  `CcCores.Stub(2 + c - 1)`). `cc_stub_through_the_slot`: PARAMS type 2 (3 on C&C 3), STATUS, ERRORS, EVENTS 0.
  **Blocked: C&C 1's own ports can't live in the peripheral.** The framework rewrites each handle in place at the build
  and checks that it is a member of the unit (`config_impl.h`: "port 'row' is not a member of the unit"), and the slot
  allocates its peripherals on the heap, so `ports(sim::Ports&)` can't declare C&C 1's rows, lines and GO. Asked of W15:
  storage for a peripheral's own ports inside `PeriphSlot` (e.g. `own_in[11]`, `own_out[64]` declared by the slot for
  the peripheral, passed to its `begin`/`end`). The C&C 1 conformance through the slot unit (`run_slot`,
  `cnc1_periph_through_the_slot_conformance_with_ref`) is written and waits behind `-DW16_SLOT_OWN_PORTS`.
- 2026-11-09: **the register block's fix, against ref's cycles**: `cc_ping` stamps mcycle at each send and when its
  STATUS poll sees send-busy clear; `tests/ref/CcPing.java` (`ccping.sh`, `tests/m3-images/cc_ping.ref`) runs it on
  ref's chip with C&C as cores: SIM_EXIT at 933, stamps 82 94 103 112 119 128. `test_cc_units` equals them on one
  domain, 4 row bands and the debug options; with the fix reverted the first poll never ends (the test fails).
- 2026-11-09: make check again after the merge and the slot's types, build by build: 52 test binaries pass on clang and
  gcc, debug and release; TSan `test_chip form2` passes on both.

## Rounds with W15 and W14 (2026-11-09; the owner: one round each)

**For W15: a peripheral's own ports in the slot** (the blocker; acceptance test: `test_cnc1_periph` built with
`-DW16_SLOT_OWN_PORTS`, `cnc1_periph_through_the_slot_conformance_with_ref`). Proposal:
- `periph_slot.h`: `struct OwnPorts { sim::LinkIn in[12]; sim::LinkOut out[64]; }`, a member `own` of `PeriphSlot`
  (one set per slot: only peripheral 1 is ever present in v1.3, and only C&C 1's has own ports). Its handles are
  members of the unit, so the build can declare and rewrite them. Cost: 352 bytes a slot (22 KB over the chip).
- The hooks take it: `void ports(sim::Ports&, OwnPorts&)` (declare into `own.in[k]`, `own.out[k]`), `void begin(sim::Ctx&,
  int64_t t, const OwnPorts&)`, `void end(sim::Ctx&, int64_t t, const OwnPorts&)`; `PeripheralBase`'s defaults the same
  signatures, doing nothing. The peripheral keeps indices, never handles.
- C&C 1's layout (mine, in `cnc1_periph.h`): `in[0..7]` the row collectors (`row`), `in[8..10]` the controllers' line
  words (`cc-line`); `out[0..63]` GO (`go`). The chip wires `slot.own.in[k]` / `slot.own.out[k]` by those indices.
- On your side, nothing else changes: the slot calls `ports` in its constructor, `begin` first at level 0, `end` after
  the peripheral's step, as now. I change `Cnc1Peripheral` to the new signatures when your commit lands (a few lines).

**For W14: the request bus's ports** (W14 NOTES section 14). Agreed as proposed: `ControlIO.req` filled by `add_g1`;
the chip builder calls `add_reqbus(cfg, rc, tiles, ccs, placement)` and `wire_reqbus(cfg, io, k, ControlIO&)` for the 64
tiles and the three controllers, with `ReqBusPlacement` from the chip's domains (each leaf with its sender, each
receive queue with its controller; C&C c is in tile c's domain). Three points for the round:
1. **The mode for T2.** The exact checks run split (the partition: C&C c with tile c, the replaying tiles spread), so
   mode 0 (one domain for every sender) can't be used there. T2 sends two requests in all (C&C 2's and C&C 3's
   "started", none from the tiles), with no backpressure: the calm case where mode 3 equals `ref`. So T2 runs mode 3 and
   requires `lag_cycles` 0, on one domain and split; mode 0 on one domain as a cross-check. Agreed?
2. **The fingerprint.** The `reqbus` stream and the 67 `req` streams are yours (`reqbus_fp.h`); I call them from the
   M3 chip line in AUDIT-M3 6.9's order. The controllers' receive queues are in `reqbus` (as ref's `RequestBus.queue`)?
3. **The controllers' REQUEST_GET/RECEIVE and the EVENTS pending bit** (your edits to `rv32.h`, `tile_ctl.h`): C&C 1's
   boot waits on them (cc1.c reads the two "started" requests); `test_cc_units` will add a C&C 2 that sends "started"
   and a C&C 1 that receives it, against ref's cycles, once your branch is on main.
- 2026-11-09: **Cnc1Peripheral::writable** (W15's static map, sim-w15 6933df3's pattern): READY_EXPECT_0-1, PULSE_GO,
  WATCHDOG, LED, UART_TX, UART_DIVISOR, UART_STATUS; checked equal to `write_word`'s result for every word 32-127.
- 2026-11-09: **the T2 harness's pieces** (`tests/t2_harness.h/.cpp`; owner's item 4): M3Inputs' `files` dump into the
  Tracer's `FileStore` (grow_to, raw_page) and `sram` dump poked into the units' big SRAMs; `Image` and
  `boot_cores` (Mach.bootCores and hostBoot: the staging from chunk 0x10000, the descriptor in cc1's data, the program,
  data and CMD_JUMP through the host port); `cc1_command`, `cc_segment` (0x54, 2, 1 << 24, 0x53 << 24);
  `cores_booted` (cc1's mbox[3] == 2); `rows_ref` and `rows_hash` (rows.bin's count and the replayer's sink hash: no row
  kept during the run; the replayer's buffer localises); `partition` (tile 2 and C&C 3 alone in domain 0, the rest
  round-robin); `build_machine` (the cores chip over 67 units). `test_t2_harness`: the dumps written in their format
  and read back (and refused when bad); the staging equal to ref's (`tests/ref/BootStage.java` runs Mach.bootCores
  itself, `bootstage.sh`, `tests/m3-images/bootstage.ref`: C&C 1's host queue, 235 packets, and big SRAM, 2,014 words,
  by count and hash) and landed into C&C 1's memories; CC_SEGMENT's packets; the rows' reference; a 6-domain build
  with a barrier every 3 cycles.
- 2026-11-09: **the boot driver** (`tests/t2_main.cpp`, `build/<cc>-<mode>/t2`: `--sram`, `--files`, `--parts`): builds,
  pokes, stages, boots and runs to `coresBooted`. Today it stops at **cycle 571 on a controller's REQUEST_SEND** (C&C
  2's or 3's "started", cc23.c / tracer_ctl.c): the first thing the boot needs is W14's request bus, then C&C 1's slot.
  C&C 3's DMA input stays off until W15's `G1Config::periph_slot` builds the slot in W8's builder (`cc_cycle` wires
  the slot's DMA when the builder offers it; C&C 1's and 2's lead nowhere, as ref's null `dmaPort`).
- 2026-11-09: make check after the harness, build by build: 53 test binaries pass on clang and gcc, debug and release; TSan `test_chip form2` on both.
- 2026-11-09: merged main (W15's own ports, 72b5da2): W15's interface adopted as built (`ports(OwnPortsDecl&)` with
  `link_in`/`link_out` returning indices; `begin`/`end` with `OwnPorts&`; `row0`, `line0`, `go0`). The duplicate
  `writable` resolved to one (mine, checked against `write_word` for 32-127; W15's was the same set of words). The
  through-the-slot conformance unconditional now (the `W16_SLOT_OWN_PORTS` flag gone). Owner on D34 for T2: mode 3
  split with `lag_cycles` 0, mode 0 on one domain as the cross-check; W14 writes `reqbus` and the 67 `req` streams.
  make check build by build: 53 test binaries pass on clang and gcc, debug and release; TSan on both.
  Next: the chip wires C&C 1's own ports (instead of `cc1.lines`) once the slot is built in W8's builder for
  controllers (`G1Config::periph_slot`); then W14's bus for the boot.
- 2026-11-09: **C&C 1's slot in the chip, prepared** (owner: land it on top of W14's merge, both touch chip.cpp/h) on
  branch `w16-cc1slot-prep` (feb73ab, from sim-w16 9789e81): `add_g1` builds the controllers' slots
  (`PeriphSlotConfig::controller`; peripheral 1 PT_CNC1 on C&C 1, PT_TRACER on C&C 3 with `G1Config::cc3_tracer`,
  PT_CC_STUB otherwise; C&C 1's `ref_cycle` from the timing) and returns `io.slot` and `io.dma`; `build_chip` wires the
  row collectors, the controllers' line words and GO to C&C 1's own-port indices (`own.link_in[row0 + r]`,
  `[line0 + c]`, `own.link_out[go0 + k]`), the stand-in only without a slot (and always the controllers' GO source);
  C&C 3's DMA into its router, C&C 1's and 2's into `NoDma` (FATAL on a packet: ref's null dmaPort). The T2 machine
  builds every slot (the tiles' replayers, C&C 3's Tracer). test_cc_units, test_chip, test_periph_slot,
  test_cnc1_periph, test_t2_harness pass on clang debug; the boot driver still stops at cycle 571 on REQUEST_SEND.
  W14's merge isn't on main yet (9c29eac: parked for W14).
- 2026-11-09: merged main (W14's request bus, ca05abc) and applied `w16-cc1slot-prep` (feb73ab) on top: chip.h's
  `ControlIO` and `CcIO` keep both `req` (W14) and `slot` (mine); `cc_cycle` passes both. **The boot runs to
  `coresBooted`: boot-end 7,222, equal to ref's.** The cycle mapping (found on the way): the framework's cycle F is
  ref's F - 1 (it starts at 1, ref at 0; the units read ref's t as now() - 1), so between cycles `now()` is ref's
  `c.cycle` + 1 and M3Seg's boot-end point (`c.cycle - 1`) is `now() - 2`; the driver first printed 7,223 for that.
  **Against ref cycle by cycle:** `BootStage trace` (the same boot on ref, without the landings: C&C 1-3's and tile 0's
  pc between every cycle to `coresBooted`, 5,269 changes) committed as `tests/m3-images/boottrace.ref.gz`
  (`bootstage.sh` writes and checks it); `run_boot` in the harness and `t2_boot_equal_to_refs_*` (one domain, T2's
  6-domain split on 6 threads, shuffle, instance-major): identical, boot-end 7,222. Ref's boot with the landings also
  ends at 7,222 (golden-m3's `# boot-end`). **Not yet: every unit's line at boot end against `golden-m3/boot`**: the M3
  chip's fingerprint writers (AUDIT-M3 6.9; step 5 of my plan) aren't built: the slot, `cnc1p`, `cc.host`, the
  controllers' 16 streams, the controller router's streams, the M3 chip line (W14's `req`/`reqbus` and W15's `rp`/`tr`
  writers to call in its order).
- 2026-11-09: **the cycle numbering**, recorded in AUDIT-M3 section 7 (every later comparison depends on it): the
  framework's first cycle is 1, ref's 0; its cycle F does ref's F - 1; between cycles `now()` is ref's `c.cycle` + 1; a
  golden-m3 point t (M3Seg's `c.cycle - 1`) is the framework's state at `now()` = t + 2. The M2 harnesses' boot cycle
  (test_sort's B = cycles run) is the same mapping.
- 2026-11-09: the chip.cpp link fix pushed (cae10e2: C&C 1's own-port indices read inline, so binaries linking the
  chip without the slot link). TSan: `make tsan-one` on clang and gcc (the eight thread tests, W14's test_reqbus and
  now `test_t2_harness`, whose split boot runs under TSan: a rule in cc.mk adds it to `TB_TESTS`), all pass with no
  race; gcc's and clang's `test_chip form2` pass. The full check before it (cae10e2's tree): 54 test binaries pass on
  clang and gcc, debug and release.
- 2026-11-09: **every unit at the boot's end against golden-m3** (the t2 tool, `--sram`, `--files`, `--fp-boot`; the
  dumps from Quest 165 and 167): on both runs, evm-zone-29 and cyclone-29, **1,149 of 1,214 lines equal**: everything
  but the 64 `rp` lines (a placeholder until W15's `Replayer::fingerprint`, asked by the owner) and the chip line that
  hashes them. On the way:
  - `cnc` with cores: ref's PULSE_GO still runs `CnC.goNet`, so `lastGoAt` is C&C 1's last GO (or -1) and
    `lastCommandWord` -1; a PULSE_GO in flight to the slot at the point counts.
  - **The label's convention** (owner: a parameter, M2's default unchanged): `FpCtx::label_shift`, 0 for M2
    (StateFp.write's cyc = c.cycle), 1 for M3 (StateFpM3.write's cyc = c.cycle - 1). ref's formulas that compare an
    event cycle with cyc use `cycle - label_shift`: a sleeping core's mcycle (lag = cyc - countedTo), `sendRetry`,
    `regLoad`, the director's K-line (`due > cyc`). The rest of M2's writers read the framework's state and are called
    with `cycle` = now() - 1 as before. **One approximation**: ref applies the mcycle lag only to a unit its idle-skip
    put to sleep (`Chip.asleep`, `Tile.canSleep`), which the framework doesn't model; the writer takes "in a WFI and
    not stepped in the last cycle" for it. It holds at both boot ends (C&C 2 asleep; the tiles after GO and C&C 3, in
    a stall or awake, not), to be checked at every 100,000-cycle point in the segment.
  - The Tracer's memory: `t2::attach_store` (Tracer::own_memory, M3Seg's MemorySystem over the carried store).
- 2026-11-09: make check build by build after the M3 fingerprint: 54 test binaries pass on clang and gcc, debug and release (test_chip's wfi, regs, a2a and test_sort's 16K sort against golden-m2 unchanged: label_shift 0); `make tsan-one` on both compilers (nine binaries, no race).
