#!/bin/bash
# routerconf.sh -- the router's conformance, ref's side (WORKER9; tests/ref/RouterConf.java, tests/test_router.cpp).
# Needs a JDK. Nothing in sim/ref is written: ref and the driver are compiled into a scratch directory.
#
#   routerconf.sh --fixtures      (re)write tests/routerconf/: tables.txt (every router's routing decisions, hashed per
#                                 tile) and, per configuration below, NAME.script.gz and NAME.obs.gz (2,000 cycles), each
#                                 opening with its provenance (tile, seed, cycles, the ref commit)
#   routerconf.sh --check         regenerate into a scratch directory and compare with the committed fixtures
#   routerconf.sh CYCLES [OUT]    longer runs (every configuration, seeds 1-3) into OUT, for the C++ side's long check
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/routerconf"
HOPS="$SIM/ref/meshsim/hops.txt"
CONFS=("0 1" "1 2" "5 3" "27 1" "63 2" "0 4 send" "5 5 send" "63 6 send" "0 7 link" "2 8 link" "64 1" "65 2" "66 3 dma")   # tile seed [send: the injection unit too; link: the C&C links (tiles 0-2); dma: C&C 3's DMA input]
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
build() {
  rm -rf "$1"; mkdir -p "$1"
  javac -nowarn -d "$1" "$SIM"/ref/meshsim/src/*.java 2>&1 | grep -v '^Note:'
  javac -nowarn -d "$1" -cp "$1" "$SIM"/tests/ref/StateFp.java "$SIM"/tests/ref/RouterConf.java 2>&1 | grep -v '^Note:'
  [ -f "$1/RouterConf.class" ] || { echo "routerconf: the driver did not compile"; exit 2; }
}
one() {   # B DIR TILE SEED CYCLES NAME [send]: the script and ref's observations, gzipped with their provenance
  local B=$1 d=$2 tile=$3 seed=$4 cyc=$5 name=$6 mode=${7:-}
  java -cp "$B" RouterConf gen "$tile" "$seed" "$cyc" $mode > "$B/$name.script" || exit 1
  java -cp "$B" RouterConf run "$HOPS" "$B/$name.script" > "$B/$name.body" || { echo "FAIL ref raised: $name"; exit 1; }
  { sed -n '1,3p' "$B/$name.script"; echo "# ref $REFCOMMIT"; sed '1,3d' "$B/$name.script"; } | gzip -n -9 > "$d/$name.script.gz"
  { echo "# routerconf observations v1: ref's side, RouterConf run"; sed -n '2,3p' "$B/$name.script"; echo "# ref $REFCOMMIT"; cat "$B/$name.body"; } | gzip -n -9 > "$d/$name.obs.gz"
}
fixtures() {
  local d=$1 B c; B=$(mktemp -d); build "$B"; mkdir -p "$d"
  java -cp "$B" RouterConf table "$HOPS" > "$d/tables.txt" || exit 1
  for c in "${CONFS[@]}"; do set -- $c; one "$B" "$d" "$1" "$2" 2000 "t$1-s$2${3:+-$3}" ${3:-}; done
  rm -rf "$B"
}
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "routerconf: ${#CONFS[@]} fixtures and tables.txt written to $FIX (ref $REFCOMMIT)"; exit 0 ;;
  --check)
    T=$(mktemp -d); fixtures "$T"; fail=0
    cmp -s "$FIX/tables.txt" "$T/tables.txt" || { echo "FAIL differs from ref: tables.txt"; fail=1; }
    for c in "${CONFS[@]}"; do set -- $c; n="t$1-s$2${3:+-$3}"
      for f in script obs; do cmp -s <(gzip -dc "$FIX/$n.$f.gz") <(gzip -dc "$T/$n.$f.gz") || { echo "FAIL differs from ref: $n.$f"; fail=1; }; done
    done
    rm -rf "$T"; [ $fail = 0 ] && echo "routerconf --check: the fixtures equal ref's regeneration (ref $REFCOMMIT)"; exit $fail ;;
esac
CYC=${1:?usage: routerconf.sh --fixtures | --check | CYCLES [OUT]}; OUT=$(mkdir -p "${2:-/tmp/routerconf}" && cd "${2:-/tmp/routerconf}" && pwd)
B="$OUT/build"; build "$B"
for c in "${CONFS[@]}"; do set -- $c; for seed in 1 2 3; do one "$B" "$OUT" "$1" "$seed" "$CYC" "t$1-s$seed${3:+-$3}" ${3:-}; done; done
echo "routerconf: ${#CONFS[@]} tiles x 3 seeds x $CYC cycles in $OUT"
