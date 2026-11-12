#!/bin/bash
# checkself.sh -- tests/mach.h against v1.3's checker, ref's side (WORKER20; oracle/ref/CheckSelf.java, tests/test_mach.cpp).
# Needs a JDK. sim/ref is only read: ref and the driver compile into a scratch directory, and the driver runs in a
# scratch copy of ref/meshsim's layout (hops.txt, sw/bootrom.hex, sw/build/ with the C++ tree's committed images).
#
#   checkself.sh --fixtures   (re)write tests/mach/checkself.out
#   checkself.sh --check      regenerate it and compare with the committed file
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/mach/checkself.out"
B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
mkdir -p "$B/cls" "$B/run/sw/build"
javac --release 17 -nowarn -d "$B/cls" "$SIM"/ref/meshsim/src/*.java "$SIM"/ref/meshsim/test/{Mach,Net,A2A}.java "$SIM"/oracle/ref/CheckSelf.java 2>&1 | grep -v '^Note:'
[ -f "$B/cls/CheckSelf.class" ] || { echo "checkself: the driver did not compile"; exit 2; }
cp "$SIM/ref/meshsim/hops.txt" "$B/run/"; cp "$SIM/ref/meshsim/sw/bootrom.hex" "$B/run/sw/"
for d in "$SIM/fw/images" "$SIM/tests/golden-m2/images" "$SIM/tests/m3-images"; do cp "$d"/*.bin "$d"/*.sym "$B/run/sw/build/"; done
(cd "$B/run" && java -Xss16m -cp "$B/cls" CheckSelf) > "$B/out" || { echo "checkself: ref's run failed"; cat "$B/out"; exit 1; }
case "${1:-}" in
  --fixtures) mkdir -p "$(dirname "$FIX")"; cp "$B/out" "$FIX"; echo "wrote $FIX"; cat "$FIX";;
  --check) cmp "$B/out" "$FIX" && echo "checkself: ref's checker equals the committed fixture";;
  *) echo "usage: checkself.sh --fixtures|--check"; exit 2;;
esac
