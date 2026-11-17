#!/bin/sh
# searches.sh: the searches of the shrunken model on one link, as a table: what was searched, how many states,
# what came of it. Each line is one run of linksearch; "complete" means every state within the budgets was tried.
#   ./searches.sh [list]      list: main (default), rules, window, forged, big
cd "$(dirname "$0")" || exit 1
one() {
    out=$(build/linksearch "$@" 2>&1)
    states=$(echo "$out" | sed -n 's/^\([0-9]*\) states.*/\1/p')
    secs=$(echo "$out" | sed -n 's/.*; \([0-9.]*\) s$/\1/p')
    rest=$(echo "$out" | sed -n 's/.*the longest \([0-9]*\) cycles.*/\1/p')
    if echo "$out" | grep -q 'NOT complete'; then verdict="NOT complete";
    elif echo "$out" | grep -q '^RESULT: every'; then verdict="holds";
    else verdict="FAILS: $(echo "$out" | sed -n '/^RESULT: FAILED/,$p' | tail -n +2 | sed 's/^ *//' | tr '\n' ';')"; fi
    printf '%-88s %10s states  %6s s  rest<=%-4s %s\n' "$*" "$states" "$secs" "$rest" "$verdict"
}
P=proposed
case "${1:-main}" in
main)
    one --rules $P --packets 2 0 2 0 --damage 1
    one --rules $P --packets 1 0 1 0 --damage 2
    one --rules $P --packets 1 0 0 0 --damage 3
    one --rules $P --packets 2 0 0 0 --damage 2
    one --rules $P --packets 1 1 0 0 --damage 2
    one --rules $P --packets 2 2 0 0 --damage 1
    one --rules $P --packets 1 1 1 0 --damage 1
    one --rules $P --packets 3 0 0 0 --damage 1 --queue 2
    one --rules $P --packets 2 1 0 0 --damage 1 --queue 2
    one --rules $P --packets 7 0 0 0 --damage 1
    one --rules $P --packets 2 0 1 0 --damage 0 --noise 2
    one --rules $P --packets 2 0 1 0 --damage 1 --noise 1
    one --rules $P --packets 2 0 1 0 --damage 1 --delay 7
    one --rules $P --packets 2 0 1 0 --damage 1 --delay 20
    one --rules $P --packets 2 0 1 0 --damage 1 --tick 8
    one --rules $P --packets 1 0 1 0 --damage 2 --images
    one --rules $P --packets 2 0 2 0 --damage 0 --power 1
    one --rules $P --packets 1 0 1 0 --damage 1 --power 1
    one --rules $P --packets 2 0 1 0 --damage 1 --flip 1
    one --rules $P,window=4,number_bits=4 --packets 5 0 0 0 --damage 1 --queue 4
    one --rules $P --packets 1 0 1 0 --damage 1 --whole
    one --rules $P --packets 1 0 1 0 --damage 1
    ;;
rules)   # each rule that the search has not shown to be needed, taken away or put in
    for r in -lost_each_stream -lost_uses_ack -long_is_lost -status_each +stop_lift_back +ack_now +ask_every ,resend_timer=96 -zero_number,resend_timer=96; do
        one --rules $P$r --packets 1 0 1 0 --damage 2
        one --rules $P$r --packets 1 1 0 0 --damage 2
        one --rules $P$r --packets 3 0 0 0 --damage 1 --queue 2
    done
    ;;
window)  # G11 without its rule, the window no more than half the queue (in packets): the stop bit alone
    one --rules $P-refuse --packets 4 0 0 0 --damage 2 --queue 4
    one --rules $P-refuse --packets 5 0 0 0 --damage 1 --queue 4
    one --rules $P-refuse --packets 3 1 0 0 --damage 1 --queue 4
    ;;
forged)  # one header forged to pass its CRC, with one packet damaged: field by field
    for f in forge-ack forge-stop forge-lost forge-recovery forge-resend forge-number forge-ack-data; do
        one --rules $P --packets 1 0 1 0 --damage 1 --forge 1 --fates gone,first0,header,data,$f --all
    done
    one --rules $P --packets 3 0 0 0 --damage 1 --forge 1 --fates gone,first0,header,data,forge-stop --queue 2 --all
    ;;
big)     # for the runner: hours and gigabytes
    one --rules $P --packets 2 0 2 0 --damage 2
    one --rules $P --packets 2 0 1 0 --damage 3
    one --rules $P --packets 2 2 0 0 --damage 2
    one --rules $P --packets 3 0 3 0 --damage 1
    ;;
esac
