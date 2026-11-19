#!/bin/bash
# The planted bugs, each in a build of its own (make VARIANT=plantN), each to be caught by the reference:
#   1  a window published before its bytes are written
#   2  a read without waiting
#   3  a window one cycle out
#   4  an index that wraps wrongly
#   5  a byte delivered a window early
# Each is run on a few cases, with the test pauses, until it gives a difference (or fails). Caught means a DIFF;
# a run that fails or stalls instead is reported as such.
#
# Then the thread sanitizer on the library in threads, which must report plants 1, 2, 4 and 5 as races.
#
#   tests/plants.sh [CXX] [ATTEMPTS]
set -u
CXX=${1:-g++}
ATTEMPTS=${2:-20}
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE/.."
COMPILER=$(basename "$CXX")
caught=0
for n in 1 2 3 4 5; do
    make -s CXX="$CXX" VARIANT=plant$n build >/dev/null || { echo "plant $n: build failed"; exit 1; }
    BIN=build/$COMPILER/plant$n
    verdict="NOT CAUGHT"
    tries=0
    for a in $(seq 1 "$ATTEMPTS"); do
        for c in "2 16 1 2" "4 64 2 4" "2 256 8 2" "1 16 1 4"; do
            tries=$((tries + 1))
            set -- $c
            line=$(WIRE_PAUSE=2000:300 NODE_ONLY="--pause 50:200" LAUNCH_ARGS="--stall 10" \
                   timeout 120 "$HERE/case.sh" "$BIN" "$1" "$2" "$3" "$4" $((a * 10 + tries)) 300000)
            case $line in
            DIFF*) verdict="caught (a difference) on run $tries: ${line#DIFF }"; break 2 ;;
            FAIL*) verdict="caught (the run failed) on run $tries: $(echo "$line" | head -1)"; break 2 ;;
            esac
        done
    done
    echo "plant $n: $verdict"
    case $verdict in caught*) caught=$((caught + 1)) ;; esac
done
echo "plants: $caught of 5 caught by the reference"
# The thread sanitizer, on the library in threads: the four plants that are races must be reported as such
# (plant 3 is a window one cycle out: exact in its timing, so no race; the reference catches it above).
tsan_ok=0
for n in 1 2 4 5; do
    make -s CXX="$CXX" VARIANT=tsan-plant$n build >/dev/null || { echo "tsan-plant $n: build failed"; exit 1; }
    r=$("build/$COMPILER/tsan-plant$n/wire-threads" --clients 2 --window 16 --lane-bytes 8 --slots 2 --seed 3 \
        --cycles 20000 --out /dev/null 2>&1 | grep -c "WARNING: ThreadSanitizer: data race")
    if [ "$r" -gt 0 ]; then echo "plant $n: the thread sanitizer reports $r races"; tsan_ok=$((tsan_ok + 1));
    else echo "plant $n: the thread sanitizer reports nothing"; fi
done
echo "plants: $tsan_ok of 4 races reported by the thread sanitizer"
[ $caught = 5 ] && [ $tsan_ok = 4 ]
