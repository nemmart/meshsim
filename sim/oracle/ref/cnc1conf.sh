#!/bin/bash
# cnc1conf.sh -- C&C 1's peripheral's conformance, ref's side (WORKER16; oracle/ref/Cnc1Conf.java,
# tests/test_cnc1_periph.cpp). Needs a JDK. Nothing in sim/ref is written: ref and the driver compile into scratch.
#
#   cnc1conf.sh --fixtures      (re)write tests/cnc1conf/: per seed, NAME.script.gz and NAME.obs.gz (20,000 cycles)
#   cnc1conf.sh --check         regenerate into a scratch directory and compare with the committed fixtures
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/cnc1conf"
SEEDS=(1 2 3)
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
build() {
  rm -rf "$1"; mkdir -p "$1"
  javac -nowarn -d "$1" "$SIM"/ref/meshsim/src/*.java 2>&1 | grep -v '^Note:'
  javac -nowarn -d "$1" -cp "$1" "$SIM"/oracle/ref/Cnc1Conf.java 2>&1 | grep -v '^Note:'
  [ -f "$1/Cnc1Conf.class" ] || { echo "cnc1conf: the driver did not compile"; exit 2; }
}
fixtures() {
  local d=$1 B s; B=$(mktemp -d); build "$B"; mkdir -p "$d"
  for s in "${SEEDS[@]}"; do
    java -cp "$B" Cnc1Conf gen "$s" 20000 > "$B/s$s.script" || exit 1
    java -cp "$B" Cnc1Conf run "$B/s$s.script" > "$B/s$s.body" || { echo "FAIL ref raised: s$s"; exit 1; }
    { sed -n '1,2p' "$B/s$s.script"; echo "# ref $REFCOMMIT"; sed '1,2d' "$B/s$s.script"; } | gzip -n -9 > "$d/s$s.script.gz"
    { echo "# cnc1conf observations v1: ref's side, Cnc1Conf run"; sed -n '2p' "$B/s$s.script"; echo "# ref $REFCOMMIT"; cat "$B/s$s.body"; } | gzip -n -9 > "$d/s$s.obs.gz"
  done
  rm -rf "$B"
}
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "cnc1conf: ${#SEEDS[@]} fixtures written to $FIX (ref $REFCOMMIT)" ;;
  --check)
    T=$(mktemp -d); fixtures "$T"; fail=0
    for s in "${SEEDS[@]}"; do for f in script obs; do
      cmp -s <(gzip -dc "$FIX/s$s.$f.gz") <(gzip -dc "$T/s$s.$f.gz") || { echo "FAIL differs from ref: s$s.$f"; fail=1; }
    done; done
    rm -rf "$T"; [ $fail = 0 ] && echo "cnc1conf --check: the fixtures equal ref's regeneration (ref $REFCOMMIT)"; exit $fail ;;
  *) echo "usage: cnc1conf.sh --fixtures | --check"; exit 2 ;;
esac
