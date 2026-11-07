# sim/ sessions: the new series, numbered from WORKER1

The workers of the next-simulator project (from 2026-11-06). The old series (WORKER1-30, v1.0 to v1.3) is in
`wip/planning/sessions/` and `wip/planning/reviews/`; numbers here never refer to it.

**While a worker runs**, its folder is `WORKERn/` here: `PROMPT.md` (the integrator's), and the worker's own `NOTES.md`,
`QUESTIONS.md`, `REPORT.md`. A message for a busy worker is parked in `WORKERn/NEXT.md` and removed once sent. Code goes on
the worker's branch (`sim-wn`); the integrator merges into `main`. **When it closes**, its report, questions and data move
here as `WORKERn-REPORT.md` etc., with a line in the table below, and its folder goes.

| session | what it did | kept here |
|---|---|---|
