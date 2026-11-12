#!/usr/bin/env bash
# calibrate.sh -- the acceptance kit's speed measurement (v1.35, WORKER25: v1.3's acceptance/calibrate.sh (meshsim
# WORKER11) on the C++ meshsim): a synthetic delivery of 65,536-row windows (synthetic_delivery, D42/D43: v1.3's
# SyntheticDelivery, 128 windows, seed 11, from the universal tables), replayed by run_shard.sh at 1, 4, 8 and 16
# processes. Gives rows per second and the parallel scaling on this machine: the basis of the runner-hours estimate.
# Stage B runs on tile 0 alone (B) and, for the 8-process run, also on the full chip (F): their cycle counts must agree
# window by window.
#   ./calibrate.sh OUTDIR [SCRATCH]
set -u
HERE=$(cd "$(dirname "$0")" && pwd); OUT=${1:?usage: calibrate.sh OUTDIR [SCRATCH]}; SCR=${2:-/tmp}/w25-calib.$$
BIN=$HERE/sim/build/clang-suite
[ -x "$BIN/synthetic_delivery" ] || { echo "the kit isn't built: run $HERE/build.sh"; exit 2; }
mkdir -p "$OUT"; trap 'rm -rf "$SCR"' EXIT
T0=$(date +%s)
"$BIN/synthetic_delivery" "$SCR" --program synthetic --windows 128 --seed 11 --tables "$HERE/sim/data/tables/replayer" > "$OUT/generate.log" 2>&1 || { cat "$OUT/generate.log"; exit 1; }
echo "generated 128 windows in $(( $(date +%s) - T0 )) s; $(du -sh "$SCR" | cut -f1)"
MS=$(sha256sum "$SCR/manifest.json" | cut -d' ' -f1)
RC=0
for spec in "1 0 8 AB" "4 8 32 AB" "8 40 32 ABF" "16 0 128 AB"; do
  set -- $spec
  echo "== $1 processes, windows $2..$(($2 + $3 - 1)), stages $4"
  "$HERE/run_shard.sh" -d "$SCR" -p synthetic -f "$2" -n "$3" -j "$1" -s "$4" -m "$MS" "$OUT/j$1" | tail -7 || RC=1
done
exit $RC
