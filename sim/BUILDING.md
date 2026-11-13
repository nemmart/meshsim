# Building v1.35

*For anyone building v1.35: what to install, which build to use for what, and one command for each thing you build.*
Commands run from this folder. Running the suites, tools and kits is `RUNNING.md`.

## Prerequisites

Checked on Ubuntu 24.04.

| to build | you need | on Ubuntu 24.04 |
|---|---|---|
| the simulator, its tests and tools | `make`, g++ and clang++ (C++20): clang for the release build, both for `make check` (D18) | `apt install make g++ clang` |
| `pipeline/`, the offline input tools | a JDK, Java 21 (`pipeline/README.md`) | `apt install openjdk-21-jdk-headless` |
| the firmware images (`fw/build.sh`) | gcc-riscv64-unknown-elf 13.2.0-11ubuntu1+12 and binutils-riscv64-unknown-elf 2.42-1ubuntu1+6, the toolchain v1.3's images were built with, and `patch` | `apt install gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf patch` |

**The firmware build must run under `LC_ALL=C`**: the symbol files (`*.sym`) list symbols in the locale's collation
order, so under another locale they differ from the committed ones.

**Without the RISC-V toolchain, everything else builds and runs.** The firmware images are committed (`fw/images/`,
`fw/boot/bootrom.hex` and the test fixture folders), and the simulator, its tests and the suites use them as they are.
`fw/build.sh --check` rebuilds them all and proves each equal to its committed copy, byte for byte. Of the suite
runner's steps, `cc-build` and `recipes-build` are that check for their parts and say they are skipped without the
toolchain; `replay-build` then checks the committed images against their manifests instead.

## The three builds

`MODE` chooses the build, `CC_NAME` the compiler (`clang`, the default, or `gcc`); each goes to `build/<cc>-<mode>/`.

| `MODE` | flags | what it is for |
|---|---|---|
| `release` (the default) | -O2 | timing and long runs |
| `debug` | -O1 -g, ASan, UBSan, the framework's debug checks (`SIM_DEBUG`) | developing and checking a change: a unit, a test, a firmware change |
| `suite` | release, with the checker's reports (`SIM_CHECKS`) | the suites (`run_tests.sh` builds it itself) |

**What every build checks** (`src/check.h`, `src/base/fatal.h`): undefined behaviour is fatal in every build, and the
silent traps stop the core in every build; every other invariant is counted in every build and reported by name only
in the debug and suite builds. The debug build adds the framework's own checks (the missed-wake detector among them,
`src/sim.h`). The checks the spec promises that the simulator doesn't have are listed in
`spec/CHANGES-v1.3-to-v1.35.md`. A ThreadSanitizer build of the thread tests is its own (`make tsan`), since TSan
can't share a process with ASan.

## One command per thing built

| to build | command |
|---|---|
| the tests, the suites and the tools, release, clang | `make` |
| the same, debug | `make debug` |
| the same, the suite build | `make MODE=suite` |
| the same with gcc | add `CC_NAME=gcc` |
| one program, including the drivers `make` leaves out (`t2`, `chain`) | `make build/clang-release/<name>` |
| everything, and check it: v1.3's copied files unchanged, the TSan thread tests, then every test in the debug and release builds with gcc and clang | `make check` |
| every test of one build, run | `make run CC_NAME=gcc MODE=debug` |
| the thread tests under TSan, gcc and clang | `make tsan` |
| the firmware images, checked against the committed copies | `LC_ALL=C fw/build.sh --check` |
| the firmware images, rewritten into `fw/images/` | `LC_ALL=C fw/build.sh` |
| the offline input tools (Java) | `sh pipeline/build.sh` |
| the speed benchmarks | `bench/README.md` |

`make clean` removes `build/`.
