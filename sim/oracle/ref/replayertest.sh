#!/bin/bash
# replayertest.sh -- the step replayer's reference (WORKER25, P7b; inventory C20): v1.3's ReplayerTest run as
# run_tests.sh runs it (`java ReplayerTest data/vec-821e1bd.tsv ../tables/opcode_table.txt`, stdout and stderr), from
# a scratch build of sim/ref, in ref/meshsim (it only reads there). ref's committed results/replayer.log is stale (242
# vector rows, 1,188 checks; W25 NOTES F4). Needs a JDK.
#   replayertest.sh --fixtures   (re)write tests/replayer/replayer.log
#   replayertest.sh --check      regenerate it and compare with the committed file
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd); R=$SIM/ref/meshsim
FIX=$SIM/tests/replayer/replayer.log
B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
grep -v '^#' "$R/test/PENDING" | awk 'NF{print "'"$R"'/test/"$1}' > "$B/pending"
javac --release 17 -nowarn -d "$B" "$R"/src/*.java 2>&1 | grep -v '^Note:'
javac --release 17 -nowarn -d "$B" -cp "$B" $(ls "$R"/test/*.java | grep -v -x -F -f "$B/pending") 2>&1 | grep -v '^Note:'
(cd "$R" && java -Xmx3g -cp "$B" ReplayerTest data/vec-821e1bd.tsv ../tables/opcode_table.txt) > "$B/out" 2>&1
echo "replayertest: v1.3's ReplayerTest exited $?"
case "${1:-}" in
  --fixtures) cp "$B/out" "$FIX"; echo "wrote $FIX"; tail -1 "$FIX";;
  --check) cmp "$B/out" "$FIX" && echo "replayertest: v1.3's ReplayerTest equals the committed fixture";;
  *) echo "usage: replayertest.sh --fixtures|--check"; exit 2;;
esac
