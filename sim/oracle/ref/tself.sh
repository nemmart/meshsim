#!/bin/bash
# tself.sh -- suite.h against v1.3's T, ref's side (WORKER20; oracle/ref/TSelf.java, tests/test_suite.cpp). Needs a JDK.
# Nothing in sim/ref is written: T and the driver are compiled into a scratch directory.
#
#   tself.sh --fixtures   (re)write tests/suite/tself.out: TSelf's stdout, then "exit <status>"
#   tself.sh --check      regenerate it and compare with the committed file
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/suite/tself.out"
B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
javac --release 17 -nowarn -d "$B" "$SIM"/ref/meshsim/src/UndefinedBehavior.java "$SIM"/ref/meshsim/test/T.java "$SIM"/oracle/ref/TSelf.java 2>&1 | grep -v '^Note:'
[ -f "$B/TSelf.class" ] || { echo "tself: the driver did not compile"; exit 2; }
(cd "$B" && java -cp . TSelf; echo "exit $?") > "$B/tself.out"
case "${1:-}" in
  --fixtures) mkdir -p "$(dirname "$FIX")"; cp "$B/tself.out" "$FIX"; echo "wrote $FIX";;
  --check) cmp "$B/tself.out" "$FIX" && echo "tself: ref's T equals the committed fixture";;
  *) echo "usage: tself.sh --fixtures|--check"; exit 2;;
esac
