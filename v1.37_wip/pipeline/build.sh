#!/bin/sh
# pipeline/build.sh: builds v1.35's offline input tools (D40: they stay Java). Needs only a JDK
# (javac, Java 21). Nothing in the simulator's build or tests reads this folder.
#   keys/      KeyGen and SplitSets, into build/keys/
#   gold/      gold (Jolt's tracer in Java), into gold/build/, with its own tests (gold/build.sh)
#   jumpmap/   the jump map, frame 6, the initial image and the pages, into jumpmap/build/ (jumpmap/build.sh)
set -e
P=$(cd "$(dirname "$0")" && pwd)
command -v javac > /dev/null || { echo "pipeline/build.sh: needs a JDK (javac)"; exit 1; }
rm -rf "$P/build/keys"; mkdir -p "$P/build/keys"
javac -nowarn -d "$P/build/keys" "$P"/keys/*.java 2>&1 | grep -v '^Note:' || true
[ -f "$P/build/keys/KeyGen.class" ] && [ -f "$P/build/keys/SplitSets.class" ] || { echo "pipeline/build.sh: keys FAILED"; exit 1; }
echo "pipeline: keys built (java -cp pipeline/build/keys KeyGen OUTDIR [--n PER_TILE] [--only slug,...]; SplitSets OUTDIR [--n PER_TILE])"
(cd "$P/gold" && ./build.sh) | tail -1
(cd "$P/jumpmap" && ./build.sh)
