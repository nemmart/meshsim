#!/bin/sh
# figures.sh: the random runs that answer the owner's questions of 2026-11-17, each as a few lines of linkrun's
# totals. The same seeds under each setting, so the lines compare.
#   ./figures.sh [a|b] [seeds, default 100]     a and b are the two halves, to run side by side
cd "$(dirname "$0")" || exit 1
N=${2:-100}
one() {
    echo "== $*"
    build/linkrun --seeds 1 "$N" --cycles 300000 "$@" | grep -E '^runs|FAILED:|^bytes sent|bytes of data a cycle|^the most|^the longest: a foreground|^recoveries|no room|seen before|started again' | cut -c1-260
}
case "${1:-a}" in
a)  # an acknowledgement as soon as the line is free (G18), and the status timer
    one
    one --rules proposed-ack_now
    one --status 2048
    one --status 8192
    one --saturate --quiet-wires
    one --saturate --quiet-wires --rules proposed-ack_now
    one --saturate --quiet-wires --status 2048
    one --saturate --quiet-wires --status 8192
    ;;
b)  # the host's window, with G11's rule and without it
    one --host-window 8
    one --host-window 16
    one --host-window 64
    one --host-window 8 --rules proposed-refuse
    one --host-window 16 --rules proposed-refuse
    one --host-window 64 --rules proposed-refuse
    one --forge
    ;;
esac
