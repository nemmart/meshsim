#!/bin/bash
# portconf.sh -- port conformance, ref's side (WORKER3; sim/src/PORTS.md section 10). Needs a JDK.
#
#   portconf.sh [CYCLES] [OUT]   the matrix: for each (kind, parameters, seed) below, generate the script, run it on ref's
#                                bus class, write OUT/NAME.script and OUT/NAME.obs; every run must finish without ref
#                                raising, and generating and driving twice must give identical files. CYCLES defaults to
#                                100000 (PLAN-M1: at least 10^5 per depth and latency); OUT/SUMS lists the sha256s.
#   portconf.sh --fixtures       (re)write the committed fixtures, sim/tests/portconf/NAME.{script,obs}.gz: seed 1 of each
#                                configuration, 2,000 cycles, each file opening with its provenance (configuration, seed,
#                                cycles, the ref commit it was made from). make check's C++ test reads them without Java.
#   portconf.sh --check          regenerate the fixtures from ref and compare them with the committed ones (bodies and
#                                provenance), so they can't drift from ref unnoticed.
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/portconf"
MATRIX=(
  "link 1" "link 2" "link 3" "link 8"
  "fifo 1" "fifo 2" "fifo 4" "fifo 16"
  "credit 1" "credit 2" "credit 4" "credit 16"
  "hold" "reg"
  "rlink 1 2" "rlink 2 4" "rlink 3 8"
)
build() {   # DIR: ref (unchanged) and the two classes
  rm -rf "$1"; mkdir -p "$1"
  javac -d "$1" "$SIM"/ref/meshsim/src/*.java || exit 2
  javac -d "$1" -cp "$1" "$SIM"/tests/ref/PortConfGen.java "$SIM"/tests/ref/PortConfDriver.java || exit 2
}
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
fixtures() {   # DIR: write the fixtures into DIR (gzip -n: no name or time, so the bytes are reproducible)
  local d=$1 B m name; B=$(mktemp -d); build "$B"; mkdir -p "$d"
  for m in "${MATRIX[@]}"; do
    name=$(echo "$m" | tr ' ' '-')
    java -cp "$B" PortConfGen $m 1 2000 > "$B/$name.script" || exit 1
    java -cp "$B" PortConfDriver "$B/$name.script" > "$B/$name.body" || { echo "FAIL ref raised: $name"; exit 1; }
    { echo "# portconf observations v1 (sim/src/PORTS.md 10): ref's side, PortConfDriver"
      echo "# kind $m"; echo "# seed 1 cycles 2000"; echo "# ref $REFCOMMIT"; cat "$B/$name.body"; } | gzip -n -9 > "$d/$name.obs.gz"
    { sed -n '1,3p' "$B/$name.script"; echo "# ref $REFCOMMIT"; sed '1,3d' "$B/$name.script"; } | gzip -n -9 > "$d/$name.script.gz"
  done
  rm -rf "$B"
}
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "portconf: ${#MATRIX[@]} fixtures written to $FIX (ref $REFCOMMIT)"; exit 0 ;;
  --check)
    T=$(mktemp -d); fixtures "$T"; fail=0
    for m in "${MATRIX[@]}"; do
      name=$(echo "$m" | tr ' ' '-')
      for f in script obs; do
        if [ ! -e "$FIX/$name.$f.gz" ]; then echo "FAIL missing: $name.$f.gz"; fail=1; continue; fi
        cmp -s <(gzip -dc "$FIX/$name.$f.gz") <(gzip -dc "$T/$name.$f.gz") || { echo "FAIL differs from ref: $name.$f"; fail=1; }
      done
    done
    rm -rf "$T"
    [ $fail = 0 ] && echo "portconf --check: ${#MATRIX[@]} fixtures equal to ref's regeneration (ref $REFCOMMIT)"
    exit $fail ;;
esac
CYC=${1:-100000}; OUT=$(mkdir -p "${2:-/tmp/portconf}" && cd "${2:-/tmp/portconf}" && pwd)
B="$OUT/build"; build "$B"
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
