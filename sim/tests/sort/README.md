# tests/sort: the sort's and its kit's references (WORKER24, P6)

Every file here is made by `ref` (v1.3's Java meshsim) through `sim/oracle/ref/`, once, and committed; nothing in
v1.35 regenerates it.

- `ref/`: v1.3's own steps (`run_tests.sh`'s sort-p1, sort, sort-shuf, sort-bt-kv32, sort-bt-k128, sweep-quick), run
  in `ref` with the committed images by `oracle/ref/sortlogs.sh`; the C++ suites' logs must equal them (the inputs
  line's first field, the simulator's own hash, masked: owner, Q2). `KEYS.sha256`: the BT steps' key set.
- `fp/`: SortFp (`oracle/ref/SortFp.java`, `sortfp.sh`): every run in `fp/RUNS` with a state fingerprint at every GO
  and all-READY, committed as its digest (`<name>.fpd`: one line per point, the sha256 of its sorted unit lines; the
  full `.fp` regenerated with `KEEP=DIR` to localise a difference, its sha256 in `HASHES`) and its facts (`<name>.out`).

v1.3's committed `results/` logs of these steps are older than v1.3's own image and boot ROM (NOTES, F1): not used.
