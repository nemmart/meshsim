# Building v1.36

> *v1.37: this page is v1.36's, and holds for this folder: v1.37 adds nothing to install. Its master program
> (`fw/cc/master.c`) is built by `fw/build.sh` with the same toolchain (the xPack riscv-none-elf-gcc 15.2.0-1 the
> committed images were built with). The simulator builds three sources more, all in this folder and held to their
> bytes at the copy (`FROM-UPDATER.sha256`): WORKER8's link (`link/`: `hostlink.cpp`, `link_end.cpp`, `node.cpp`),
> into every chip's Ingress/Egress and the harnesses' host end; WORKER10's wire (`wire/`: `wire.cpp`,
> `wire_args.cpp`, `launch.cpp`), for the chain of processes (`make build/clang-suite/wire_launch
> build/clang-suite/driver build/clang-suite/standin_asic`); and, for the host's side of the proof steps
> (`pipeline/frames/build.sh`, `tests/updater/make_fixtures.sh`), the frames' gold (`pipeline/frames/gold/`), which
> needs a JDK (Java 21), as `pipeline/` does. `pipeline/gold/` is v1.3's gold, pinned, for the segment files.*

*For anyone building v1.36: what to install, which build to use for what, and one command for each thing you build.*
Commands run from this folder. Running the suites, tools and kits is `RUNNING.md`.

## Prerequisites

Checked on Ubuntu 24.04.

| to build | you need | on Ubuntu 24.04 |
|---|---|---|
| the simulator, its tests and tools | `make`, g++ and clang++ (C++20): clang for the release build, both for `make check` | `apt install make g++ clang` |
| `pipeline/`, the offline input tools | a JDK, Java 21 (`pipeline/README.md`) | `apt install openjdk-21-jdk-headless` |
| the firmware images (`fw/build.sh`) | xPack `riscv-none-elf-gcc` 15.2.0-1 (GCC 15.2.0, binutils 2.45), the toolchain the committed images were built with, on `PATH` or named by `RISCV_PREFIX`; and a C++ compiler (`c++`, or `CXX`), with which its part `runs` builds `tools/fwimage/`. (v1.35's images were built with Ubuntu 24.04's `riscv64-unknown-elf-gcc` 13.2, which this toolchain replaces: `fw/README.md`) | not packaged: the release `v15.2.0-1` of xpack-dev-tools' `riscv-none-elf-gcc-xpack`, unpacked (`fw/build.sh` prints where) |
| riscv-tests' and CoreMark's images, with the above | a checkout of each at the revision `fw/core/riscv-tests/riscv-tests.toml` and `fw/core/coremark/coremark.toml` pin, named by `RISCV_TESTS` and `COREMARK` | `git clone` at that revision |

**Without the RISC-V toolchain, everything else builds and runs.** The firmware images are committed (`fw/images/`,
`tests/core-programs/elf/`), and the simulator, its tests and the suites
use them as they are. `fw/build.sh --check` rebuilds every C and assembly image from the sources in `fw/` and proves
each equal to its committed copy, byte for byte; without the toolchain, `fw/check_images.sh` checks the committed
images against the hash lists committed beside them. The suite runner's step `fw-build` does the first when the
toolchain is there and is printed as SKIPPED when it isn't; `fw-images` does the second always. No image is built
outside meshsim.

## The three builds

`MODE` chooses the build, `CC_NAME` the compiler (`clang`, the default, or `gcc`); each goes to `build/<cc>-<mode>/`.

| `MODE` | flags | what it is for |
|---|---|---|
| `release` (the default) | -O2 | timing and long runs |
| `debug` | -O1 -g, ASan, UBSan, the framework's debug checks (`SIM_DEBUG`) | developing and checking a change: a unit, a test |
| `suite` | release, with the checker's reports (`SIM_CHECKS`) | the suites (`run_tests.sh` builds it itself) |

**What every build checks** (`src/check.h`, `src/base/fatal.h`): undefined behaviour is fatal in every build; every
other invariant the units check is counted in every build and reported by name only in the debug and suite builds
(`src/units/README.md`, "The checker and the invariants"). The debug build adds the framework's own checks (the
missed-wake detector among them, `src/sim.h`). A ThreadSanitizer build of the thread tests is its own (`make tsan`),
since TSan can't share a process with ASan.

## One command per thing built

| to build | command |
|---|---|
| the tests and the tools, release, clang | `make` |
| the same, debug | `make debug` |
| the same, the suite build | `make MODE=suite` |
| the same with gcc | add `CC_NAME=gcc` |
| one program, including the two `make` leaves out (`tracer_block`, `suite_bus`) | `make build/clang-release/<name>` |
| everything, and check it: the files copied from v1.3 unchanged, the TSan runs, then every test in the debug and release builds with gcc and clang | `make check` |
| every test of one build, run | `make run CC_NAME=gcc MODE=debug` |
| the thread tests under TSan, gcc and clang | `make tsan` |
| the chip split over threads under TSan (the sort's 16-record run on 2 and 8 threads), gcc and clang | `make tsan-chip` |
| the firmware images, rebuilt and checked against the committed copies | `fw/build.sh --check` |
| the firmware images, rewritten into `fw/images/` | `fw/build.sh` |
| the committed images against their hash lists, without the toolchain | `fw/check_images.sh` |
| the 64K sort runs' key file (16 MiB; the runner builds it itself) | `build/clang-release/fwimage keys 65536 FILE` |
| the documents' check | `make doccheck` |
| the offline input tools (Java) | `sh pipeline/build.sh` |
| the speed benchmark | `bench/README.md` |

`make clean` removes `build/`.

**`make check` on newer kernels needs `setarch`.** It runs the ThreadSanitizer tests (`make tsan`, `make tsan-chip`).
On a kernel whose address randomisation is above what TSan supports (`vm.mmap_rnd_bits` above 28, as on Ubuntu
24.04's 6.8 kernel) they stop at once with "ThreadSanitizer: unexpected memory mapping". Run it as
`setarch $(uname -m) -R make check` (address randomisation off for make and its children).
