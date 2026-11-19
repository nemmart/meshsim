#!/bin/sh
# The frames are as they were (WORKER2): the frames command's file and its side file from this gold byte-identical to
# another gold's, built into a scratch directory from its sources: on every guest that runs to its end, four times
# (as test/frames/guests.sh writes them), and on any extra runs given as "<program dir> <input dir> <cuts file|->"
# triples, in workloads.sh's three configurations. What each command prints is compared too, without its time.
#   test/packets/frames_unchanged.sh <the other gold's directory> [<program dir> <input dir> <cuts file|->]...
# The other gold is main's before WORKER2's branch: git archive <commit> updater_wip/pipeline/gold | tar -x -C <dir>
set -u
cd "$(dirname "$0")/../.."
REF=$1; shift
OLD=/tmp/gold-ref-classes; rm -rf "$OLD"; mkdir -p "$OLD"
javac -d "$OLD" -nowarn $(find "$REF/src" -name '*.java') 2>&1 | grep -v "^Note:\|Picked up" || true
W=/tmp/gold-frames-unchanged; rm -rf "$W"; mkdir -p "$W/empty"
J="java -Xmx5g"
fail=0; n=0
same() {  # same <name> <program dir> <input dir> [options]
  name=$1; prog=$2; in=$3; shift 3
  rm -f "$W"/old.* "$W"/new.*
  $J -cp "$OLD" gold.Main frames "$prog" "$in" "$W/old.frames" "$@" 2>&1 | sed -e 's/; [0-9.]* s$//' -e 's/old\.frames/x.frames/g' > "$W/old.out"
  $J -cp build gold.Main frames "$prog" "$in" "$W/new.frames" "$@" 2>&1 | sed -e 's/; [0-9.]* s$//' -e 's/new\.frames/x.frames/g' > "$W/new.out"
  n=$((n + 1))
  if [ -s "$W/old.frames" ] && cmp -s "$W/old.frames" "$W/new.frames" && cmp -s "$W/old.frames.json" "$W/new.frames.json" \
      && cmp -s "$W/old.out" "$W/new.out"; then
    case $prog in test/guests/*) ;; *) echo "$name: $(sha256sum < "$W/new.frames" | cut -d' ' -f1), $(wc -c < "$W/new.frames") bytes: identical";; esac
  else
    echo "$name: DIFFERS"; diff "$W/old.out" "$W/new.out" | head -4; fail=$((fail + 1))
  fi
}
guest() {  # guest <name> <segment rows> <base> <compression> <N>
  same "$1, base $3, compression $4, N $5" "test/guests/$1" "$W/empty" --segment-rows=$2 --base=$3 --compression=$4 --asics=$5
}
for g in $(grep "rows, run hash" test/guests/expected.txt | cut -d: -f1); do
  rows=$(java -cp build gold.Main run "test/guests/$g" "$W/empty" | sed 's/ rows.*//')
  R=$(( (rows + 59) / 60 )); [ $R -lt 1 ] && R=1
  for v in "none none 128" "static lru 64" "static auto 128" "none auto 256"; do guest "$g" $R $v; done
done
guests=$n
workload() {  # workload <program dir> <input dir> <cuts option> <base> <compression> <N>
  same "$2, base $4, compression $5, N $6" "$1" "$2" $3 --base=$4 --compression=$5 --asics=$6
}
while [ $# -ge 3 ]; do
  C=""; [ "$3" != - ] && C="--cuts-file=$3"
  for v in "none none 128" "static auto 128" "static lru 256"; do workload "$1" "$2" "$C" $v; done
  shift 3
done
rm -rf "$W" "$OLD"
echo "frames against the other gold's: $n runs ($guests on the guests), $([ $fail = 0 ] && echo "all byte-identical" || echo "$fail DIFFER")"
[ $fail = 0 ]
