#!/bin/bash
# reqconf.sh -- the request bus's conformance, ref's side (WORKER14; oracle/ref/ReqConf.java, tests/test_reqbus.cpp).
# Needs a JDK. Nothing in sim/ref is written: ref, StateFp and the driver are compiled into a scratch directory.
#   reqconf.sh --fixtures   (re)write tests/reqconf/ (RUNS below), each opening with its provenance (the ref commit)
#   reqconf.sh --check      regenerate into a scratch directory and compare with the committed fixtures
#   reqconf.sh N [OUT]      long runs: seeds 1-N of mix, 20,000 cycles each, into OUT (TRACERCONF_LONG-style replay:
#                           REQCONF_LONG=OUT test_reqbus long)
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/reqconf"
REF="$SIM/ref/meshsim"
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
RUNS=("mix 1 6000" "calm 2 6000" "full 3 4000" "fanin 4 4000" "drop 5 4000")
build() {
  rm -rf "$1"; mkdir -p "$1"
  javac -nowarn -d "$1" -sourcepath "$REF/test" "$REF"/src/*.java "$SIM"/oracle/ref/StateFp.java "$SIM"/oracle/ref/ReqConf.java 2>&1 | grep -v '^Note:'
  [ -f "$1/ReqConf.class" ] || { echo "reqconf: the driver did not compile"; exit 2; }
}
one() {   # B DIR FAMILY SEED CYCLES
  local B=$1 d=$2 f=$3 s=$4 n=$5
  { echo "# ref $REFCOMMIT"; ( cd "$REF" && java -cp "$B" ReqConf "$s" "$n" "$f" ) || exit 1; } | gzip -n -9 > "$d/$f-s$s.txt.gz"
}
fixtures() { local d=$1 B r; B=$(mktemp -d); build "$B"; mkdir -p "$d"; for r in "${RUNS[@]}"; do set -- $r; one "$B" "$d" $1 $2 $3; done; rm -rf "$B"; }
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "reqconf: fixtures written to $FIX (ref $REFCOMMIT)"; exit 0 ;;
  --check)
    T=$(mktemp -d); fixtures "$T"; fail=0
    for f in "$T"/*.gz; do n=$(basename "$f"); cmp -s <(gzip -dc "$FIX/$n") <(gzip -dc "$f") || { echo "FAIL differs from ref: $n"; fail=1; }; done
    rm -rf "$T"; [ $fail = 0 ] && echo "reqconf --check: the fixtures equal ref's regeneration (ref $REFCOMMIT)"; exit $fail ;;
esac
N=${1:?usage: reqconf.sh --fixtures | --check | N [OUT]}; OUT=$(mkdir -p "${2:-/tmp/reqconf}" && cd "${2:-/tmp/reqconf}" && pwd)
B="$OUT/build"; build "$B"
for s in $(seq 1 "$N"); do one "$B" "$OUT" mix "$s" 20000; done
echo "reqconf: mix seeds 1-$N in $OUT"
