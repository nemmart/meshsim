# pipeline/: the offline input tools (Java)

What makes the simulator's inputs, kept in Java (DECISIONS D40-D41): self-contained, with its own build, **a JDK its
only prerequisite**, and nothing in the simulator's build, tests or runs reads it.

| folder | what | from |
|---|---|---|
| `keys/` | `KeyGen` (the sort's key sets, bit-exact with numpy 2.x) and `SplitSets` (the split's 25 data sets) | v1.3's `meshsim/test/`, unchanged |

`sh build.sh` builds into `build/`; `sh check.sh` builds and checks the outputs against v1.3's (`keys/EXPECTED.sha256`).
Still to come (WORKER19, the owner deciding): ConvertDelivery and SyntheticDelivery; `gold`, `jumpmap`, `inputs/`.
