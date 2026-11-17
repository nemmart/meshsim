#!/bin/sh
# searches.sh: the searches of the shrunken model on one link, as a table: what was searched, how many states,
# what came of it. Each line is one run of linksearch; "complete" means every state within the budgets was tried.
#   ./searches.sh [list]      list: main (default), starve, rules, window, forged, chain; big, big2 to big6 for the runner
cd "$(dirname "$0")" || exit 1
one() {
    out=$(build/linksearch "$@" 2>&1)
    states=$(echo "$out" | sed -n 's/^\([0-9]*\) states.*/\1/p')
    secs=$(echo "$out" | sed -n 's/.*; \([0-9.]*\) s$/\1/p')
    rest=$(echo "$out" | sed -n 's/.*the longest \([0-9]*\) cycles.*/\1/p')
    if ! echo "$out" | grep -q '^RESULT'; then verdict="NO RESULT: the search was stopped or killed (memory?)";
    elif echo "$out" | grep -q 'NOT complete'; then verdict="NOT complete";
    elif echo "$out" | grep -q '^RESULT: every'; then verdict="holds";
    else verdict="FAILS: $(echo "$out" | sed -n '/^RESULT: FAILED/,$p' | tail -n +2 | sed 's/^ *//' | tr '\n' ';')"; fi
    printf '%-88s %10s states  %6s s  rest<=%-4s %s\n' "$*" "$states" "$secs" "$rest" "$verdict"
}
P=proposed
IM=",max_words=2,flush_zeros=52,zeros=64"     # sizes for data that is a header's image: 20 bytes do not fit one word
case "${1:-main}" in
main)
    one --rules $P --packets 2 0 2 0 --damage 1
    one --rules $P --packets 1 0 1 0 --damage 2
    one --rules $P --packets 1 0 0 0 --damage 3
    one --rules $P --packets 2 0 0 0 --damage 2
    one --rules $P --packets 1 1 0 0 --damage 2
    one --rules $P --packets 2 2 0 0 --damage 1
    one --rules $P --packets 1 1 1 0 --damage 1
    one --rules $P --packets 3 0 0 0 --damage 1 --queue 4
    one --rules $P --packets 3 0 0 0 --damage 1 --queue 2
    one --rules $P --packets 2 1 0 0 --damage 1 --queue 4
    one --rules $P --packets 7 0 0 0 --damage 1
    one --rules $P --packets 2 0 1 0 --damage 0 --noise 2
    one --rules $P --packets 2 0 1 0 --damage 1 --noise 1
    one --rules $P --packets 2 0 1 0 --damage 1 --delay 7
    one --rules $P --packets 2 0 1 0 --damage 1 --delay 20
    one --rules $P --packets 2 0 1 0 --damage 1 --tick 8
    one --rules $P$IM --packets 1 0 1 0 --damage 2 --images --words 2
    one --rules $P --packets 2 0 2 0 --damage 0 --power 1
    one --rules $P --packets 1 0 1 0 --damage 1 --power 1
    one --rules $P --packets 2 0 1 0 --damage 1 --flip 1
    one --rules $P --packets 2 0 1 0 --damage 1 --wrong 1
    one --rules $P,number_bits=4 --packets 5 0 0 0 --damage 1 --queue 4 --window0 4
    one --rules $P --packets 1 0 1 0 --damage 1 --whole
    one --rules $P --packets 1 0 1 0 --damage 1
    ;;
starve)  # the header of 16 bytes starved one stream's acknowledgements under the other's data. With 20 bytes, one
         # timer and no rule for it: a sender never waits 150 cycles for an acknowledgement, while the other side
         # sends 5 or 8 packets back to back (180 and 288 cycles). plants.sh shows the same search catching a header
         # that does not speak for the other stream
    one --rules as_written --packets 0 3 5 0 --damage 0 --still 150
    one --rules $P-ack_now --packets 0 3 5 0 --damage 0 --still 150
    one --rules $P --packets 0 3 5 0 --damage 0 --still 150
    one --rules $P --packets 0 2 8 0 --damage 0 --still 150
    ;;
rules)   # each rule that the search has not shown to be needed, taken away or put in, and the sizes of two timers
    for r in -lost_uses_ack -long_is_lost +stop_lift_back -ack_now +ask_every ,resend_timer=96 -zero_number,resend_timer=96 ,reask=24 ,reask=400; do
        one --rules $P$r --packets 1 0 1 0 --damage 2
        one --rules $P$r --packets 1 1 0 0 --damage 2
        one --rules $P$r --packets 3 0 0 0 --damage 1 --queue 4
    done
    ;;
window)  # G11 without its rule: the stop bit alone. The window a quarter of the queue (the default) and half of it
    one --rules $P-refuse --packets 4 0 0 0 --damage 2
    one --rules $P-refuse --packets 4 0 0 0 --damage 2 --queue 4
    one --rules $P-refuse --packets 5 0 0 0 --damage 1 --queue 4
    one --rules $P-refuse --packets 3 1 0 0 --damage 1 --queue 4
    ;;
forged)  # one header forged to pass its CRC, with one packet damaged: field by field
    for f in forge-ack forge-stop forge-lost forge-recovery forge-resend forge-number forge-number-back forge-ack-data; do
        one --rules $P --packets 1 0 1 0 --damage 1 --forge 1 --fates gone,first0,header,data,$f --all
    done
    one --rules $P --packets 3 0 0 0 --damage 1 --forge 1 --fates gone,first0,header,data,forge-stop --queue 2 --all
    one --rules $P --packets 2 0 1 0 --damage 1 --forge 1 --fates gone,first0,header,data,forge-number --all
    one --rules $P --packets 1 0 1 0 --damage 2 --forge 1 --fates gone,first0,header,data,forge-number --all
    one --rules $P --packets 2 0 0 0 --damage 1 --forge 1 --fates gone,first0,header,data,forge-number-back --all
    ;;
chain)   # the chain: the host and one or two nodes; packets from the host on each stream, and from each node
    one --rules $P --chain 1 --cpackets 1 1 1 --damage 1
    one --rules $P --chain 1 --cpackets 2 0 1 --damage 1
    one --rules $P --chain 1 --cpackets 1 0 1 --damage 2
    one --rules $P --chain 1 --cpackets 0 2 1 --damage 1 --queue 2
    one --rules $P --chain 2 --cpackets 1 0 0 --damage 2
    one --rules $P --chain 2 --cpackets 0 1 0 --damage 2
    one --rules $P --chain 2 --cpackets 0 0 1 --damage 1
    one --rules $P --chain 2 --cpackets 1 0 1 --damage 0
    one --rules $P --chain 2 --cpackets 0 2 0 --damage 0 --queue 2 --fwd 2
    one --rules $P --chain 2 --cpackets 0 1 0 --damage 1 --queue 2 --fwd 2
    one --rules $P --chain 2 --cpackets 0 0 2 --damage 0 --fwd 2
    one --rules $P --chain 3 --cpackets 1 0 0 --damage 1
    one --rules $P --chain 3 --cpackets 0 0 1 --damage 0
    ;;
big)     # for the runner: one a task
    one --rules $P --packets 2 0 2 0 --damage 2 --max-states ${MAX:-150000000}
    ;;
big2) one --rules $P --packets 2 0 1 0 --damage 3 --max-states ${MAX:-150000000} ;;
big3) one --rules $P --packets 2 2 0 0 --damage 2 --max-states ${MAX:-150000000} ;;
big4) one --rules $P --packets 3 0 3 0 --damage 1 --max-states ${MAX:-150000000} ;;
big5) one --rules $P --chain 2 --cpackets 1 1 1 --damage 1 --max-states ${MAX:-150000000} ;;
big6) one --rules $P --chain 2 --cpackets 1 0 1 --damage 2 --max-states ${MAX:-150000000} ;;
big7) one --rules $P --chain 2 --cpackets 0 2 0 --damage 1 --queue 2 --fwd 2 --max-states ${MAX:-150000000} ;;
esac
