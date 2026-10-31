# Decisions toward v1.25

**v1.25 is an intermediate milestone between v1.2 and v1.3** (owner, 2026-10-31): **a working clone of current
jolt-cpp.** It is v1.2 with the replayer and the Tracer's tables brought up to jolt-cpp's current expansions, so v1.3
(the Tracer) starts from a machine that matches today's Jolt. Same style as the other logs: one entry per decision,
later entries may revise earlier ones. The analysis behind it is the v1.3 log's section 9.

---

## 1. Scope (2026-10-31)

Decided (O). Starts once v1.2 is frozen; ends with v1.25 frozen as `v1.25/`.

- **Pin a current jolt-cpp `main` commit** (not a moving branch), and diff its expansions against v1.2's reference
  (jolt-asic-planning `4f36b3d`) in full.
- **The replayer** (spec 4.19, meshsim's model): rebuild the opcode table, the ROM templates and the decode table,
  re-checking the template count and the ROM fill; **add `shift_offset` to the expansion word**; add the new opcodes
  (`VirtualLoadB/H/W`, `VirtualStoreB/H/W`) and the changed ones (`ShiftData*`'s operands, the narrow memory rows'
  immediate, `VirtualHostIO`, the AMO\*.W bodies, the renumbering; v1.3 section 9 as corrected by WORKER9); **PEXT correct for any
  mask** (v1.3 section 9: the spec requires it, the implementation chooses how, full speed promised only for
  contiguous masks; replacing v1.1's narrowing and invariant 16); drop `VirtualAlignAddr` and the
  alignment assertions; regenerate the replayer's test vectors.
- **The Tracer's tables: rebuilt too** (owner): its ROM and decode table (v1.3 section 7) and its advice templates (v1.3
  section 8, `wip/tracer/`), against the same pinned commit, though nothing executes them until the Tracer exists.
- **The spec** (4.19, the expansion word, the opcodes, the parameters) and **CHANGES** for v1.25.

## 2. Acceptance (2026-10-31)

Decided (O). **v1.25 matches the jolt-cpp build on our two 2²⁹-row runs:** **EVM Zone** (trace-29) and **Cyclone**
(input/29), the two workloads v1.1's replayer was measured on (planning notes, item 113's guests).

- **Every window of both**, replayed on meshsim's replayer from run logs made at the pinned jolt-cpp commit, compared
  **row for row** (by row hash) against that jolt-cpp build's trace. This is v1.1's planning to-do 7 ("full-program
  replay on the runner") and spec item 58's validation harness, at the new reference. Run on the Quest runner.
- The Tracer's rebuilt tables are checked the same way as far as possible without a Tracer: WORKER7's harness style,
  against the pinned jolt-cpp's own expansion and advice code.

## 3. Deliveries (2026-10-31)

Decided (O).

- **A delivery is the Tracer's output, made in software** until the hardware Tracer exists: per program the tables; per
  window the state record, the three streams (raw, compressed, the Tracer's pick) and jolt-cpp's reference rows
  (meshsim's `REPLAY-INPUT-REQUIREMENTS.md`). v1.1's deliveries 1-3 (240 windows, 15.7 M rows, all matched) were made by
  a v1.1-era session's software stand-in; **neither the tool nor the files were kept.**
- **v1.2 freezes without real-window runs:** v1.1's replayer matched the 240 windows, v1.2 changed its interface rather
  than its row logic, and WORKER4's StageB synthetic run checks the new interface against Stage A on 18 windows covering
  every compression and store-log combination, a controller-fed expansion and a mid-sequence start.
- **v1.25 rebuilds the delivery writer** (WORKER9's part 3, in jolt-asic-planning PR #2, next to the tracer and the table
  builder): a functional model of the Tracer's output, which v1.25's acceptance runs need and which gives v1.3 a head
  start. Deliveries are not committed to any repository.

---

## Work list

| # | task | from | status |
|---|---|---|---|
| W1 | **Bring the replayer and the Tracer's tables up to date with jolt-cpp's latest** (1), and pass the acceptance runs (2) | v1.3 log, section 9 and its former W1 | agreed; starts after v1.2 is frozen |
