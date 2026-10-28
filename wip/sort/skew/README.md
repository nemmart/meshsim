# sort/skew — the skew planner

The per-tile planner that chooses the sort's transfer path (direct, LB, TH, BT) and builds its tables. **Read first:** `skew-v2.md`, the design.

| path | what |
|---|---|
| `skew-v2.md` | the design: the four paths (direct, LB, TH, BT), placement, commands and tables, the three-stage switch, memory and code |
| `RESULTS.md` | the planner's measurements: every data set, planning and set-up cycles per function, the (w, L) tuning (its model comparisons refer to the Python model, not carried into v1.0) |
| `planner/` | the per-tile planner in portable C for RV32I + Zmmul with no divide: `skew.h` (state, constants), `skew_common.c` (cost formulas, stage 1), `skew_th.c` (window table, stage 2, TH set-up), `skew_bt.c` (BT: selection, sweep, descriptors, layers, set-up). The sort links a patched copy (`../program/patches/planner.patch`, applied in `meshsim/sw/ext/skew-planner/`). |
| `harness/` | the planner's regression test: `harness.c` (64 tiles, exchanges, reductions, record-level execution and checks), `rv32emu.c` (RV32 cycle counts with spec 4.11's timing), `rv_entry.c` and `link.ld` (the RV32 image), `build.sh`, `data/` (the 20 stream matrices), and the expected results `results.txt` (128-bit values) and `results-fmt32.txt` (32-bit values); `tune*.txt` and `profile-*.txt` are the tuning and profile runs |

**Regression test:** `harness/check.sh` builds the harness and the RV32 planner image, runs every set in both formats and compares with the expected results. It needs `gcc` and `riscv64-unknown-elf-gcc`.
