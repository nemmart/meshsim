#!/bin/bash
# regen.sh [--check] -- tile 0's G1 boundary in ref for regs and wfi (sim/tests/ref/G1Trace.java: eject arrivals, GO
# scheduling, the send hold's takes), replayed by tests/test_g1_replay.cpp against golden-m2. Builds sim/ref and W7's
# drivers in a scratch copy exactly as golden-m2/regen.sh does (its images). --check: compare with the committed files.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); SIM=$(cd "$HERE/../.." && pwd); G=$SIM/tests/golden-m2
CHECK=0; [ "${1:-}" = --check ] && CHECK=1
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
mkdir -p "$W/m" "$W/cls"
(cd "$SIM/ref/meshsim" && tar cf - --exclude=./build --exclude=./sw/build --exclude=./sw/ext/sort/build --exclude=./results .) | (cd "$W/m" && tar xf -)
mkdir -p "$W/m/sw/build" "$W/m/sw/ext/sort/build"
for f in "$G"/images/*.bin "$G"/images/*.sym; do
  case $(basename "$f") in sort.*) cp "$f" "$W/m/sw/ext/sort/build/";; *) cp "$f" "$W/m/sw/build/";; esac
done
javac -nowarn -d "$W/cls" "$W/m"/src/*.java 2>&1 | grep -v '^Note' || true
grep -v '^#' "$W/m/test/PENDING" | awk 'NF{print "'"$W/m"'/test/"$1}' > "$W/pending.txt"
javac -nowarn -d "$W/cls" -cp "$W/cls" $(ls "$W/m"/test/*.java | grep -v -x -F -f "$W/pending.txt") 2>&1 | grep -v '^Note' || true
javac -nowarn -d "$W/cls" -cp "$W/cls" "$SIM"/tests/ref/{TraceRun,StateFp,FpHooks,M2Prog,G1Trace}.java 2>&1 | grep -v '^Note' || true
RC=0
for p in regs wfi; do
  OUT=$HERE/$p.g1; [ $CHECK = 1 ] && OUT=$W/$p.g1
  (cd "$W/m" && java -cp "$W/cls" G1Trace "$p" 0 "$OUT") || { echo "$p: G1Trace failed"; RC=1; continue; }
  if [ $CHECK = 1 ]; then cmp -s "$OUT" "$HERE/$p.g1" && echo "$p: SAME" || { echo "$p: DIFF"; RC=1; }; else echo "$p: written"; fi
done
exit $RC
