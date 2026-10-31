# Worker 13: the change guide from v1.1 to v1.25

You are writing a guide for the RTL engineer of the mesh, part of an ASIC for proving the Jolt zkVM. His RTL implements
**v1.1** of the machine, and he will upgrade it **straight to v1.25**, skipping v1.2. The project has two change documents,
`CHANGES-v1.2.md` (v1.1 to v1.2, a large redesign) and `CHANGES-v1.25.md` (v1.2 to v1.25, the replayer brought up to date with
current Jolt), but he'll never build v1.2, so he needs **one guide from v1.1 to v1.25, organized by hardware unit**: for each
part of the machine, everything that changed since v1.1, with the earlier step's changes already overridden by the later ones.

REPOSITORY
- Clone https://github.com/nemmart/meshsim with the meshsim token from your user preferences (as
  `https://x-access-token:<token>@github.com/nemmart/meshsim.git`). Never write a token into a file.
- Commit to `main`; `git pull --rebase` before every push. **Don't touch `v1.0/`, `v1.1/` or `v1.2/`** (frozen).

INPUTS
- **`v1.1/spec/machine-spec.md`**: what his RTL implements.
- **`wip/spec/machine-spec.md`** (v1.25) and `wip/spec/replayer-opcodes.md`: the target. **The spec is the authority**; the change
  documents are maps to it.
- `wip/spec/CHANGES-v1.2.md` and `wip/spec/CHANGES-v1.25.md`: the two steps, section by section, each citing its decision.
- `wip/planning/DECISIONS-v1.2.md` and `DECISIONS-v1.25.md`: why, when the change documents don't say.
- `v1.2/README.md` and the top-level `README.md` for orientation.

WHAT TO WRITE: `wip/spec/CHANGES-v1.1-to-v1.25.md`
1. **A one-page overview first:** what the machine looked like at v1.1, what it looks like at v1.25, in a dozen bullets, and a short
   list of what was **removed outright** (HALT and the RUN/HALT nets, REPORT, the four named counters, the sender, the resident
   loader, spanning accesses, v1.1's modes and their bad-packet rules, `ts_busy`, `mem_idle`/`cm_idle`, per-tile UART, and so on)
   and what is **entirely new** (the boot ROM, terminal traps, the toast pin, the standard peripheral window, count-only packets,
   and so on), so he can plan the work.
2. **Then one section per hardware unit**, in the order of the v1.25 spec (the link word and network; the address map; each tile
   unit; the controller and its registers; peripherals; the replayer; control, reset and boot; C&C 1-3). For each change:
   - **v1.1** said / built; **v1.25** says (with the v1.25 spec section);
   - **kind:** new, changed, removed, or renamed;
   - **why**, in a sentence, citing the decision (DECISIONS-v1.2 or v1.25 section number);
   - where a v1.2 change was itself revised in v1.25, **give only the final state**, mentioning the v1.2 intermediate only if it
     helps.
3. **Register-level tables:** every register, field and bit that moved, appeared or went, v1.1 to v1.25 (the controller block,
   `EVENTS`, `ERRORS`, the peripheral windows, C&C 1's registers, the replayer's words, the link word's layout, the expansion
   word's fields), so he can diff his RTL against them.
4. **Parameters and invariants:** what changed in the spec's sections 9 and 10, and what the simulator now checks that v1.1's did
   not (and what the hardware itself must check).

HOW
- **Check every entry against both specs**, not just the change documents. Where the documents and the specs disagree, the spec
  wins; record the disagreement.
- **Look for what the documents missed:** walk both specs section by section and note any difference that neither change document
  lists. Those are the most valuable findings.
- Questions (the spec silent, ambiguous or self-contradictory on something the RTL needs) go in `WORKER13/QUESTIONS.md` with your
  proposed answer; carry on.

RULES
- **Write, don't design.** This prompt is the go-ahead to create `wip/spec/CHANGES-v1.1-to-v1.25.md` and to write in `WORKER13/`.
  Change nothing else, the spec included: report spec errors in QUESTIONS.md for the integrator.
- The v1.25 spec may still receive small fixes from the replayer's model check (WORKER11); pull before finishing, and note at the
  top which spec commit the guide matches.
- **Keep the last few tool calls of every turn for committing and pushing**, and end each turn with a clean `git status`. Keep
  `WORKER13/NOTES.md` current.

DELIVERABLES
- `wip/spec/CHANGES-v1.1-to-v1.25.md`.
- `WORKER13/QUESTIONS.md` (with the differences neither change document listed) and `WORKER13/REPORT.md`.
