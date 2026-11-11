#!/bin/bash
# tracersuites.sh -- the Tracer's suites, ref's side (WORKER21; P7a, inventory C21-C23). Needs a JDK. Nothing in
# sim/ref is written: ref, its test classes, gold and the driver are compiled into a scratch directory.
#
# v1.3's run_tests.sh steps memsys, rvc and tracer (MemorySystemTest, RvcTest with gold's classes, TracerTest), run as
# run_tests.sh runs them (from ref/meshsim, stdout and stderr together), are the references the C++ suites' output must
# equal (tests/suite_memsys.cpp, suite_rvc.cpp, suite_tracer.cpp); and gold's expansion of every compressed encoding
# (GoldRvc.java) is RvcTest's fixture, gold staying Java in pipeline/ (D40).
#
#   tracersuites.sh --fixtures    (re)write tests/tracersuites/: memsys.log, rvc.log, tracer.log, rvc-gold.txt.gz
#   tracersuites.sh --check       regenerate into a scratch directory and compare with the committed fixtures
#
# The logs carry no provenance line (the C++ suites' output is compared with them as they are); it is in PROVENANCE.
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/tracersuites"
REF="$SIM/ref/meshsim"
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
build() {
  rm -rf "$1"; mkdir -p "$1"
  javac -nowarn -d "$1" $(find "$SIM/ref/gold/src" -name '*.java') 2>&1 | grep -v '^Note:'
  javac -nowarn -d "$1" -cp "$1" -sourcepath "$REF/test" "$REF"/src/*.java "$REF/test/MemorySystemTest.java" \
        "$REF/test/RvcTest.java" "$REF/test/TracerTest.java" "$SIM/tests/ref/GoldRvc.java" 2>&1 | grep -v '^Note:'
  for c in gold/Decoder MemorySystemTest RvcTest TracerTest GoldRvc; do
    [ -f "$1/$c.class" ] || { echo "tracersuites: $c did not compile"; exit 2; }
  done
}
step() {    # B DIR LOG CLASS: one run_tests.sh step, its log as run_tests.sh writes it
  local B=$1 d=$2 log=$3 cls=$4
  ( cd "$REF" && java -Xmx3g -cp "$B" "$cls" ) > "$d/$log.log" 2>&1 || { cat "$d/$log.log"; echo "FAIL ref's $cls"; exit 1; }
  grep -q "^$cls: .* 0 failed" "$d/$log.log" || { cat "$d/$log.log"; echo "FAIL ref's $cls"; exit 1; }
  tail -1 "$d/$log.log"
}
fixtures() {
  local d=$1 B; B=$(mktemp -d); build "$B"; mkdir -p "$d"
  step "$B" "$d" memsys MemorySystemTest
  step "$B" "$d" rvc RvcTest
  grep -q PENDING "$d/rvc.log" && { cat "$d/rvc.log"; echo "FAIL RvcTest ran without gold"; exit 1; }
  step "$B" "$d" tracer TracerTest
  java -cp "$B" GoldRvc | gzip -n -9 > "$d/rvc-gold.txt.gz"
  [ "$(gzip -dc "$d/rvc-gold.txt.gz" | wc -l)" = 49152 ] || { echo "FAIL GoldRvc: not 49,152 lines"; exit 1; }
  { echo "tests/tracersuites: made by tests/ref/tracersuites.sh --fixtures from sim/ref at $REFCOMMIT"
    echo "memsys.log, rvc.log, tracer.log: run_tests.sh's steps of the same names (MemorySystemTest, RvcTest with gold, TracerTest)"
    echo "rvc-gold.txt.gz: gold.Decoder.uncompress on every compressed encoding (tests/ref/GoldRvc.java)"
    java -version 2>&1 | head -1; } > "$d/PROVENANCE"
  rm -rf "$B"
}
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "tracersuites: fixtures written to $FIX (ref $REFCOMMIT)"; exit 0 ;;
  --check)
    T=$(mktemp -d); fixtures "$T" > /dev/null; fail=0
    for n in memsys.log rvc.log tracer.log; do cmp -s "$FIX/$n" "$T/$n" || { echo "FAIL differs from ref: $n"; fail=1; }; done
    cmp -s <(gzip -dc "$FIX/rvc-gold.txt.gz") <(gzip -dc "$T/rvc-gold.txt.gz") || { echo "FAIL differs from gold: rvc-gold.txt.gz"; fail=1; }
    rm -rf "$T"; [ $fail = 0 ] && echo "tracersuites --check: the fixtures equal ref's regeneration (ref $REFCOMMIT)"; exit $fail ;;
  *) echo "usage: tracersuites.sh --fixtures | --check"; exit 2 ;;
esac
