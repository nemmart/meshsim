#!/bin/sh
# plants.sh: each planted bug (link_params.h, Plant) against the search. A check that catches no planted bug proves
# nothing: every line must end in "caught". Uses the build with the plants compiled in (build/clang).
#   ./plants.sh [max states for each search]
cd "$(dirname "$0")" || exit 1
max=${1:-3000000}
missed=0
i=0
for name in ACK_ONE_OFF WINDOW_NOT_KEPT WRAP_WRONG TIMER_NOT_RESTARTED DUPLICATE_TAKEN STOP_IGNORED RECOVERY_ONCE \
            ZERO_TAKES_NUMBER ACK_FROM_BAD_DATA NO_ZEROS; do
    i=$((i + 1))
    found=
    for cfg in "--damage 1 --packets 2 0 1 0" "--damage 1 --packets 3 0 0 0 --queue 2" "--damage 0 --packets 7 0 0 0" \
               "--damage 2 --packets 1 0 1 0" "--damage 1 --forge 1 --fates gone,forge-ack-data --packets 1 0 1 0"; do
        out=$(build/clang/linksearch --rules "proposed,plant=$i" $cfg --max-states "$max" | grep '^FAILED' | cut -c1-110)
        if [ -n "$out" ]; then found="[$cfg] $out"; break; fi
    done
    if [ -n "$found" ]; then echo "$name: caught $found"; else echo "$name: NOT CAUGHT"; missed=$((missed + 1)); fi
done
[ "$missed" = 0 ] && echo "plants: every one caught" || echo "plants: $missed NOT caught"
[ "$missed" = 0 ]
