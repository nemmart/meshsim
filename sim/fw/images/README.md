# fw/images: the committed firmware images

*For anyone running or checking the firmware: which images are here and how they are checked.* Each image is three
files, `NAME.code.bin`, `NAME.data.bin` and `NAME.sym`; `MANIFEST` lists each file's sha256. `fw/build.sh` writes
them, and `fw/build.sh --check` (or, without the RISC-V toolchain, `fw/check_images.sh`) proves them equal to their
sources (`fw/README.md`, `BUILDING.md`). Images kept in a test's fixture folder are not here.

| images | what |
|---|---|
| `rcp_*` | the recipes, each compiled from `fw/recipes/` (`fw/RECIPES.md`) |
| `cc_bad`, `cc_rcp` | test variants of the controllers' programs (`fw/cc/`) |
| `blockcopy`, `cmd`, `cond`, `gather`, `inv`, `rdport`, `req`, `search` | tile test programs (`fw/tests/`) |
| `log8k/`, `oldmap/` | the replay controller's two variants, for the 8 KB log and (tests only) the old queue map (`fw/replay/`) |
