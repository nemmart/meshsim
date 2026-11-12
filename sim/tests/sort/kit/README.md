# The sort's measurement kit (meshsim v1.35)

v1.3's kit (`v1.3/meshsim/kit`, WORKER24's port, V135-INVENTORY D8) on the C++ simulator: the same job list
(`jobs.txt`: 92 runs, the 20-set catalogue in both formats, three shuffled-order identity runs, a 46-point sweep), the
same options, outputs and summary; **no Java to run it** (D40).

- `./assemble.sh OUTDIR [KEYDIR]` (from `sim/`, in `tests/sort/kit/`): the kit, with `sort_test` (v1.3's SortTest),
  `sort_summary` (SortSummary), the sort's image, the boot ROM, hops.txt and `HASHES`; with KEYDIR, its key sets.
- `OUTDIR/run.sh [-j JOBS] [-o OUTDIR] [-n KEYS_PER_TILE] [-g p4,shuffle,sweep] [-f FILTER] [-J JOBFILE] [-s]`
  (v1.3's options; `-m` accepted and unused). A smoke test: `./run.sh -n 2000 -j 4`.
- **Key sets**: KeyGen (`pipeline/keys`, Java) makes them; the kit takes them prebuilt (`keys-N/<set>.keys`, checked
  against `HASHES`, whose key lines are v1.3's), and makes a missing one only where a JDK is present (owner's Q4).
- **Outputs** as v1.3's: `<id>.csv`, `<id>.phases.csv`, `<id>.log` per run; `SUMMARY.md`, `all.csv`.

Differences from v1.3's kit, all tooling: the simulator is `sort_test` (the inputs line names it, owner's Q2); the
kit's own two fixes to v1.3's (NOTES F2: v1.3's image files, the boot ROM beside the simulator); `make_jobs.py` isn't
carried (Python avoided, D40; `jobs.txt` is its output). Six sweep points are refused as v1.3 refuses them (F3).

**Status** (WORKER24): the p4 and shuffle groups run; their CSVs and logs equal v1.3's (`n2000.tgz`, Quest 178) but
for the peaks (W20's PhaseLog); the phases CSVs and the sweep's `cfg.*` fields wait for W20's PhaseLog and Config.
