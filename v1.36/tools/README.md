# tools: helper scripts and tools

*For anyone maintaining v1.36: what each helper does and how to run it.* The programs build with the simulator
(`make`, `BUILDING.md`) into `build/<cc>-<mode>/`.

| tool | what | run |
|---|---|---|
| `regs/` | `gen_regs` writes `fw/include/mesh.h` and `fw/REGISTERS.md` from the register table (`regs/regtable.h`); `make check` (`tests/test_regs.cpp`) checks the committed files are its output and the table matches the simulator's (`src/units/regmap.h`), the specification's and the RTL firmware's | `gen_regs mesh.h fw/include/mesh.h`; `gen_regs registers fw/REGISTERS.md`; `gen_regs check fw/include/mesh.h fw/REGISTERS.md` |
| `delivery/` | `convert_delivery` converts a v1.25 replay delivery to the current format; `synthetic_delivery` writes a synthetic one, reproducible from its seed | `convert_delivery SRC DST PROGRAM [--manifest M]`; `synthetic_delivery OUT [--program NAME] [--windows N] [--seed S] …` |
| `fwimage/` | `fwimage` makes the sort runs' key files, in the RTL's format (`fw/README.md`), and writes what a loader takes from an ELF file (its entry point and its loaded segments' addresses, sizes and bytes), to compare two files' loaded contents. `fw/check_images.sh` and `fw/build.sh` run it against the committed keys; the runner builds the 64K key file with it | `fwimage keys RECORDS OUT`; `fwimage loaded ELF OUT` |
| `check_copies.sh` | checks that every file copied from v1.3 (`COPIES.sha256`: the data files and the pipeline's key tools) still has v1.3's bytes; `make check` and the runner's step `layout` run it | `sh tools/check_copies.sh` |
| `machine.sh` | records what the machine is and chooses CPUs for a run | `tools/machine.sh record [FILE]`; `tools/machine.sh cpus KIND [N]` |
| `doccheck.sh`, `doccheck.mk` | the documents' checks: every cited path exists in this folder, every folder a reader starts in has a README, every document code names is there. A path into Snarky One is written as such ("Snarky One's …") and is not checked; `doccheck-exempt.txt` lists the other exemptions | `make doccheck`; `tools/doccheck.sh [-v]` |
| `freeze/` | the version's file list (`freeze/frozen.sh`, `freeze/exclude.txt`), which the documents' check reads, and what freezing v1.36 takes (its `README.md`) | `freeze/README.md` |
| `test_machine.sh`, `fake_sysfs.sh` | `machine.sh`'s test on fake `/sys` trees with known CPU layouts | `tools/test_machine.sh` |

To change a register: edit `regs/regtable.h` (and `src/units/regmap.h`, the simulator's decoding), regenerate both files
with `gen_regs`, and run `make check`.
