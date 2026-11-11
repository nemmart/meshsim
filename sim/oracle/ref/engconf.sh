#!/bin/bash
# engconf.sh -- the engines' conformance, ref's side (WORKER12; oracle/ref/EngConf.java, tests/test_engconf.cpp).
# Needs a JDK. Nothing in sim/ref is written: ref and the driver are compiled into a scratch directory.
#
#   engconf.sh --fixtures      (re)write tests/engconf/: per configuration below, NAME.script.gz and NAME.obs.gz
#                              (3,000 cycles), each opening with its provenance (tile, seed, cycles, L, the ref commit)
#   engconf.sh --check         regenerate into a scratch directory and compare with the committed fixtures
#   engconf.sh CYCLES [OUT]    longer runs (every configuration, seeds 1-6) into OUT, for ENGCONF_LONG=OUT test_engconf
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/engconf"
CONFS=("5 1" "0 2" "27 3" "63 4")      # tile seed (L = 1 + seed mod 3)
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
build() {
  rm -rf "$1"; mkdir -p "$1"
  javac -nowarn -d "$1" "$SIM"/ref/meshsim/src/*.java 2>&1 | grep -v '^Note:'
  javac -nowarn -d "$1" -cp "$1" "$SIM"/oracle/ref/StateFp.java "$SIM"/oracle/ref/EngConf.java 2>&1 | grep -v '^Note:'
  [ -f "$1/EngConf.class" ] || { echo "engconf: the driver did not compile"; exit 2; }
}
one() {   # B DIR TILE SEED CYCLES NAME: the script and ref's observations, gzipped with their provenance
  local B=$1 d=$2 tile=$3 seed=$4 cyc=$5 name=$6
  java -cp "$B" EngConf gen "$tile" "$seed" "$cyc" > "$B/$name.script" || exit 1
  java -cp "$B" EngConf run "$B/$name.script" > "$B/$name.body" || { echo "FAIL ref raised: $name"; exit 1; }
  { sed -n '1,3p' "$B/$name.script"; echo "# ref $REFCOMMIT"; sed '1,3d' "$B/$name.script"; } | gzip -n -9 > "$d/$name.script.gz"
  { echo "# engconf observations v1: ref's side, EngConf run"; sed -n '2,3p' "$B/$name.script"; echo "# ref $REFCOMMIT"; cat "$B/$name.body"; } | gzip -n -9 > "$d/$name.obs.gz"
}
fixtures() {
  local d=$1 B c; B=$(mktemp -d); build "$B"; mkdir -p "$d"
  for c in "${CONFS[@]}"; do set -- $c; one "$B" "$d" "$1" "$2" 3000 "t$1-s$2"; done
  rm -rf "$B"
}
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "engconf: ${#CONFS[@]} fixtures written to $FIX (ref $REFCOMMIT)"; exit 0 ;;
  --check)
    T=$(mktemp -d); fixtures "$T"; fail=0
    for c in "${CONFS[@]}"; do set -- $c; n="t$1-s$2"
      for f in script obs; do cmp -s <(gzip -dc "$FIX/$n.$f.gz") <(gzip -dc "$T/$n.$f.gz") || { echo "FAIL differs from ref: $n.$f"; fail=1; }; done
    done
    rm -rf "$T"; [ $fail = 0 ] && echo "engconf --check: the fixtures equal ref's regeneration (ref $REFCOMMIT)"; exit $fail ;;
esac
CYC=${1:?usage: engconf.sh --fixtures | --check | CYCLES [OUT]}; OUT=$(mkdir -p "${2:-/tmp/engconf}" && cd "${2:-/tmp/engconf}" && pwd)
B="$OUT/build"; build "$B"
for c in "${CONFS[@]}"; do set -- $c; for seed in 1 2 3 4 5 6; do one "$B" "$OUT" "$1" "$seed" "$CYC" "t$1-s$seed"; done; done
echo "engconf: ${#CONFS[@]} tiles x 6 seeds x $CYC cycles in $OUT"
