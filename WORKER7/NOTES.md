# Worker 7 notes

Running log; newest last. The report (`REPORT.md`) is the deliverable; this file is the trail.

## 2026-10-30

**Sources.**
- meshsim at `b70ebc1` (prompt with jolt-asic-planning's URL).
- **Reference tracer: jolt-asic-planning `tracer-standalone` at `4f36b3d`** (`tracer/src/tracer/`), the copy the
  spec and the prompt cite; its line numbers match the prompt's (advice cases `trace_owner.cpp:545–630`,
  `VirtualAdvice` logging `:780–787` and `:847–854`). All file:line references below are to this copy.
- jolt-cpp `main` at `52fbbab3b` read as a cross-check. **It has moved on from 4f36b3d**: new opcodes
  `VirtualLoadB/H/W`, `VirtualStoreB/H/W`; `VirtualAlignAddr` and the two alignment assertions are gone; the
  sub-word load and store expansions are rewritten; secp256k1's modular inverse is now libsecp256k1's
  `modinv_var` (`inlines/secp256k1_inverse.c`) instead of Fermat. **The advice cases themselves are identical**
  (`trace_owner.cpp` main 527–621 vs 4f36b3d 536–630, byte for byte), and the inverse is unique, so the advice
  *values* are unchanged. Recorded as Q1 (which commit the Tracer tracks).
- Access: both repositories cloned with the meshsim token (unauthenticated API calls got 403). Remote URLs reset
  to token-free form after cloning; no token is written to any file.

**The template builder.** Not found: searched jolt-asic-planning (`main` and `tracer-standalone`, all 55
commits), jolt-cpp `main`, and meshsim for its fields (`seq_remaining`, `from_log`, `imm0_pc0`, `advice_reg`,
`untraced`). The only related code is `wip/tools/replayer-opcodes/` (the appendix's enumeration and vector
harness), which compiles against the tracer but builds no tables. So the Tracer's templates are written as data
plus a small generator (`wip/tracer/`), as the prompt allows. Q2.

**Advice sources found** (detail in REPORT step 1):
- six DIV/REM advice opcodes (jolt-cpp's own; `riscv.inc:190` says "not part of jolt");
- `VirtualAdvice` in seven secp256k1 inlines only (no other inline takes advice), values from
  `inlines/secp256k1.cpp` `evaluate`, prepared at the start of the inline from the operands in memory
  (`trace_owner.cpp:52–66`, `isa/virtual.cpp:1238–1244`);
- not row advice, recorded for completeness: the trusted/untrusted advice memory regions (`loader.hpp:25–30`, read
  by ordinary LDs) and the `JOLT_ADVICE_WRITE` syscall, which jolt-cpp ignores (`syscalls.cpp:63–69`).

**Division family** (templates in `wip/tracer/templates.py`).
- Untraced rows go immediately before the advice row. DIVW's trick: Jolt's DIVW advice equals a 64-bit DIV of the
  sign-extended operands (overflow gives +2³¹, zero-extended), and the first two traced rows already hold them.
- Harness compiles against 4f36b3d (`#include`s `virtual.cpp` and `trace_owner.cpp`, as the appendix harness does;
  `expand_virt_seq` is file-local). Assertion failures caught with SIGABRT + siglongjmp.
- First mutant run looked like four mutants escaped C1: the harness returned on an assertion failure before
  comparing advice. Fixed (C1 first). Then a mutant-file name clash (two REM mutants, one file) and a Python
  slice-copy bug in two mutants; fixed. The clash briefly made the REM mutant look like an unpinned case; the
  unpinned cases (F3) were re-established with correctly named mutants.
- Full run: 8,040,000 cases, 0 failures, 84 s. 11/11 mutants caught.
- Upstream (735355e) read via the API (`api.github.com/repositories/978427161`, the repo is now
  LayerZero-Labs/jolt): its `trace` advice values match jolt-cpp's in all cases; it also has the advice tape (F2).

**Inlines** (`wip/tracer/inline_rows.py`).
- Multiply/square advice generated as straight-line rows, run in Python, compared to jolt-cpp `evaluate()` compiled
  (`harness/secp_advice.cpp`): 4 × 20,000 cases, 0 mismatches; a mutant without the final compare gives ~25 %.
- Bounds for the rounds: q (c < 2³³): after round 1 H < 2³⁴, after round 2 V < 2²⁵⁶ + 2⁶⁷ < 2m. r (c < 2¹²⁹):
  H < 2¹³⁰, then < 2⁴, then V < 2²⁵⁶ + 2¹³³ < 2m.
- GLV and the inverse sized from the blocks, not built.
