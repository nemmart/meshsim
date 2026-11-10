# WORKER16 report: the C&C cores and the chip's integration, T2 end to end (M3, G7)

Status 2026-11-10: **T2 exact on the framework, on one domain and on T2's 6-domain split**; the first timing done;
waiting for W15's D35 fix (the slot publishes only the registers that changed) to time again. On main at fb5f845.

## What T2 shows

M3's acceptance test, evm-zone-29 segment 10 and cyclone-29 segment 10 (64 windows each, `Config` defaults, mode 3),
run on the framework's chip with C&C 1-3 as cores, from W13's dumps (the memory at the segment's start, the harness's
big-SRAM landings):

- **The boot** ends at cycle 7,222, as ref's; C&C 1-3's and tile 0's pc equal to ref's between every cycle
  (`tests/m3-images/boottrace.ref.gz`, in `make check`, one domain, the split and the debug orders); every unit at the
  boot's end equal to golden-m3, **1,214 of 1,214 lines on both runs**.
- **The segment**: C&C 3 exits and the run ends at ref's cycles (evm 23,694,305 and 23,940,706; cyclone 28,106,741 and
  28,319,575); **every point equal, every unit and the chip line: evm 241 of 241, cyclone 285 of 285** (every 100,000
  cycles, C&C 3's exit, the end), **on one domain and on the 6-domain split** (Quest 170).
- **T2's own acceptance check**: the rows of all 64 tiles equal `rows.bin` (count and the replayer's sink hash, Quest
  169); every tile exits 1; C&C 3's `tr_results` status 1, 64 windows (14,162 and 15,716 transfers); the request bus's
  `lag_cycles` 0 (D34: mode 3 equals ref here).

## The first timing (Quest 170; i5-14400F, P core at up to 4.7 GHz, clang 18.1.3 release)

The segment only (CC_SEGMENT to the end), setup and boot apart as W13's baseline; 3 in-process repetitions:

| run | one thread | µs/cycle | split, 6 P cores | µs/cycle | ref | ×, one thread | ×, split |
|---|---|---|---|---|---|---|---|
| evm-zone-29 10 | 24.30 s | 1.015 | 20.95 s | 0.875 | 46.23 s | 1.90 | 2.21 |
| cyclone-29 10 | 26.56 s | 0.938 | 24.07 s | 0.850 | 50.90 s | 1.92 | 2.12 |

C&C 3's domain alone (tile 2 and C&C 3): 15.79 s and 18.59 s, the split's floor; the split's threads idle on imbalance
0.71 of their time. Where the time goes (one thread, Options::calibrate): the units' steps 94%; the controllers' slots
(C&C 3's Tracer) 58-64%, the tiles' slots (the replayers) 25-31%, everything else a few percent. perf: the slot
republishing its window every step (`SlotCore::peek`, `PeriphSlot::publish`) about 59%; the Tracer's own work about
13%, the replayers' about 6%; the engine about 1%. The owner's choice: D35 (W15).

## Delivered

- **The router's controller variant** (FEEDERS_CC, ccRoute; the C&C link, host and DMA inputs) and tiles 0-2's fifth
  port as the C&C links; RouterConf extended (five fixtures, equal to ref).
- **C&C 1's peripheral (type 1) and the stub (type 2)** on W15's slot (`units/cnc1_periph.h`): READY, the error lines,
  GO as events through the slot's own ports; Cnc1Conf (three fixtures, equal to ref directly and through the slot).
- **Units 64-66**: the tile builder over 67 units, `cc_cycle`, `build_chip` with `cc_cores`, the host port, the
  controllers' slots and C&C 1's chip lines (on top of W14's request bus).
- **The T2 harness** (`tests/t2_harness.*`): the dumps, Mach.bootCores (staging equal to ref's), CC_SEGMENT,
  coresBooted, the rows' reference, the partition; the `t2` tool (`--segment`, `--fp-points`, `--fp-boot`, `--fp-dump`,
  `--dump-at`, `--rows-ref`, `--time`, `--units`, `--thread-times`).
- **The M3 fingerprint** (`tests/statefp_m3.*`, AUDIT-M3 6.6-6.9): slot, cc1p, cc.host, cnc, ep, the chip line;
  W14's `req`/`reqbus` and `fingerprint_tracer`, W15's `fingerprint_replayer`; M2's writers for controllers.
- **Local oracles** (no runner): `tests/ref/M3Local.java` (ref's T2 chip from the dumps, any unit at any point),
  `BootStage.java`, `CcPing.java`.
- **Images**: `tests/m3-images` (cc1, cc23, tracer_ctl, replay_ctl, byte-identical to 110u's; `build.sh --check`).
- Quest tasks 165 (the boot-end lines), 167 (files dumps), 168 (unit lines), 169 (the rows' reference), 170 (T2 on
  the runner); `WORKER16/tasks/mk_task.sh`, `t2-body.sh`.

## Findings

1. **The cycle numbering** (AUDIT-M3 7): the framework's cycle F is ref's F - 1; a golden-m3 point t is the framework's
   state at `now()` = t + 2.
2. **M3's labelling** (StateFpM3: cyc = c.cycle - 1, M2's StateFp: c.cycle): `FpCtx::label_shift`, M2's default
   unchanged. Under it: a sleeping or halted core's mcycle is one less; a put made in the cycle just run isn't in ref's
   lazily settled bus yet, and a put of cyc followed by a take is settled; an EVENTS_MASK store or a consuming EVENTS
   load executed at s is shown pending at s + 1 with its effect already applied. All in the writers, not the model.
3. **ref's idle-skip sleep** (Chip.asleep) isn't modelled by the framework; the writer judges it by Tile.canSleep's
   conditions on the framework's state (a word put after the tile went idle doesn't keep it awake; a core halted in
   the cycle just run isn't asleep yet). Checked at every point of both runs.
4. **A missed wake in W8's register block** (approved): the block steps again after the core's own send, so a polling
   core sees STATUS's send-busy clear in ref's cycle (`test_cc_units` against ref's cycles).
5. **D33** (approved): C&C 1's own stores apply under the slot's rule, its watchdog and UART one cycle after ref's;
   neither run reaches them.
6. **cnc with cores**: ref's PULSE_GO still runs CnC.goNet (lastGoAt).
7. A crash in the router's fingerprint (a FIFO emptied after a put of cyc), found as a "silent stop" at 15.1 M.

## Open

- Time both runs again after W15's D35 fix, against Quest 170, with every point checked once more.
- `--fp-boot` and the segment's checks need the runner's dumps (Quest 165, 167, 168, 169 results), so they aren't in
  `make check`; the boot's trace and the harness's pieces are.
- C&C 1's own error line reaches the toast pin two cycles after ref's (no run reaches an error); UART RX has no source
  in M3.
