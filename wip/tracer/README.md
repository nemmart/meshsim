# wip/tracer: the Tracer's advice expansions (WORKER7)

Templates, models and checks for the Tracer's untraced advice rows (DECISIONS-v1.3 sections 7 and 8). The
findings are in `WORKER7/REPORT.md`.

| file | what it is |
|---|---|
| `templates.py` | the division family's Tracer templates as data: Jolt's traced rows plus the untraced rows |
| `gen.py` | encodes them as 128-bit expansion words (spec 4.19.3): `out/div_templates.tracer.hex`, `.replayer.hex` (untraced rows removed), `.inc` (for the harness), `.md` (a listing) |
| `harness/check_div.cpp` | runs every template against the jolt-cpp tracer's own code, compiled: advice, rows, values, assertions, RV64 M |
| `harness/build.sh` | builds and runs it; writes `results/check_div.txt` |
| `harness/mutants.py` | broken templates that must be caught; writes nothing itself (`results/mutants.txt` is its output) |
| `harness/secp_advice.cpp` | a driver around jolt-cpp's `inlines/secp256k1.cpp` `evaluate()` |
| `inline_rows.py` | the secp256k1 multiply/square advice as straight-line untraced rows, checked against `secp_advice` |
| `results/` | the outputs quoted in the report |

## Rebuilding

The reference tracer is jolt-asic-planning's `tracer-standalone` branch at 4f36b3d (the spec's copy). Needs CMake
3.24+, g++ 13, `libelf-dev`, and network access for the tracer's CMake.

```sh
git clone --branch tracer-standalone https://github.com/quarky93/jolt-asic-planning.git
(cd jolt-asic-planning && git checkout 4f36b3d)
cmake -S jolt-asic-planning/tracer -B /tmp/tb -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
make -C /tmp/tb -j8 tracer
TRACER_SRC=$PWD/jolt-asic-planning/tracer/src TB=/tmp/tb wip/tracer/harness/build.sh 1000000   # ~85 s
python3 wip/tracer/harness/mutants.py > wip/tracer/results/mutants.txt
TR=$PWD/jolt-asic-planning/tracer/src
g++ -std=c++20 -O2 -DNDEBUG -I$TR -I/tmp/tb/_deps/fmt-src/include wip/tracer/harness/secp_advice.cpp \
    $TR/tracer/inlines/secp256k1.cpp -o /tmp/secp_advice
python3 wip/tracer/inline_rows.py 20000 > wip/tracer/results/inline_rows.txt
```

The repository needs a token for API-rate-limited clones in some environments; never write it into a file.
`build.sh` defaults `TRACER_SRC` to a jolt-asic-planning checkout beside meshsim. Against a later tracer commit,
rebuild `libtracer.a` there and rerun; the harness `static_assert`s every opcode number the templates use.
