# Worker 18: a Java emulator of Jolt's tracer, to become our gold

You are building a **standalone Java emulator of the Jolt zkVM's tracer**: a program that reads a guest's ELF, sets up Jolt's
memory, and runs the program, producing **Jolt's trace rows** exactly as jolt-cpp's tracer does. It is for the mesh, an ASIC for
proving Jolt: the chip's **Tracer** executes the guest program and writes the run log its replayers consume. The emulator is to
become **our gold reference** for that work. Read meshsim's `wip/planning/DECISIONS-v1.3.md` first, especially sections 12-17
(section 17 is this project; 13 the advice; 15 and 16 the per-segment data and the segment file).

THE OWNER'S REQUIREMENTS
- **Self-contained and clear.** Its own folder and build, `wip/gold/` in meshsim; no imports from meshsim's other classes (the
  replayer model, the universal tables), no native code, no ELF library (parse ELF64 by hand), no use of the C++ tools. Each part
  written to be read: the ELF loader; Jolt's memory layout; an RV64 interpreter; **Jolt's expansions (virtual sequences and
  inlines) written out per instruction family as plain code**, following jolt-cpp's `isa/virtual.cpp` and its inlines; the CSRs,
  traps, LR/SC and ECALL handling; the advice (the division family directly, secp256k1 in `BigInteger`).
- **Validation is central.** jolt-cpp is the reference, used as **data, never as code**: its trace rows, WORKER16's segment files,
  WORKER17's layout and page tables. Every claim of correctness is a comparison against that data.

STAGES
- **Stage 1, a Tracer model.** The loader, the layout, the interpreter and the expansions. Advice values may come from the data
  (jolt-cpp's rows, or a segment file's frame 3). Goal: rows identical to jolt-cpp's, first on the small inputs (EVM Zone
  trace-23, Cyclone input/22), then on both workloads at 2²⁹ (stream and hash per window; don't hold 2²⁹ rows in memory).
- **Stage 2, gold.** It computes everything itself: the advice, the processor state at each 2²² cut (pc, `instr_idx`, next
  offset, registers 0-127), each segment's dirty runs and the page counts, and writes section 16's segment file. It earns gold by
  writing files **byte-identical** to WORKER16's tool (work item W3) on both workloads at 2²⁹ and on its hard-cut tests.

REPOSITORIES AND DATA
- **meshsim**, https://github.com/nemmart/meshsim: your code in `wip/gold/`, your notes and report in `WORKER18/`, committed to
  `main`. Touch nothing else.
- **jolt-cpp**, https://github.com/snarkify/jolt-cpp, pinned at `821e1bd`: read `src/tracer/` (`isa/`, `inlines/`, `loader.*`,
  `trace_owner.*`, `vm.*`) for the semantics. Cite file and line for each rule you implement.
- **jolt-asic-planning**, https://github.com/Quarky93/jolt-asic-planning, branch `tracer-standalone-main` (PR #2): the CPU-only
  tracer and the two workloads with their inputs. WORKER9's report (`wip/planning/sessions/WORKER9-REPORT.md`) says how to build
  and run it; WORKER16's `state_probe` (branch `worker16-state`) and WORKER17's probe (`worker17-memory`) show how jolt-cpp's rows
  are produced and compared. **To produce reference data** (row dumps, hashes), add tools on your own branch of this repository,
  made from `tracer-standalone-main`; don't push to anyone else's branch. The emulator itself never calls them.
- WORKER16's notes (`WORKER16/`) record registers 40-47, the secp advice buffer and the restart method; WORKER17's (`WORKER17/`)
  the memory layout of both workloads and the page measurements.
- Clone each with the meshsim token from your user preferences (as `https://x-access-token:<token>@github.com/...`). Never write a
  token into a file.

RULES
- **Propose a plan first, and wait for the owner's go-ahead** before writing code or files. Start with the row format you will
  compare and how you'll get jolt-cpp's rows as data.
- Quest task numbers, if you need the runner: claim yours in the Quest repository's `NUMBERS.md` first (pull, append, commit,
  push; on a rejected push, pull and take the next), and end task scripts with an explicit `exit` before any embedded data.
- **Keep the last few tool calls of every turn for committing and pushing**, and end each turn with a clean `git status`.
  Background jobs and long runs don't survive a turn or a tool call's time limit; use the runner for anything long. Keep
  `WORKER18/NOTES.md` current.

DELIVERABLES
- `wip/gold/`: the emulator, its build (a script with plain `javac`), its tests, and a README on how to run it and what it
  matches.
- `WORKER18/REPORT.md`: what was built, the validation results (per workload, per stage), every rule with its jolt-cpp citation
  where non-obvious, and open questions.
