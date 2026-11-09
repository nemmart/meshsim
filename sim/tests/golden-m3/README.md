# M3's references (WORKER13; `sim/tests/AUDIT-M3.md` section 7)

Made by Quest 161 (`sim/ref`, sim-w13, the runner: i5-14400F, `baseline/machine.txt`) with `sim/tests/ref/M3Seg.java`,
which runs `TracerT2.chip` exactly (Quest 160: the same figures as `TracerT2`, hooks on or off).

| file | what |
|---|---|
| `RUNS` | per run, T2's figures: segment, windows, C&C 3's cycles (start to exit), the Tracer's row cycles, drain cycles, transfers, DMA packets, the longest replay, rows checked, the chip's last cycle |
| `evm.chip`, `cyclone.chip` | the D12 header (`Config`'s values, the run, the inputs' hashes) and the chip line of every fingerprint point: every 100,000 cycles, `# boot-end 7222`, `# cc3-exit`, `# end` (AUDIT-M3 6.9) |
| `HASHES` | sha256 of the large files on the runner, `$DATA/w13-golden-m3/`: every unit's line at every point (`*.fpchip.gz`: 293,788 and 347,204 lines; gzip's header holds a name and time, so compare contents, as `regen.sh` does), the memory at segment 10's start (`*.files.gz`) and the harness's big-SRAM landings before the boot (`*.sram.gz`), both in `M3Inputs`' formats |
| `cover/` | Quest 160's coverage (AUDIT-M3 section 1) |
| `baseline/` | `ref`'s wall time (Quest 161, one JVM pinned to one P core): `BASELINE.txt`, the six runs' CSVs, the machine |
| `regen.sh` | regenerates both runs and checks the figures, every chip line, every unit line and the inputs' dumps |

The runs: **evm-zone-29 segment 10** and **cyclone-29 segment 10**, 64 windows, raw, `Config` defaults (GO 8, READY 8,
error 8, `stage_delay` 64, idle-skip on), the images in `sim/planning/sessions/WORKER13-tasks/IMAGES` (110u's).
