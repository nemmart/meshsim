#!/bin/bash
# shardsummary.sh -- the acceptance kit's summary step against v1.3's (WORKER25, P7b): v1.3's own inline Python, taken
# verbatim from sim/ref/meshsim/acceptance/run_shard.sh, run on each case in tests/acceptance/summary/<case>/
# (results.csv, args: PROGRAM FIRST N STAGES WALL JOBS FULL_EVERY; the manifest tests/acceptance/summary/manifest.json);
# its SUMMARY.txt, VERDICT.txt and exit code are the fixtures (<case>/expect/) that tests/test_shard_summary compares
# shard_summary with. Needs python3 (the oracle side only; v1.35 has none, D40). sim/ref is only read.
#
#   shardsummary.sh --fixtures   (re)write every case's expect/
#   shardsummary.sh --check      regenerate them and compare with the committed ones
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
D=$SIM/tests/acceptance/summary
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
sed -n "/^python3 - .*<<'PY'\$/,/^PY\$/p" "$SIM/ref/meshsim/acceptance/run_shard.sh" | sed '1d;$d' > "$T/summary.py"
[ "$(wc -l < "$T/summary.py")" -gt 30 ] || { echo "shardsummary: v1.3's Python not found in run_shard.sh"; exit 2; }
bad=0
for c in "$D"/*/; do
  c=${c%/}; n=$(basename "$c"); mkdir -p "$T/$n"; cp "$c/results.csv" "$T/$n/"
  read -r P F N ST W J X < "$c/args"
  python3 "$T/summary.py" "$T/$n" "$P" "$F" "$N" "$ST" "$W" "$J" "$X" "$D/manifest.json"; echo $? > "$T/$n/EXIT"
  case "${1:-}" in
    --fixtures) mkdir -p "$c/expect"; cp "$T/$n/SUMMARY.txt" "$T/$n/VERDICT.txt" "$T/$n/EXIT" "$c/expect/";;
    --check) for f in SUMMARY.txt VERDICT.txt EXIT; do cmp -s "$T/$n/$f" "$c/expect/$f" || { echo "$n/$f differs"; bad=1; }; done;;
    *) echo "usage: shardsummary.sh --fixtures|--check"; exit 2;;
  esac
done
[ "${1:-}" = --check ] && [ $bad = 0 ] && echo "shardsummary: v1.3's summary equals the committed fixtures"
exit $bad
