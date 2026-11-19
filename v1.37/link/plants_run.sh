#!/bin/sh
# plants_run.sh: each planted bug against the random runs: in how many of 20 seeds it is caught. Uses the build with
# the plants compiled in (build/clang). A bug that no seed catches is left to the search (plants.sh), and said so.
cd "$(dirname "$0")" || exit 1
i=0
for name in ACK_ONE_OFF WINDOW_NOT_KEPT WRAP_WRONG TIMER_NOT_RESTARTED DUPLICATE_TAKEN STOP_IGNORED RECOVERY_ONCE \
            ZERO_TAKES_NUMBER ACK_FROM_BAD_DATA NO_ZEROS HOPS_NOT_COUNTED NOT_SEALED_AGAIN OWN_BEFORE_FORWARDED \
            WRONG_FLAG_CLEARED ERROR_ON_RELEASE RESTART_ANY_DATA CHECKSUM_KEPT MASK_KEPT; do
    i=$((i + 1))
    n=$(build/clang/linkrun --seeds 1 "${1:-20}" --cycles 60000 --plant $i | sed -n 's/^runs.*FAILED \([0-9]*\)$/\1/p')
    echo "$name: caught in $n of ${1:-20} runs"
done
