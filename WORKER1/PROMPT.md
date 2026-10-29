# Worker 1: review of the v1.2 decisions — errors, interrupts and recovery

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
- Background on the request bus, C&C and boot: spec sections 2, 4.18, 6 (C&C, boot, GO, the memory
  contract) and 6.6-6.7.

YOUR FOCUS
Decision sections 1 (condition handling), 15 (C&C 1's error and run lines), 14 (errors as a concept),
16 (events, as they meet `WFI` and the `done` gate) and 18's `ERRORS`; pending Q1-Q4. Spec 4.11-4.13,
4.17.7, 4.19.11, 6.1, 6.3, 6.6, 6.7 and the global nets in section 2. Questions worth working through:
- What a peripheral, the address generator and the send port do while the handler runs and after it parks
  (Q3), and whether anything can keep sending onto the mesh from a failed tile.
- Errors raised while a tile is halted (loading a halted tile now counts, section 3): do they raise its
  error line, and C&C 1's interrupt, during boot?
- Many tiles failing at once: about 17 request-bus reports each, the request FIFO and C&C 1's receive
  queue, and C&C 1 halting units whose reports are still in flight.
- C&C 1 taking an interrupt inside its own handler; `mie`/`mstatus` use with no privilege modes.
- `RUN_LINES_0`-`2` at chip reset and at boot; what halts C&C 1's own peripheral; what HALT now clears;
  the error lines' fixed latency against the HALT edge.
- A tile parked in its handler and C&C 1's READY/GO flow: who notices, through the watchdog or the lines.

TWO PARALLEL SESSIONS review the rest: WORKER2 (the packet path) and WORKER3 (registers, peripherals and the software impact). Stay in your area; when you find something in theirs,
note it in one line under "For other workers" and move on.

WHAT TO LOOK FOR
- **Contradictions:** between two decisions, or between a decision and spec text it doesn't mention.
- **Races and windows:** orderings in which the hardware or a program sees an inconsistent state.
- **Broken invariants:** each of the spec's invariants (section 9) and every "(H)" claim the decisions touch.
- **Missed consequences:** spec sections, registers, rules or parameters a decision changes that the log
  doesn't list.
- **Gaps:** behaviour the decisions leave undefined that hardware or software must define.
Report what you find with evidence; don't redesign. Where a fix is obvious, suggest it in a sentence.

DELIVERABLES (in `WORKER1/`, and nowhere else)
- `WORKER1/REVIEW.md`:
  1. A summary: the five findings that matter most, one line each.
  2. Findings, numbered W1-01, W1-02, ..., each with: severity (**blocker**: the design doesn't work
     as decided; **issue**: needs a decision or a fix; **question**: needs the owner; **nit**: wording or
     naming), the decision sections and spec sections involved, what is wrong, and a suggested resolution.
  3. "Spec sections to update": every spec section the decisions in your area change, with one line on what.
  4. "Questions for the owner": those that can't be settled from the documents.
  5. "For other workers": one line each.
- `WORKER1/NOTES.md`: your running notes, so the work survives a context compaction.

RULES
- This prompt is the go-ahead to write and push `WORKER1/REVIEW.md` and `WORKER1/NOTES.md`. Change
  nothing else: not the decisions log, the spec, the code or another worker's folder.
- Commit to `main`; run `git pull --rebase` before every push. Push your notes as you go and the review
  when it is done.
- Cite sections and line numbers. Mark anything you infer rather than read as *(inferred)*.
- Keep the review readable: prose findings, tables where they help, no padding.
