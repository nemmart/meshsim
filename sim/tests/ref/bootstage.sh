#!/bin/bash
# bootstage.sh [--check] (WORKER16): ref's boot staging (tests/ref/BootStage.java: Mach.bootCores on tests/m3-images)
# into tests/m3-images/bootstage.ref, and the boot itself without the landings (BootStage trace: the controllers' and tile
# 0's pc between cycles, to Mach.coresBooted) into tests/m3-images/boottrace.ref.gz; or with --check regenerate and compare. Needs a JDK; sim/ref is only read.
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd); B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
javac -nowarn -d "$B/cls" "$SIM"/ref/meshsim/src/*.java 2>&1 | grep -v '^Note:'
javac -nowarn -d "$B/cls" -cp "$B/cls" "$SIM"/ref/meshsim/test/Mach.java "$SIM"/tests/ref/StateFp.java "$SIM"/tests/ref/BootStage.java 2>&1 | grep -v '^Note:'
mkdir -p "$B/run/sw"; ln -s "$SIM/tests/m3-images" "$B/run/sw/build"; ln -s "$SIM/ref/meshsim/hops.txt" "$B/run/hops.txt"; ln -s "$SIM/ref/meshsim/sw/bootrom.hex" "$B/run/sw/bootrom.hex"
OUT=$( { echo "# Mach.bootCores(replay_ctl, tracer_ctl) on ref (BootStage; ref $(git -C "$SIM" log -1 --format=%h -- ref))"; \
        cd "$B/run" && java -cp "$B/cls" BootStage "$SIM/tests/m3-images"; } ) || { echo "bootstage: ref failed"; exit 1; }
(cd "$B/run" && java -cp "$B/cls" BootStage "$SIM/tests/m3-images" trace) | grep -v '^hostq\|^sram' > "$B/trace" || { echo "bootstage: ref's boot failed"; exit 1; }
if [ "${1:-}" = --check ]; then
  fail=0
  [ "$OUT" = "$(cat "$SIM/tests/m3-images/bootstage.ref")" ] || { echo "bootstage --check: the staging differs"; fail=1; }
  cmp -s "$B/trace" <(gzip -dc "$SIM/tests/m3-images/boottrace.ref.gz") || { echo "bootstage --check: the boot's trace differs"; fail=1; }
  [ $fail = 0 ] && echo "bootstage --check: equal to ref"; exit $fail
fi
echo "$OUT" > "$SIM/tests/m3-images/bootstage.ref"; cat "$SIM/tests/m3-images/bootstage.ref"
gzip -n -9 < "$B/trace" > "$SIM/tests/m3-images/boottrace.ref.gz"; echo "boottrace: $(wc -l < "$B/trace") lines, last $(tail -1 "$B/trace")"
