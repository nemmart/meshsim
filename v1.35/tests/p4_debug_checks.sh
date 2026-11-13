#!/bin/bash
# p4_debug_checks.sh: every P4 suite under the framework's debug options (p4_debug.h): --step-all and
# --instance-major must print exactly what the plain run prints (and, for the steps equal to v1.3's, its log), at each
# step's seeds. B: the build directory (default build/clang-suite). Run from sim/.
set -u
B=${1:-build/clang-suite}
fail=0
for cmd in "suite_net -1 quick" "suite_net 5 quick" "suite_net 11 quick" "suite_net -1" "suite_mem -1" "suite_mem 3" "suite_mem 8" \
           "suite_engine -1" "suite_engine 4" "suite_ag -1" "suite_ag 4" "suite_rdport -1" "suite_rdport 4" "suite_search" \
           "suite_reqbus -1" "suite_reqbus 4" "suite_agbench"; do
  "$B"/$cmd > /tmp/p4dc.plain 2>&1
  for opt in --step-all --instance-major; do
    "$B"/$cmd $opt > /tmp/p4dc.opt 2>&1
    if cmp -s /tmp/p4dc.plain /tmp/p4dc.opt; then echo "same  $cmd $opt"; else echo "DIFF  $cmd $opt"; fail=1; fi
  done
done
rm -f /tmp/p4dc.plain /tmp/p4dc.opt
[ $fail = 0 ] && echo "p4_debug_checks: every suite the same under step-all and instance-major" || echo "p4_debug_checks: differences"
exit $fail
