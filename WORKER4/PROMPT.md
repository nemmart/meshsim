# Worker 4: meshsim to v1.2

You are bringing meshsim, the cycle-level simulator of the mesh (part of an ASIC for proving the Jolt zkVM),
from the v1.1 machine to v1.2. The v1.2 spec is written and reviewed, and the v1.2 recipes are written and
compile; none of it has run. **Your simulator is where v1.2 first runs**, and the recipes are its first tests.
After you, the sort and the replayer are ported onto it.

REPOSITORY
- Clone https://github.com/nemmart/meshsim with the meshsim token from your user preferences (as
  `https://x-access-token:<token>@github.com/nemmart/meshsim.git`). Never write a token into a file.
- `wip/` is the work in progress; `v1.0/` and `v1.1/` are frozen. Work on a branch, **`v1.2-meshsim`**; push
  often and leave merging into `main` to the owner and the integrator.
- For long runs, the Quest runner runs tasks in parallel: its repository and `PIPELINE.md` are in your user
  preferences. It can't reach meshsim, so a task carries the code it needs.

INPUTS
- **`wip/spec/machine-spec.md` v1.2: the authority.** Read it all. Section 13 lists what is still open,
  *(Proposed)* or *(Deferred)*: build *(Proposed)* items as written, and nothing *(Deferred)*.
- `wip/spec/CHANGES-v1.2.md`: every change from v1.1, section by section, each citing its decision. Use it as the
  map from v1.1's simulator to v1.2's.
- `wip/planning/DECISIONS-v1.2.md`: the decisions behind the spec, for the reasoning. Where the spec and the log
  disagree, tell the integrator (below); don't pick one silently.
- **The recipes:** `wip/sw/RECIPES.md` and `wip/sw/CONVENTIONS.md`, generated from the compiled sources in
  `wip/sw/recipes/` (build with `build.sh`; `check_book.py` keeps the book in step with the code), and the draft
  header `wip/sw/mesh-v1.2-draft.h`.
- `wip/meshsim/`: the v1.1 simulator (`src/`, `test/`, `sw/`, `README.md`). `Registers.java` generates `mesh.h`
  and the register tables.
- `wip/planning/reviews/WORKER3-REVIEW-2.md`, section 6: a file-by-file migration inventory of meshsim. It
  predates the boot ROM and several later decisions, so check each row against the spec.
- `wip/planning/sessions/`: the spec and recipe sessions' reports, questions and findings.

YOUR SCOPE
- **Yours:** `wip/meshsim/` (`src/`, `test/`, `sw/` except `sw/ext/`, `run_tests.sh`, the generated `mesh.h` and
  register tables), and the boot ROM image.
- **The header:** generate the real `mesh.h` from `Registers.java` so it matches the draft header's structs,
  names and offsets (the recipes compile against the draft); where they must differ, change the generator, not
  the recipes, and list the differences.
- **Not yours:** the sort (`wip/sort/`, `sw/ext/`), the replayer's own software, the spec, the recipes, the
  decisions log. Provide the standard peripheral window and a peripheral framework, and move the replayer model
  onto them only as far as the replayer recipes need.
- **C&C 1 stays a behavioural Java model** (as `CnC.java` in v1.1), driving the registers of its peripheral
  (spec 6.6, 8.14) from the test harness, which also plays the outside world: it lands C&C 1's state and issues
  the hard and soft resets (6.1). C&C 2 and C&C 3 likewise, until their firmware exists.

WHAT TO BUILD, in phases (each ends with the suites passing, then a push)
1. **The packet path** (spec 2, 3, 4.1-4.10): the 256-bit link word; 32-bit byte addresses and regions; the six
   modes, count-only included; seven counters, additive arming, the delay K; natural alignment and the
   bad-packet rules; ordering per sending unit.
2. **The controller and its registers** (4.11-4.16, 8): the register block, `PARAMS`/`STATUS`/`ERRORS`, the pulses,
   `EVENTS` and the wait rules, the send port with the no-increment bit, the read port, the request port and the
   receive queues in the request tree with their sizes (3.3, 4.18); the load latencies (2 cycles for registers and
   window words, 1 for memories and the table window).
3. **Peripherals** (4.17, 4.19, 6.6, 8.11-8.14): up to three per tile, the standard window, counted and live
   events, the DMA on its own router port; the UART; C&C 1's peripheral registers.
4. **Errors, reset and boot** (4.13, 6.1-6.3): terminal traps and the handler, the error lines, the toast pin and
   the watchdog, hard and soft reset, the boot ROM exactly as specified and the boot sequence.
5. **meshsim-only checks** (spec section 9 and every rule marked as meshsim's): out of bounds, reserved modes,
   misalignment, the conventions, `ecall`/`ebreak`, the reserved bytes, broadcast streams from mesh tiles,
   count-only packets on counter 0, and the stray diagnostic for unexpected arrival. Replace `ebreak` as the way a
   test ends with the simulator-only exit word (8.15) before rewriting the suites.
6. **The recipes as tests:** build `wip/sw/recipes/` against the generated `mesh.h` and run every recipe on the
   simulator as a suite (v1.1's `RecipeTest` is the model). Fill in the book's "to measure" figures as
   measurements, and measure K's margin (S1) and the table-window case.

RULES
- **The spec is the design.** When it is silent, ambiguous or wrong, or a recipe fails because the spec is wrong,
  don't invent: record it in `WORKER4/QUESTIONS.md` (what, where, your proposed answer) and carry on with your
  proposal, marked in the code. The integrator collects the questions.
- Keep v1.1's suites where their meaning survives, rewritten to v1.2; retire those whose subject is gone (REPORT,
  HALT, the named counters, the sender), noting each.
- This prompt is the go-ahead to change the files in your scope on `v1.2-meshsim` and to write in `WORKER4/`.
  Change nothing else. Run `git pull --rebase` before every push, and keep `WORKER4/NOTES.md` as you go.

DELIVERABLES
- The branch `v1.2-meshsim`, building, with the suites and the recipes passing.
- `WORKER4/REPORT.md`: what was built per phase, each spec section mapped to its code, suites rewritten and
  retired, the measurements, the header's differences from the draft, and what the sort and replayer ports need.
- `WORKER4/QUESTIONS.md`.
