# WORKER6/check: the v1.2 recipes as compiled code

Each recipe in `wip/sw/RECIPES.md` is quoted from a region of these files, marked `/* RECIPE name */` …
`/* END */`, as v1.1's `meshsim/sw/tests/rcp_*.c` are. `check_book.py` confirms that every code block in the
book equals its region.

`build.sh` compiles every file against `wip/sw/mesh-v1.2-draft.h` with `riscv64-unknown-elf-gcc
-march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -O2 -Wall -Wextra -Werror`. Nothing is linked or run: there is
no v1.2 linker script or simulator yet.

When meshsim v1.2 exists, these files are the starting point for its `rcp_*.c` tests. They are not tests yet;
WORKER4 or a later session ports them.
