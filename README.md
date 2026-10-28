# meshsim

Simulator and software for a parallel compute mesh, part of an ASIC for proving the Jolt zkVM.

| folder | status | what |
|---|---|---|
| `v1.0/` | frozen | sorting and basic mesh routing; full docs and diagrams |
| `v1.1/` | frozen | adds the RV64 trace replayer |
| `wip/` | in progress | work toward the next milestone; started as a copy of v1.1 |

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

Each session works on a short-lived branch and merges into `main`. Only `wip/` changes.
