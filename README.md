# meshsim

> **For the RTL: start here.** The current target is **`v1.3/`**, the Tracer. Read **`v1.3/spec/CHANGES-v1.26.1-to-v1.3.md`**
> first (what to build, unit by unit), then **spec 4.21**. The replayer is unchanged from v1.26.1.


Simulator and software for a parallel compute mesh, part of an ASIC for proving the Jolt zkVM.

| folder | status | what |
|---|---|---|
| `v1.0/` | frozen | sorting and basic mesh routing; full docs and diagrams |
| `v1.1/` | frozen | adds the RV64 trace replayer |
| `v1.2/` | frozen (pure) | **v1.2 exactly as the RTL engineer took it** (tag `v1.2-tony`): the spec, recipes, draft header and decisions; no simulator (none existed for it). `ERRATA.md` lists everything since |
| `v1.2.1/` | frozen (point release, 2026-11-01) | v1.2 with every erratum and cleanup applied, **plus the simulator and software in line, verified**: what was frozen as tag `v1.2` (the spec, meshsim with every suite, the sort at full size), updated. Known gaps: the boot ROM's branch for C&C 1-3 and C&C 2/3's "started" requests are not exercised (the controllers are Java models; WORKER14 is adding real cores in `wip/`), the replayer is checked on synthetic windows, `rcp_cc1.c` is built but not run |
| `v1.25/` | frozen (2026-11-01) | **the RTL's next target** (from v1.1 directly): the replayer and its universal tables at jolt-cpp `821e1bd`, **accepted on every window of EVM Zone and Cyclone at 2²⁹, row for row** (Quest 083, 084); what the hardware checks and what is UB (the spec's opening principle), the 128-bit gather for even *k* (the full-size sort 1,229,008 cycles, from 1,360,082), the 256-byte boot ROM, C&C 1-3 as real cores in meshsim (opt-in). Freeze check: Quest 093, 094. `spec/CHANGES-v1.25.md` is the delta from v1.2; **`spec/CHANGES-to-v1.25.md` is the RTL's guide from v1.1 or v1.2**. Decisions: `planning/DECISIONS-v1.25.md` |
| `v1.26.1/` | frozen (2026-11-05) | **the RTL's target, from v1.25 directly** (guide: `spec/CHANGES-v1.25-to-v1.26.1.md`): v1.26 plus the Tracer's final layouts for what the two units share: `RUN_CONFIG` at window word 32 (the rows a pass runs, the replayer's two per-pass flags, the Tracer's compression enables; sticky), words 32–35 the same on both units (`ROWS_RUN` 24 bits), no flags in the state word (the record loaded verbatim), the stream word rearranged with the advice field. **Accepted on every window of EVM Zone and Cyclone at 2²⁹, identical to v1.26 cycle for cycle** (Quest 106, 107); the suites and the full-size sort at 1,229,008 cycles (108) |
| ~~`v1.26/`~~ | superseded by v1.26.1, deleted 2026-11-05 (owner) | v1.25 plus the replayer changes designed for v1.3 (the 68-word record, the stream word's sizes, 4 KB queues, the flush on a state write, part 0 as the working state); accepted by Quest 098-104. Nobody built on it; **the folder is in git at commit 63ea8de** (`git show 63ea8de:v1.26/...`) |
| `v1.3/` | frozen (2026-11-06) | **the RTL's target: the Tracer** (guide: `spec/CHANGES-v1.26.1-to-v1.3.md`; spec 4.21): C&C 3's RV64 row machine, its memory interface and output path, the DMA in 128-bit words, the segment's barriers; meshsim with the Tracer and exact idle-skipping (about 18× on the full chip), gold, jumpmap and the input pipeline. **Accepted on every window of both workloads at 2²⁹ (T1), 18 full-chip T2 runs, and the final regression (Quest 118)** |
| `wip/` | in progress | work toward **v1.4**; started from v1.3 |
| `sim/` | started 2026-11-06 | **the next simulator**: a clean-sheet C++ framework (`sim/src/`), with v1.3's Java simulator as its reference and oracle (`sim/ref/`); see `sim/README.md` |

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
