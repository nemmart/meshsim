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
gen data-taken-for-a-packet "After a failed header CRC the receiver takes the packet's data for a packet (the data is a header's image), with neither the wait for 528 zeros nor the mask on the data. Either of the two is enough here" "hl-flush hl-mask hl" --rules hl-flush-mask --damage 2 --packets 1 0 1 0 --images --fates $F
gen overrun "A window larger than the room behind stop, and the packet that says stop lost" "hl,window=4,number_bits=4" --rules hl-refuse,window=4,number_bits=4 --queue 4 --packets 5 0 0 0 --damage 1
gen power-on "End 1 starts again from power-on in the middle" "hl" --rules hl-restart --damage 0 --power 1 --packets 2 0 2 0
gen made-wrong "A packet its own sender made wrong (hops 15 at an end that would forward it): asked for again for ever. HL: the second time running, the error, and the link starts again" "hl" --rules hl-wrong_twice --damage 0 --wrong 1 --packets 2 0 1 0
gen false-acknowledgement "A forged acknowledge field releases a packet that was lost on the wire. HL: the other end asks for it, the sender no longer keeps it: the error, and the link starts again" "hl" --rules hl-ask_not_kept --damage 1 --forge 1 --fates $F,forge-ack --packets 1 0 1 0
gen forged-number "A forged packet number: a later packet taken for a lost one before it. It was a known limit (owner, 2026-11-17). With the data masked from the header CRC, a header that noise has forged takes the wrong mask off its data, and the data checksum fails" "hl" --rules hl-mask --damage 1 --forge 1 --fates $F,forge-number-back --packets 2 0 0 0
gen packet-in-data "A header reads as zeros (a burst that ends where the data starts) and the data holds a packet: a header and a word. Without the mask the receiver sees idle, then takes the packet inside for the next one: its word is delivered, the true packet is acknowledged and let go, and no check fails. HL, 'The data is masked on the wire': on the wire the data is no packet, a header CRC fails, and the link repairs" "hl,max_words=2,flush_zeros=48,zeros=64" --rules hl-mask,max_words=2,flush_zeros=48,zeros=64 --packets 1 0 0 0 --damage 1 --images3 --words 2
gen data-made-on-purpose "The mask's limit. The data is made by one who knows every field of the header its packet will go out under (stream, length, number, acknowledge, hops, stop bits): the CRC and the mask's sequence are both linear, so data can be solved for whose masked bytes are a packet. The header reads as zeros, and the packet inside is taken, as in packet-in-data. A KNOWN LIMIT (HL, 'The data is masked on the wire': whoever made the data would have to know the header's CRC): this case fails under HL as it stands" "" --rules hl,max_words=2,flush_zeros=48,zeros=64 --packets 1 0 0 0 --damage 1 --images4 --words 2
