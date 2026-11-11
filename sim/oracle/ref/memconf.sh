#!/bin/bash
# memconf.sh -- the memory path's conformance, ref's side (WORKER11; oracle/ref/MemConf.java, tests/test_memconf.cpp).
# Needs a JDK. Nothing in sim/ref is written: ref and the driver are compiled into a scratch directory.
#
#   memconf.sh --fixtures      (re)write tests/memconf/: per configuration below, NAME.script.gz and NAME.obs.gz (3,000
#                              cycles), each opening with its provenance (the ref commit)
#   memconf.sh --check         regenerate into a scratch directory and compare with the committed fixtures
#   memconf.sh CYCLES [OUT]    longer runs (every configuration, seeds 1-3) into OUT, for MEMCONF_LONG
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/memconf"
CONFS=("5 1" "0 2" "63 3" "27 4 comb" "9 5 comb")      # tile seed [comb: write combining on]
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
build() {
  rm -rf "$1"; mkdir -p "$1"
  javac -nowarn -d "$1" "$SIM"/ref/meshsim/src/*.java 2>&1 | grep -v '^Note:'
  javac -nowarn -d "$1" -cp "$1" "$SIM"/oracle/ref/StateFp.java "$SIM"/oracle/ref/MemConf.java 2>&1 | grep -v '^Note:'
  [ -f "$1/MemConf.class" ] || { echo "memconf: the driver did not compile"; exit 2; }
}
one() {   # B DIR TILE SEED CYCLES NAME [comb]
  local B=$1 d=$2 tile=$3 seed=$4 cyc=$5 name=$6 mode=${7:-}
  java -cp "$B" MemConf gen "$tile" "$seed" "$cyc" $mode > "$B/$name.script" || exit 1
  (cd "$SIM/ref/meshsim" && java -cp "$B" MemConf run "$B/$name.script") > "$B/$name.body" || { echo "FAIL ref raised: $name"; exit 1; }
  { sed -n '1,4p' "$B/$name.script"; echo "# ref $REFCOMMIT"; sed '1,4d' "$B/$name.script"; } | gzip -n -9 > "$d/$name.script.gz"
  { echo "# memconf observations v1: ref's side, MemConf run"; sed -n '2,4p' "$B/$name.script"; echo "# ref $REFCOMMIT"; cat "$B/$name.body"; } | gzip -n -9 > "$d/$name.obs.gz"
}
fixtures() {
  local d=$1 B c; B=$(mktemp -d); build "$B"; mkdir -p "$d"
  for c in "${CONFS[@]}"; do set -- $c; one "$B" "$d" "$1" "$2" 3000 "t$1-s$2${3:+-$3}" ${3:-}; done
  rm -rf "$B"
}
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "memconf: ${#CONFS[@]} fixtures written to $FIX (ref $REFCOMMIT)"; exit 0 ;;
  --check)
    T=$(mktemp -d); fixtures "$T"; fail=0
    for c in "${CONFS[@]}"; do set -- $c; n="t$1-s$2${3:+-$3}"
      for f in script obs; do cmp -s <(gzip -dc "$FIX/$n.$f.gz") <(gzip -dc "$T/$n.$f.gz") || { echo "FAIL differs from ref: $n.$f"; fail=1; }; done
    done
    rm -rf "$T"; [ $fail = 0 ] && echo "memconf --check: the fixtures equal ref's regeneration (ref $REFCOMMIT)"; exit $fail ;;
esac
CYC=${1:?usage: memconf.sh --fixtures | --check | CYCLES [OUT]}; OUT=$(mkdir -p "${2:-/tmp/memconf}" && cd "${2:-/tmp/memconf}" && pwd)
B="$OUT/build"; build "$B"
for c in "${CONFS[@]}"; do set -- $c; for seed in 1 2 3; do one "$B" "$OUT" "$1" "$seed" "$CYC" "t$1-s$seed${3:+-$3}" ${3:-}; done; done
echo "memconf: ${#CONFS[@]} tiles x 3 seeds x $CYC cycles in $OUT"
