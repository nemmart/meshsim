#!/bin/bash
# machself.sh -- tests/mach.h against v1.3's Mach, ref's side (WORKER20; tests/ref/MachSelf.java, tests/test_mach.cpp).
# Needs a JDK. sim/ref is only read: ref and the driver compile into a scratch directory, and the driver runs in a
# scratch copy of ref/meshsim's layout (hops.txt, sw/bootrom.hex, sw/build/ with the C++ tree's committed images).
#
#   machself.sh --fixtures   (re)write tests/mach/machself.out
#   machself.sh --check      regenerate it and compare with the committed file
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/mach/machself.out"
B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
mkdir -p "$B/cls" "$B/run/sw/build"
javac --release 17 -nowarn -d "$B/cls" "$SIM"/ref/meshsim/src/*.java "$SIM"/ref/meshsim/test/{Mach,A2A}.java "$SIM"/tests/ref/MachSelf.java 2>&1 | grep -v '^Note:'
[ -f "$B/cls/MachSelf.class" ] || { echo "machself: the driver did not compile"; exit 2; }
cp "$SIM/ref/meshsim/hops.txt" "$B/run/"; cp "$SIM/ref/meshsim/sw/bootrom.hex" "$B/run/sw/"
for d in "$SIM/tests/golden-m2/images" "$SIM/tests/m3-images"; do cp "$d"/*.bin "$d"/*.sym "$B/run/sw/build/"; done
(cd "$B/run" && java -Xss16m -cp "$B/cls" MachSelf) > "$B/out" || { echo "machself: ref's run failed"; cat "$B/out"; exit 1; }
case "${1:-}" in
  --fixtures) mkdir -p "$(dirname "$FIX")"; cp "$B/out" "$FIX"; echo "wrote $FIX"; cat "$FIX";;
  --check) cmp "$B/out" "$FIX" && echo "machself: ref's Mach equals the committed fixture";;
  *) echo "usage: machself.sh --fixtures|--check"; exit 2;;
esac
