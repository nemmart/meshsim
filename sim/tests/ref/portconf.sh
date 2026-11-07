#!/bin/bash
# portconf.sh [CYCLES] [OUT] -- port conformance, ref's side (WORKER3; sim/src/PORTS.md section 10).
# Builds ref (sim/ref/meshsim/src, unchanged) and PortConfGen/PortConfDriver into OUT/build, then for each (kind,
# parameters, seed) of the matrix below: generates the script, runs it on ref's bus class, and writes OUT/NAME.script and
# OUT/NAME.obs. Every run must finish without ref raising (a legal script never does), and generating and driving twice
# must give identical files (determinism). OUT/SUMS lists each observation file's sha256. CYCLES defaults to 100000
# (PLAN-M1: at least 10^5 per depth and latency). Needs a JDK. The C++ side (stage 2) runs the same scripts and diffs.
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
CYC=${1:-100000}; OUT=$(mkdir -p "${2:-/tmp/portconf}" && cd "${2:-/tmp/portconf}" && pwd)
B="$OUT/build"; rm -rf "$B"; mkdir -p "$B"
javac -d "$B" "$SIM"/ref/meshsim/src/*.java || exit 2
javac -d "$B" -cp "$B" "$SIM"/tests/ref/PortConfGen.java "$SIM"/tests/ref/PortConfDriver.java || exit 2
MATRIX=(
  "link 1" "link 2" "link 3" "link 8"
  "fifo 1" "fifo 2" "fifo 4" "fifo 16"
  "credit 1" "credit 2" "credit 4" "credit 16"
  "hold" "reg"
  "rlink 1 2" "rlink 2 4" "rlink 3 8"
)
fail=0; : > "$OUT/SUMS"
for m in "${MATRIX[@]}"; do
  for seed in 1 2 3; do
    name=$(echo "$m" | tr ' ' '-')-s$seed
    java -cp "$B" PortConfGen $m $seed "$CYC" > "$OUT/$name.script" || { echo "FAIL gen $name"; fail=1; continue; }
    if ! java -cp "$B" PortConfDriver "$OUT/$name.script" > "$OUT/$name.obs"; then echo "FAIL ref raised: $name"; fail=1; continue; fi
    java -cp "$B" PortConfGen $m $seed "$CYC" | cmp -s - "$OUT/$name.script" || { echo "FAIL gen not deterministic: $name"; fail=1; }
    java -cp "$B" PortConfDriver "$OUT/$name.script" | cmp -s - "$OUT/$name.obs" || { echo "FAIL driver not deterministic: $name"; fail=1; }
    (cd "$OUT" && sha256sum "$name.obs") >> "$OUT/SUMS"
  done
done
[ $fail = 0 ] && echo "portconf: ${#MATRIX[@]} configurations x 3 seeds x $CYC cycles, all ran, deterministic ($OUT)"
exit $fail
