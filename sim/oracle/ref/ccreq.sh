#!/bin/bash
# ccreq.sh [--check] (WORKER14): ref's figures for test_cc_req (oracle/ref/CcReq.java on tests/m3-images/cc_req and
# tile_req): writes tests/m3-images/cc_req.ref, or with --check regenerates and compares. Needs a JDK; sim/ref is only read.
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd); B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
javac -nowarn -d "$B" "$SIM"/ref/meshsim/src/*.java 2>&1 | grep -v '^Note:'
javac -nowarn -d "$B" -cp "$B" "$SIM"/ref/meshsim/test/Mach.java "$SIM"/oracle/ref/CcReq.java 2>&1 | grep -v '^Note:'
OUT=$( { echo "# cc_req on ref (CcReq; ref $(git -C "$SIM" log -1 --format=%h -- ref), images $(cat "$SIM/tests/m3-images/cc_req.code.bin" "$SIM/tests/m3-images/tile_req.code.bin" | sha256sum | cut -c1-16))"; \
        cd "$SIM/ref/meshsim" && java -cp "$B" CcReq "$SIM/tests/m3-images"; } ) || { echo "ccreq: ref failed"; exit 1; }
if [ "${1:-}" = --check ]; then
  [ "$OUT" = "$(cat "$SIM/tests/m3-images/cc_req.ref")" ] && echo "ccreq --check: equal to ref" && exit 0
  echo "ccreq --check: differs"; diff <(echo "$OUT") "$SIM/tests/m3-images/cc_req.ref"; exit 1
fi
echo "$OUT" > "$SIM/tests/m3-images/cc_req.ref"; head -3 "$SIM/tests/m3-images/cc_req.ref"; tail -3 "$SIM/tests/m3-images/cc_req.ref"
