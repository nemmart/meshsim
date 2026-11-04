# Upstream check (brief)

**Status.** This is not a blocker for the simulator. No further investigation for now (owner, 2026-10-27).

**Spec:** item 113 in mesh-sorter-v1.1-2026-10-26n: 4.19's "Ground truth" paragraph and section 13, item 113. It pins the version, and the rules under review carry the tag *(pending the upstream check, item 113)*. The guest-program comparison (item 113, point 4) stays Open.

## Setup

**The authority for this milestone is jolt-cpp,** as the spec and the appendix describe it: quarky93/jolt-asic-planning `tracer-standalone` at 4f36b3d, `tracer/src/tracer/`. Its reference rows are the comparison target.

**jolt-cpp's Rust baseline** is LayerZero-Research/jolt at 735355e43026e9ef392b36b4ab5ed0f5e680127f (jolt-asic-planning `docs/provenance.md`).

**a16z/jolt HEAD** (bd9aef4, 2026-10-27) is only a view of where upstream is heading. Its differences are future-compatibility notes, not errors.

**Method:** source reading only; nothing was built or run.

**Out of scope or already explained (owner):**
- `VirtualPow2I` and `VirtualPow2IW` are defined upstream but emitted by nothing; jolt-cpp's `riscv.inc` omits them on purpose.
- jolt-cpp's DIV/REM advice opcodes are internal to its interpreter. Both record advice rows as plain `VirtualAdvice`.
- HEAD's AdviceLB, LH, LW and LD, which use the runtime advice tape and expand to `VirtualAdviceLoad`, are a new guest feature: a superset of what we replay.

## Per-case findings

| # | case | finding |
|---|---|---|
| 1 | program end | **Agrees.** Guests end with `j .` (`jolt-platform/src/exit.rs`).<br>Upstream stops the trace at the self-loop; jolt-cpp sends pc to 0.<br>Both give the same final row, `JAL v40, 0` (rd = x0 remapped), followed by all-zero padding. |
| 2 | new pc 0 from JALR or a branch (N6) | **Not yet determined upstream** (item 113).<br>The model follows the spec, as jolt-cpp does: the program ends (4.19.7's pc table, tagged pending in 26n).<br>*Note from source reading at 735355e, unexecuted:* the next step fetches at address 0. The jolt device's zero padding returns 0, the reserved compressed halfword 0 uncompresses to `0xffffffff`, and decoding that panics. That would mean the tracer aborts, so no valid trace contains the case. |
| 3 | self-jump rule | Candidate difference at HEAD (below). |
| 4 | LD to x0 memory fields | Follows from case 5's LR/SC row (below). |
| 5 | rd = x0 remap | JAL, JALR and VirtualHostIO to v40: **agrees** (upstream's first temporary is also 40).<br>ECALL, MRET, EBREAK, CSRRW and CSRRS are exempt in both.<br>LR/SC: candidate difference at HEAD (below). |
| 6 | advice rows | **Agrees.** DIV/REM expand to the same rows. Both record `VirtualAdvice` with only rd; rs1, rs2 and imm are 0. |
| 7 | EBREAK | **Agrees:** one row, `JAL v40, 0`, virtual and first; it ends the trace. |
| 8 | padding | **Agrees:** all-zero rows. |
| 9 | representation | `JoltTraceRow` (64 B) and `RiscCycle` (80 B) differ only in representation:<br>optional operands vs 0 plus an is-immediate bit; a normalised i128 imm vs the raw `_imm`; derivable `IsLastInSequence` and `IsNoop`.<br>The F5 word-form extensions I spot-checked agree. |

## Candidate row-content differences at HEAD

Each of these is **unconfirmed; to recheck at 735355e later.** A first source reading of 735355e is noted beside each, also unconfirmed, since nothing was executed.

1. **The self-jump rule** (4.19.7, item 57).
   - At HEAD, a `jalr rd, imm(rd)` that targets its own address runs again at the same pc (test `jalr_overwriting_its_base_runs_again`), where jolt-cpp's rule advances to pc + 4.
   - *735355e reading:* its tracer stops after any self-jump, so the rule is never observed; this would agree.
2. **LR/SC with rd = x0.**
   - HEAD's exempt list is only ECALL, MRET, EBREAK, CSRRW and CSRRS (`crates/jolt-program/src/expand/operands.rs`), so LR/SC (and the inlines' rd field) are remapped to v40. jolt-cpp exempts them, following a16z #1439, and emits `LD x0`, `VirtualPextSigned x0` and `XORI x0`.
   - *735355e reading:* the same as HEAD, so this would be a real difference from jolt-cpp.
3. **The shared NOP row's virtual flag.**
   - At HEAD the rd = x0 NOP row is never virtual or first (`noop_for`). jolt-cpp makes it virtual and first unless the source is ADDI (`isa/virtual.cpp:183`; our `rd0` = 1).
   - *735355e reading:* the same as HEAD.

The model and spec follow jolt-cpp on all three (owner, 2026-10-27).
