# Building v1.36

*For anyone building v1.36: what to install, which build to use for what, and one command for each thing you build.*
Commands run from this folder. Running the suites, tools and kits is `RUNNING.md`.

## Prerequisites

Checked on Ubuntu 24.04.

| to build | you need | on Ubuntu 24.04 |
|---|---|---|
| the simulator, its tests and tools | `make`, g++ and clang++ (C++20): clang for the release build, both for `make check` | `apt install make g++ clang` |
| `pipeline/`, the offline input tools | a JDK, Java 21 (`pipeline/README.md`) | `apt install openjdk-21-jdk-headless` |

**Nothing here needs a RISC-V toolchain.** The firmware the simulator runs is the RTL's, built in Snarky One by its
own pinned toolchain and vendored here, sources and images (`fw/rtl/README.md`). `fw/check_images.sh` checks every
vendored file against its hash list; `fw/rtl/refresh.sh` copies a newer build from a Snarky One checkout.

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
| the vendored firmware against its hash list | `fw/check_images.sh` |
| the documents' check | `make doccheck` |
| the offline input tools (Java) | `sh pipeline/build.sh` |
| the speed benchmark | `bench/README.md` |

`make clean` removes `build/`.

**`make check` on newer kernels needs `setarch`.** It runs the ThreadSanitizer tests (`make tsan`, `make tsan-chip`).
On a kernel whose address randomisation is above what TSan supports (`vm.mmap_rnd_bits` above 28, as on Ubuntu
24.04's 6.8 kernel) they stop at once with "ThreadSanitizer: unexpected memory mapping". Run it as
`setarch $(uname -m) -R make check` (address randomisation off for make and its children).
