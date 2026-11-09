# WORKER13 report: M3's reference side

Branch `sim-w13`. Plan and log: `NOTES.md`. The audit: `sim/tests/AUDIT-M3.md`. The references: `sim/tests/golden-m3/`.
Quests 159-162.

## What M3 runs

**Two runs, both T2's acceptance (Quest 110u):** **evm-zone-29 segment 10** and **cyclone-29 segment 10**, 64 windows,
raw, every unit on its real firmware (C&C 1's `CC_SEGMENT`). The second was added by the owner's rule: evm-zone 10 never
reaches controller-fed expansions or the memory system's early evictions and read waits, cyclone-29 10 does, and
reaches 151 more `replay_ctl` and 116 more `tracer_ctl` instructions (evm-zone 22 `tracer_ctl` ones cyclone doesn't).

`sim/ref` reproduces 110u in every figure but C&C 3's span (+26 cycles in both): `Config`'s GO, READY and error
latencies (1 → 8) and `stage_delay` (32 → 64) changed after 110u; with 110u's values `ref` gives 110u's 23,693,506
exactly (finding 9.1). The images and tables built here are byte-identical to 110u's.

**ReplayBench** (the D8/D15 bar) is T2's replay phase only, on segment 0, with the windows preloaded and no Tracer or
C&C cores: its numbers compare with T2's tiles' replay (at most 234K cycles a tile), not with T2's run.

## Delivered

| item | where | state |
|---|---|---|
| coverage of the runs: counters per unit group and phase, the new units' statistics, firmware pcs | AUDIT-M3 1; `golden-m3/cover/`; `M3Seg` | Quest 160 |
| the audit: the slot (3.1) and replayer (3.2) for W15, the Tracer and its memory system (3.3) for W14, the C&C cores (3.4), the cycle's order (2), D23's events (4), **the unit groups (5)** | AUDIT-M3 | W15's and W14's sections published first and merged |
| the fingerprint, `ref`'s side, for every M3 unit (6.6 Tracer, 6.7 slot, 6.8 replayer, 6.9 C&C units and the chip line) | `StateFpM3.java`; local checks `M3FpSelf`, `M3ChipFpSelf` (neutral, deterministic) | about 17 ms a whole-chip call, 0.5 ms the Tracer's |
| the references: both runs' figures, chip lines every 100,000 cycles with D12 headers; every unit's lines, the memory snapshot and the SRAM landings on the runner with hashes; `regen.sh` | `golden-m3/`; `$DATA/w13-golden-m3/`; `M3Inputs.java` | Quest 161; regen proven by Quest 162 |
| the baseline | AUDIT-M3 8; `golden-m3/baseline/` | Quest 161 |
| the task maker (a precompiled `sim/ref` kit, images checked against 110u's) | `WORKER13-tasks/` | |

## The baseline (Quest 161: `ref`, one JVM pinned to one P core of the runner's i5-14400F)

| run | segment (boot's end to the end) | per chip cycle | setup | boot |
|---|---|---|---|---|
| evm-zone-29 10 | **46.23 s** (45.87-46.93, 3 runs) | 1.93 µs | 10.5 s | 0.29 s |
| cyclone-29 10 | **50.90 s** (50.28-51.38) | 1.80 µs | 8.8 s | 0.29 s |

## What decides the next work

- **G7, the next worker: the C&C cores and the chip's integration** (AUDIT-M3 3.4, 4, 5): units 64-66 as framework
  tiles (M2's units, the controller router variant), the C&C links (`Link(1)`: C&C c and tile c in one domain), the
  host port, C&C 1's peripheral (READY, GO, error lines, watchdog, UART) and C&C 2's stub in W15's slot, the request
  bus's controller ports, the chip lines as events, and the T2 harness (loading the two dumps in place of T1's carry and
  the `poke`s). Its first milestone: the boot exact (cycle 7,222 and the boot-end fingerprints of all 1,214 units).
- **Timing**: T2 is serial on C&C 3's Tracer (23.7 M cycles of row and drain work) while the tiles mostly sleep and
  replay in turn; the speed of C&C 3's domain on one thread decides M3's wall time more than the split does.

## Findings (AUDIT-M3 9)

- **9.1** the latency defaults since 110u: +26 cycles, confirmed.
- **9.2** `EventCounts`' header says a consume takes effect at the end of s + 1; the slot does it at the end of s (`ref`
  as it runs). A depth-1 post consumed in its own cycle would be lost: reached in neither run.
- **9.3** `outStart` (PULSE_DMA) doesn't settle the Tracer; the framework's order differs only in undefined cases:
  reached 0 times of 14,162 and 15,716.
- **9.4** (W14) the Tracer's write buffer isn't capped at its depth; reproduced exactly (D27). Neither run comes near:
  at most 3 lines (cyclone), none in evm-zone.

## Open

- C&C 1's peripheral applies stores within the store's cycle; under the slot's rule the framework applies them a cycle
  later for the peripheral's own work (the watchdog's first count, a UART byte's start). Neither run reaches either:
  for G7 to keep or propose as a deliberate difference (D30). AUDIT-M3 3.4.
- Never reached by either run: `send_exp_word` (N3's send-port word) in both firmwares, every unit's error paths, C&C
  1's toast and traps, the UART, the watchdog's expiry. A segment that reaches N3 (W22's subset lists others) would be
  a third reference if wanted.
- Running T2 locally needs the runner's inputs (about 25 MB for a few windows); every T2 run here went to the runner,
  where a segment takes under a minute.
