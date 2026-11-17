#!/bin/sh
# plants.sh: each planted bug (link_params.h, Plant) against the search. A check that catches no planted bug proves
# nothing: every line must end in "caught". Uses the build with the plants compiled in (build/clang).
# The last configuration asks "resend" again at once (reask=1): the only way found to put a packet on the wire that
# its receiver has already taken, which is what DUPLICATE_TAKEN needs. With the header of 20 bytes a sender goes
# back exactly as far as it must, and in the random runs no packet arrives twice.
#   ./plants.sh [max states for each search]
cd "$(dirname "$0")" || exit 1
max=${1:-3000000}
missed=0
i=0
for name in ACK_ONE_OFF WINDOW_NOT_KEPT WRAP_WRONG TIMER_NOT_RESTARTED DUPLICATE_TAKEN STOP_IGNORED RECOVERY_ONCE \
            ZERO_TAKES_NUMBER ACK_FROM_BAD_DATA NO_ZEROS OTHER_STREAM_STALE HOPS_NOT_COUNTED NOT_SEALED_AGAIN OWN_BEFORE_FORWARDED; do
    i=$((i + 1))
    found=
    for cfg in "--damage 1 --packets 2 0 1 0" "--damage 1 --packets 3 0 0 0 --queue 2" "--damage 0 --packets 7 0 0 0" \
               "--damage 2 --packets 1 0 1 0" "--damage 1 --forge 1 --fates gone,forge-ack-data --packets 1 0 1 0" \
               "--damage 0 --packets 0 3 5 0 --still 150" "--damage 0 --chain 2 --cpackets 0 0 2" \
               "@,reask=1 --damage 1 --packets 2 0 1 0"; do
        more=; case "$cfg" in @*) more=${cfg%% *}; more=${more#@}; cfg=${cfg#* };; esac     # "@,size=value ..." adds to the rules
        out=$(build/clang/linksearch --rules "proposed$more,plant=$i" $cfg --max-states "$max" | grep '^FAILED' | cut -c1-110)
        if [ -n "$out" ]; then found="[$cfg] $out"; break; fi
    done
    if [ -n "$found" ]; then echo "$name: caught $found"; else echo "$name: NOT CAUGHT"; missed=$((missed + 1)); fi
done
[ "$missed" = 0 ] && echo "plants: every one caught" || echo "plants: $missed NOT caught"
[ "$missed" = 0 ]
