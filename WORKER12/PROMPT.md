# Worker 12: counting what the two workloads execute

You are measuring the two reference workloads of the mesh project (an ASIC for proving the Jolt zkVM), to inform design
decisions about its **Tracer**: the unit that executes the guest program, expands each RISC-V instruction into Jolt's
trace rows, and computes the **advice** values Jolt's expansions consume, using **untraced rows** that cost the Tracer time
but never appear in the trace. Some expansions are very heavy: the secp256k1 division inlines compute a modular inverse in
about 124,000 (DIVQ) or 215,000 (DIVR) untraced rows each. **How often they run decides whether that matters.** Your job
is the counts.

REPOSITORIES
- **jolt-asic-planning PR #2**, https://github.com/quarky93/jolt-asic-planning, branch `tracer-standalone-main`: a
  standalone CPU-only build of jolt-cpp's tracer at commit `821e1bd`, with the two workloads and their 2²⁹ inputs, and
  `tracer/tables/` (the table builder). Clone with the meshsim token from your user preferences (as
  `https://x-access-token:<token>@github.com/...`). Never write a token into a file.
- **meshsim**, https://github.com/nemmart/meshsim, the same way: `wip/tables/` (the replayer's and the Tracer's tables,
  with each template's traced and untraced row counts, and `wip/tables/README.md`), `WORKER9/REPORT.md` (how they were built,
  and how the 2²⁹ runs were done locally: they need `vm.overcommit_memory=1`), and `wip/planning/DECISIONS-v1.3.md` and
  `DECISIONS-v1.25.md` for context. Your notes and report go in meshsim's `WORKER12/`, on its `main`.

THE WORKLOADS
**Cyclone** (`cyclone/input/29`) and **EVM Zone**, jolt-cpp's new release (`evm-zone-4e3451c1d8c5`, input
`trace-29-4599adc50712`), both at 2²⁹ rows, run by PR #2's tracer. Also the small inputs (EVM Zone trace-23 and Cyclone's
smallest) as a first check.

WHAT TO MEASURE, for each workload
1. **Every source instruction's execution count:** all of RV64IMAC, the system and CSR instructions (by CSR and register
   pattern, as the tables split them), and every inline (the 15 in jolt-cpp, the seven secp256k1 inlines especially).
2. **For each, its rows:** traced rows (as Jolt's trace counts them) and **untraced rows** (from the Tracer's tables in
   `wip/tables/tracer/`: the untraced words of its template, loops unrolled by their repeat counts), times its executions.
3. **Totals:** traced rows against untraced rows for the whole run, and the untraced share; the top contributors to untraced
   rows.
4. **Per window of 2¹⁶ traced rows:** the distribution of untraced rows (mean, maximum, and which windows), since a window
   full of inverses stalls the Tracer locally even if the total is small.
5. **The controller-fed set:** how often each controller-fed template runs (the inlines; the CSR instructions), per run and
   per window, which bears on whether the CSR instructions belong in the ROM.
6. **The inverse's alternatives:** for DIVQ and DIVR, the untraced rows with the single Fermat loop (as built) and with an
   addition chain (about half, per WORKER9's report), so the difference is concrete.

HOW
- **Count from the tracer, don't re-implement it:** instrument PR #2's tracer (a counting pass over its compressed trace or
  its bytecode execution), or a small tool next to it, without changing what it produces. Check the totals against the
  traced row counts WORKER9 reported (Cyclone 371,526,406; new EVM Zone 354,861,942).
- The 2²⁹ runs fit a container with overcommit (WORKER9 ran them); if not, use the Quest runner (in your user preferences;
  a task carries what it needs).

RULES
- **Measure, don't decide.** Report the numbers and what they imply; the owner decides.
- This prompt is the go-ahead to add a counting tool on your own branch of jolt-asic-planning (made from
  `tracer-standalone-main`; don't push to that branch or to PR #1's), and to write in meshsim's `WORKER12/`. Change nothing
  else.
- **Keep the last few tool calls of every turn for committing and pushing.** Background jobs don't survive the end of a
  turn. Don't `pkill -f` or `pgrep -f` with a pattern that appears in your own command line. Keep `WORKER12/NOTES.md` current.

DELIVERABLES
- `WORKER12/REPORT.md`: the tables for 1-6, per workload, with the method and how the totals were checked; the CSV data
  beside it (`WORKER12/data/`, small files only).
- The counting tool on your branch, with how to rerun it.
