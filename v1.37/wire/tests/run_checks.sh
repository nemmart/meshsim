#!/bin/bash
# Every check of the wire, for the runner. Works on any number of cores, only slower with fewer than the
# processes (17 for a chain of 16); says how many it had and how long each step took.
#
#   tests/run_checks.sh OUT_DIR [STEP...]
#
# Steps (all by default, in this order):
#   build    gcc and clang, every variant
#   matrix   the 128 cases (clients 1, 2, 4, 16 x windows 16, 64, 256, 1024 x bytes 1, 2, 4, 8 x slots 2, 4) at a
#            million cycles: with gcc, plain, loaded, pinned to one core and paused, each case REPEATS times
#            (env REPEATS, 5); with clang, plain once
#   long     a hundred million cycles: clients 1, 2, 4, 16 x windows 16, 64, 1024 x bytes 1, 8
#   stopping tests/stopping.sh, ROUNDS (env, 5) rounds with gcc, once with clang, once under asan
#   plants   the five planted bugs, gcc and clang
#   sanitize wire-threads under TSan (gcc, clang); cases under asan and ubsan
#   bench    the cost (tests/bench.sh with the release build), for 1 and 2 clients
#
# OUT_DIR gets a log per step and summary.txt. The exit status is 0 only if every step passed.
set -u
OUT=$1; shift
STEPS=${*:-build matrix long stopping plants sanitize bench}
REPEATS=${REPEATS:-5}
ROUNDS=${ROUNDS:-5}
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE/.."
mkdir -p "$OUT"
SUM=$OUT/summary.txt
: > "$SUM"
note() { echo "$*" | tee -a "$SUM"; }
note "run_checks: $(date -u +%FT%TZ), $(nproc) cores, $(uname -r), commit $(git rev-parse --short HEAD 2>/dev/null || echo ?)"
note "  gcc: $(g++ --version | head -1); clang: $(clang++ --version | head -1)"
FAILED=0
step() {  # name, command...
    local name=$1; shift
    local t0=$(date +%s)
    if "$@" > "$OUT/$name.log" 2>&1; then r=passed; else r=FAILED; FAILED=$((FAILED + 1)); fi
    note "$(printf '%-28s %-7s %6ss   %s' "$name" "$r" "$(( $(date +%s) - t0 ))" "$(tail -1 "$OUT/$name.log")")"
}
G=build/g++ C=build/clang++

long_runs() {
    local bad=0
    for n in 1 2 4 16; do for w in 16 64 1024; do for b in 1 8; do
        "$HERE/case.sh" "$1" $n $w $b 4 $((n * 1000 + w + b)) 100000000 || bad=$((bad + 1))
    done; done; done
    echo "long: $bad failed"
    [ $bad = 0 ]
}
asan_cases() {
    local bad=0
    for n in 1 2 4 16; do for w in 16 256; do for b in 1 8; do
        "$HERE/case.sh" "$G/asan" $n $w $b 2 $((n + w + b)) 300000 1 -- --wide || bad=$((bad + 1))
        WIRE_PAUSE=300:200 NODE_ONLY="--pause 30:100" "$HERE/case.sh" "$G/asan" $n $w $b 3 $((n + w + b + 1)) 200000 || bad=$((bad + 1))
    done; done; done
    echo "asan: $bad failed"
    [ $bad = 0 ]
}

for s in $STEPS; do
    case $s in
    build)
        step build-gcc make -s CXX=g++ all-variants
        step build-clang make -s CXX=clang++ all-variants ;;
    matrix)
        for m in plain loaded pinned paused; do step "matrix-gcc-$m" "$HERE/matrix.sh" $G/check 1000000 "$REPEATS" $m; done
        step matrix-clang-plain "$HERE/matrix.sh" $C/check 1000000 1 plain ;;
    long)
        step long-gcc long_runs $G/check ;;
    stopping)
        step stopping-gcc "$HERE/stopping.sh" $G/check "$ROUNDS"
        step stopping-clang "$HERE/stopping.sh" $C/check 1
        step stopping-asan "$HERE/stopping.sh" $G/asan 1 ;;
    plants)
        step plants-gcc "$HERE/plants.sh" g++
        step plants-clang "$HERE/plants.sh" clang++ ;;
    sanitize)
        step tsan-gcc "$HERE/threads.sh" $G/tsan 100000
        step tsan-clang "$HERE/threads.sh" $C/tsan 100000
        step threads-gcc "$HERE/threads.sh" $G/check 3000000
        step asan-gcc asan_cases ;;
    bench)
        step bench-1 "$HERE/bench.sh" $G/release 1
        step bench-2 "$HERE/bench.sh" $G/release 2 ;;
    *) note "unknown step $s"; FAILED=$((FAILED + 1)) ;;
    esac
done
note "run_checks: $FAILED steps failed"
[ $FAILED = 0 ]
