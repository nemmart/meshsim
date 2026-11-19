#!/bin/sh
# Nothing old broke (WORKER1): the segments command's file from this gold byte-identical to v1.36 gold's (built into a
# scratch directory from v1.36/pipeline/gold/src, which is never touched), on every gold guest at two cut sizes, in both
# formats, and on any extra runs given as "<program dir> <input dir> <cuts file>" triples; then test/guests/check_guests.sh.
#   test/frames/old_unchanged.sh [<program dir> <input dir> <cuts file>]...
set -e
cd "$(dirname "$0")/../.."
OLD=/tmp/gold-v136-classes; rm -rf "$OLD"; mkdir -p "$OLD"
javac -d "$OLD" -nowarn $(find ../../../../v1.36/pipeline/gold/src -name '*.java') 2>&1 | grep -v "^Note:\|Picked up" || true
W=/tmp/gold-old-unchanged; rm -rf "$W"; mkdir -p "$W/empty"
fail=0; n=0
same() {  # same <name> <program dir> <input dir> [options]: the segments file from both golds, and their output
  name=$1; prog=$2; in=$3; shift 3
  rm -f "$W/x.seg"; java -cp "$OLD" gold.Main segments "$prog" "$in" "$W/x.seg" "$@" 2>&1 | sed -e 's/; [0-9.]* s$//' -e '/^[[:space:]]*at gold./d' > "$W/old.out" || true
  [ -f "$W/x.seg" ] && mv "$W/x.seg" "$W/old.seg" || : > "$W/old.seg"
  java -cp build gold.Main segments "$prog" "$in" "$W/x.seg" "$@" 2>&1 | sed -e 's/; [0-9.]* s$//' -e '/^[[:space:]]*at gold./d' > "$W/new.out" || true
  [ -f "$W/x.seg" ] || : > "$W/x.seg"
  n=$((n + 1))
  if cmp -s "$W/old.seg" "$W/x.seg" && cmp -s "$W/old.out" "$W/new.out"; then :; else
    echo "$name: DIFFERS"; diff "$W/old.out" "$W/new.out" | head -4; fail=$((fail + 1)); fi
}
for g in $(ls -d test/guests/*/ | xargs -n1 basename); do
  for r in 1 7; do
    for fmt in v1.3 v1.2; do
      same "$g --segment-rows=$r --format=$fmt" "test/guests/$g" "$W/empty" --segment-rows=$r --format=$fmt
    done
  done
done
while [ $# -ge 3 ]; do
  same "$1 $2" "$1" "$2" --cuts-file="$3"
  same "$1 $2 (2^22 rows a segment)" "$1" "$2"
  shift 3
done
rm -rf "$W" "$OLD"
echo "segments against v1.36 gold: $n runs, $([ $fail = 0 ] && echo "all byte-identical" || echo "$fail DIFFER")"
sh test/guests/check_guests.sh
[ $fail = 0 ]
