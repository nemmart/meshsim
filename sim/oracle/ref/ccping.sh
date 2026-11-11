#!/bin/bash
# ccping.sh [--check] (WORKER16): ref's cycles for test_cc_units (oracle/ref/CcPing.java on tests/m3-images/cc_ping):
# writes tests/m3-images/cc_ping.ref, or with --check regenerates and compares. Needs a JDK; sim/ref is only read.
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd); B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
javac -nowarn -d "$B" "$SIM"/ref/meshsim/src/*.java 2>&1 | grep -v '^Note:'
javac -nowarn -d "$B" -cp "$B" "$SIM"/ref/meshsim/test/Mach.java "$SIM"/oracle/ref/CcPing.java 2>&1 | grep -v '^Note:'
OUT=$( { echo "# cc_ping on ref (CcPing; ref $(git -C "$SIM" log -1 --format=%h -- ref), images $(sha256sum "$SIM/tests/m3-images/cc_ping.code.bin" | cut -c1-16))"; \
        cd "$SIM/ref/meshsim" && java -cp "$B" CcPing "$SIM/tests/m3-images"; } ) || { echo "ccping: ref failed"; exit 1; }
if [ "${1:-}" = --check ]; then
  [ "$OUT" = "$(cat "$SIM/tests/m3-images/cc_ping.ref")" ] && echo "ccping --check: equal to ref" && exit 0
  echo "ccping --check: differs"; diff <(echo "$OUT") "$SIM/tests/m3-images/cc_ping.ref"; exit 1
fi
echo "$OUT" > "$SIM/tests/m3-images/cc_ping.ref"; cat "$SIM/tests/m3-images/cc_ping.ref"
