# tools/v137-check: v1.37's checks as a task for the measurement runner

`make_task.sh NNN-name ITEM > NNN-name.sh` writes a self-contained task for Quest, the owner's measurement runner:
this folder's tree at `HEAD` as an embedded kit, and `body.sh` as what the task runs. Commit first; the kit is
`HEAD`'s. It needs `javac` where it is run, for the classes the kit carries, so that the runner needs none.

| item | what the task runs |
|---|---|
| `suites` | the small inputs, the host's side of the proof steps, then `./run_tests.sh all --check` with the proof steps |
| `suites-varied` | the same with every chip's LPDDR model jittered |
| `check` | `make check` from a clean tree |
| `faster` | the proof runs with the link at 2, 4 and 8 bytes a cycle |
| `chain-evm-zone`, `chain-cyclone` | the driver and two chips as three processes, at 2^29, twice |

It was `updater_wip/tasks/v137-check/` until that folder was removed at the freeze (2026-11-19); the freeze's own
pass (Quests 264 to 268) was made by it from there.
