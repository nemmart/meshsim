# WORKER12 notes

## State (2026-10-31): done; REPORT.md delivered

- Tool: jolt-asic-planning branch `worker12-counts` (from `tracer-standalone-main` fd942dd): `tracer/tables/count_exec.cpp`,
  `count.sh`, `count_report.py`. Nothing else changed; not pushed to PR #1's or PR #2's branches.
- Runs local (1 CPU, 3 GB, overcommit 1): Cyclone 22 and 29, EVM Zone trace-23 and trace-29. All checks pass; totals
  equal WORKER9's (371,526,406; 354,861,942). Quest runner not used.
- meshsim: `WORKER12/REPORT.md`, `report_tables.py` (makes the report's tables from data/), `data/` (CSV + JSON, ~0.5 MB).

## Log

- Built the tracer with CMake 4.4 (pip): needs `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` for nlohmann_json 3.10.5.
- count_exec `#include`s build_tables.cpp (main renamed) so grouping uses the tables' own decode_index / fed_key.
- Window attribution: an execution's untraced rows go to the window of its first traced row.
- Item 6 chains: libsecp256k1's p - 2 chain (verified by exponent arithmetic: 255 sq, 15 mul); for n - 2 only a generic
  sliding window (k = 5 best of 1-7). Rows per step from tracer_tables.py's mulmod + 4 moves; not executed.

## Open (for the owner)

- A hand-made n - 2 chain for DIVR (libsecp256k1's older scalar inverse) would refine item 6's DIVR figure.
- The chains' segment counts and register needs were not checked against the segment format's limits.
