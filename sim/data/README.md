# data: the simulator's input data

*For anyone running the simulator or making its inputs: what each data file is, what reads it, and the replay inputs'
format.* The data files are v1.3's, unchanged; the programs read them from the version's root.

| file | what | read by |
|---|---|---|
| `hops.txt` | the mesh's offset table: for each relative offset, the horizontal and vertical hops | the network (`src/units/net.h`) |
| `zipfraw.txt`, `trace.txt`, `one99.txt` | three all-to-all workloads, each a 64 × 64 stream matrix (words from each tile to each) | the machine harness's all-to-all (`A2A`, `tests/mach.h`); the sweep reads `zipfraw.txt` |
| `vec-821e1bd.tsv` | the tracer's ALU vectors from jolt-cpp `821e1bd`: 252 rows of operands and results | the replayer's ALU tests (`tests/test_replayer_alu.cpp`, `tests/suite_replayer.cpp`) |
| `tables/` | the replayer's universal expansion tables from jolt-cpp `821e1bd`: the decode table, the ROM, the template index (its `README.md`, `manifest.json`) | the replayer's harnesses and suites |
| `REPLAY-INPUT-REQUIREMENTS.md` | **the replay inputs' format**: what a delivery (the tracer's output) must hold for the replayer, file by file | the format's reference |
