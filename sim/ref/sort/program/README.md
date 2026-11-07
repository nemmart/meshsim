# sort/program — the sort program

The tile program that sorts N = 64 × n records on the machine: local sorts (L1), the split (SPLIT, `../split/`), the
transfer planning (XB, RED, XC; the planner `../skew/`), the keys and values all-to-alls on the chosen path (direct, LB,
TH or BT: K, V and their layers) and the final local merge (L2). Written in C against `mesh.h` 1.0 (`../../sw/`), with
overlays so the kernel fits instruction memory.

| path | what |
|---|---|
| `sw/` | the program: `main.c` (the command loop and the overlay loads), `sort.h`, `local.c` (L1, L2: the local sorts), `exchange.c` (the resident one-word exchange and tree reduction), `transfer.c` (XB, RED, K and V) and `config.c` (the direct/LB tables and K's configure), `skew.c` (stage 2 of the skew switch and the TH path), `bt_plan.c` and `bt_run.c` (BT), `loader.S` (the resident loader) and `sort.ld` (the kernel and overlay layout) |
| `build.sh` | builds the images against `../../meshsim` (its `mesh.h`, `crt0.S`, loader and the patched planner and split in `meshsim/sw/ext/`); outputs in `build/` |
| `patches/` | the sort's changes to the planner (`planner.patch`) and the split (`split-barrier.patch`), as applied in `meshsim/sw/ext/` |

meshsim builds the same sources from its own copy (`meshsim/sw/ext/sort/`); `meshsim/tools/check_layout.sh` (run by
`run_tests.sh`) checks the copies equal these. The sort is run and checked by meshsim's `SortTest` (`meshsim/test/`)
and measured by the kit (`../../kit/`).
