# Worker 7: the Tracer's advice expansions

You are working on the Tracer, part of an ASIC for proving the Jolt zkVM. The Tracer is an RV64 "row machine"
that executes the guest program, expanding each instruction into Jolt's trace rows, and writes the run log that
the tiles' replayers use to regenerate the trace (spec 4.19). Where Jolt's expansion consumes **advice** (values
the prover is handed rather than computing), the replayer pops the advice from the run log, and **the Tracer must
compute it**. It does so with **untraced rows**: rows it executes with tracing off, in its second register bank,
which the replayer never sees. Your job is to work out every advice the Tracer must produce, write the untraced
expansions for the division family, and lay out the inlines' advice so the owner can decide how to handle them.

REPOSITORY
- Clone https://github.com/nemmart/meshsim with the meshsim token from your user preferences (as
  `https://x-access-token:<token>@github.com/nemmart/meshsim.git`). Never write a token into a file.
- Commit to `main`; run `git pull --rebase` before every push. Push as you go, at least every turn.
- Public repositories you will read: **jolt-cpp**, https://github.com/snarkify/jolt-cpp, our C++ Jolt tracer and
  **your reference**; and **Rust Jolt**, https://github.com/a16z/jolt, for reading only where jolt-cpp is unclear.
  Comparing the two tracers' traces is a separate study (ST1 in the v1.3 log); don't take it on.

INPUTS
- `wip/planning/DECISIONS-v1.3.md`, all of it, and especially section 7 (the Tracer's expansion tables: same format
  and builder as the replayer's, different contents; untraced work in **bank 1, registers 146-255**, code *c* naming
  register *c*; the traced row reads the advice through `advice_reg`) and section 8 (**the Tracer's row ALU has
  RV64 M's DIV, DIVU, REM, REMU and the W forms as base operations, for untraced rows only**).
- `wip/spec/machine-spec.md` v1.2, section 4.19: the expansion word and operand codes (4.19.3), templates and the
  ROM (4.19.4-4.19.6), the opcodes and row classes (4.19.7: class L, the six DIV/REM advice opcodes and
  `VirtualAdvice`), and the requirement on the tracer (4.19.2). `wip/spec/replayer-opcodes.md`: the opcode table.
- jolt-cpp: `trace_owner.cpp` (it computes the DIV/REM advice in its interpreter, around lines 545-630, and logs
  `VirtualAdvice` around 822-855), `isa/virtual.cpp` (the expansions), and the inline instructions.
- **The template builder:** the replayer's templates are built by a table builder that isn't in this repository.
  Find it: in jolt-cpp, or in **https://github.com/quarky93/jolt-asic-planning**, PR #1, the `tracer-standalone`
  branch (the spec cites commit `4f36b3d` as its reference copy of the tracer). If that repository needs access you
  don't have, say so in QUESTIONS.md. If you can't find the builder, say so, and write your templates as data plus a
  small generator instead.

WHAT TO DO
1. **Inventory every advice source** in Jolt's expansions: the DIV/REM family, every use of `VirtualAdvice`
   (which inline instructions, and what each advice value is), and anything else that takes advice. For each,
   give Jolt's exact definition of the value, with the file and line.
2. **The division family.** For each of the six DIV/REM advice opcodes:
   - compare Jolt's advice value with RV64 M's result **in every edge case**: division by zero (RISC-V: quotient
     all ones, remainder the dividend), signed overflow (−2⁶³ ÷ −1), and the W forms' 32-bit sign extension;
   - write the Tracer's template: the untraced row(s) with the native operation into a bank-1 register, then the
     traced rows exactly as in the replayer's template, reading the advice through `advice_reg`; add fix-up rows
     wherever Jolt differs from RV64 M;
   - check it: a small model of the untraced rows against jolt-cpp's advice, over the edge cases and random
     operands.
3. **The inlines' advice** (the open question T5): the inlines are among the templates C&C 3 feeds to the Tracer
   (section 7). For each inline that takes advice, describe what computing it needs (for example, a modular
   inverse), its size as untraced rows with the Tracer's ALU, and whether C&C 3's software could compute it instead
   when it supplies the expansion words. Recommend one approach per inline; don't decide.
4. **A validation plan** for the Tracer's advice against jolt-cpp on real programs.

RULES
- **Don't design hardware.** If the expansions need something the Tracer's ALU lacks, record it as a question.
- Questions go in `WORKER7/QUESTIONS.md` (what, where, your proposed answer); carry on with your proposal.
- This prompt is the go-ahead to write in `WORKER7/` and to create `wip/tracer/` for templates, models and scripts.
  Change nothing else. Keep `WORKER7/NOTES.md` as you go.

DELIVERABLES
- `WORKER7/REPORT.md`: the advice inventory (step 1), the division family's findings and templates (step 2), the
  inlines' layout and recommendations (step 3), and the validation plan (step 4).
- `wip/tracer/`: the Tracer's division-family templates as data, the checking model and its results, and any
  generator scripts, with a README.
- `WORKER7/QUESTIONS.md`.
