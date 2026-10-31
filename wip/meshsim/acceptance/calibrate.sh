#!/usr/bin/env bash
# calibrate.sh -- the acceptance kit's speed measurement (meshsim WORKER11): synthetic 65,536-row windows
# (SyntheticDelivery, from the universal tables) through run_shard.sh at 1, 8 and 16 JVMs, Stage A and Stage B.
# Gives rows per second on this machine, the basis of the runner-hours estimate. ./calibrate.sh OUTDIR
set -u
HERE=$(cd "$(dirname "$0")" && pwd); OUT=${1:?usage: calibrate.sh OUTDIR}; mkdir -p "$OUT"
GEN="java -Xmx2g -Dreplay.tables=$HERE/tables/replayer -cp $HERE/meshsim.jar SyntheticDelivery {out} --program {program} --first {first} --windows {count} --seed 11"
RC=0
for spec in "1 8" "8 32" "16 64"; do
  set -- $spec
  echo "== $1 JVMs, $2 windows"
  "$HERE/run_shard.sh" -p synthetic -f 0 -n "$2" -b "$2" -j "$1" -w "$GEN" "$OUT/j$1" | tail -6 || RC=1
done
exit $RC
