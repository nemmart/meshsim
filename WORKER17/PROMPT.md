# Worker 17: Jolt's memory, from the tracing side and the checking side

You are mapping **Jolt's memory space and what gets memory-checked**, for the **Tracer**, the unit of the mesh (an ASIC for
proving the Jolt zkVM) that executes the guest program and writes the run log the replayers consume, and for the prover's
**memory sumchecks**, which consume what the trace records. Look at it from both sides and connect them: what the tracer
produces about memory, and what the checking needs from it. The Tracer's design is in `wip/planning/DECISIONS-v1.3.md`; section
12 has its memory map (a sparse two-level page table over 4 GiB) and first findings from upstream Jolt; section 13 has advice
coming from the host in the read log. The replayer, which already consumes the run log, is spec 4.19.

REPOSITORIES
- **meshsim**, https://github.com/nemmart/meshsim: the decisions logs, the spec (`wip/spec/machine-spec.md`), `v1.25/` (frozen).
  Your notes and report go in its `WORKER17/`, committed to `main`.
- **jolt-cpp**, https://github.com/snarkify/jolt-cpp: our tracer **and** our prover (its RAM, register and bytecode checking,
  including the GPU sumchecks). The commit our work is pinned to is `821e1bd`.
- **jolt-asic-planning PR #2**, https://github.com/Quarky93/jolt-asic-planning, branch `tracer-standalone-main`: a standalone
  CPU-only build of jolt-cpp's tracer, with the two workloads (EVM Zone, Cyclone) and their 2²⁹ inputs. WORKER9's report
  (`wip/planning/sessions/WORKER9-REPORT.md`) says how to build and run them; the 2²⁹ runs need `vm.overcommit_memory=1`.
  WORKER12's counting tool (`worker12-counts` branch, `tracer/tables/count_exec.cpp`) and WORKER16's `state_probe`
  (`worker16-state` branch, `tracer/state/`) are models for instrumenting the tracer.
- **upstream Jolt**, https://github.com/a16z/jolt (public): `jolt-core` (the zkvm's RAM, registers, bytecode and instruction
  lookups) and `common` (`jolt_device.rs`, `constants.rs`: the memory layout).
- Clone each with the meshsim token from your user preferences (as `https://x-access-token:<token>@github.com/...`). Never write a
  token into a file.

THE QUESTIONS
Tracing side (what the tracer does with memory):
1. **The layout, in jolt-cpp and upstream.** Every region (advice, inputs, outputs, termination and panic words, program, stack
   canary, stack, heap), its address range and how it is configured, `lowest_address`, how an address becomes a cell index, and
   how K (the number of cells) is sized. Then the actual values for EVM Zone and Cyclone. Where jolt-cpp and upstream differ, say so.
2. **Accesses per row.** Which instructions and virtual rows touch RAM; the access width and alignment (how byte and halfword
   loads and stores become aligned 64-bit accesses); what a trace row records about the access (address, value read, value before
   and after a write); whether rows with no access appear in the RAM checking at all.
3. **What the workloads touch, at 2²⁹.** Distinct 64-bit cells and distinct 16 KB pages touched, by region, read-only versus
   written; the same per 2²² slice (the working set) and how much is new in each slice. This sizes the page table (T3).
4. **The initial and final memory.** What the initial image holds (program, data, inputs, advice) and where it comes from; which
   cells change; what is read at the end (outputs, termination).

Checking side (what the prover checks, in upstream and in jolt-cpp):
5. **Every sumcheck that touches RAM, registers or bytecode:** read-write checking, Val evaluation, the one-hot address checks
   (Hamming weight, booleanity, `ra` virtualization), the output check, and any others. For each: what it proves, what it takes
   per row (`ra`/`wa` addresses, read and write values, `Inc`), what is committed, its dimensions (K, T, the chunking d) and its
   binding order. An earlier session found jolt-cpp binds RAM and register read-write checking address-first, the opposite of
   upstream; confirm or correct that.
6. **The boundary values.** How the verifier gets the initial RAM (the program image, inputs, advice and their commitments), and
   what the output check ties the final I/O region to.
7. **What is not memory-checked:** advice values (constrained by their sequences), anything else the trace carries unchecked.

Connecting the two:
8. **What the checking needs from the trace, per row and per run, against what the replayer and the Tracer produce** (the row's
   fields, the read log, the write log, spec 4.19): what lines up, what is missing, what is produced but unused.

HOW
- **Read the code for the definitions, and measure on the real runs for the numbers**: instrument PR #2's tracer on your own
  branch without changing what it produces (check that its rows are unchanged, as WORKER16 did). Check against WORKER9's,
  WORKER12's and WORKER16's results where they overlap (`wip/planning/sessions/`, `WORKER16/`).
- Cite file and line for every definition, in both codebases.

RULES
- **Propose a plan first, and wait for the owner's go-ahead** before writing code or files.
- **Measure and report; don't design.** The owner decides what follows.
- With the go-ahead: add your tools on your own branch of jolt-asic-planning (made from `tracer-standalone-main`; don't push to it
  or to anyone else's branch), and write in meshsim's `WORKER17/`. Change nothing else.
- Quest task numbers, if you need the runner: claim yours in the Quest repository's `NUMBERS.md` first (pull, append, commit, push;
  on a rejected push, pull and take the next), and end task scripts with an explicit `exit` before any embedded data.
- **Keep the last few tool calls of every turn for committing and pushing**, and end each turn with a clean `git status`. Background
  jobs and long runs don't survive a turn or a tool call's time limit; use the runner for anything long. Keep `WORKER17/NOTES.md`
  current.

DELIVERABLES
- `WORKER17/REPORT.md`: the memory map (both codebases, both workloads); the per-row access rules; the sumcheck table (question 5);
  the boundary values; what is unchecked; the mapping from the trace to the checking (question 8), with gaps; the measurements;
  open questions. Small data files in `WORKER17/data/`.
