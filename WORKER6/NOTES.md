# WORKER6 notes

Working notes for the v1.2 recipes. The deliverables are `wip/sw/RECIPES.md`, `wip/sw/CONVENTIONS.md`,
`wip/sw/mesh-v1.2-draft.h`, `WORKER6/FINDINGS.md` and `WORKER6/REPORT.md`.

## Inputs read (2026-10-29)

- `wip/planning/DECISIONS-v1.2.md`, all 36 sections.
- `wip/sw/RECIPES.md` and `CONVENTIONS.md` (v1.1), `wip/sw/mesh.h` (1.1), `meshsim/sw/tests/rcp.h`.
- `wip/spec/machine-spec.md` (still v1.1 at the start: 4.12, 4.18, 4.19.8-4.19.11, 6.2-6.6).
- `wip/planning/reviews/`: WORKER3-REVIEW-2 (sections 4-7) and WORKER1-REVIEW-3 in full.

## Plan (approved by the owner)

1. Read the inputs.
2. Draft header `wip/sw/mesh-v1.2-draft.h`.
3. Port recipes 0-21; retire REPORT (20) and the HALT material.
4. Add the new recipes: boot ROM and boot, error handler, tables by events, a peripheral window, the
   replayer's credits, C&C 1's side of a phase.
5. Rewrite 15 (rules) and 16 (gaps); update CONVENTIONS.md.
6. FINDINGS.md and REPORT.md.

Owner's notes:
- Commit straight to `main`.
- Recipe 14: any cycle figure that can't be justified for v1.2 is marked "to measure".
- If `machine-spec.md` changes under me (WORKER5), the decisions log stays my authority.

## Choices made while writing (not design; recorded so the spec session can check them)

- **Recipe numbering:** v1.1's 0-21 keep their numbers. 20 (REPORT) becomes a retirement note, and the new
  recipes are 22-27. This keeps the diff against v1.1 readable.
- **Counter convention:** v1.1's four counters map 1:1 (W3's migration advice). Counter 1 is big SRAM,
  2 controller memory (read-port results included), 3 replies, 4 peripheral writes. It is a convention
  only; the hardware treats the four counters alike.
- **Compile check:** the recipe code is compiled with `riscv64-unknown-elf-gcc -march=rv32i_zmmul
  -mabi=ilp32 -std=gnu11 -O2` against the draft header (`WORKER6/check/`). It can't run: there is no v1.2
  simulator yet.
- **Header qualifier macros:** W3-27 used `R`, `W` and `RW` as the members' qualifiers, but
  `rcp_reduce`'s `#define RW 16` collides with them. The draft uses `MESH_RO`, `MESH_WO` and `MESH_RW`
  instead.

## Log

- Cloned and pulled at d9d6c5c. `machine-spec.md` was still v1.1 then.
- Round 1: header, check sources, RECIPES.md v1.2 (43 blocks generated from compiled regions).
- The owner answered F1-F5, F7, F8 in DECISIONS-v1.2 sections 37 and 38. The recipes follow: the ROM jumps
  only on a landed command, boot part 2 enables interrupts, 16 reserved bytes, the no-increment bit is
  cleared, request limits are kept with replies. F15 and F16 are new from writing to D38.
- Round 2: CONVENTIONS.md, FINDINGS.md, REPORT.md. The replayer's words were renamed to the spec's
  `REPLAYER_STATUS` and `REPLAYER_OPCODE` (W5-09).
- Round 2 (D39-D42): the replayer's words 32-34, seven counters and the renumbered `EVENTS`, request slots
  with their own reply buffers (counters 5-7), 24 reports, `mcycle`, and the boot streamed from C&C 1's big
  SRAM with the command in the same run. F6 and F9-F16 are answered; F17 is new (open). REPORT-2.md is
  written for WORKER5.
- Round 3 (closing; D43, D44 and WORKER5's RECIPE-CHECK): M3-M8 fixed, and both observations taken (the
  watchdog now calls the toast path; MAX_REPORTS is 66 x 24). Mode 5 and `CTR_FENCE` are in the header. Recipe 28
  covers the fence and a signal between tiles. Rules 12 (ordering per sending unit) and 13 (a read is
  outstanding until its data lands) are new, and `request_issue` takes a reply flag. F17 is answered;
  REPORT-3.md is written.
