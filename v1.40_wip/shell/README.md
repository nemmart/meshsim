# v1.40_wip/shell: the console shell

The shell of `fs_wip/planning/SHELL-SPEC.md`, built on the file system's API (`../fs/fs.h`) and tested on its C++
emulation (`../fs/`). The session's notes are `../sessions/SHELL/NOTES.md`: the outline and the owner's rulings, what
the shell decided where the spec is silent, the findings for the API, and the sizes.

| file | what |
| --- | --- |
| `shell.c` | `shell_main`, the line editor, the parser and redirection, paths, the error messages |
| `cmds.c` | the commands but `grep`; option parsing |
| `grep.c` | the matcher (Pike's, with its backtracking in a static stack) and `grep` |
| `glob.c` | `*` in file names: a command's file arguments, a pattern's matches among them, sorted |
| `fmt.c` | the formatter and the sinks: the console (`\n` as `\r\n`), a redirection's 4 KB buffer |
| `str.c`, `div.c` | string helpers and `memcpy` and its kin; the division helpers the compilers call |
| `shell.h`, `console.h` | what the files share; the console's three calls |
| `host/` | the host build: the console (a terminal, or a script), the entry, the test seeding (`seed.cpp`) |
| `rv32/` | the link-only RV32 build: `crt0.S`, `link.ld`, stubs of the console and of the calls the shell may make |
| `tests/` | `NN-name.in` (keystrokes), `.seed` (the file system, made through the API), `.out` (the console's bytes); `divtest.c`, `retest.c`; `run.sh` |

```
make test              # the division tests, the matcher's, and every scripted test (CC=gcc, the default)
make test CC=clang
make size              # RV32 without and with C; fails over 32 KB of code (CC=gcc or CC=clang)
make host && build/gcc/shell_host -s tests/14-proof.seed      # the shell, by hand, on a proof's working directory
```

The RV32 builds need `riscv64-unknown-elf-gcc` (Ubuntu: `gcc-riscv64-unknown-elf`) or clang 16 or later with `ld.lld`.
