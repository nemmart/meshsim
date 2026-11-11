#!/bin/sh
# pipeline/build.sh (WORKER19, P1): builds v1.35's offline input tools (D40: they stay Java) into pipeline/build/.
# Needs only a JDK (javac, 17 or later). Nothing in the simulator's build or tests reads this folder.
#   keys/   KeyGen (the sort's key sets, bit-exact with numpy 2.x) and SplitSets (the split's 25 data sets), v1.3's
set -e
P=$(cd "$(dirname "$0")" && pwd)
command -v javac > /dev/null || { echo "pipeline/build.sh: needs a JDK (javac)"; exit 1; }
rm -rf "$P/build/keys"; mkdir -p "$P/build/keys"
javac -nowarn -d "$P/build/keys" "$P"/keys/*.java 2>&1 | grep -v '^Note:' || true
[ -f "$P/build/keys/KeyGen.class" ] && [ -f "$P/build/keys/SplitSets.class" ] || { echo "pipeline/build.sh: FAILED"; exit 1; }
echo "pipeline: keys built (java -cp pipeline/build/keys KeyGen OUTDIR [--n PER_TILE] [--only slug,...]; SplitSets OUTDIR [--n PER_TILE])"
