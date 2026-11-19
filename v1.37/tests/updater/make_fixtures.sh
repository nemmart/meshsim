#!/bin/bash
# make_fixtures.sh: the Updater's and the master tile's fixtures (v1.37), made by gold from the repository's guests.
#   tests/updater/make_fixtures.sh [--check]
# Gold is pipeline/frames/gold (the frames' gold, copied from ../updater_wip; not pipeline/gold, which is v1.3's), compiled
# into a scratch folder: it needs a JDK (Java 21) and python3. For each fixture it writes
#   tests/updater/NAME.frames        gold's frame file (`frames`), N 64
#   tests/updater/NAME-sS.images.txt gold's reference for segment S (`frames-images`): each image's SHA-256 and pages
# The fixtures:
#   kin-raw    keccak256 with input, trusted and untrusted advice (frame 6 has three runs), no base, no compression
#   kin-lru    the same, a static base table, every update-word frame LRU-coded
#   jumps-base jumpmap's jumps guest, a static base table, each frame in its smaller form
#   divs       a loop of divisions and stores (make_div_guest.py): 51 segments with advice values and a page written
#   kin-big    keccak256 with 24 KB of untrusted advice, no base, no compression: a data image of more than two credits
#              of the segment queue (WORKER4), so that the master's allowance holds words back
# Each guest is cut into segments of 50 rows, so that most of the 64 segments have rows. --check makes them in scratch
# and requires the committed files equal: exit 1 on a difference.
set -eu
export LC_ALL=C
cd "$(dirname "$0")/../.."
CHECK=0; [ "${1:-}" = --check ] && CHECK=1
GOLD=pipeline/frames/gold
command -v javac > /dev/null || { echo "make_fixtures: no javac (a JDK, Java 21)"; exit 2; }
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
javac -d "$W/classes" $(find "$GOLD/src" -name '*.java') 2> "$W/javac.log" || { cat "$W/javac.log"; exit 2; }
J="java -cp $W/classes gold.Main"
mkdir -p "$W/kin" "$W/kin-input" "$W/kinbig" "$W/kinbig-input" "$W/empty" "$W/out"
cp "$GOLD/test/guests/keccak256/program.elf" "$W/kin/"
cp "$GOLD/test/guests/keccak256/program.elf" "$W/kinbig/"
cat > "$W/kinbig/program.json" <<'J'
{"mem_conf": {"max_untrusted_advice_size": 32768, "max_trusted_advice_size": 1024, "max_input_size": 300,
 "max_output_size": 8, "stack_size": 1048576, "memory_size": 1048576}}
J
cat > "$W/kin/program.json" <<'J'
{"mem_conf": {"max_untrusted_advice_size": 8192, "max_trusted_advice_size": 1024, "max_input_size": 300,
 "max_output_size": 8, "stack_size": 1048576, "memory_size": 1048576}}
J
python3 -c "
import random; r=random.Random(7)
open('$W/kin-input/input.bin','wb').write(bytes(r.randrange(256) for _ in range(299)))
open('$W/kin-input/trusted_advice.bin','wb').write(bytes(r.randrange(256) for _ in range(1001)))
open('$W/kin-input/untrusted_advice.bin','wb').write(bytes([0]*4096 + [r.randrange(256) for _ in range(4000)]))
r=random.Random(11)
open('$W/kinbig-input/input.bin','wb').write(bytes(r.randrange(256) for _ in range(299)))
open('$W/kinbig-input/trusted_advice.bin','wb').write(bytes(r.randrange(256) for _ in range(1001)))
open('$W/kinbig-input/untrusted_advice.bin','wb').write(bytes(r.randrange(256) for _ in range(24000)))"
python3 pipeline/jumpmap/test/make_jump_guest.py "$W" > /dev/null
python3 tests/updater/make_div_guest.py "$W" > /dev/null
OUT=tests/updater; [ $CHECK = 1 ] && OUT=$W/out
make_one() {   # NAME PROGRAM INPUT BASE COMPRESSION
  $J frames "$2" "$3" "$OUT/$1.frames" --segment-rows=50 --asics=64 --base="$4" --compression="$5" --verify 2>/dev/null | grep -q VERIFIED \
    || { echo "make_fixtures: gold did not verify $1"; exit 1; }
  rm -f "$OUT/$1.frames.json"
  for s in 0 1 20 60 63; do   # 1: the chain of two (v1.37 stage 6)
    $J frames-images "$OUT/$1.frames" $s 2>/dev/null | grep -v '^ok$' > "$OUT/$1-s$s.images.txt"
  done
}
make_one kin-raw "$W/kin" "$W/kin-input" none none
make_one kin-lru "$W/kin" "$W/kin-input" static lru
make_one jumps-base "$W/jumps" "$W/empty" static auto
make_one divs "$W/divs" "$W/empty" none auto
make_one kin-big "$W/kinbig" "$W/kinbig-input" none none
if [ $CHECK = 1 ]; then
  fail=0
  for f in "$W"/out/*; do cmp -s "$f" "tests/updater/$(basename "$f")" || { echo "make_fixtures: tests/updater/$(basename "$f") differs from gold's"; fail=1; }; done
  [ $fail = 0 ] && echo "make_fixtures: $(ls "$W/out" | wc -l) fixture files equal to gold's" || exit 1
else
  ls -la tests/updater | awk '{print $5, $9}'
fi
