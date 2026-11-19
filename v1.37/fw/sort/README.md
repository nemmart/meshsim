# fw/sort: the sort's firmware

*For anyone reading or changing the sort: its three parts and where each is described.* All three build with
`fw/build.sh` (`PARTS=sort`, `fw/README.md`).

| folder | what |
|---|---|
| `program/` | the sort program a tile runs, with its own build tools and patches (its `README.md`) |
| `split/` | the split, design v1's bisection ported to the controller read port: its sources and `BINDING.md`, how it binds into the sort program |
| `skew/` | the skew planner: the model, its harness and planner, and their results (its `README.md`, `RESULTS.md`) |
