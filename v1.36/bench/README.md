# bench: the simulator's speed benchmark

*For anyone measuring meshsim's speed: what the benchmark times, and how to build and run it.* It is timed in the
clang release build and run from this folder's parent, the version's root. It prints its result as one line of
`key=value` fields, with the machine's digest, so a timing is only compared between runs that agree.

| folder | program | what it times | run |
|---|---|---|---|
| `threads/` | `bench_threads` | the framework over threads: forms 1 to 4 (independent chips, one chip split into domains, the barrier, the step cost) and the footprint by structure | `bench_threads --form 2 --threads T [--chip mesh\|synth] [--domains D] …`; `bench_threads --sizes --chip mesh\|synth` |

`bench_threads` is built by plain `make` (its rule is in `src/threads.mk`); `threads/mesh_fw.h` is its mesh chip, a
framework test chip, not the model of `src/units/`.

v1.35's benchmarks of the sort and of the chip ran v1.35's machine and images and are not in v1.36
(`../README.md`, "What v1.36 does not carry over"). The simulator's speed on v1.36's chip is printed by the chip's
tests as they run (`test_rtl_sort`, `test_rtl_segment`: cycles and wall time), and `tracer_block` prints its own.
