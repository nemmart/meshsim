# tools/freeze: the version's file list, and freezing v1.36

> *v1.36's page. v1.37 was made in a work folder, `v1.37_wip/`, and frozen by renaming it to `v1.37/` (2026-11-19); the list and
> the documents' check here serve it unchanged.*

*For whoever freezes v1.36: what is here, what the freeze takes, and what it does not need.*

| file | what it does |
|---|---|
| `frozen.sh` | the files of the version, one per line: the tracked files less `exclude.txt`'s (`--untracked`: with the untracked files git doesn't ignore). `tools/doccheck.sh` checks the documents against this list |
| `exclude.txt` | the paths the list leaves out. v1.36 leaves none out |

## v1.36 is frozen in place

v1.35 was made in a work folder and frozen by copying it, less its oracle and session material, into `v1.35/`; its
tools for that (the copy, the comment strip, the freeze check's task for the measurement runner) are in `v1.35/`, beside this folder,
and are not here. v1.36 began as that frozen folder, copied to `v1.36/` on its own branch, and was changed there. So
there is nothing to copy and nothing to strip: **the freeze is the commit that puts this folder on `main`**, after
which it is never edited again but for an errata file (`spec/GUIDELINES.md`, 8).

What the freeze takes, in order, from this folder on the commit to be frozen:

1. A clean tree: `git status --short` prints nothing for this folder.
2. `make -j20`: builds without a warning (warnings are errors).
3. `./run_tests.sh all --check`, with what is outside the repository (`RUNNING.md`: `MESHSIM_INPUTS`, the real
   workloads' data, and the RISC-V toolchain of `fw/build.sh`, with `RISCV_TESTS` and `COREMARK` naming their
   checkouts, so that every image is rebuilt; and a JDK first on `PATH`): its last line is `== all passed; logs in results/`, with no step `SKIPPED`.
4. `setarch $(uname -m) -R make -j20 check`: its last line is `==== make check: all passed (gcc clang; debug,
   release)`.
5. `make doccheck`: `doccheck: 0 failures`.
6. The acceptance kit's calibration (`RUNNING.md`): `PASS` at 1, 4, 8 and 16 processes.
7. Then, outside this folder: the repository's top-level `README.md` gains v1.36's row and names v1.36 as the RTL's
   target, and the commit lands on `main`. The folder's own `README.md` is already written for the frozen state.

What the frozen folder is pinned to: the firmware built here, by its hash lists (`fw/images/MANIFEST` and
`tests/core-programs/SHA256SUMS`), the RTL's programs among it at the Snarky One commit 370e3ac; the files copied from v1.3 by `COPIES.sha256`; the RTL's references
under `tests/` (each folder's `README.md` names the Snarky One commit and the command that made them). The
specification follows Snarky One's `rtl/tile/base/spec.md` and `rtl/blocks/core/spec.md` at the commit
`spec/README.md` names.
