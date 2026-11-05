#!/bin/bash
# The quick suite of run_tests.sh (its "$J ..." steps outside the full-mode block), Java only (the tile images, the boot
# ROM and the replay controller are prebuilt in the kit), up to P at a time; each step's log goes to OUT/<LOG>.log.
OUT=$1; P=${P:-6}; mkdir -p "$OUT"
awk '/^if \[ "\$MODE" != quick \]/{full=1} /^fi$/{full=0} !full && /^LOG=/ && /\$J /' run_tests.sh | while read -r line; do
  log=$(echo "$line" | sed 's/^LOG=\([^ ]*\).*/\1/'); cmd=$(echo "$line" | sed 's/.*\$J //; s/\$Q/quick/')
  echo "$log|$cmd"
done > "$OUT/steps.txt"
run() { log=${1%%|*}; cmd=${1#*|}; java -Xmx3g -cp build $cmd > "$OUT/$log.log" 2>&1; echo "$log rc=$?" >> "$OUT/rc.txt"; }
export -f run; export OUT
[ -n "${ONLY:-}" ] && grep -E "$ONLY" "$OUT/steps.txt" > "$OUT/s2" && mv "$OUT/s2" "$OUT/steps.txt"
cat "$OUT/steps.txt" | xargs -P "$P" -I{} bash -c 'run "{}"'
F=$(grep -c 'rc=[^0]' "$OUT/rc.txt"); X=$(grep -l '^FAIL' "$OUT"/*.log | wc -l); N=$(wc -l < "$OUT/steps.txt")
{ sort "$OUT/rc.txt"; for f in "$OUT"/*.log; do echo "$(basename $f): $(grep -E 'passed, [0-9]+ failed|windows, [0-9]+ failed' $f | tail -1)"; done; echo "$N steps; nonzero exits $F, logs with FAIL $X"; } > "$OUT/VERDICT.txt"
[ "$F" = 0 ] && [ "$X" = 0 ]
