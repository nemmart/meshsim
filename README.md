# meshsim

Simulator and software for a parallel compute mesh, part of an ASIC for proving the Jolt zkVM.

| folder | status | what |
|---|---|---|
| `v1.0/` | frozen | sorting and basic mesh routing; full docs and diagrams |
| `v1.1/` | frozen | adds the RV64 trace replayer |
| `v1.2_wip/` | snapshot | the v1.2 spec and recipes, read-only, for sessions working against v1.2 while the simulator is ported; not yet frozen. **In use by Tony: nobody changes it without the owner's agreement** |
| `wip/` | in progress | work toward the next milestone; started as a copy of v1.1 |

**How versions are used:** each frozen version (spec plus meshsim) is the reference the RTL is upgraded against,
outside this repository. What the RTL work turns up comes back afterwards as a delta, folded into the next version;
this repository doesn't track the RTL in between.

Frozen folders are never edited. Each one is also tagged (`v1.0`, `v1.1`). When `wip/` reaches a
milestone it is copied to a new frozen folder and tagged.

## What's in the repo and what isn't

- **Docs and diagrams** in each folder describe that version. The v1.0 documentation was carried into
  v1.1 and `wip/` without being updated; treat it as describing v1.0.
- **Figures:** SVGs only. PNGs are renderings of them; `make_docs.sh` regenerates both.
- **Results:** summaries only (`SUMMARY.md`, `all.csv`, the verification and comparison reports).
  Every run's CSV and log is in the release attachment for that tag.
- **Build outputs** (`.class`, `build/`) are left out. The prebuilt kits (`kit/`, `verify-v1.0/`)
  are kept whole so they run with just Java.

## Working here

Only `wip/` changes. A session that changes code works on a branch and leaves merging to the owner and the
integrator. A session that owns its own files (a review, the spec, the recipes) may commit straight to `main`,
running `git pull --rebase` before every push. Work-in-progress session folders (`WORKERn/`) sit at the top
level while a session is running and are cleaned up when it closes.
