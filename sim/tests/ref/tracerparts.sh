#!/bin/bash
# tracerparts.sh -- the Tracer's pure pieces, ref's side (WORKER14; tests/ref/TracerParts.java, tests/test_tracer_parts.cpp).
# Needs a JDK. Nothing in sim/ref is written: ref and the driver are compiled into a scratch directory.
#
#   tracerparts.sh --fixtures     (re)write tests/tracerparts/: rvc.txt.gz (every 16-bit encoding), lru-sN.txt.gz and
#                                 mem-sN.txt.gz (seeded streams), each opening with its provenance (the ref commit)
#   tracerparts.sh --check        regenerate into a scratch directory and compare with the committed fixtures
#   tracerparts.sh N [OUT]        longer runs (N events, seeds 1-5) into OUT, for TRACERPARTS_LONG=OUT test_tracer_parts
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/tracerparts"
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
RUNS=("lru 1 20000" "lru 2 20000" "mem 1 8000" "mem 2 8000")
build() {
  rm -rf "$1"; mkdir -p "$1"
  javac -nowarn -d "$1" "$SIM"/ref/meshsim/src/*.java 2>&1 | grep -v '^Note:'
  javac -nowarn -d "$1" -cp "$1" "$SIM"/tests/ref/TracerParts.java 2>&1 | grep -v '^Note:'
  [ -f "$1/TracerParts.class" ] || { echo "tracerparts: the driver did not compile"; exit 2; }
}
one() {   # B DIR NAME ARGS...: ref's output, gzipped with its provenance
  local B=$1 d=$2 name=$3; shift 3
  { echo "# tracerparts v1: ref's side, TracerParts $*"; echo "# ref $REFCOMMIT"; java -cp "$B" TracerParts "$@" || exit 1; } | gzip -n -9 > "$d/$name.txt.gz"
}
fixtures() {
  local d=$1 B r; B=$(mktemp -d); build "$B"; mkdir -p "$d"
  one "$B" "$d" rvc rvc
  for r in "${RUNS[@]}"; do set -- $r; one "$B" "$d" "$1-s$2" $r; done
  rm -rf "$B"
}
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "tracerparts: fixtures written to $FIX (ref $REFCOMMIT)"; exit 0 ;;
  --check)
    T=$(mktemp -d); fixtures "$T"; fail=0
    for f in "$T"/*.txt.gz; do n=$(basename "$f"); cmp -s <(gzip -dc "$FIX/$n") <(gzip -dc "$f") || { echo "FAIL differs from ref: $n"; fail=1; }; done
    rm -rf "$T"; [ $fail = 0 ] && echo "tracerparts --check: the fixtures equal ref's regeneration (ref $REFCOMMIT)"; exit $fail ;;
esac
N=${1:?usage: tracerparts.sh --fixtures | --check | N [OUT]}; OUT=$(mkdir -p "${2:-/tmp/tracerparts}" && cd "${2:-/tmp/tracerparts}" && pwd)
B="$OUT/build"; build "$B"
for s in 1 2 3 4 5; do one "$B" "$OUT" "lru-s$s" lru $s "$N"; one "$B" "$OUT" "mem-s$s" mem $s "$N"; done
echo "tracerparts: seeds 1-5 x $N events in $OUT"
