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

