# tools/replayer-opcodes: the harness behind `spec/replayer-opcodes.md`

Two small programs compiled against the jolt-cpp tracer's own code, which produced the opcode
appendix's slot-form and producer table (its section 6) and its test vectors (section 9). They live
outside the tracer's repository; nothing is written into it.

| file | what it is |
|---|---|
| `enum.cpp` | `#include`s `tracer/isa/virtual.cpp` and runs `expand_virt_seq` on every source opcode (rd ≠ x0 and rd = x0; for the immediate shifts the amounts 0, 3, 31, 32, 63), printing every emitted row's opcode, slot kinds and whether it is its expansion's last row |
| `enum.txt` | its output at tracer commit 4f36b3d |
| `summary.txt` | `enum.txt` condensed per row opcode: last-row status and slot forms with their source instructions (`summarize.py summary`) |
| `sites.json` | the static emit sites per opcode, grepped from `isa/virtual.cpp` and `inlines/` (`summarize.py sites`) |
| `vec.cpp` | `#include`s `tracer/trace_owner.cpp` and calls the tracer's `execute_instruction<false>` (the log-driven path) for about 240 hand-built rows, each in a forked child so a failing assertion shows as ABORT |
| `vec.tsv` | its output at 4f36b3d: opcode, operands, rd after, new pc, next (sequential, transfer or end), log words popped, note |
| `summarize.py` | the two post-processing steps |
| `render/` | the scripts that rendered the appendix: `rows.py` (the hand-written per-opcode semantics), `gen.py` and `build.py` (tables from the files above). Their paths are absolute to the session that wrote them and need adjusting |

## Building against the tracer

Requirements: CMake 3.24 or later, g++ 13 (C++20), `libelf-dev`, and network access for the
tracer's CMake to fetch fmt, nlohmann_json and xbyak.

```sh
git clone --branch tracer-standalone https://github.com/quarky93/jolt-asic-planning.git
cd jolt-asic-planning && git checkout 4f36b3d          # the commit the appendix was derived from
cmake -S tracer -B /tmp/tb -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
make -C /tmp/tb -j8 tracer                              # builds /tmp/tb/libtracer.a
TR=$PWD/tracer/src
FLAGS="-std=c++20 -O1 -DNDEBUG -fno-rtti -I$TR -I/tmp/tb/_deps/xbyak-src -I/tmp/tb/_deps/fmt-src/include \
       -I/tmp/tb/_deps/json-src/include -DXBYAK_NO_EXCEPTION -DXBYAK64"
LIBS="-Wl,--allow-multiple-definition /tmp/tb/libtracer.a /tmp/tb/_deps/fmt-build/libfmt.a -lelf -no-pie -lpthread"
cd <this directory>
g++ $FLAGS enum.cpp -o /tmp/enum $LIBS && /tmp/enum > enum.txt
g++ $FLAGS vec.cpp  -o /tmp/vec  $LIBS && /tmp/vec  > vec.tsv 2> /tmp/vec.err   # the ABORTs log to stderr
python3 summarize.py summary enum.txt > summary.txt
python3 summarize.py sites "$TR/tracer" > sites.json
```

`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` is needed with CMake 4, whose policy floor rejects the pinned
nlohmann_json. `--allow-multiple-definition` lets the harness's copy of `virtual.cpp` or
`trace_owner.cpp` (included to reach their file-local functions) coexist with the library's.

## Regenerating for a later tracer commit

1. Check out the new commit and rebuild `libtracer.a` as above.
2. Rerun both harnesses and both `summarize.py` steps; compare `summary.txt`, `sites.json` and
   `vec.tsv` with the committed copies. A new or vanished row opcode, a changed slot form, a
   changed last-row status or a changed vector is a change to the appendix (and possibly to the
   replayer: its hardwired arithmetic is per opcode, spec item 57).
3. If `riscv.inc` gained opcodes or source formats, extend `enum.cpp`'s format choice (it picks
   the decoder's R, I, S/B, U or system form from the opcode name) and add vectors to `vec.cpp`.
4. Update `spec/replayer-opcodes.md`: its section 6 and 9 tables from the new files (the scripts
   in `render/`), its sections 1–5 by hand where the semantics changed, and its header's commit.
