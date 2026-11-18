#!/bin/bash
# The cost: cycles a second of the processes on the wire, at each window, against the same loop with no wire.
#
#   tests/bench.sh BIN [CLIENTS] [CYCLES] [WORK_CYCLES] [TRIES]
#
# Ends that do nothing (--null): CYCLES (50,000,000) cycles. Ends that do about a chip's work, 500 ns a cycle
# (two million cycles a second): WORK_CYCLES (4,000,000). The baseline runs CLIENTS + 1 processes with no wire at
# the same time (so that both see the same machine); the rate is the slowest process's, the best of TRIES (3).
# The cost is the time added, as a share of the baseline's. Run it on a quiet machine with a core for every
# process (CLIENTS + 1); it says if there are fewer. The waits (how many, how long, how many slept) go to stderr.
set -u
BIN=$1 N=${2:-1} C=${3:-50000000} WC=${4:-4000000} TRIES=${5:-3}
TMP=$(mktemp -d /tmp/wirebench.XXXXXX)
trap 'rm -rf "$TMP"' EXIT
[ "$(nproc)" -ge $((N + 1)) ] || echo "note: $(nproc) cores for $((N + 1)) processes: the rates are not the wire's alone"
slowest() { cat "$@" | awk '/^# p .* mcps/ {print $9}' | sort -g | head -1; }
best() { printf '%s\n' "$@" | sort -g | tail -1; }
echo "$((N + 1)) processes; Mcycles/s of the slowest, best of $TRIES; cost = time added"
printf "%-34s %24s %24s %24s %24s\n" "" "W=16" "W=64" "W=256" "W=1024"
for work in 0 500; do
    cyc=$C; [ $work = 0 ] || cyc=$WC
    row=()
    for w in 16 64 256 1024; do
        nws=() ws=()
        for t in $(seq 1 "$TRIES"); do
            rm -f "$TMP"/*.txt
            for p in $(seq 0 "$N"); do
                "$BIN/wire-node" --no-wire --nw-window $w --null --work-ns $work --cycles $cyc --out "$TMP/nw$p.txt" &
            done
            wait
            nws+=("$(slowest "$TMP"/nw*.txt)")
            rm -f "$TMP"/*.txt
            "$BIN/wire-launch" --clients "$N" --window $w \
                -- "$BIN/wire-node" --null --work-ns $work --cycles $cyc --out "$TMP/p{p}.txt" \
                -- "$BIN/wire-node" --null --work-ns $work --cycles $cyc --out "$TMP/p{p}.txt" 2>/dev/null
            ws+=("$(slowest "$TMP"/p*.txt)")
            grep -h waits "$TMP"/p*.txt | sed "s/^/work=$work W=$w try=$t /" >&2
        done
        a=$(best "${nws[@]}") b=$(best "${ws[@]}")
        row+=("$(awk -v a="$a" -v b="$b" 'BEGIN { printf "%s / %s (%+.1f%%)", a, b, 100 * (a / b - 1) }')")
    done
    printf "%-34s %24s %24s %24s %24s\n" "work ${work} ns: no wire / wire" "${row[@]}"
done
