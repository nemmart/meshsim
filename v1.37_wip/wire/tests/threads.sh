#!/bin/bash
# wire-threads (the library with each process a thread) against the reference, on a few cases. With the tsan
# build, this is the thread sanitizer's look at every access between ends.
#
#   tests/threads.sh BIN [CYCLES]      (the reference is taken from BIN's sibling check build)
set -u
BIN=$1 C=${2:-300000}
REF=$(dirname "$BIN")/check/wire-ref
TMP=$(mktemp -d /tmp/wirethr.XXXXXX)
trap 'rm -rf "$TMP"' EXIT
bad=0
for c in "1 16 1 2" "2 64 2 4" "4 16 8 2" "4 256 4 3" "16 64 1 2"; do
    set -- $c
    seed=$((RANDOM + 1))
    "$REF" --clients "$1" --window "$2" --lane-bytes "$3" --seed $seed --cycles "$C" --out "$TMP/ref.txt"
    if ! "$BIN/wire-threads" --clients "$1" --window "$2" --lane-bytes "$3" --slots "$4" --seed $seed --cycles "$C" \
            --out "$TMP/thr.txt" 2> "$TMP/err.txt"; then
        echo "FAIL threads clients=$1 window=$2 bytes=$3 slots=$4"; head -30 "$TMP/err.txt"; bad=$((bad + 1)); continue
    fi
    if [ -s "$TMP/err.txt" ]; then
        echo "WARN threads clients=$1 window=$2 bytes=$3 slots=$4: stderr:"; head -30 "$TMP/err.txt"; bad=$((bad + 1))
    fi
    grep -v '^#' "$TMP/ref.txt" | sort > "$TMP/a"
    grep -v '^#' "$TMP/thr.txt" | sort > "$TMP/b"
    if cmp -s "$TMP/a" "$TMP/b"; then
        echo "OK   threads clients=$1 window=$2 bytes=$3 slots=$4 seed=$seed"
    else
        echo "DIFF threads clients=$1 window=$2 bytes=$3 slots=$4 seed=$seed"; bad=$((bad + 1))
    fi
done
[ $bad = 0 ] && echo "threads: all passed" || { echo "threads: $bad failed"; exit 1; }
