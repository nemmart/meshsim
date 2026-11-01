# meshsim

Simulator and software for a parallel compute mesh, part of an ASIC for proving the Jolt zkVM.

| folder | status | what |
|---|---|---|
| `v1.0/` | frozen | sorting and basic mesh routing; full docs and diagrams |
| `v1.1/` | frozen | adds the RV64 trace replayer |
| `v1.2/` | frozen (pure) | **v1.2 exactly as the RTL engineer took it** (tag `v1.2-tony`): the spec, recipes, draft header and decisions; no simulator (none existed for it). `ERRATA.md` lists everything since |
| `v1.2.1/` | frozen (point release, 2026-11-01) | v1.2 with every erratum and cleanup applied, **plus the simulator and software in line, verified**: what was frozen as tag `v1.2` (the spec, meshsim with every suite, the sort at full size), updated. Known gaps: the boot ROM's branch for C&C 1-3 and C&C 2/3's "started" requests are not exercised (the controllers are Java models; WORKER14 is adding real cores in `wip/`), the replayer is checked on synthetic windows, `rcp_cc1.c` is built but not run |
| `wip/` | in progress | work toward **v1.25** (the replayer and its tables at jolt-cpp `821e1bd`), then v1.3 (the Tracer); started from v1.2 |

**How versions are used:** each frozen version (spec plus meshsim) is the reference the RTL is upgraded against,
outside this repository. What the RTL work turns up comes back afterwards as a delta, folded into the next version;
this repository doesn't track the RTL in between.

Frozen folders are never edited, **except for an `ERRATA.md`** listing known errors and their corrections (`v1.2/ERRATA.md`), and **point releases** made in their own folder (`v1.2.1/`). **Each version is a folder on `main`**; from v1.2.1 on there are no new tags (the older tags `v1.0`, `v1.1`, `v1.2`,
`v1.2-tony` stay as history). When `wip/` reaches a milestone it is copied to a new frozen folder.

## What's in the repo and what isn't

- **Docs and diagrams** in each folder describe that version. The v1.0 documentation was carried into
  v1.1 and `wip/` without being updated; treat it as describing v1.0.
- **Figures:** SVGs only. PNGs are renderings of them; `make_docs.sh` regenerates both.
- **Results:** summaries only (`SUMMARY.md`, `all.csv`, the verification and comparison reports).
  Every run's CSV and log: for the older versions, the release attachment for their tag; from v1.2.1 on, the Quest
  repository's `results/`, by task number.
- **Build outputs** (`.class`, `build/`) are left out. The prebuilt kits (`kit/`, `verify-v1.0/`)
  are kept whole so they run with just Java.

## Working here

Only `wip/` changes. A session that changes code works on a branch and leaves merging to the owner and the
integrator. A session that owns its own files (a review, the spec, the recipes) may commit straight to `main`,
running `git pull --rebase` before every push. Work-in-progress session folders (`WORKERn/`) sit at the top
level while a session is running and are cleaned up when it closes.
