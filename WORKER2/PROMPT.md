# Worker 2: review of the v1.2 decisions — the packet path

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
- Background: spec sections 3 (links, the request bus), 4.1-4.10 (big SRAM through the table memories),
  4.12, 4.15, 4.16, 9 (invariants), 10 (parameters) and 11 (budget).

YOUR FOCUS
Decision sections 2 (link word), 3 (counters), 4 (`STREAM`), 5 (address map), 6 (modes), 7 and 12
(headers), 13 (read port), 17 (tables by events) and the big-SRAM list S1-S9; pending Q5, Q8-Q10. Questions
worth working through:
- Every place the v1.1 modes are used (traffic director, write engine, `PTR`/`HIST`, GATHER, loopback,
  bad-packet rules, invariants): does the new mode set plus region plus counter cover each, and how does a
  32-bit address combine with size, alignment and the 24-bit offset (Q10)?
- The delay K: for each destination (big SRAM, port B, peripherals, gather values made after the
  director), what "landed" must mean and whether K = 8 can guarantee it, including 17's guarantees A-C.
- Commands as mode-0 writes to command slot 0: 64- and 128-bit writes covering slot 0, argument blocks,
  and the reply block as a software convention.
- Addresses on endpoints 64-74: what the regions mean on C&C 1-3 and their engines, and the memory
  system's requests and replies with counter fields.
- The 256-bit word's cost claims (FIFOs, loopback, padding stages, wires) against section 11.

TWO PARALLEL SESSIONS review the rest: WORKER1 (errors, interrupts and recovery) and WORKER3 (registers, peripherals and the software impact). Stay in your area; when you find something in theirs,
note it in one line under "For other workers" and move on.

WHAT TO LOOK FOR
- **Contradictions:** between two decisions, or between a decision and spec text it doesn't mention.
- **Races and windows:** orderings in which the hardware or a program sees an inconsistent state.
- **Broken invariants:** each of the spec's invariants (section 9) and every "(H)" claim the decisions touch.
- **Missed consequences:** spec sections, registers, rules or parameters a decision changes that the log
  doesn't list.
- **Gaps:** behaviour the decisions leave undefined that hardware or software must define.
Report what you find with evidence; don't redesign. Where a fix is obvious, suggest it in a sentence.

DELIVERABLES (in `WORKER2/`, and nowhere else)
- `WORKER2/REVIEW.md`:
  1. A summary: the five findings that matter most, one line each.
  2. Findings, numbered W2-01, W2-02, ..., each with: severity (**blocker**: the design doesn't work
     as decided; **issue**: needs a decision or a fix; **question**: needs the owner; **nit**: wording or
     naming), the decision sections and spec sections involved, what is wrong, and a suggested resolution.
  3. "Spec sections to update": every spec section the decisions in your area change, with one line on what.
  4. "Questions for the owner": those that can't be settled from the documents.
  5. "For other workers": one line each.
- `WORKER2/NOTES.md`: your running notes, so the work survives a context compaction.

RULES
- This prompt is the go-ahead to write and push `WORKER2/REVIEW.md` and `WORKER2/NOTES.md`. Change
  nothing else: not the decisions log, the spec, the code or another worker's folder.
- Commit to `main`; run `git pull --rebase` before every push. Push your notes as you go and the review
  when it is done.
- Cite sections and line numbers. Mark anything you infer rather than read as *(inferred)*.
- Keep the review readable: prose findings, tables where they help, no padding.
