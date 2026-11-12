# spec/check

The tools that restructured v1.3's spec as v1.35's and prove it a move. They read `v1.3/`, so this folder is left out of the frozen copy; the record of every edit is `spec/history/EDITS.md`.

- `MAP.md`: the plan (where each unit went, the new section 13, the edits), reviewed before the move; `map.tsv`, its line ranges.
- `build.py`: builds the destinations from `v1.3/` by line range, applies the edits, writes `map.tsv`, `edits.json` and `spec/history/EDITS.md`.
- `check_move.py`: the proof (run from the repository root): every source line in exactly one destination, unchanged and in order, or an edit; every destination line a source line, an edit or added; every section number a heading; every path present.
- `inventory.py`, `section13.py`: the source's units, and section 13's statuses with their evidence.
- `MECHANISMS.md`: what v1.35's meshsim does for each check the spec promises; `FINDINGS.md`: what looks wrong, for the owner.

**At the freeze:** rerun `MECHANISMS.md` against `main`; bring the drivers' table of `spec/CHANGES-v1.3-to-v1.35.md` up to date; the section "Checks the spec promises that neither simulator has" stays, matching item 159; every other check `MECHANISMS.md` finds missing or unreported (invariants 2, 3, 6 and 22, `DMA_HDR` mid-transfer, the Tracer's oversized transfer) should by then be ported, and any that isn't goes to the owner; rebuild and rerun `check_move.py`.
