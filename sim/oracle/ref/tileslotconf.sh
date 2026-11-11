#!/bin/bash
# tileslotconf.sh -- PeripheralSlotTest's groups as conformance, ref's side (WORKER15; oracle/ref/TileSlotConf.java,
# tileslotconf_progs.py, tests/test_tileslotconf.cpp). Needs a JDK and python3; nothing in sim/ref is written.
#   tileslotconf.sh --fixtures   write tests/tileslotconf/<group>.prog (the generator) and <group>.obs (ref's run)
#   tileslotconf.sh --check      the programs regenerated and ref rerun: both equal to the committed files
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd); FIX="$SIM/tests/tileslotconf"; GRPS="window events landing dma replayer"
B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
javac --release 17 -nowarn -d "$B" "$SIM"/ref/meshsim/src/*.java "$SIM"/ref/meshsim/test/*.java 2>&1 | grep -v '^Note:'
javac --release 17 -nowarn -d "$B" -cp "$B" "$SIM"/oracle/ref/TileSlotConf.java 2>&1 | grep -v '^Note:'
[ -f "$B/TileSlotConf.class" ] || { echo "tileslotconf: the driver did not compile"; exit 2; }
python3 "$SIM/oracle/ref/tileslotconf_progs.py" "$B/progs" > /dev/null || exit 2
for g in $GRPS; do (cd "$SIM/ref/meshsim" && java -cp "$B" TileSlotConf "$B/progs/$g.prog") > "$B/progs/$g.obs" || { echo "FAIL ref's run of $g"; exit 1; }; done
case "${1:-}" in
  --fixtures) mkdir -p "$FIX"; for g in $GRPS; do cp "$B/progs/$g.prog" "$B/progs/$g.obs" "$FIX/"; done; echo "tileslotconf: fixtures written to $FIX" ;;
  --check)
    fail=0
    for g in $GRPS; do for e in prog obs; do cmp -s "$B/progs/$g.$e" "$FIX/$g.$e" || { echo "FAIL differs: $g.$e"; fail=1; }; done; done
    [ $fail = 0 ] && echo "tileslotconf --check: $(echo $GRPS | wc -w) groups equal ref"; exit $fail ;;
  *) echo "usage: tileslotconf.sh --fixtures | --check"; exit 2 ;;
esac
