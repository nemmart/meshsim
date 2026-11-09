#!/bin/bash
# bootstage.sh [--check] (WORKER16): ref's boot staging (tests/ref/BootStage.java: Mach.bootCores on tests/m3-images)
# into tests/m3-images/bootstage.ref, or with --check regenerate and compare. Needs a JDK; sim/ref is only read.
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd); B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
javac -nowarn -d "$B/cls" "$SIM"/ref/meshsim/src/*.java 2>&1 | grep -v '^Note:'
javac -nowarn -d "$B/cls" -cp "$B/cls" "$SIM"/ref/meshsim/test/Mach.java "$SIM"/tests/ref/StateFp.java "$SIM"/tests/ref/BootStage.java 2>&1 | grep -v '^Note:'
mkdir -p "$B/run/sw"; ln -s "$SIM/tests/m3-images" "$B/run/sw/build"; ln -s "$SIM/ref/meshsim/hops.txt" "$B/run/hops.txt"; ln -s "$SIM/ref/meshsim/sw/bootrom.hex" "$B/run/sw/bootrom.hex"
OUT=$( { echo "# Mach.bootCores(replay_ctl, tracer_ctl) on ref (BootStage; ref $(git -C "$SIM" log -1 --format=%h -- ref))"; \
        cd "$B/run" && java -cp "$B/cls" BootStage "$SIM/tests/m3-images"; } ) || { echo "bootstage: ref failed"; exit 1; }
if [ "${1:-}" = --check ]; then
  [ "$OUT" = "$(cat "$SIM/tests/m3-images/bootstage.ref")" ] && echo "bootstage --check: equal to ref" && exit 0
  echo "bootstage --check: differs"; diff <(echo "$OUT") "$SIM/tests/m3-images/bootstage.ref"; exit 1
fi
echo "$OUT" > "$SIM/tests/m3-images/bootstage.ref"; cat "$SIM/tests/m3-images/bootstage.ref"
