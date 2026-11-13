#!/bin/bash
# p4_lines.sh: a partial run's printed lines against a fixture, while a suite is ported group by group.
#   p4_lines.sh FIXTURE CMD...    runs CMD; its output, the final "<suite>: N passed, M failed" line aside, must appear
#                                 as one contiguous block of FIXTURE's lines; and no FAIL line. Prints the block's place.
#   P4_ALLOW_FAIL=1 p4_lines.sh   the same, the FAIL lines (and a violation report continuing them: "violations: N", its
#                                 indented lines, the blank line ending it) listed and left out of the comparison: printed lines only
# The whole-suite check (every line and the counts, byte for byte) is test_p4suites's; this is only for the way there.
set -u
fix=$1; shift
o=$(mktemp); trap 'rm -f "$o" "$o.b"' EXIT
"$@" > "$o" 2>&1; rc=$?
if [ -n "${P4_ALLOW_FAIL:-}" ]; then
  grep "^FAIL" "$o" | sed 's/^/  (allowed) /' | cut -c1-200
  awk '/^FAIL/{skip=1; next} skip && /^$/{skip=0; next} skip && (/^  / || /^violations: [0-9]+$/){next} {skip=0; print}' "$o" > "$o.f"; mv "$o.f" "$o"
else
  grep -q "^FAIL" "$o" && { grep "^FAIL" "$o" | head -20; echo "p4_lines: FAIL lines (exit $rc)"; exit 1; }
fi
head -n -1 "$o" > "$o.b"; n=$(wc -l < "$o.b")
[ "$n" -gt 0 ] || { echo "p4_lines: no lines printed"; cat "$o"; exit 1; }
total=$(wc -l < "$fix")
for ((i = 1; i + n - 1 <= total; i++)); do
  if sed -n "${i},$((i + n - 1))p" "$fix" | cmp -s - "$o.b"; then echo "p4_lines: equal, $n lines = $fix lines $i-$((i + n - 1)); $(tail -1 "$o")"; exit 0; fi
done
echo "p4_lines: $n lines not found in $fix; diff against the nearest start:"
first=$(head -1 "$o.b" | cut -c1-30); i=$(grep -n -F -- "$first" "$fix" | head -1 | cut -d: -f1); i=${i:-1}
diff <(sed -n "${i},$((i + n - 1))p" "$fix") "$o.b" | head -40
exit 1
