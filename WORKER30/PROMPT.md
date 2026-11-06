# Worker 30: v1.3's spec, brought up to date for the RTL (the fold-in)

v1.3 is the Tracer, made solid enough for Tony (RTL) to start building it. Its chapter, spec 4.21 in wip/spec/machine-spec.md,
was written before the Tracer was modelled; since then the model (B1) has answered 17 spec questions and the owner has made a day
of rulings. Your job: bring 4.21 and everything around it up to date, so Tony can build from the text alone.

THE STANCE (owner, top of DECISIONS-v1.3): the spec makes reasonable default calls and states what the hardware must do
observably, not how; where the RTL finds a better way, the spec follows it. Don't over-specify.

READ FIRST: wip/planning/DECISIONS-v1.3.md (sections 1-25; the 11-05 and 11-06 entries are the new ones), spec 4.21, 4.17, 4.19
and section 13; wip/planning/sessions/WORKER22-SPEC-QUESTIONS.md (Q1-Q17, all answered), WORKER22-REPORT.md (its section 7 lists
what the spec should take up) and WORKER21-REPORT.md (who wrote 4.21 and how); wip/spec/CHANGES-v1.3.md;
v1.26.1/spec/CHANGES-v1.25-to-v1.26.1.md (the RTL guide's model).

REPOSITORY: https://github.com/nemmart/meshsim (clone with the meshsim token from your user preferences, as
`https://x-access-token:<token>@github.com/...`; never write a token into a file). Work on a branch, `v1.3-spec-final`, from
`main`; the integrator merges. Notes and report in `WORKER30/` on `main`.

THE WORK
1. Fold Q1-Q17 into 4.21 and wherever else they belong, among them: the segment's barriers (C&C 1's READY/GO, the arming after
   GO, stage_delay, unexpected arrival as the detector), the per-window handshake, the final GO; queue 0's transfer order; the
   fill statuses; the dropped expansion on a record load (Q14); the write-back and allocation rules the model uses, where they're
   the Tracer's to state.
2. Check that every decision dated 2026-11-05 and 11-06 in DECISIONS-v1.3 that concerns the Tracer is in the spec: items 142,
   148, 149 and 157, stage_delay, word 66's I/O-region field, the end at any JAL with immediate 0, the misalignment rule, the
   range comparator, the stores-into-code rule, D13 and D14, frame 7, the counters at words 44-51.
3. CHANGES-v1.3.md complete, every row citing its decision.
4. The RTL guide from v1.26.1 to v1.3, wip/spec/CHANGES-v1.26.1-to-v1.3.md: what Tony must build or change, unit by unit (the
   Tracer; the DMA in 128-bit words; item 118's router port, src 74 through the down port; the replayer unchanged), each item
   marked whether it affects the RTL.
5. The statistics counters (words 44-51) in meshsim's Tracer model, with a test, and mesh.h and the register reference
   regenerated.

RULES
- Propose a plan first and wait for the owner's go-ahead.
- Spec and docs, plus the counters' small model change; nothing else in meshsim. WORKER28 is merging the simulator's stage 1 and
  the latency change (GO, READY and the error line at 8) at the same time: coordinate on the spec's latency lines, which it
  changes.
- Where the decisions leave a gap or conflict, propose (marked Proposed, gathered in section 13) and ask.
- Commit and push before anything long in a turn, and keep the last tool calls for committing; end each turn with a clean git
  status.

DELIVERABLES: the updated spec, CHANGES-v1.3.md, the RTL guide, the counters in the model, WORKER30/REPORT.md.
