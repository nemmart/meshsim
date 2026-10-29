# Worker 3: review of the v1.2 decisions — registers, peripherals and the software impact

You are reviewing a set of design decisions for the mesh, part of an ASIC for proving the Jolt zkVM. The
decisions were made in one long session with the owner and recorded as they went. Your job is to find what
is wrong, inconsistent or missing **before** anyone writes them into the spec or the code. This session
writes a review only.

REPOSITORY
- Clone https://github.com/nemmart/meshsim with the meshsim token from your user preferences (use it as
  `https://x-access-token:<token>@github.com/nemmart/meshsim.git`). Never write the token into a file.
- `wip/` is the work in progress; `v1.0/` and `v1.1/` are frozen milestones (see the top-level README).
  `wip/` matches v1.1 except for its `planning/` folder.

INPUTS
- `wip/planning/DECISIONS-v1.2.md`: the decisions. Read "Where the design stands" first, then every
  section; later sections revise earlier ones, and superseded points are marked. Its pending questions
  (Q1-Q15) and the big-SRAM list (S1-S9) are known open items: don't report them as findings, but do
  report anything they get wrong or any consequence they miss.
- `wip/spec/machine-spec.md`: the v1.1 specification, the authority on everything the decisions don't
  change (2,494 lines; its section 13 lists 116 open items).
- `wip/planning/PLANNING-NOTES-2026-10-27.md`: where v1.1 left off.
- Also read `wip/sw/REGISTERS.md`, `wip/sw/mesh.h`, the sort's code under `wip/sort/`, and the simulator
  under `wip/meshsim/` as far as needed to judge what changes.

YOUR FOCUS
Decision sections 7-14, 16 and 18 (the controller's register block, the peripheral window, names,
events, `PARAMS`/`STATUS`/`ERRORS`), 9 (peripherals) and 10's standard window; pending Q11-Q12. Spec
section 8, 4.11, 4.17 and 4.19. Questions worth working through:
- The register map as a whole: no overlaps; every v1.1 register accounted for (kept, moved, renamed or
  gone); the table window's new place; region 0xFF's layout.
- The controller's map moving (program memory to 0x01000000, data to 0x00000000): reset PC, the resident
  loader, linker scripts, overlays, the boot ROM.
- The replayer as peripheral 1 under the standard window: where each of its v1.1 registers, service lines
  and conditions goes. The UART as a peripheral on the C&C tile.
- **A migration inventory:** every file in `wip/sw/`, `wip/sort/` and `wip/meshsim/` that must change, why,
  and a rough size (small / medium / large). Flag code that relies on removed behaviour: combined
  `TILE_PULSE` stores, REPORT, modes 6-9, the named `DMA_*_REMAINING` counters, `wait_tables()`, HALT
  suppressing counts, the per-tile UART console.

TWO PARALLEL SESSIONS review the rest: WORKER1 (errors, interrupts and recovery) and WORKER2 (the packet path). Stay in your area; when you find something in theirs,
note it in one line under "For other workers" and move on.

WHAT TO LOOK FOR
- **Contradictions:** between two decisions, or between a decision and spec text it doesn't mention.
- **Races and windows:** orderings in which the hardware or a program sees an inconsistent state.
- **Broken invariants:** each of the spec's invariants (section 9) and every "(H)" claim the decisions touch.
- **Missed consequences:** spec sections, registers, rules or parameters a decision changes that the log
  doesn't list.
- **Gaps:** behaviour the decisions leave undefined that hardware or software must define.
Report what you find with evidence; don't redesign. Where a fix is obvious, suggest it in a sentence.

DELIVERABLES (in `WORKER3/`, and nowhere else)
- `WORKER3/REVIEW.md`:
  1. A summary: the five findings that matter most, one line each.
  2. Findings, numbered W3-01, W3-02, ..., each with: severity (**blocker**: the design doesn't work
     as decided; **issue**: needs a decision or a fix; **question**: needs the owner; **nit**: wording or
     naming), the decision sections and spec sections involved, what is wrong, and a suggested resolution.
  3. "Spec sections to update": every spec section the decisions in your area change, with one line on what.
  4. "Questions for the owner": those that can't be settled from the documents.
  5. "For other workers": one line each.
- `WORKER3/NOTES.md`: your running notes, so the work survives a context compaction.

RULES
- This prompt is the go-ahead to write and push `WORKER3/REVIEW.md` and `WORKER3/NOTES.md`. Change
  nothing else: not the decisions log, the spec, the code or another worker's folder.
- Commit to `main`; run `git pull --rebase` before every push. Push your notes as you go and the review
  when it is done.
- Cite sections and line numbers. Mark anything you infer rather than read as *(inferred)*.
- Keep the review readable: prose findings, tables where they help, no padding.
