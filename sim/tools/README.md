# tools: helper scripts and tools

*For anyone maintaining v1.35: what each helper does and how to run it.* The programs build with the simulator
(`make`, `BUILDING.md`) into `build/<cc>-<mode>/`.

| tool | what | run |
|---|---|---|
| `regs/` | `gen_regs` writes `fw/include/mesh.h` and `fw/REGISTERS.md` from the register table (`regs/regtable.h`); `make check` (`tests/test_regs.cpp`) checks the committed files are its output and the table matches the simulator's (`src/units/regmap.h`) | `gen_regs mesh.h fw/include/mesh.h`; `gen_regs registers fw/REGISTERS.md`; `gen_regs check fw/include/mesh.h fw/REGISTERS.md` |
| `regs/header_diff.sh` | compares a generated header with a draft one, over the host gcc | `tools/regs/header_diff.sh GENERATED DRAFT` |
| `delivery/` | `convert_delivery` converts a v1.25 replay delivery to the current format; `synthetic_delivery` writes a synthetic one (D42, D43) | `convert_delivery SRC DST PROGRAM [--manifest M]`; `synthetic_delivery OUT [--program NAME] [--windows N] [--seed S] …` |
| `check_copies.sh` | checks that every file v1.35 copied from v1.3 (`COPIES.sha256`) still has v1.3's bytes; `make check` runs it | `sh tools/check_copies.sh` |
| `machine.sh` | records what the machine is and chooses CPUs for a run (D21) | `tools/machine.sh record [FILE]`; `tools/machine.sh cpus KIND [N]` |
| `test_machine.sh`, `fake_sysfs.sh` | `machine.sh`'s test on fake `/sys` trees with known CPU layouts | `tools/test_machine.sh` |

To change a register: edit `regs/regtable.h` (and `src/units/regmap.h`, the simulator's decoding), regenerate both files
with `gen_regs`, and run `make check`.
