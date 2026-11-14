#!/bin/bash
# fw/rtl/refresh.sh SNARKY_ONE_WORKTREE: copies the RTL's firmware, sources and built images, from a Snarky One
# checkout into this folder and rewrites its hash list (MANIFEST). The checkout must have its images built by its own
# toolchain (`mise run core-images` and `mise run sort-images` there: README.md); nothing is built here.
#   src/    the sources, each at its path in Snarky One (only files its git tracks)
#   boot/   build/core-tests/boot_rom.elf
#   tile/   build/core-tests/tile/<set>/: the tile programs' ELF files and programs.txt
#   sort/   build/sort/: the runs' master.elf, tile/sort.elf, memory-tile/memory.elf, segment/{replay,tracer}.elf,
#           sort.keys, sort.txt; sort-64k.keys (16 MiB) is not copied: its sha256 goes into sort/sort-64k.keys.sha256
# It refuses a checkout whose vendored sources differ from its HEAD, and says so when an image is older than a source
# (a build to run again). After it: read `git status`, update the commit README.md names, run fw/check_images.sh rtl.
set -eu
export LC_ALL=C
HERE=$(cd "$(dirname "$0")" && pwd)
[ $# = 1 ] && [ -d "$1/.git" -o -f "$1/.git" ] || { echo "usage: fw/rtl/refresh.sh SNARKY_ONE_WORKTREE"; exit 2; }
S=$(cd "$1" && pwd)

# The sources: directories and files of Snarky One, copied as its git tracks them.
SOURCES="rtl/blocks/core/src/boot_rom.S rtl/blocks/core/src/boot_rom_pkg.veryl rtl/tile/base/test/programs
rtl/chip/test/programs/sort rtl/chip/test/programs/firmware xtask/src/core_tests.rs xtask/src/tile_tests.rs
xtask/src/sort_images.rs"
# The runs of build/sort/ that have a master.elf.
RUNS="kv32 k128 kv32-64k k128-64k memory trace"

dirty=$(git -C "$S" status --porcelain -- $SOURCES)
[ -z "$dirty" ] || { echo "fw/rtl/refresh.sh: $S has changes to the vendored sources against its HEAD:"; echo "$dirty"; exit 1; }
commit=$(git -C "$S" rev-parse HEAD)
for f in build/core-tests/boot_rom.elf build/core-tests/tile/base_tile/programs.txt build/sort/sort.txt; do
  [ -f "$S/$f" ] || { echo "fw/rtl/refresh.sh: no $S/$f: run 'mise run core-images' and 'mise run sort-images' there"; exit 1; }
done

rm -rf "$HERE/src" "$HERE/boot" "$HERE/tile" "$HERE/sort"
git -C "$S" ls-files -- $SOURCES | while read -r f; do
  mkdir -p "$HERE/src/$(dirname "$f")" && cp "$S/$f" "$HERE/src/$f"
done
newest=$(git -C "$S" ls-files -- $SOURCES | while read -r f; do stat -c %Y "$S/$f"; done | sort -n | tail -1)

image() {   # image FROM TO: one built file, from the checkout's build/ into this folder
  [ -f "$S/build/$1" ] || { echo "fw/rtl/refresh.sh: no $S/build/$1"; exit 1; }
  mkdir -p "$HERE/$(dirname "$2")" && cp "$S/build/$1" "$HERE/$2" && chmod 644 "$HERE/$2"
  [ "$(stat -c %Y "$S/build/$1")" -ge "$newest" ] || echo "NOTE: build/$1 is older than the newest vendored source: build again?"
}
image core-tests/boot_rom.elf boot/boot_rom.elf
for set in "$S"/build/core-tests/tile/*/; do
  set=$(basename "$set")
  image "core-tests/tile/$set/programs.txt" "tile/$set/programs.txt"
  while read -r _kind elf; do image "core-tests/tile/$set/$elf" "tile/$set/$elf"; done < "$S/build/core-tests/tile/$set/programs.txt"
done
for run in $RUNS; do image "sort/$run/master.elf" "sort/$run/master.elf"; done
for f in tile/sort.elf memory-tile/memory.elf segment/replay.elf segment/tracer.elf sort.keys sort.txt; do image "sort/$f" "sort/$f"; done
[ -f "$S/build/sort/sort-64k.keys" ] || { echo "fw/rtl/refresh.sh: no $S/build/sort/sort-64k.keys"; exit 1; }
(cd "$S/build/sort" && sha256sum sort-64k.keys) > "$HERE/sort/sort-64k.keys.sha256"

{
  echo "# fw/rtl's hash list (sha256, path in fw/rtl/), written by fw/rtl/refresh.sh; fw/check_images.sh rtl checks it."
  echo "# Snarky One $commit"
  (cd "$HERE" && find src boot tile sort -type f | sort | xargs sha256sum)
} > "$HERE/MANIFEST"
echo "fw/rtl/refresh.sh: $(grep -vc '^#' "$HERE/MANIFEST") files from Snarky One $commit"
