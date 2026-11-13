# bench: the simulator's speed benchmarks

*For anyone measuring meshsim's speed: what each benchmark times, and how to build and run it.* Benchmarks are timed
in the clang release build (D18); each is built by name (`make build/clang-release/<name>`, `BUILDING.md`) and run from
this folder's parent, the version's root. Each prints its result as one line of `key=value` fields, with the machine's
digest, so a timing is only compared between runs that agree.

| folder | program | what it times | run |
|---|---|---|---|
| `sort/` | `bench_sort` | the 16K sort's counted span (L1's GO to L2's all-READY), by placement; the freeze's 6-domain sort uses it | `bench_sort --keys DIR/uniform.keys [--threads T] [--partition one\|rows\|tiles\|load\|profile] [--reps R]`; the keys from `KeyGen … --n 16384 --only uniform` (`pipeline/keys/README.md`) |
| `sort/` | `bench_chip` | the chip on a program from cycle 0 (`sort`, `wfi` or `regs`), by placement | `bench_chip --prog sort\|wfi\|regs [--cycles N] [--threads T] [--partition …]` |
| `sort/` | `trace_filter`, `trace.sh` | memory footprints of a `bench_chip` or `bench_sort` run, from valgrind's lackey trace (needs valgrind) | `bench/sort/trace.sh OUT ARGS…` (`BENCH=bench_sort` for the sort) |
| `tracer/` | `tracer_play` | the Tracer alone on a real run's calls: a TracerConf log converted, then replayed timed | `tracer_play convert LOG OUT`; `tracer_play run TRACE [REPS]` |
| `threads/` | `bench_threads` | the framework over threads: forms 1 to 4 (independent chips, one chip split into domains, the barrier, the step cost) and the footprint by structure | `bench_threads --form 2 --threads T [--chip mesh\|synth] [--domains D] …`; `bench_threads --sizes --chip mesh\|synth` |

`sort/w10.mk` holds the rules for `sort/`'s programs; `tracer_play`'s and `bench_threads`'s are in the Makefile's
includes (`bench_threads` is also built by plain `make`). `threads/mesh_fw.h` is `bench_threads`'s mesh chip. The
Tracer's log for `tracer_play` comes from v1.3's TracerConf, which the frozen copy doesn't carry.
