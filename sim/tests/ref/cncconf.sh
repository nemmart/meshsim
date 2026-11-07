#!/bin/bash
# cncconf.sh -- C&C's conformance, ref's side (WORKER9; tests/ref/CncConf.java, tests/test_cnc.cpp). Needs a JDK.
# Nothing in sim/ref is written: ref and the driver are compiled into a scratch directory.
#
#   cncconf.sh --fixtures      (re)write tests/cncconf/: sS.script.gz and sS.obs.gz for each seed below (6,000 cycles),
#                              each opening with its provenance; idle.txt (the endpoints' and the request bus's streams)
#   cncconf.sh --check         regenerate into a scratch directory and compare with the committed fixtures
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/cncconf"
HOPS="$SIM/ref/meshsim/hops.txt"
SEEDS=(1 2 3)
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
build() {
  rm -rf "$1"; mkdir -p "$1"
  javac -nowarn -d "$1" "$SIM"/ref/meshsim/src/*.java 2>&1 | grep -v '^Note:'
  javac -nowarn -d "$1" -cp "$1" "$SIM"/tests/ref/StateFp.java "$SIM"/tests/ref/CncConf.java 2>&1 | grep -v '^Note:'
  [ -f "$1/CncConf.class" ] || { echo "cncconf: the driver did not compile"; exit 2; }
}
fixtures() {
  local d=$1 B s; B=$(mktemp -d); build "$B"; mkdir -p "$d"
  cd "$SIM/ref/meshsim"   # Config.machine() reads hops.txt from the working directory
  for s in "${SEEDS[@]}"; do
    java -cp "$B" CncConf gen "$s" 6000 > "$B/s$s.script" || exit 1
    java -cp "$B" CncConf run "$HOPS" "$B/s$s.script" > "$B/s$s.body" || { echo "FAIL ref raised: s$s"; exit 1; }
    { sed -n '1,2p' "$B/s$s.script"; echo "# ref $REFCOMMIT"; sed '1,2d' "$B/s$s.script"; } | gzip -n -9 > "$d/s$s.script.gz"
    { echo "# cncconf observations v1: ref's side, CncConf run"; sed -n '2p' "$B/s$s.script"; echo "# ref $REFCOMMIT"; cat "$B/s$s.body"; } | gzip -n -9 > "$d/s$s.obs.gz"
  done
  java -cp "$B" CncConf idle "$HOPS" > "$d/idle.txt" || exit 1
  rm -rf "$B"
}
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "cncconf: ${#SEEDS[@]} fixtures and idle.txt written to $FIX (ref $REFCOMMIT)"; exit 0 ;;
  --check)
    T=$(mktemp -d); fixtures "$T"; fail=0
    cmp -s "$FIX/idle.txt" "$T/idle.txt" || { echo "FAIL differs from ref: idle.txt"; fail=1; }
    for s in "${SEEDS[@]}"; do for f in script obs; do
      cmp -s <(gzip -dc "$FIX/s$s.$f.gz") <(gzip -dc "$T/s$s.$f.gz") || { echo "FAIL differs from ref: s$s.$f"; fail=1; }
    done; done
    rm -rf "$T"; [ $fail = 0 ] && echo "cncconf --check: the fixtures equal ref's regeneration (ref $REFCOMMIT)"; exit $fail ;;
  *) echo "usage: cncconf.sh --fixtures | --check"; exit 2 ;;
esac
