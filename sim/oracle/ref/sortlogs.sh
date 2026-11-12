#!/bin/bash
# sortlogs.sh -- v1.3's sort and sweep steps (sim/ref/meshsim/run_tests.sh), run once in ref, their logs committed as
# the C++ suites' fixtures (WORKER24, P6). Needs a JDK. sim/ref is only read: its sources are copied into a scratch
# directory with the C++ tree's committed images (tests/golden-m2/images for the sort, fw/images for the rest: both
# byte-identical to v1.3's own build, oracle/fw_crosscheck.sh), compiled as run_tests.sh compiles them, and each step
# run there exactly as run_tests.sh runs it (the same arguments, `2>&1`, the same heap).
#
#   sortlogs.sh --fixtures [STEP...]   (re)write tests/sort/ref/<step>.log (and sweep-quick.csv)
#   sortlogs.sh --check [STEP...]      regenerate and compare with the committed files
# STEP: sort-p1 sort sort-shuf sort-bt-kv32 sort-bt-k128 sweep-quick (default: all), or H7's h7-uniform-kv32,
# h7-reverse-k128 (SortTest's profiles on one run each), sweep-full (Sweep's full mode); not v1.3 steps. The BT steps' key set
# (keys-16384/trace10-10.keys, pipeline/keys' KeyGen) is checked against tests/sort/ref/KEYS.sha256 when it lists it.
set -u
export LC_ALL=C
SIM=$(cd "$(dirname "$0")/../.." && pwd); REF=$SIM/ref/meshsim; FIX=$SIM/tests/sort/ref
MODE=${1:-}; shift || true
case "$MODE" in --fixtures|--check) ;; *) echo "usage: sortlogs.sh --fixtures|--check [STEP...]"; exit 2;; esac
STEPS=${*:-sort-p1 sort sort-shuf sort-bt-kv32 sort-bt-k128 sweep-quick}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT

# 1. the scratch copy of ref/meshsim (sources only), the images installed, compiled as run_tests.sh does
mkdir -p "$W/m"
(cd "$REF" && tar cf - --exclude=./build --exclude=./sw/build --exclude=./sw/ext/sort/build --exclude=./results .) | (cd "$W/m" && tar xf -)
mkdir -p "$W/m/sw/build" "$W/m/sw/ext/sort/build" "$W/m/build" "$W/m/results"
cp "$SIM"/fw/images/*.bin "$SIM"/fw/images/*.sym "$W/m/sw/build/"
for f in "$SIM"/tests/golden-m2/images/*.bin "$SIM"/tests/golden-m2/images/*.sym; do
  case $(basename "$f") in sort.*) cp "$f" "$W/m/sw/ext/sort/build/";; *) cp "$f" "$W/m/sw/build/";; esac
done
(cd "$W/m" && javac -nowarn -d build src/*.java) || exit 2
grep -v '^#' "$W/m/test/PENDING" | awk 'NF{print "test/"$1}' > "$W/pending.txt"
(cd "$W/m" && javac -nowarn -d build -cp build $(ls test/*.java | grep -v -x -F -f "$W/pending.txt")) 2>&1 | grep -v '^Note:'
[ -f "$W/m/build/SortTest.class" ] || { echo "sortlogs: ref did not compile"; exit 2; }
J="java ${JS:--Xmx3g} -cp build"

# 2. the steps, as run_tests.sh runs them
OUT=$W/out; mkdir -p "$OUT"
step() { local log=$1; shift; echo "== $log"; (cd "$W/m" && "$@" > "$OUT/$log.log" 2>&1); tail -1 "$OUT/$log.log"; }
for s in $STEPS; do
  case $s in
    sort-p1)   step sort-p1   $J SortTest -1 p1;;
    sort)      step sort      $J SortTest -1 sort;;
    sort-shuf) step sort-shuf $J SortTest 4 sort;;
    sort-bt-kv32|sort-bt-k128)
      if [ ! -f "$W/m/build/keys-16384/trace10-10.keys" ]; then
        javac -nowarn -d "$W/kg" "$SIM/pipeline/keys/KeyGen.java" || exit 2
        (cd "$W/m" && java -Xmx2g -cp "$W/kg" KeyGen build/keys-16384 --n 16384 --only trace10-10 > /dev/null) || exit 2
        h=$(sha256sum < "$W/m/build/keys-16384/trace10-10.keys" | cut -c1-64)
        want=$(awk '$2 == "keys-16384/trace10-10.keys" {print $1}' "$FIX/KEYS.sha256" 2>/dev/null)
        if [ -n "$want" ] && [ "$h" != "$want" ]; then echo "sortlogs: keys-16384/trace10-10.keys differs from KEYS.sha256"; exit 2; fi
        echo "$h  keys-16384/trace10-10.keys" > "$OUT/KEYS.sha256"
      fi
      f=${s#sort-bt-}
      step $s $J SortTest -1 run label=bt16k-$f keys=build/keys-16384/trace10-10.keys fmt=$f ready=8 force=bt;;
    sweep-quick) step sweep-quick $J Sweep quick; cp "$W/m/results/sweep-quick.csv" "$OUT/";;
    # not a run_tests.sh step: v1.3's `java Sweep full` (about 25 minutes), for the C++ sweep's full mode
    sweep-full) step sweep-full $J Sweep full; cp "$W/m/results/sweep-full.csv" "$OUT/";;
    # H7's references (not v1.3 steps): SortTest's profiles (-Dsort.profile, sort.prof, sort.watch) on one run each
    h7-uniform-kv32) step h7-uniform-kv32 $J -Dsort.profile -Dsort.prof=010045ac-0100556c -Dsort.watch=split_get_cuts,step_loop,main SortTest -1 run kind=uniform n=2000 fmt=kv32;;
    h7-reverse-k128) step h7-reverse-k128 $J -Dsort.profile -Dsort.prof=01002304-0100261c -Dsort.watch=th_build,bt_sweep,th_hop2 SortTest -1 run kind=reverse n=2000 fmt=k128;;
    *) echo "sortlogs: no step $s"; exit 2;;
  esac
done

# 3. fixtures or the check
if [ "$MODE" = --fixtures ]; then
  mkdir -p "$FIX"; cp "$OUT"/* "$FIX/"; echo "wrote $(ls "$OUT" | tr '\n' ' ')into $FIX"
else
  bad=0
  for f in $(ls "$OUT"); do cmp -s "$OUT/$f" "$FIX/$f" && echo "SAME $f" || { echo "DIFF $f"; bad=1; }; done
  exit $bad
fi
