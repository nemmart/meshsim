#!/bin/sh
# figures.sh: the random runs that put figures to the numbers HL marks as proposed, each as a few lines of linkrun's
# totals. The same seeds under each setting, so the lines compare.
#   ./figures.sh [a|b|c|d|e] [seeds, default 100]
cd "$(dirname "$0")" || exit 1
N=${2:-100}
one() {
    echo "== $*"
    build/linkrun --seeds 1 "$N" --cycles 300000 "$@" | grep -E '^runs|FAILED:|^bytes sent|bytes of data a cycle|^the most|^the longest: a foreground|^a foreground packet ready|^cycles a line|^recoveries|^bits flipped|no room|seen before|sent again  |went back|started again|made wrong|no longer kept|acknowledgement be' | cut -c1-300
}
case "${1:-a}" in
a)  # an acknowledgement as soon as the line is free, the status timer, and the pass over the data
    one
    one --rules hl-ack_now
    one --rules hl,pass=0
    one --status 2048
    one --status 8192
    one --saturate --quiet-wires
    one --saturate --quiet-wires --rules hl-ack_now
    one --saturate --quiet-wires --rules hl,pass=0
    ;;
b)  # the host's window
    one --host-window 8
    one --host-window 16
    one --host-window 64
    ;;
c)  # the cycles before a resend is asked for again
    for r in 128 512 2048 8192 32768; do one --host-window 8 --rules hl,reask=$r; done
    ;;
d)  # the window of every end, and the forwarding buffers
    for w in 2 4 8 16; do one --host-window $w --window $w; done
    for f in 1024 512 256; do one --host-window 8 --fwd $f; done
    ;;
e)  # forged headers among the damage (but the false packet number, the known limit), and bits flipped in memory
    one --host-window 8 --forge-not forge-number,forge-number-back
    one --host-window 8 --forge
    one --host-window 8 --flips
    ;;
esac
