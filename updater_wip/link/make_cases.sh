#!/bin/sh
# make_cases.sh: finds each kept case again with the search and writes cases/: the shortest failure there is under
# the rules named first, then "run" lines for the rules under which it must hold. Run it after a change that moves
# the timing; linkcase (make check) then says whether every case still fails and holds as it should.
# cases/stopped-blocks.case and cases/seeds-227.case were found the same way and are kept by hand, for their notes.
cd "$(dirname "$0")" || exit 1
F=gone,first0,header,data
gen() {
    name=$1; note=$2; fixed=$3; shift 3
    build/linksearch "$@" --out cases/$name.case > /tmp/make_cases.log
    if grep -q '^FAILED' /tmp/make_cases.log; then
        sed -i "1i # $note" cases/$name.case
        for f in $fixed; do echo "run $f ok" >> cases/$name.case; done
    else echo "$name: NO FAILURE FOUND"; fi
}
gen wrong-number "A packet is lost; the next arrives good and ahead of the one expected. Without a rule for it nothing is ever asked (HL, 'When a packet is bad')" "hl" --rules hl-resend_ahead-zero_number --damage 1 --packets 2 0 2 0
gen last-packet-gone "The last packet of a burst is lost, and nothing sends it again: the empty packet's number is what shows it" "hl" --rules hl-zero_number --damage 1 --packets 2 0 2 0
gen both-lost "Both ends lose their place at once; each waits for the other's recovery" "hl" --rules hl-lost_answers --damage 2 --packets 2 0 2 0
gen data-taken-for-a-packet "After a failed header CRC the receiver takes the packet's data for a packet (the data is a header's image)" "hl" --rules hl-flush --damage 2 --packets 1 0 1 0 --images --fates $F
gen overrun "A window larger than the room behind stop, and the packet that says stop lost" "hl,window=4,number_bits=4" --rules hl-refuse,window=4,number_bits=4 --queue 4 --packets 5 0 0 0 --damage 1
gen power-on "End 1 starts again from power-on in the middle" "hl" --rules hl-restart --damage 0 --power 1 --packets 2 0 2 0
gen made-wrong "A packet its own sender made wrong (hops 15 at an end that would forward it): asked for again for ever. HL: the second time running, the error, and the link starts again" "hl" --rules hl-wrong_twice --damage 0 --wrong 1 --packets 2 0 1 0
gen false-acknowledgement "A forged acknowledge field releases a packet that was lost on the wire. HL: the other end asks for it, the sender no longer keeps it: the error, and the link starts again" "hl" --rules hl-ask_not_kept --damage 1 --forge 1 --fates $F,forge-ack --packets 1 0 1 0
gen forged-number "A forged packet number: a later packet taken for a lost one before it. A KNOWN LIMIT (owner, 2026-11-17): this case fails under HL as it stands" "" --rules hl --damage 1 --forge 1 --fates $F,forge-number-back --packets 2 0 0 0
gen packet-in-data "A header reads as zeros (a burst that ends where the data starts) and the data holds a packet: a header and a word. The receiver sees idle, then takes the packet inside for the next one: its word is delivered, the true packet is acknowledged and let go, and no check fails. NOT MENDED: REPORT.md" "" --rules hl,max_words=2,flush_zeros=48,zeros=64 --packets 1 0 0 0 --damage 1 --images3 --words 2
