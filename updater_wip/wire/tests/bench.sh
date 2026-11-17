#!/bin/bash
# The cost: cycles a second of a process on the wire, at each window, against the same loop with no wire.
#
#   tests/bench.sh BIN [CLIENTS] [CYCLES] [WORK_CYCLES]
#
# Ends that do nothing (--null): CYCLES (50,000,000) cycles. Ends that do about a chip's work, 500 ns a cycle
# (two million cycles a second): WORK_CYCLES (4,000,000). The rate shown is the slowest process's.
# Run it on a quiet machine with a core for every process (CLIENTS + 1); it says if there are fewer.
set -u
BIN=$1 N=${2:-1} C=${3:-50000000} WC=${4:-4000000}
TMP=$(mktemp -d /tmp/wirebench.XXXXXX)
trap 'rm -rf "$TMP"' EXIT
[ "$(nproc)" -ge $((N + 1)) ] || echo "note: $(nproc) cores for $((N + 1)) processes: the rates are not the wire's alone"
rate() {  # the slowest '# p' line in the files given
    cat "$@" | awk '/^# p/ {print $10}' | sort -g | head -1
}
printf "%-28s %10s %10s %10s %10s\n" "Mcycles/s (slowest process)" "W=16" "W=64" "W=256" "W=1024"
for work in 0 500; do
    cyc=$C; [ $work = 0 ] || cyc=$WC
    nw=() ww=()
    for w in 16 64 256 1024; do
        "$BIN/wire-node" --no-wire --nw-window $w --null --work-ns $work --cycles $cyc --out "$TMP/nw.txt"
        nw+=("$(rate "$TMP/nw.txt")")
        rm -f "$TMP"/p*.txt
        "$BIN/wire-launch" --clients "$N" --window $w \
            -- "$BIN/wire-node" --null --work-ns $work --cycles $cyc --out "$TMP/p{p}.txt" \
            -- "$BIN/wire-node" --null --work-ns $work --cycles $cyc --out "$TMP/p{p}.txt" 2>/dev/null
        ww+=("$(rate "$TMP"/p*.txt)")
    done
    printf "%-28s %10s %10s %10s %10s\n" "work ${work} ns, no wire" "${nw[@]}"
    printf "%-28s %10s %10s %10s %10s\n" "work ${work} ns, $((N + 1)) processes" "${ww[@]}"
done
