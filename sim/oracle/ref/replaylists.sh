#!/bin/bash
# replaylists.sh -- the replayer's error lists against ref (WORKER25, P7b; oracle/ref/ReplayLists.java,
# tests/test_replay_lists.cpp): every committed ReplayConf script (tests/replayer/*.script.gz) replayed on ref's
# Replayer, its lists printed with every entry in ref's text. Needs a JDK; sim/ref is only read.
#
#   replaylists.sh --fixtures   (re)write tests/replayer/lists.txt
#   replaylists.sh --check      regenerate it and compare with the committed file
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/replayer/lists.txt"
B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
mkdir -p "$B/cls" "$B/s"
javac --release 17 -nowarn -d "$B/cls" "$SIM"/ref/meshsim/src/*.java "$SIM"/ref/meshsim/test/*.java 2>&1 | grep -v '^Note:'
javac --release 17 -nowarn -d "$B/cls" -cp "$B/cls" "$SIM"/oracle/ref/ReplayConf.java "$SIM"/oracle/ref/StateFp.java \
  "$SIM"/oracle/ref/StateFpM3.java "$SIM"/oracle/ref/ReplayLists.java 2>&1 | grep -v '^Note:'
[ -f "$B/cls/ReplayLists.class" ] || { echo "replaylists: the driver did not compile"; exit 2; }
ARGS=()
for f in "$SIM"/tests/replayer/*.script.gz; do n=$(basename "$f" .script.gz); gzip -dc "$f" > "$B/s/$n"; ARGS+=("$n" "$B/s/$n"); done
java -cp "$B/cls" ReplayLists "${ARGS[@]}" > "$B/out" || { echo "replaylists: ref's run failed"; exit 1; }
case "${1:-}" in
  --fixtures) cp "$B/out" "$FIX"; echo "wrote $FIX ($(grep -c '^clean' "$FIX") scripts)";;
  --check) cmp "$B/out" "$FIX" && echo "replaylists: ref's lists equal the committed fixture";;
  *) echo "usage: replaylists.sh --fixtures|--check"; exit 2;;
esac
