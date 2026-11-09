#!/bin/bash
# tracerconf.sh -- the Tracer's conformance, ref's side (WORKER14; tests/ref/TracerConf.java, tests/test_tracer.cpp).
# Needs a JDK. Nothing in sim/ref is written: ref, its test classes and the driver are compiled into a scratch directory.
#
# TracerTest's cases are recorded through copies of TracerTest and TracerRig made here, changed only so that the rig's
# Tracer is TracerConf.Rec (a recording proxy) and the rig's reads of the Tracer's fields go to the real one:
#   TracerRig.java -> TracerConfRig.java, TracerTest.java -> TracerConfCases.java (sed, below).
#
#   tracerconf.sh --fixtures      (re)write tests/tracerconf/: cases.log.gz and the seeded guests GENS (FAMILY-sSEED.log.gz),
#                                 each opening with its provenance (the ref commit)
#   tracerconf.sh --check         regenerate into a scratch directory and compare with the committed fixtures
#   tracerconf.sh SEEDS [OUT]     long runs: seeds 1..SEEDS of the mix family and 1..4 of wb into OUT
#                                 (then TRACERCONF_LONG=OUT build/<cc>-<mode>/test_tracer long)
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/tracerconf"
REF="$SIM/ref/meshsim"
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
GENS=("mix 1" "mix 3" "mix 4" "mix 5" "mix 9" "mix 16" "mix 21" "wb 1" "wbover 1")   # family seed: in make check
FIELDS='mem|mapMem|onStateWrite|icacheLines|rowsM|rowsPad|rows|instrs|runCycles|runs|iAccess|iMissLpddr|iMiss|dAccess|dMiss|writebacks|lruHits|lruMisses|probeReads|straddles|runStartAt|rowsEndAt|drainedAt|stateWrites|newExpansions|drainCycles|scanStalls|codeN|logN'
build() {
  rm -rf "$1"; mkdir -p "$1/src"
  sed -E -e 's/\bTracerRig\b/TracerConfRig/g' \
         -e 's/final Tracer tr = new Tracer\(66\);/final TracerConf.Rec tr = new TracerConf.Rec(new Tracer(66), () -> this.fs);/' \
         -e "s/\\btr\\.($FIELDS)\\b/tr.x.\\1/g" "$REF/test/TracerRig.java" > "$1/src/TracerConfRig.java"
  sed -E -e 's/\bTracerTest\b/TracerConfCases/g' -e 's/\bTracerRig\b/TracerConfRig/g' \
         -e 's/System\.exit\(T\.done/TracerConf.exit(T.done/' \
         -e "s/\\btr\\.($FIELDS)\\b/tr.x.\\1/g" "$REF/test/TracerTest.java" > "$1/src/TracerConfCases.java"
  grep -q 'TracerConf.Rec tr = new TracerConf.Rec' "$1/src/TracerConfRig.java" || { echo "tracerconf: the rig's Tracer was not replaced"; exit 2; }
  javac -nowarn -d "$1" -sourcepath "$REF/test" "$REF"/src/*.java \
        "$SIM"/tests/ref/StateFp.java "$SIM"/tests/ref/StateFpM3.java "$SIM"/tests/ref/TracerConf.java "$1"/src/*.java 2>&1 | grep -v '^Note:'
  [ -f "$1/TracerConf.class" ] || { echo "tracerconf: the driver did not compile"; exit 2; }
}
cases() {   # B DIR: TracerTest's cases recorded
  local B=$1 d=$2
  ( cd "$REF" && java -cp "$B" TracerConf cases "$B/cases.log" ) > "$B/cases.out" || { cat "$B/cases.out"; echo "FAIL ref's TracerTest cases"; exit 1; }
  grep -q "TracerConfCases: .* 0 failed" "$B/cases.out" || { cat "$B/cases.out"; echo "FAIL ref's TracerTest cases"; exit 1; }
  { echo "# ref $REFCOMMIT"; cat "$B/cases.log"; } | gzip -n -9 > "$d/cases.log.gz"
}
gen() {     # B DIR FAMILY SEED: one seeded guest recorded
  local B=$1 d=$2 f=$3 s=$4
  ( cd "$REF" && java -cp "$B" TracerConf gen "$B/$f-s$s.log" "$s" "$f" ) > "$B/$f-s$s.out" 2>&1 || { cat "$B/$f-s$s.out"; echo "FAIL ref: $f $s"; exit 1; }
  { echo "# ref $REFCOMMIT"; cat "$B/$f-s$s.log"; } | gzip -n -9 > "$d/$f-s$s.log.gz"
  tail -1 "$B/$f-s$s.out" | cut -c1-200
}
fixtures() {
  local d=$1 B g; B=$(mktemp -d); build "$B"; mkdir -p "$d"; cases "$B" "$d"; tail -1 "$B/cases.out"
  for g in "${GENS[@]}"; do set -- $g; gen "$B" "$d" "$1" "$2"; done
  rm -rf "$B"
}
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "tracerconf: fixtures written to $FIX (ref $REFCOMMIT)"; exit 0 ;;
  --check)
    T=$(mktemp -d); fixtures "$T"; fail=0
    for f in "$T"/*.gz; do n=$(basename "$f"); cmp -s <(gzip -dc "$FIX/$n") <(gzip -dc "$f") || { echo "FAIL differs from ref: $n"; fail=1; }; done
    rm -rf "$T"; [ $fail = 0 ] && echo "tracerconf --check: the fixtures equal ref's regeneration (ref $REFCOMMIT)"; exit $fail ;;
esac
N=${1:?usage: tracerconf.sh --fixtures | --check | SEEDS [OUT]}; OUT=$(mkdir -p "${2:-/tmp/tracerconf}" && cd "${2:-/tmp/tracerconf}" && pwd)
B="$OUT/build"; build "$B"
for s in $(seq 1 "$N"); do gen "$B" "$OUT" mix "$s"; done
for s in 1 2 3 4; do gen "$B" "$OUT" wb "$s"; done
echo "tracerconf: mix seeds 1-$N and wb 1-4 in $OUT"
