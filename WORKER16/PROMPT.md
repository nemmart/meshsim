# Worker 16: the Tracer's starting state and address range (T2, T8)

You are checking two assumptions for the **Tracer**, the unit of the mesh (an ASIC for proving the Jolt zkVM) that executes the
guest program and writes the run log the replayers consume. Its design is in `wip/planning/DECISIONS-v1.3.md`; section 12 lays out
its memory map, and rests on two assumptions about Jolt that need confirming against the real code and the real workloads.

REPOSITORIES
- **meshsim**, https://github.com/nemmart/meshsim: the decisions logs, the spec (`wip/spec/machine-spec.md`, the replayer in 4.19).
  Your notes and report go in its `WORKER16/`, committed to `main`.
- **jolt-asic-planning PR #2**, https://github.com/quarky93/jolt-asic-planning, branch `tracer-standalone-main`: a standalone
  CPU-only build of jolt-cpp's tracer at commit `821e1bd`, with the two workloads and their 2²⁹ inputs (WORKER9's report,
  `wip/planning/sessions/WORKER9-REPORT.md`, says how to build and run them; the 2²⁹ runs need `vm.overcommit_memory=1`).
  jolt-cpp itself is https://github.com/snarkify/jolt-cpp. WORKER12's counting tool is on that repository's `worker12-counts`
  branch (`tracer/tables/count_exec.cpp`), a model for instrumenting the tracer.
- Clone each with the meshsim token from your user preferences (as `https://x-access-token:<token>@github.com/...`). Never write a
  token into a file.

THE TWO QUESTIONS
1. **T2: is pc, `instr_idx` and registers 0-127 the Tracer's entire starting state?** The Tracer starts a program from part 0 of
   its region, the replayer's layout: a state word (pc, `instr_idx`, next offset) and a register image of registers 0-127 (x0-x31
   and Jolt's virtual registers). jolt-cpp maps each CSR onto a virtual register, and Cyclone takes 875 traps, so the CSRs are live.
   Find **every piece of state jolt-cpp's tracer keeps** that affects the rows it produces: registers, CSRs, the LR/SC reservation,
   any counters, the memory layout's own state (input and output pointers, the advice tape if any), anything in the interpreter
   outside the register file. For each: where it lives (file and line), whether it's in registers 0-127, and if not, what the
   Tracer would need in addition.
2. **T8: does a flat page table of 32,768 entries × 16 KB pages (512 MiB of guest space) cover the workloads?** Jolt places RAM at a
   fixed base (0x80000000) and bounds it by its configured RAM size, **K** (the owner expects 2²⁴ or 2²⁵ cells; not to be confused
   with meshsim's counter delay, also called K). For each workload (EVM Zone `4e3451c1d8c5` trace-29 and Cyclone input/29, plus the
   small inputs as a first check), report:
   - the configured K and the memory layout jolt-cpp derives from it (where code, data, heap, stack, input, output and advice sit),
     with the file and line;
   - **the lowest and highest addresses actually touched**, loads and stores, and how the touched 16 KB pages are distributed
     across the range (count them, and show where they cluster);
   - whether everything falls inside 512 MiB from the base, and if not, by how much and where.
   - **the key question: does Jolt guarantee the range?** Jolt's memory-checking argument translates each address into a cell index
     that must be below K (an offset from the base divided by 8, with the input, output, advice and other I/O areas mapped into the
     same index space). Confirm from jolt-cpp's code (the address remapping and where K bounds it, file and line) whether **every
     address a provable trace can touch lies in one contiguous range of K × 8 bytes**. If so, the page table's size depends only on
     the largest K the system supports, not on the program. Say exactly how the I/O areas fit, and whether anything can fall outside.

HOW
- **Read jolt-cpp for the definitions, and measure on the real runs for the numbers**: instrument PR #2's tracer (or extend WORKER12's
  counting tool) without changing what it produces. Check against WORKER9's and WORKER12's totals where they overlap.

RULES
- **Measure and report; don't design.** The owner decides what follows.
- This prompt is the go-ahead to add a tool on your own branch of jolt-asic-planning (made from `tracer-standalone-main`; don't push to
  it or to PR #1's branch), and to write in meshsim's `WORKER16/`. Change nothing else.
- Quest task numbers, if you need the runner: claim yours in the Quest repository's `NUMBERS.md` first (pull, append, commit, push;
  on a rejected push, pull and take the next), and end task scripts with an explicit `exit` before any embedded data.
- **Keep the last few tool calls of every turn for committing and pushing**, and end each turn with a clean `git status`. Background
  jobs don't survive a turn. Keep `WORKER16/NOTES.md` current.

DELIVERABLES
- `WORKER16/REPORT.md`: T2's inventory of the tracer's state, and T8's table per workload (K, the layout, the touched range, the page
  distribution), with the method and how it was checked; small data files in `WORKER16/data/`.
