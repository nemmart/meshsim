#!/bin/bash
# fw/rtl/refresh.sh SNARKY_ONE_WORKTREE: copies the chip's Rust firmware, sources and built images, from a Snarky One
# checkout into this folder and rewrites its hash list (MANIFEST). These are the images meshsim does not build yet
# (README.md); everything else the simulator runs is built by fw/build.sh from fw/'s own sources, and is not touched
# here. The checkout must have its images built by its own toolchain (`mise run core-images` and `mise run sort-images`
# there: README.md).
#   src/    the Rust sources and the two recipes, each at its path in Snarky One (only files its git tracks)
#   sort/   build/sort/: the runs' master.elf, memory-tile/memory.elf, segment/{replay,tracer}.elf, sort.txt
# It refuses a checkout whose copied sources differ from its HEAD, and says so when an image is older than a source
# (a build to run again). After it: read `git status`, update the commit README.md names, and run `fw/build.sh --check`:
# its part `runs` fails if a master's image no longer holds the run image built here, which is how a tile image, a
# replay image or a tracer tile's image that changed in Snarky One and not here shows.
# The core's Rust program (tests/core-programs/elf/firmware-rust.elf) is a fixture with the RTL's measurements of it:
# tests/core-programs/README.md says how that folder is made.
set -eu
export LC_ALL=C
HERE=$(cd "$(dirname "$0")" && pwd)
[ $# = 1 ] && [ -d "$1/.git" -o -f "$1/.git" ] || { echo "usage: fw/rtl/refresh.sh SNARKY_ONE_WORKTREE"; exit 2; }
S=$(cd "$1" && pwd)

# The sources: directories and files of Snarky One, copied as its git tracks them.
SOURCES="rtl/chip/test/programs/firmware rtl/blocks/core/test/programs/firmware/Cargo.toml
rtl/blocks/core/test/programs/firmware/Cargo.lock rtl/blocks/core/test/programs/firmware/src
xtask/src/core_tests.rs xtask/src/sort_images.rs"
# The runs of build/sort/ that have a master.elf.
RUNS="kv32 k128 kv32-64k k128-64k memory trace"

dirty=$(git -C "$S" status --porcelain -- $SOURCES)
[ -z "$dirty" ] || { echo "fw/rtl/refresh.sh: $S has changes to the copied sources against its HEAD:"; echo "$dirty"; exit 1; }
commit=$(git -C "$S" rev-parse HEAD)
[ -f "$S/build/sort/sort.txt" ] || { echo "fw/rtl/refresh.sh: no $S/build/sort/sort.txt: run 'mise run sort-images' there"; exit 1; }

rm -rf "$HERE/src" "$HERE/sort"
git -C "$S" ls-files -- $SOURCES | while read -r f; do
  mkdir -p "$HERE/src/$(dirname "$f")" && cp "$S/$f" "$HERE/src/$f"
done
newest=$(git -C "$S" ls-files -- $SOURCES | while read -r f; do stat -c %Y "$S/$f"; done | sort -n | tail -1)

image() {   # image FILE: one built file of the checkout's build/sort/ into sort/
  [ -f "$S/build/sort/$1" ] || { echo "fw/rtl/refresh.sh: no $S/build/sort/$1"; exit 1; }
  mkdir -p "$HERE/sort/$(dirname "$1")" && cp "$S/build/sort/$1" "$HERE/sort/$1" && chmod 644 "$HERE/sort/$1"
  [ "$(stat -c %Y "$S/build/sort/$1")" -ge "$newest" ] || echo "NOTE: build/sort/$1 is older than the newest copied source: build again?"
}
for run in $RUNS; do image "$run/master.elf"; done
for f in memory-tile/memory.elf segment/replay.elf segment/tracer.elf sort.txt; do image "$f"; done

{
  echo "# fw/rtl's hash list (sha256, path in fw/rtl/), written by fw/rtl/refresh.sh; fw/check_images.sh rtl checks it."
  echo "# Snarky One $commit"
  (cd "$HERE" && find src sort -type f | sort | xargs sha256sum)
} > "$HERE/MANIFEST"
echo "fw/rtl/refresh.sh: $(grep -vc '^#' "$HERE/MANIFEST") files from Snarky One $commit"
