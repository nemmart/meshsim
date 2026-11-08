#!/bin/bash
# trace.sh OUT args... (WORKER10): footprints of a bench_chip run (BENCH=bench_sort: of the sort) --trace-map: the map is
# written by a first run under valgrind (--tool=none: valgrind's layout, the same as lackey's for the same arguments),
# then lackey's trace of a second, identical run is filtered against it as it streams; the second run's own map must
# equal the first's, else the result is refused. Run from sim/; needs valgrind, build/clang-release/{bench_chip,trace_filter}.
set -u
OUT=$1; shift
MAP1=$(mktemp); MAP2=$(mktemp)
valgrind --tool=none ./build/clang-release/${BENCH:-bench_chip} "$@" --trace-map "$MAP1" > /dev/null 2>&1 || { echo "trace.sh: the map run failed"; exit 1; }
valgrind --tool=lackey --trace-mem=yes --log-fd=9 ./build/clang-release/${BENCH:-bench_chip} "$@" --trace-map "$MAP2" 9>&1 1>/dev/null 2>/dev/null \
  | ./build/clang-release/trace_filter "$MAP1" > "$OUT"
if cmp -s "$MAP1" "$MAP2"; then echo "trace.sh: the maps agree" >> "$OUT"; rc=0
else echo "trace.sh: REFUSED: the traced run's map differs from the first run's ($(diff "$MAP1" "$MAP2" | wc -l) lines)" >> "$OUT"; rc=1; fi
rm -f "$MAP1" "$MAP2"
exit $rc
