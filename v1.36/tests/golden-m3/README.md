# M3's records (v1.35; `AUDIT-M3.md` section 7)

What is left of v1.35's references of T2, the segment on v1.35's full chip under v1.35's firmware (`ref`'s `M3Seg`,
which ran `TracerT2.chip` exactly; Quests 160 and 161, the runner: i5-14400F, `baseline/machine.txt`). **v1.36 retired
T2** (the tools `t2` and `chain`, their harness and its test) when acceptance C took the segment on the chip under the
RTL's firmware (`tests/test_rtl_segment.cpp`), and with it the references only T2 read: the runs' figures (`RUNS`), the
chip lines of every fingerprint point (`evm.chip`, `cyclone.chip`), the boot-end points (`boot/`) and the chain's
starts (`tests/golden-chain/`). Nothing in the tree reads this folder.

| file | what |
|---|---|
| `HASHES` | sha256 of v1.35's large files on the runner, `$DATA/w13-golden-m3/`: every unit's line at every fingerprint point (`*.fpchip.gz`), the memory at segment 10's start (`*.files.gz`), the harness's big-SRAM landings before the boot (`*.sram.gz`) and the boot-end points (`*.boot.fp.gz`) |
| `cover/` | Quest 160's coverage of `ref`'s run (AUDIT-M3 section 1) |
| `baseline/` | `ref`'s wall time (Quest 161, one JVM pinned to one P core): `BASELINE.txt`, the six runs' CSVs, the machine |

The runs were **evm-zone-29 segment 10** and **cyclone-29 segment 10**, 64 windows, raw, v1.35's `Config` defaults.
