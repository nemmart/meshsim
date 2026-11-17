#!/bin/sh
# searches.sh: the searches of the shrunken model, as a table: what was searched, how many states, what came of it.
# Each line is one run of linksearch; "holds" means every state within the budgets was tried and every property held.
#   ./searches.sh [list]      list: main (default), held, starve, rules, window, forged, nested, chain; big1 to big18 are larger,
#                             for the runner, one a task
cd "$(dirname "$0")" || exit 1
one() {
    out=$(${LINKSEARCH:-build/linksearch} "$@" 2>&1)
    states=$(echo "$out" | sed -n 's/^\([0-9]*\) states.*/\1/p')
    secs=$(echo "$out" | sed -n 's/.*; \([0-9.]*\) s$/\1/p')
    rest=$(echo "$out" | sed -n 's/.*the longest \([0-9]*\) cycles.*/\1/p')
    if ! echo "$out" | grep -q '^RESULT'; then verdict="NO RESULT: the search was stopped or killed (memory?)";
    elif echo "$out" | grep -q 'NOT complete'; then verdict="NOT complete";
    elif echo "$out" | grep -q '^RESULT: every'; then verdict="holds";
    else verdict="FAILS: $(echo "$out" | sed -n '/^RESULT: FAILED/,$p' | tail -n +2 | sed 's/^ *//' | tr '\n' ';')"; fi
    printf '%-96s %10s states  %6s s  rest<=%-4s %s\n' "$*" "$states" "$secs" "$rest" "$verdict"
}
M="--max-states ${MAX:-150000000} ${SECS:+--seconds $SECS}"     # the runner's: MAX states, SECS seconds, then NOT complete
F=gone,first0,header,data
case "${1:-main}" in
main)    # one link: the list of the headers before, for the state counts side by side
    one --packets 2 0 2 0 --damage 1
    one --packets 1 0 1 0 --damage 2
    one --packets 1 0 0 0 --damage 3
    one --packets 2 0 0 0 --damage 2
    one --packets 1 1 0 0 --damage 2
    one --packets 2 2 0 0 --damage 1
    one --packets 1 1 1 0 --damage 1
    one --packets 3 0 0 0 --damage 1 --queue 4
    one --packets 3 0 0 0 --damage 1 --queue 2
    one --packets 2 1 0 0 --damage 1 --queue 4
    one --packets 7 0 0 0 --damage 1
    one --packets 2 0 1 0 --damage 0 --noise 2
    one --packets 2 0 1 0 --damage 1 --noise 1
    one --packets 2 0 1 0 --damage 1 --delay 7
    one --packets 2 0 1 0 --damage 1 --delay 20
    one --packets 2 0 1 0 --damage 1 --tick 8
    one --packets 1 0 1 0 --damage 2 --images --fates $F
    one --packets 2 0 2 0 --damage 0 --power 1
    one --packets 1 0 1 0 --damage 1 --power 1
    one --packets 2 0 1 0 --damage 1 --flip 1
    one --packets 2 0 1 0 --damage 1 --wrong 1
    one --rules hl,number_bits=4 --packets 5 0 0 0 --damage 1 --queue 4 --window0 4
    one --packets 1 0 1 0 --damage 1 --whole
    one --packets 1 0 1 0 --damage 1
    ;;
held)    # HL, "a stopped stream cannot block the other": from every state one stream is never taken again, and the
         # other must finish (--hold 2 the background held, 1 the foreground, 3 each in turn). The queues are 4 words,
         # so that "stop" comes after two packets and the window of 2 is half a queue. The first line is HL as first
         # written and must fail
    one --rules hl+stop_holds_again --packets 1 3 0 0 --queue 4 --damage 1 --hold 2
    one --packets 1 3 0 0 --queue 4 --damage 1 --hold 3
    one --packets 3 1 0 0 --queue 4 --damage 1 --hold 3
    one --packets 1 3 0 0 --queue 4 --damage 2 --hold 2
    one --packets 1 2 1 0 --queue 4 --damage 1 --hold 3
    one --packets 1 3 0 0 --queue 4 --damage 0 --noise 2 --hold 3
    one --chain 1 --cpackets 1 3 0 --queue 4 --damage 1 --hold 3
    one --chain 1 --cpackets 1 3 1 --queue 4 --damage 0 --hold 3
    ;;
starve)  # the first header (numbers for each stream) starved one stream's acknowledgements under the other's data. A
         # sender never waits 150 cycles for an acknowledgement while the other side sends 5 or 8 packets back to back
    one --rules hl-ack_now --packets 0 3 5 0 --damage 0 --still 150
    one --packets 0 3 5 0 --damage 0 --still 150
    one --packets 0 2 8 0 --damage 0 --still 150
    ;;
rules)   # each rule that the search has not shown to be needed, taken away, and sizes either way
    for r in -lost_uses_ack -ack_now ,reask=24 ,reask=400 ,pass=0 ,pass=4; do
        one --rules hl$r --packets 1 0 1 0 --damage 2
        one --rules hl$r --packets 1 1 0 0 --damage 2
        one --rules hl$r --packets 3 0 0 0 --damage 1 --queue 4
    done
    ;;
window)  # a packet with no room, without the rule that refuses it: the stop bit alone. The window a quarter of the
         # queue (the default) and half of it
    one --rules hl-refuse --packets 4 0 0 0 --damage 2
    one --rules hl-refuse --packets 4 0 0 0 --damage 2 --queue 4
    one --rules hl-refuse --packets 5 0 0 0 --damage 1 --queue 4
    one --rules hl-refuse --packets 3 1 0 0 --damage 1 --queue 4
    ;;
forged)  # one header forged to pass its CRC, with one packet damaged: field by field. --all: every kind of failure
    for f in forge-ack forge-stop forge-stop-bg forge-lost forge-recovery forge-resend forge-number forge-number-back forge-ack-data \
             forge-wrong forge-long forge-restart; do
        one --packets 1 0 1 0 --damage 1 --forge 1 --fates $F,$f --all
    done
    one --packets 3 0 0 0 --damage 1 --forge 1 --fates $F,forge-stop --queue 2 --all
    one --packets 1 3 0 0 --damage 1 --forge 1 --fates $F,forge-stop-bg --queue 4 --hold 3 --all
    one --packets 2 0 1 0 --damage 1 --forge 1 --fates $F,forge-ack --all
    one --packets 3 0 0 0 --damage 1 --forge 1 --fates $F,forge-ack --all
    one --packets 2 0 1 0 --damage 0 --forge 2 --fates forge-ack --all
    one --packets 2 0 1 0 --damage 1 --forge 1 --fates $F,forge-wrong --all
    one --packets 2 0 1 0 --damage 1 --forge 1 --fates $F,forge-long --all
    one --packets 2 0 1 0 --damage 0 --forge 2 --fates forge-wrong,forge-long --all
    one --packets 2 0 1 0 --damage 1 --forge 1 --fates $F,forge-number --all
    one --packets 1 0 1 0 --damage 2 --forge 1 --fates $F,forge-number --all
    one --packets 2 0 0 0 --damage 1 --forge 1 --fates $F,forge-number-back --all
    ;;
nested)  # data that holds headers, or whole packets. Lines 2 to 4: the zeros a lost receiver waits for, against data
         # that is a word of zeros and then a header's image, without the fate head0: one more than the zeros a packet
         # can hold is enough, and no fewer. Then with head0, a header that reads as zeros in front of such data: no
         # check fails, and the header in the data is believed. With a header's image and a second packet damaged the
         # link starts again; with a whole packet in the data its word is delivered and nothing is said (REPORT.md)
    one --rules hl,max_words=2,flush_zeros=48,zeros=64 --packets 1 0 1 0 --damage 1 --words 2
    one --rules hl,max_words=2,flush_zeros=48,zeros=64 --packets 1 0 1 0 --damage 2 --images2 --words 2 --fates $F
    one --rules hl,max_words=2,flush_zeros=17,zeros=64 --packets 1 0 1 0 --damage 2 --images2 --words 2 --fates $F
    one --rules hl,max_words=2,flush_zeros=16,zeros=64 --packets 1 0 1 0 --damage 2 --images2 --words 2 --fates $F
    one --packets 1 0 1 0 --damage 2 --images --all
    one --rules hl,max_words=2,flush_zeros=48,zeros=64 --packets 1 0 0 0 --damage 1 --images3 --words 2 --all
    one --rules hl,max_words=2,flush_zeros=48,zeros=64 --packets 2 0 1 0 --damage 1 --images --words 2 --all
    ;;
chain)   # the chain: the host and one, two or three nodes; packets from the host on each stream, and from each node
    one --chain 1 --cpackets 1 1 1 --damage 1
    one --chain 1 --cpackets 2 0 1 --damage 1
    one --chain 1 --cpackets 1 0 1 --damage 2
    one --chain 1 --cpackets 0 2 1 --damage 1 --queue 2
    one --chain 2 --cpackets 1 0 0 --damage 2
    one --chain 2 --cpackets 0 1 0 --damage 2
    one --chain 2 --cpackets 0 0 1 --damage 1
    one --chain 2 --cpackets 1 0 1 --damage 0
    one --chain 2 --cpackets 0 2 0 --damage 0 --queue 2 --fwd 2
    one --chain 2 --cpackets 0 1 0 --damage 1 --queue 2 --fwd 2
    one --chain 2 --cpackets 0 0 2 --damage 0 --fwd 2
    one --chain 3 --cpackets 1 0 0 --damage 1
    one --chain 3 --cpackets 0 0 1 --damage 0
    ;;
big1)  one --packets 2 4 1 0 --queue 4 --damage 1 --hold 3 $M ;;
big2)  one --packets 2 0 2 0 --damage 2 $M ;;
big3)  one --packets 2 0 1 0 --damage 3 $M ;;
big4)  one --packets 2 2 0 0 --damage 2 --hold 3 $M ;;
big5)  one --packets 3 0 3 0 --damage 1 $M ;;
big6)  one --packets 2 3 1 0 --queue 4 --damage 2 --hold 2 $M ;;
big7)  one --chain 2 --cpackets 1 1 1 --damage 1 $M ;;
big8)  one --chain 2 --cpackets 1 0 1 --damage 2 $M ;;
big9)  one --chain 2 --cpackets 1 3 0 --queue 4 --fwd 4 --damage 1 --hold 3 $M ;;
big10) one --chain 3 --cpackets 1 0 1 --damage 1 $M ;;
big11) one --chain 1 --cpackets 1 3 1 --queue 4 --damage 1 --hold 3 $M ;;
big12) one --packets 2 2 2 0 --damage 1 --hold 3 $M ;;
big13) one --packets 1 2 1 2 --queue 4 --damage 1 --hold 3 $M ;;
big14) one --packets 1 3 0 0 --queue 4 --damage 3 --hold 2 $M ;;
big15) one --chain 2 --cpackets 1 2 1 --queue 4 --fwd 4 --damage 1 --hold 3 $M ;;
big17) one --packets 1 3 1 0 --queue 4 --damage 1 --hold 3 --delay 7 $M ;;
big18) one --chain 2 --cpackets 1 3 0 --queue 4 --fwd 4 --damage 0 --hold 3 $M ;;
big16) one --packets 2 0 1 0 --damage 1 --forge 2 --fates gone,first0,header,data,forge-ack,forge-wrong,forge-long --all $M ;;
esac
