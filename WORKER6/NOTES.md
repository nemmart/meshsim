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
