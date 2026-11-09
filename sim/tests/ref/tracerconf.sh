#!/bin/bash
# tracerconf.sh -- the Tracer's conformance, ref's side (WORKER14; tests/ref/TracerConf.java, tests/test_tracer.cpp).
# Needs a JDK. Nothing in sim/ref is written: ref, its test classes and the driver are compiled into a scratch directory.
#
# TracerTest's cases are recorded through copies of TracerTest and TracerRig made here, changed only so that the rig's
# Tracer is TracerConf.Rec (a recording proxy) and the rig's reads of the Tracer's fields go to the real one:
#   TracerRig.java -> TracerConfRig.java, TracerTest.java -> TracerConfCases.java (sed, below).
#
#   tracerconf.sh --fixtures      (re)write tests/tracerconf/cases.log.gz, opening with its provenance (the ref commit)
#   tracerconf.sh --check         regenerate into a scratch directory and compare with the committed fixtures
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/tracerconf"
REF="$SIM/ref/meshsim"
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
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
        "$SIM"/tests/ref/TracerConf.java "$1"/src/*.java 2>&1 | grep -v '^Note:'
  [ -f "$1/TracerConf.class" ] || { echo "tracerconf: the driver did not compile"; exit 2; }
}
cases() {   # B DIR: TracerTest's cases recorded
  local B=$1 d=$2
  ( cd "$REF" && java -cp "$B" TracerConf cases "$B/cases.log" ) > "$B/cases.out" || { cat "$B/cases.out"; echo "FAIL ref's TracerTest cases"; exit 1; }
  grep -q "TracerConfCases: .* 0 failed" "$B/cases.out" || { cat "$B/cases.out"; echo "FAIL ref's TracerTest cases"; exit 1; }
  { echo "# ref $REFCOMMIT"; cat "$B/cases.log"; } | gzip -n -9 > "$d/cases.log.gz"
}
fixtures() { local d=$1 B; B=$(mktemp -d); build "$B"; mkdir -p "$d"; cases "$B" "$d"; tail -1 "$B/cases.out"; rm -rf "$B"; }
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "tracerconf: fixtures written to $FIX (ref $REFCOMMIT)"; exit 0 ;;
  --check)
    T=$(mktemp -d); fixtures "$T"; fail=0
    for f in "$T"/*.gz; do n=$(basename "$f"); cmp -s <(gzip -dc "$FIX/$n") <(gzip -dc "$f") || { echo "FAIL differs from ref: $n"; fail=1; }; done
    rm -rf "$T"; [ $fail = 0 ] && echo "tracerconf --check: the fixtures equal ref's regeneration (ref $REFCOMMIT)"; exit $fail ;;
esac
echo "usage: tracerconf.sh --fixtures | --check"; exit 2
