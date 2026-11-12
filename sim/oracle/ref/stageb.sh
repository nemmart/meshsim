#!/bin/bash
# stageb.sh -- Stage B's references from v1.3 (WORKER25, P7b; inventory E3): v1.3's own StageB, run as run_tests.sh's
# `stageb` step runs it (`java StageB synthetic`, stdout and stderr), in a scratch copy of ref/meshsim's layout with the
# C++ tree's committed replay_ctl images (tests/m3-images/replay_ctl, fw/images/oldmap/; byte-identical to v1.3's build, W19's
# fw_crosscheck). Needs a JDK. sim/ref is only read.
#
#   stageb.sh --fixtures   (re)write tests/stageb/stageb.log
#   stageb.sh --check      regenerate it and compare with the committed file (the CL4 line's seconds masked)
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/stageb/stageb.log"
B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
R="$SIM/ref/meshsim"
mkdir -p "$B/cls" "$B/run/sw/replay/build/oldmap"
grep -v '^#' "$R/test/PENDING" | awk 'NF{print "'"$R"'/test/"$1}' > "$B/pending"
javac --release 17 -nowarn -d "$B/cls" "$R"/src/*.java 2>&1 | grep -v '^Note:'
javac --release 17 -nowarn -d "$B/cls" -cp "$B/cls" $(ls "$R"/test/*.java | grep -v -x -F -f "$B/pending") 2>&1 | grep -v '^Note:'
[ -f "$B/cls/StageB.class" ] || { echo "stageb: v1.3's StageB did not compile"; exit 2; }
cp "$R/hops.txt" "$B/run/"; cp "$R/sw/bootrom.hex" "$B/run/sw/"
cp "$SIM"/tests/m3-images/replay_ctl.{code.bin,data.bin,sym} "$B/run/sw/replay/build/"
cp "$SIM"/fw/images/oldmap/replay_ctl.{code.bin,data.bin,sym} "$B/run/sw/replay/build/oldmap/"
(cd "$B/run" && java -Xmx3g -cp "$B/cls" StageB synthetic) > "$B/out" 2>&1
echo "stageb: v1.3's StageB synthetic exited $?"
mask() { sed -E 's/(failed as starved in )[0-9.]+ s$/\1X s/' "$1"; }
case "${1:-}" in
  --fixtures) mkdir -p "$(dirname "$FIX")"; cp "$B/out" "$FIX"; echo "wrote $FIX"; cat "$FIX";;
  --check) cmp <(mask "$B/out") <(mask "$FIX") && echo "stageb: v1.3's StageB equals the committed fixture (seconds masked)";;
  *) echo "usage: stageb.sh --fixtures|--check"; exit 2;;
esac
