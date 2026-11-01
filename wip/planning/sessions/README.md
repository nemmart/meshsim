# Sessions: their reports, questions and findings

Their prompts and notes were removed when the sessions closed; git history has them.

| session | wrote | kept here |
|---|---|---|
| WORKER5 | `wip/spec/machine-spec.md` v1.2 and `wip/spec/CHANGES-v1.2.md` | its reports (rounds 1-3), `QUESTIONS.md` (W5-01 to W5-26, all answered), `RECIPE-CHECK.md` (the recipes checked against the spec) |
| WORKER6 | `wip/sw/RECIPES.md` v1.2, `wip/sw/CONVENTIONS.md`, `wip/sw/mesh-v1.2-draft.h`, and the compiled recipe sources now in `wip/sw/recipes/` | its reports (rounds 1-3) and `FINDINGS.md` (F1 to F17, all answered) |

The reviews of the decisions themselves are in `../reviews/`.

## Added at v1.2's freeze (2026-11-01)

| session | what it did | kept here |
|---|---|---|
| WORKER4 | ported meshsim to v1.2 in six phases, the replayer included, and verified the combined branch with the sort (Quest tasks 073-077) | `WORKER4-REPORT.md`, `WORKER4-QUESTIONS.md` (W4-01 to W4-08) |
| WORKER7 | the Tracer's advice: the inventory, the division family's untraced templates (8.04 M cases), the inlines' layout | `WORKER7-REPORT.md`, `WORKER7-QUESTIONS.md`; its templates and harness are in `wip/tracer/` |
| WORKER8 | ported the sort to v1.2: all 43 full-size runs pass (Quest 070-072), every cycle difference explained | `WORKER8-REPORT.md`, `-QUESTIONS.md`, `-REQUESTS.md`, `WORKER8-results/`; its runner kit was stale after the boot ROM and is not kept (WORKER4 rebuilt one for task 077) |
| WORKER12 | counted what EVM Zone and Cyclone execute at 2²⁹: inlines, untraced rows, CSR instructions, per window (v1.3 log section 11) | `WORKER12-REPORT.md`, `WORKER12-data/`, `WORKER12-report_tables.py`; its counting tool is on jolt-asic-planning's `worker12-counts` branch |
| WORKER9 | v1.25's tables and deliveries: the standalone tracer refreshed to jolt-cpp `821e1bd` (jolt-asic-planning PR #2), the universal replayer and Tracer tables (`wip/tables/`), the delivery writer, both 2²⁹ programs delivered and proved (Quest 079, 080), `--validate` at 2²⁹ (069b) | `WORKER9-REPORT.md` (with how to rebuild the deliveries and redo all of it for a later jolt-cpp), `-QUESTIONS.md`, `-PLAN-PART2.md`, `-PLAN-PART3.md`, `WORKER9-expansion-diff/`, `WORKER9-tracer-div-821e1bd/`; its opcode-harness outputs moved to `wip/tools/replayer-opcodes/out-821e1bd/` |

## Added at v1.2.1's freeze (2026-11-01)

| session | what it did | kept here |
|---|---|---|
| WORKER15 | v1.2.1: v1.2 with errata E1-E11 and cleanups CL1-CL5 applied, meshsim and the software in line; `v1.2.1/CHANGES-v1.2.1.md`, the changelog from Tony's copy; verified on the runner (Quest 088: quick 34/34, full 10/10, p4/uniform-k128 1,360,082 cycles) | `WORKER15-REPORT.md`, `WORKER15-QUESTIONS.md` (Q1-Q3, which became E9, CL3 and CL4). Its report's diff commands use a `v1.2.1` tag that was never made; `CHANGES-v1.2.1.md` has the folder diffs |
| WORKER13 | the change guide `wip/spec/CHANGES-to-v1.25.md` (v1.1 -> v1.25 and v1.2 -> v1.25 in one; 200 entry IDs), stamped at spec `d590722`, with DECISIONS-v1.25 section 8 and CL1-CL6; found v1.2 errata E1 (extended), E9, E10, E11 | `WORKER13-REPORT.md`, `WORKER13-QUESTIONS.md` (Q1-Q10; Q10, v1.1's two ordinal ranges, is for Tony), `WORKER13-opcode-map.txt` |
