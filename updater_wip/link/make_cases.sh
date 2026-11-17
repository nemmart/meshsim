#!/bin/sh
# make_cases.sh: finds each kept case again with the search and writes cases/: the shortest failure there is under
# the rules named first, then "run" lines for the rules under which it must hold. Run it after a change that moves
# the timing; linkcase (make check) then says whether every case still fails and holds as it should.
cd "$(dirname "$0")" || exit 1
R2=as_written+resend_ahead; R3=$R2+zero_number; R4=$R3+lost_answers; IM=",max_words=2,flush_zeros=52,zeros=64"
gen() {
    name=$1; note=$2; fixed=$3; shift 3
    build/linksearch "$@" --out cases/$name.case > /tmp/make_cases.log
    if grep -q '^FAILED' /tmp/make_cases.log; then
        sed -i "1i # $note" cases/$name.case
        for f in $fixed; do echo "run $f ok" >> cases/$name.case; done
    else echo "$name: NO FAILURE FOUND"; fi
}
gen g6-wrong-number "QUESTIONS.md G6: a packet is lost; the next arrives good with the wrong number, and HL has no rule for it" "$R2 proposed" --rules as_written --damage 1 --packets 2 0 2 0
gen g7-last-packet-gone "QUESTIONS.md G7: the last packet of a burst is lost, and nothing sends it again" "$R3 as_written+resend_ahead,resend_timer=96 proposed" --rules $R2 --damage 1 --packets 2 0 2 0
gen g9-both-lost "QUESTIONS.md G9: both ends lose their place at once; each waits for the other's recovery" "$R4 proposed" --rules $R3 --damage 2 --packets 2 0 2 0
gen g10-data-taken-for-a-packet "QUESTIONS.md G10: after a failed header CRC the receiver takes the packet's data for a packet (the data is a header's image)" "$R4+flush$IM proposed$IM" --rules $R4$IM --damage 2 --packets 1 0 1 0 --images --words 2
gen g11-overrun "QUESTIONS.md G11: a window larger than the room behind stop, and the packet that says stop lost" "$R4+refuse,window=4,number_bits=4 proposed,window=4,number_bits=4" --rules $R4,window=4,number_bits=4 --queue 4 --packets 5 0 0 0 --damage 1
gen g4-power-on "QUESTIONS.md G4: end 1 starts again from power-on in the middle" "proposed" --rules proposed-restart --damage 0 --power 1 --packets 2 0 2 0
gen g20-false-acknowledgement "QUESTIONS.md G20: a forged acknowledge field releases a packet that was lost on the wire" "proposed" --rules proposed-tell_behind --damage 1 --forge 1 --fates gone,first0,header,data,forge-ack --packets 1 0 1 0
gen g21-kept-packet-bad "QUESTIONS.md G21: a bit flips in a packet the sender keeps" "proposed" --rules proposed-check_kept --damage 0 --flip 1 --packets 2 0 1 0
gen g22-made-wrong "QUESTIONS.md G22: a packet its own sender made wrong (hops 15 at an end that would forward it)" "proposed" --rules proposed-made_wrong --damage 0 --wrong 1 --packets 2 0 1 0
gen forged-number "QUESTIONS.md F1: a forged packet number: a later packet taken for a lost one before it. A KNOWN LIMIT (owner, 2026-11-17): this case fails under the proposed rules" "" --rules proposed --damage 1 --forge 1 --fates gone,first0,header,data,forge-number-back --packets 2 0 0 0
