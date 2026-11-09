#!/bin/bash
# replayconf.sh -- the replayer's conformance, ref's side (WORKER15; tests/ref/ReplayConf.java, tests/test_replayer_*.cpp).
# Needs a JDK. Nothing in sim/ref is written: ref and the driver are compiled into a scratch directory.
#
#   replayconf.sh --fixtures   (re)write tests/replayer/: alu-report.txt (ReplayerTest's ALU report, lines 1-29) and
#                              alu-sweep.txt (ReplayConf alu 1 2000: a digest per opcode, and RowCore's)
#   replayconf.sh --check      regenerate into a scratch directory and compare with the committed fixtures
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/replayer"
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
build() {
  rm -rf "$1"; mkdir -p "$1"
  javac --release 17 -nowarn -d "$1" "$SIM"/ref/meshsim/src/*.java "$SIM"/ref/meshsim/test/*.java 2>&1 | grep -v '^Note:'
  javac --release 17 -nowarn -d "$1" -cp "$1" "$SIM"/tests/ref/ReplayConf.java 2>&1 | grep -v '^Note:'
  [ -f "$1/ReplayConf.class" ] || { echo "replayconf: the driver did not compile"; exit 2; }
}
fixtures() {
  local d=$1 B; B=$(mktemp -d); build "$B"; mkdir -p "$d"
  (cd "$SIM/ref/meshsim" && java -cp "$B" ReplayerTest data/vec-821e1bd.tsv ../tables/opcode_table.txt) > "$B/rt.out" || { echo "FAIL ref's ReplayerTest"; exit 1; }
  sed -n '1,29p' "$B/rt.out" > "$d/alu-report.txt"
  java -cp "$B" ReplayConf alu 1 2000 > "$d/alu-sweep.txt" || exit 1
  rm -rf "$B"
}
FILES="alu-report.txt alu-sweep.txt"
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "replayconf: fixtures written to $FIX (ref $REFCOMMIT)"; exit 0 ;;
  --check)
    T=$(mktemp -d); fixtures "$T"; fail=0
    for f in $FILES; do cmp -s "$FIX/$f" "$T/$f" || { echo "FAIL differs from ref: $f"; fail=1; }; done
    rm -rf "$T"; [ $fail = 0 ] && echo "replayconf --check: the fixtures equal ref's regeneration (ref $REFCOMMIT)"; exit $fail ;;
  *) echo "usage: replayconf.sh --fixtures | --check"; exit 2 ;;
esac
