# Integrator handover, v1.35 (end of 2026-11-12)

For the integrator session that picks up v1.35's freeze. Read this first, then `PLAN-V135.md` ("Where it stands"),
`DECISIONS.md` (D37-D48 especially), `V135-INVENTORY.md`, `V135-DOCS-PROPOSAL.md` and `V14-NOTES.md`.

## The roles

- **The owner** (Niall) decides scope and rulings. He relays messages between you and the worker sessions by hand, so
  every message you write for a worker goes in its own code block, labelled with the worker. He prefers short replies:
  no restating what a worker said, no long agreement. Nobody writes code or changes documents without a plan agreed and
  a go-ahead (his standing preference, for workers too).
- **You, the integrator**, review, rule on what's within the decisions, merge workers' branches into `main`, and relay.
  You write prompts for new workers (`sim/planning/sessions/WORKERn/PROMPT.md`) and park messages for busy workers
  (`sessions/WORKERn/NEXT.md`, removed once delivered). Ask the owner when a ruling changes scope.
- **The docs session** (a separate session, with the owner) owns v1.35's documentation (D45): it wrote the restructured
  spec (`sim/spec/`, on `main`) and the docs proposal, and it writes the prompt for, and reviews, a docs-writing session
  on branch `v1.35-docs`. You merge its branches when asked.
- **Workers** (W19-W26 in this phase) each have a branch `sim-wNN`. Earlier workers' reports are indexed in
  `sim/planning/sessions/README.md`.

## The repository and tools

- `github.com/nemmart/meshsim`, branch `main`. The token and the Quest token are in the owner's preferences; pass them
  on the command line only, never write them into a file (and strip them from `.git/config` after cloning).
- Quest (`github.com/nemmart/quest`): runner tasks in `tasks/NNN-name.sh`, results in `results/`, numbers claimed in
  `NUMBERS.md`. The rule: pull and re-read `NUMBERS.md` immediately before claiming a number. The runner box is an
  i5-14400F (6 P-cores, 4 E-cores); the owner restarts it if it stalls.
- The sandbox has one CPU and a 300 s limit per command; background jobs die at the turn's end.

## How you merge

1. `git fetch origin sim-wNN && git merge --no-ff origin/sim-wNN -m "Merge sim-wNN (WORKERNN: what)"`.
2. Conflicts are usually shared files where several workers append: `sim/Makefile`'s include lines and
   `sim/run_tests.sh`'s `SUITES` lines and step lines. Keep every side's lines. In anything harder (a fixture, a
   checker case), abort and send it back to the worker to merge `main` and resolve (regenerate fixtures from `ref`,
   don't hand-merge them).
3. `cd sim && make run CC_NAME=clang MODE=release -j8` (75 binaries; it may need two calls under the time limit), then
   `./run_tests.sh quick --check` (must end "all passed", with no LOG DIFFERS). Workers run the four builds; you run
   these two at every merge.
4. Push, then tell the workers what landed that they were waiting for.
Closing a worker: move `sessions/WORKERn/{NOTES,REPORT}.md` and task folders to `sessions/WORKERn-*.md`, drop its
PROMPT.md, fix paths that cite the old names, add its row to `sessions/README.md`, mark it done in PLAN-V135's table.

## The rules that keep coming up

- **v1.35 is a freeze** (D38-D41): the C++ simulator, off Java, stand-alone, with an equivalent for everything v1.3's
  Java had, ported at v1.3's bar (suites byte-identical to v1.3's logs, regenerated fresh from `ref`; the owner chose
  to port everything at this bar). No improvements; v1.3's checks, not the spec's wider promises (D48); deliberate
  differences approved and recorded (D30; D33, D43, D44, D46, D47 so far).
- **Statistics a suite needs** go into units under `SIM_CHECKS` (suite and debug builds) unless v1.3 keeps them
  everywhere; timing builds unchanged, shown by test_chip and test_sort before and after.
- **Harness-only additions** to the framework and units are fine, off by default, with the five protected tests
  (test_chip, test_sort, test_t2_harness, test_cc_units, test_golden) unchanged.
- **A unit moving level in the check builds** (new same-cycle Wires) is accepted when order-independence holds on the
  16K sort, T2's boot and M1's goldens in both builds under shuffle, step-all and instance-major.
- **No worker names or ticket codes in new comments** (D45); the existing ones are stripped once, by script, at P8.

## Where every session stands

| session | state | what it needs |
|---|---|---|
| W19, W21, W24, W25 | closed (reports in `sessions/`) | W24 may get P8's tooling (message below), on a fresh branch |
| W20 (the harness) | paused, queue empty, REPORT written | **one bug** (below): CondTest's reset |
| W22 (P4) | finishing AgTest (W22-21 is on main); then the freeze check's runner task, whose maker (`tests/freeze/make_task.sh`, 83013b2) is pushed but **not yet merged** | merge sim-w22 when it reports |
| W23 (P5) | the longest tail: RecipeTest's `boot`, CtrlTest's `faults`, `invariants` and all-to-all, then `ctrl-full` and `accounting-full` on the runner. CondTest is ported locally (b30e95df, 152 of 153) waiting for W20's fix | tell it when W20's fix lands |
| W26 (the checker) | paused; its queue is complete (REPORT written) | whatever CtrlTest's `invariants` group needs, from W23 |
| docs session | writing a docs-writing session's prompt; reviews P8's documents, the fresh-reader answers, its own freeze-time rerun of the mechanisms table, and the S1 result | the limits below, and the S1 line below |

## Messages not yet sent (send these first)

**W20**: "CondTest (W23, 152 of 153) finds a difference across the reset. Error 0 is a router overflow (ERRORS bit 0):
tiles 8, 10, 1 and 17 flood tile 9. After the 9th soft reset (cycle 6137) and the reboot, tile 9's ERRORS bit 0 comes
back at cycle 6198, 61 cycles after the reset; at the check (6247) tile 9 has ERRORS = 0x1 and its error line set,
every other tile is clear, and the other eleven resets clear everything. W23's guess: the router overflow (its event,
or the FIFO's overflow record) survives the reset; v1.3's reset empties the router FIFOs and their overflow state. Fix
it against ref with a MachSelf case. Also: W26 changed your two endpoint_checks calls in tests/mach.cpp to pass
cycle() + 1, because ref reports a send made between cycles at the next cycle's check (e55b5d72, matching ref)."

**W23**: "Merged (1c9243f9): cccores on, S1Bench equal. CondTest's reset difference is with W20; push b30e95df when it
passes. Then boot, CtrlTest's last groups, and tell me what invariants needs for W26."

**W26**: "Merged (e55b5d72): the endpoint sends line for line, and the +1 fix in W20's file (told W20). Pause until W23
reports invariants' needs."

**docs session**: the S1 line from S1Bench (W23, v1.35 suite build, matching v1.3's log): "L = 4, K = 12, rcp_bucket:
S1: accept -> complete, the largest: big SRAM 12 (128000 writes; in its area's queue by 11), PTR 7 (64000 updates),
HIST 7 (51200 updates); K = 12: margin 0 before the controller's reaction (+4)"; at every L no write completes late; the
margin is 2 at L = 1 and 0 at L = 2, 3 and 4. And the docs-writing session's limits (already agreed): branch
`v1.35-docs`; may create any new document in `sim/` and a README in any folder lacking one; may rewrite existing
documents where no worker is active (`sim/README.md`, `src/` and `src/units/` READMEs, `data/`, `fw/`, `pipeline/`,
`tools/regs/`, `tools/delivery/`, `spec/`); hands off code, `Makefile`, `run_tests.sh`, `tests/` (except a new README),
`planning/`, `oracle/`; RUNNING.md waits for the final step list.

**W24** (if the owner reopens it for P8's tooling): the drafted message in the last turn of 2026-11-12: on branch
`sim-w24-p8`, (1) `tools/doccheck.sh` per the proposal's section 6, in make check behind a switch; (2) the freeze's copy
script (sim/ to v1.35/ without ref/, oracle/, and the workers' session material; reports and the index stay); (3) the
comment-strip script, proven by byte-identical preprocessed output, dry-run only until the ports finish; (4) the
closed workers' Java-format helpers switched to `tests/java_format.h`.

## The way to the freeze

1. The ports finish: W22's AgTest, W23's list (with W20's reset fix and W26's last messages). Then every runner step is
   live, none PENDING (the docs session's point: a suite counts only when its step is live).
2. P8: the documents (docs-writing session; RUNNING.md last), W24's tooling, then the comment strip once, then the
   shared-helper switch in the remaining files and the image-folder consolidation (the inventory's P8 notes).
3. The freeze check: W22's runner task (every step in all mode with --check; the full-size sort; T2 on both workloads at
   every point; the two 128-segment chains; the kit's calibration; the machine record); the docs session's gate
   (section 6's checks; its rerun of the mechanisms table; CHANGES' "checks differ" section framed as D48's inherited
   gaps).
4. The copy into `v1.35/` (W24's script), a row in the top-level README, and the owner's sign-off.
