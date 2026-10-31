#!/usr/bin/env bash
# calibrate.sh -- the acceptance kit's speed measurement (meshsim WORKER11): a synthetic delivery of 65,536-row windows
# (SyntheticDelivery, from the universal tables), replayed by run_shard.sh at 1, 8 and 16 JVMs, Stage A and Stage B.
# Gives rows per JVM-second and the parallel scaling on this machine: the basis of the runner-hours estimate. Stage B runs
# on tile 0 alone (B) and, for the 8-JVM run, also on the full chip (F): their cycle counts must agree window by window.
#   ./calibrate.sh OUTDIR [SCRATCH]
set -u
HERE=$(cd "$(dirname "$0")" && pwd); OUT=${1:?usage: calibrate.sh OUTDIR [SCRATCH]}; SCR=${2:-/tmp}/w11-calib.$$
mkdir -p "$OUT"; trap 'rm -rf "$SCR"' EXIT
T0=$(date +%s)
java -Xmx2g -Dreplay.tables="$HERE/tables/replayer" -cp "$HERE/meshsim.jar" SyntheticDelivery "$SCR" --program synthetic --windows 64 --seed 11 > "$OUT/generate.log" 2>&1 || { cat "$OUT/generate.log"; exit 1; }
echo "generated 64 windows in $(( $(date +%s) - T0 )) s; $(du -sh "$SCR" | cut -f1)"
MS=$(sha256sum "$SCR/manifest.json" | cut -d' ' -f1)
RC=0
for spec in "1 0 8 AB" "8 8 32 ABF" "16 0 64 AB"; do
  set -- $spec
  echo "== $1 JVMs, windows $2..$(($2 + $3 - 1)), stages $4"
  "$HERE/run_shard.sh" -d "$SCR" -p synthetic -f "$2" -n "$3" -j "$1" -s "$4" -m "$MS" "$OUT/j$1" | tail -7 || RC=1
done
exit $RC
