# WORKER15 report: M3's replayer and peripheral slot

**So far: done, paused.** The replayer and the peripheral slot are built, equal to `ref`, wired into the tiles and the
chip, fingerprinted, and the slot's window publishing made cheap (D35). W16 is timing D35 on the runner against Quest
170; T2 questions or measurements come back through the owner.

Branch `sim-w15`, merged into `main` piece by piece (last 71eacd7). Day-by-day detail is in `NOTES.md`; this is what the
integrator, W14, W16 and the rest of M3 need.

**State at hand-off.**
- **The replayer equals `ref` every cycle**: ReplayConf's 13 traces (synthetic deliveries, error-path mutations) in
  `make check`, 36 more locally; ReplayerUnitTests ported, 1,456 checks (`ref` runs 1,473: the 17 not ported are
  ConvertDelivery's and the 8 KB control, accounted for exactly); the ALU against `vec.tsv` line for line and a
  differential sweep over every opcode.
- **Stage A on the runner** (Quest 164): every window of evm-zone-29 and cyclone-29 on the C++ replayer, 20,480 windows
  against the deliveries' `rows.bin`, streams, `end.bin`; the selected windows byte-identical to `ref`'s ReplayDelivery
  in all three variants.
- **The slot equals `ref`**: SlotConf call for call (5 seeds in `make check`, 20 × 10,000 cycles locally);
  PeripheralSlotTest's groups as conformance on a real tile (TileSlotConf: five data-memory images with every mcycle
  stamp, the accepts, a bad packet and seven DMA packets equal to `ref`).
- **The fingerprint** (AUDIT-M3 6.8, unit `rp`) equals `ref`'s `StateFpM3.replayer` every cycle on the 13 traces; in a
  chip the slot's helper adds the inputs still in its links, as `ref`'s lists hold them.
- **D35**: the slot publishes only the window registers that changed; T2 on one domain exact at every point on both
  runs; **53 s → 17 s** on the checked evm run locally.
- **make check** on 07f6636: gcc and clang, debug and release, 57 binaries each; TSan (`tsan-one`, `tsan-chip`,
  `tsan-units`) on both compilers, no races. The owner's release builds on main: 58 binaries clean.

## 1. Files

| file (`sim/`) | what |
|---|---|
| `src/units/row_core.h`, `replayer_alu.h`, `event_counts.h` | shared with W14 (read-only for it): RowCore, the ALU, EventCounts |
| `src/units/replayer.h`, `.cpp` | the replayer (type 4), Replayer.java method for method; `fingerprint` (6.8) |
| `src/units/peripheral.h` | the peripheral interface (non-virtual, `PeripheralBase<Self>`), type tags, OwnPorts |
| `src/units/periph_slot.h`, `.cpp` | SlotCore (PeripheralSlot.java) and the unit PeriphSlot (AUDIT-M3 3.1) |
| `src/units/test_peripheral.h` | ref's TestPeripheral (type 15) |
| `tests/test_replayer_alu.cpp`, `test_replayer.cpp`, `test_replayconf.cpp` | the ALU, ReplayerUnitTests, ReplayConf |
| `tests/test_slotconf.cpp`, `test_periph_slot.cpp`, `test_tile_slot.cpp`, `test_tileslotconf.cpp` | the slot |
| `tests/replay_window.h`, `replay_driver.h`, `replay_t.h`, `replayconf.h`, `slotconf.h` | test harnesses |
| `tests/replay_delivery_main.cpp` | Stage A's driver (`build/<cc>-<mode>/replay_delivery`) |
| `tests/ref/ReplayConf.java`, `SlotConf.java`, `TileSlotConf.java`, `tileslotconf_progs.py`, `*.sh` | ref's sides |

Changes in closed workers' files (each recorded in NOTES with its reason): the core's window paths (`rv32.h`,
`tile_ctl.h`), the block's live bits (`regblock.h`), the director's S1 (`director.h`), the builder (`tile_ctl.cpp`), the
link lists (`units.mk`, `net.mk`), W5's death-test message; W16's C&C 1 peripheral moved to OwnPorts (agreed).

## 2. The replayer

`Replayer.java` as it runs in v1.3 (type 4; RUN_CONFIG, the 68-word record, the flush at the state word's write, part 0
as working state, underflow and leftover errors), with ref's lazy settle kept: inputs at the end of t, landings at t + 2,
level reads the end of t - 1. Fixed rings, fixed-size facts (D24), no allocation, no virtual calls; rows to a sink (a
running hash, an optional buffer). Non-copyable (ASan found a copied replayer's streams posting to the old events).

## 3. The slot and the tile's paths

- **SlotCore** is `PeripheralSlot` method for method (the window, consuming loads, EVENTS_MASK, live bits, reserved-word
  flags, region accepts, the DMA); **PeriphSlot** the unit, in AUDIT-M3 3.1's level-0 order (begin; the consume and mask
  store of t - 1; the landings, `accept(t - 3)`; the other store of t - 1; publish; step; end), S1 Link(3), S2/S3
  Link(1), the window a RegFile, the DMA a credit Fifo of 4 (controllers); a mesh tile records its DMA's packets as ref's
  `dmaOut`. Dispatch: one `switch` on the type (`visit`), W14's and W16's cases in it.
- **Own ports**: a peripheral's own ports (C&C 1's READY, error lines, GO) live in the slot (`OwnPorts`), declared
  through `OwnPortsDecl`, reached by index in `begin`/`end`.
- **The tile's paths** behind `G1Config::periph_slot` (M2 unchanged): the core's window loads, stores and stalls; bus
  errors judged at once from a static writable map (`writable(w)`); the block's EVENTS [15:13], ERRORS [13:11], wake,
  irq and error line; the director's S1; the builder adds and wires the slot.
- **One UB-only difference** (AUDIT-M3 3.1): a DMA_HDR/DMA_ADDR store mid-transfer acts a cycle later than ref's; ref
  flags it as undefined; coverage 0 in both M3 runs.

## 4. The fingerprint (AUDIT-M3 6.8)

`Replayer::fingerprint(o, c, pending)` writes 6.8's stream with ref's values. In a chip, ref's replayer still holds the
region writes accepted at c - 2 .. c and a PULSE_RUN or RUN_CONFIG store of c; here they are in the slot's links, so
**`PeriphSlot::fingerprint_replayer(sim, n, o, c)`** reads them with the port observer and writes them as ref's. W16's
chip line calls it for every tile's `rp`.

## 5. D35: the slot publishes only what changed

**The change.** Quest 170's profile had about 59% of one thread's time in the slot republishing its whole window every
step (`SlotCore::peek` 34.8%, `PeriphSlot::publish` 24.1%). Now the first publish writes every register (the plain
per-word recompute, `full_values`) and records each peripheral's defined own words; every later publish makes **one
dispatch per peripheral** and computes only the standard words, the defined own words (the replayer 4 of 96), the store
stalls only for a type with its own `store_stalls` (C&C 1), and the live bits from those values; only registers whose
value changed are written, as before.

**The guard.** In debug builds, after each publish, every register is compared with `full_values`; a mismatch is fatal
and names the register (e.g. "window register 1 (peripheral 1, standard word 1) is 0x1, published 0x0
(PeriphSlot::publish misses it)"). A planted missed register (STATUS) fires it.

**Unchanged behaviour.** The slot's tests (`test_periph_slot`, `test_slotconf`, `test_tileslotconf`, `test_tile_slot`,
`test_tracer_slot`, `test_cnc1_periph`); **T2 locally on one domain with every point** (inputs from Quests 165, 167 and
168, sha256-checked against Quest 170's list): evm-zone-29 241 points and cyclone-29 285, all 1,214 units equal, C&C 3's
exit and the end at ref's cycles, 64 of 64 tiles' rows equal to `rows.bin`, `lag_cycles` 0. The 6-domain split is left
to the runner (this container has one core). make check build by build and TSan on both compilers: clean.

**Local timing** (this container, clang release, the checked evm run with every point and the rows): **53 s before,
17 s after**. The runner's figures are W16's measurement against Quest 170.

## 6. Open

- The split (6 domains) with D35 is checked and timed on the runner by W16.
- `inflight.instr` in the fingerprint is always 0 in practice (a row in flight always belongs to `cur`); kept as 6.8
  writes it.
- Two findings in ref's test tools, recorded in NOTES: ReplayerTest's coverage check can't fail (a comment swallows its
  append, line 105); SyntheticDelivery's last window isn't reproducible run to run (ReplayConf's `--check` replays the
  committed scripts instead).
