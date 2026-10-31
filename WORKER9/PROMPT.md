# Worker 9: the standalone tracer at current jolt-cpp (jolt-asic-planning PR #2)

You are refreshing a tool for the mesh project (an ASIC for proving the Jolt zkVM). PR #1 in
**https://github.com/quarky93/jolt-asic-planning** (branch `tracer-standalone`, commit `4f36b3d`) added a
**standalone, CPU-only build of jolt-cpp's tracer** under `tracer/`, copied from jolt-cpp at `4ab349bb` (main,
2026-10-19). jolt-cpp has moved on since: its `main` has new instructions and shorter expansions for the sub-word loads
and stores and the 32-bit atomics. Your job is **the same standalone tracer, re-copied from a pinned current jolt-cpp
`main`, as PR #2**. It becomes the reference build for milestone v1.25, a working clone of current jolt-cpp, whose
replayer must match this tracer's trace row for row on two 2²⁹-row workloads.

REPOSITORIES
- **jolt-asic-planning:** https://github.com/quarky93/jolt-asic-planning. Clone with the meshsim token from your user
  preferences (as `https://x-access-token:<token>@github.com/...`); it has access. Never write a token into a file.
- **jolt-cpp:** https://github.com/snarkify/jolt-cpp, the same way. Pin one current `main` commit at the start and use
  it throughout.
- **meshsim:** https://github.com/nemmart/meshsim, the same way, for this prompt and your notes and report
  (`WORKER9/`, committed to its `main`, `git pull --rebase` first).

INPUTS
- PR #1's `tracer/`: its `README.md` (the provenance table, build, runs, output, how `capture_writes` works),
  `CMakeLists.txt`, `src/` (`tracer/`, `utils/`, `zklib/bn/`, `trace_elf.cpp`, `capture_writes.cpp`), `evm-zone/` and
  `cyclone/` (programs and the 2²⁹ inputs), and `analysis/`; and `docs/specs/source-provenance.json`.
- meshsim's `wip/planning/DECISIONS-v1.3.md` section 9: what changed in jolt-cpp's expansions (new opcodes
  `VirtualLoadB/H/W`, `VirtualStoreB/H/W`, `VirtualWindowMaskB/H/W`, `VirtualShiftDataB/H/W`, `VirtualPext`/`Signed`;
  `VirtualAlignAddr` and the alignment assertions gone; a row-count table). `wip/planning/DECISIONS-v1.25.md`: why this
  build matters.

WHAT TO DO
1. **Branch** from `tracer-standalone` (e.g. `tracer-standalone-main`), so PR #2's diff against PR #1 shows exactly what
   changed.
2. **Re-copy** from the pinned jolt-cpp commit by PR #1's rules: only the files the tracer includes, the same renames
   and moves, the dependencies pinned as jolt-cpp pins them. Update `capture_writes.cpp` only as far as jolt-cpp's API
   changes require. Update the provenance table and `source-provenance.json`.
3. **The programs:** keep EVM Zone and Cyclone with their inputs. If jolt-cpp's copies of the example programs changed,
   say so and use jolt-cpp's, with the reason.
4. **Build and run both 2²⁹ workloads** (`make runs`), and with `--validate`. Compare with PR #1's runs: cycle (row)
   counts, write and load counts, and where they differ, why (the shorter expansions should shorten the traces). Don't
   commit outputs; they run to gigabytes.
5. **Document** in `tracer/README.md`: the new source commit, what changed against PR #1, the new run numbers.
6. **The run logs.** v1.25's replayer needs run logs (spec 4.19.2: state records and three streams) made at this commit.
   Find out where v1.1's run logs came from (meshsim's spec 4.19, items 108-113, `wip/meshsim/REPLAYER-NOTES.md` and
   the replayer's test-vector provenance). If a run-log writer exists on jolt-asic-planning's branches or elsewhere,
   report where and whether it ports to the new tracer; don't build one.
7. **Open PR #2** from your branch, as a draft, with a description summarising 2-5, if the token allows; otherwise push
   the branch and say so.

RULES
- One CPU and limited memory: the 2²⁹ runs may be slow, and the VM reserves 4 GiB of guest address space (PR #1's README
  on `vm.overcommit_memory`). If they can't run here, use the Quest runner (in your user preferences; a task carries what
  it needs), or report the limit.
- Questions go in meshsim's `WORKER9/QUESTIONS.md`, with your proposed answer; carry on.
- This prompt is the go-ahead to create and push your branch in jolt-asic-planning, open PR #2, and write in meshsim's
  `WORKER9/`. Change nothing else: not PR #1's branch, not meshsim's other files.
- Keep `WORKER9/NOTES.md` as you go; push at least every turn.

DELIVERABLES
- PR #2 in jolt-asic-planning (or its branch, pushed).
- `WORKER9/REPORT.md`: the pinned commit, what changed against PR #1, both runs' numbers against PR #1's, and the run-log
  findings (step 6).
