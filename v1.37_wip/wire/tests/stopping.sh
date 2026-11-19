#!/bin/bash
# Starting and stopping: a process killed, stopped, stuck, exiting early; the launcher killed or interrupted;
# two runs with the same name at once. Nothing may hang, the status must say what happened, and no process
# and nothing in /dev/shm may be left behind.
#
#   tests/stopping.sh BIN [ROUNDS]
#
# Each kill comes at a random moment; ROUNDS (1) repeats the whole list.
set -u
BIN=$1
ROUNDS=${2:-1}
HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d /tmp/wirestop.XXXXXX)
trap 'rm -rf "$TMP"' EXIT
FAILS=0
N=4
LONG=4000000000  # cycles: longer than any test waits

fail() { echo "FAIL $*"; FAILS=$((FAILS + 1)); }

shm_before=$(ls /dev/shm | grep -c meshsim || true)

# Starts a run in the background: LPID, NAME. Args: name, launcher options..., then '--' node options.
start() {
    NAME=$1; shift
    local lopts=() nopts=()
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do lopts+=("$1"); shift; done
    [ $# -gt 0 ] && shift
    nopts=("$@")
    "$BIN/wire-launch" --clients $N --link "$NAME" "${lopts[@]}" \
        -- "$BIN/wire-node" --seed 7 --cycles $LONG --out /dev/null "${nopts[@]}" \
        -- "$BIN/wire-node" --seed 7 --cycles $LONG --out /dev/null "${nopts[@]}" 2> "$TMP/$NAME.err" &
    LPID=$!
}

# The PID of process p (0 the driver, k + 1 client k) of the launcher LPID.
child_pid() {
    local want=$1 pid cmd
    for i in $(seq 1 100); do
        for pid in $(pgrep -P "$LPID"); do
            cmd=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null)
            if [ "$want" = 0 ] && [[ "$cmd" == *" --driver "* ]]; then echo "$pid"; return; fi
            if [ "$want" != 0 ] && [[ "$cmd" == *" --client $((want - 1)) "* ]]; then echo "$pid"; return; fi
        done
        sleep 0.05
    done
}

# Waits for the launcher with a time limit; RC, SECS.
finish() {
    local t0=$(date +%s.%N) limit=$1
    for i in $(seq 1 $((limit * 20))); do
        kill -0 "$LPID" 2>/dev/null || break
        sleep 0.05
    done
    if kill -0 "$LPID" 2>/dev/null; then
        fail "$NAME: the launcher still running after ${limit}s"
        kill -9 "$LPID"
    fi
    wait "$LPID"; RC=$?
    SECS=$(echo "$(date +%s.%N) - $t0" | bc)
}

# No process of the run is left.
no_leftovers() {
    sleep 0.3
    local left
    left=$(pgrep -f -- "--link $NAME " || true)
    [ -z "$left" ] || { fail "$NAME: processes left: $left"; for p in $left; do kill -9 "$p"; done; }
}

expect() {  # pattern in the launcher's report
    grep -q -- "$1" "$TMP/$NAME.err" || { fail "$NAME: expected '$1'"; sed 's/^/    /' "$TMP/$NAME.err"; }
}

rand_sleep() { sleep "0.$(printf '%03d' $((RANDOM % 900 + 100)))"; }

for round in $(seq 1 "$ROUNDS"); do
    # 1. Each process in turn killed with SIGKILL at a random moment.
    for p in 0 1 2 $N; do
        start "kill$p-$round$$"
        rand_sleep
        victim=$(child_pid $p)
        kill -9 "$victim"
        finish 20
        [ "$RC" = 1 ] || fail "$NAME: launcher exit $RC"
        expect "died of signal 9"
        expect "pid $victim *signal 9\$"
        expect "gave up"
        no_leftovers
        echo "ok   killed process $p (pid $victim): stopped in ${SECS}s"
    done

    # 2. One stopped with SIGSTOP: the watchdog names it.
    start "stop$round$$" --stall 2
    rand_sleep
    victim=$(child_pid 2)
    kill -STOP "$victim"
    finish 30
    [ "$RC" = 1 ] || fail "$NAME: launcher exit $RC"
    expect "client 1 (pid $victim) made no progress"
    expect "stopped by the launcher"
    no_leftovers
    echo "ok   SIGSTOP on client 1: named by the watchdog, stopped in ${SECS}s"

    # 3. One stuck in a loop without exchanging.
    start "hang$round$$" --stall 2 -- --fault-on 3 --hang-at $((RANDOM * 50))
    finish 40
    [ "$RC" = 1 ] || fail "$NAME: launcher exit $RC"
    expect "client 2 (pid [0-9]*) made no progress"
    no_leftovers
    echo "ok   client 2 stuck in a loop: named by the watchdog, stopped in ${SECS}s"

    # 4. One exits 0 without finishing; one exits with status 5.
    start "early$round$$" -- --fault-on 1 --exit-at $((RANDOM * 30)):0
    finish 20
    [ "$RC" = 1 ] || fail "$NAME: launcher exit $RC"
    expect "client 0 (pid [0-9]*) exited without finishing"
    expect "exit 0 (without finishing)"
    no_leftovers
    echo "ok   client 0 exited 0 without finishing: stopped in ${SECS}s"
    start "status$round$$" -- --fault-on 0 --exit-at $((RANDOM * 30)):5
    finish 20
    [ "$RC" = 1 ] || fail "$NAME: launcher exit $RC"
    expect "driver *(pid [0-9]*) exited with status 5"
    no_leftovers
    echo "ok   the driver exited with status 5: stopped in ${SECS}s"

    # 5. The launcher killed with SIGKILL: the children die with it.
    start "lkill$round$$"
    rand_sleep
    kids=$(pgrep -P "$LPID")
    kill -9 "$LPID"
    wait "$LPID" 2>/dev/null
    sleep 0.5
    for k in $kids; do  # a zombie (state Z) is dead, only not yet reaped by whoever inherited it
        st=$(awk '{print $3}' "/proc/$k/stat" 2>/dev/null || true)
        [ -z "$st" ] || [ "$st" = Z ] || fail "$NAME: child $k alive (state $st) after the launcher was killed"
    done
    no_leftovers
    echo "ok   the launcher killed: its $(echo $kids | wc -w) children died with it"

    # 6. The launcher interrupted (SIGINT): it stops the run and says so.
    start "lint$round$$"
    rand_sleep
    kill -INT "$LPID"
    finish 20
    [ "$RC" = 1 ] || fail "$NAME: launcher exit $RC"
    expect "the launcher was stopped by signal 2"
    no_leftovers
    echo "ok   the launcher interrupted: stopped in ${SECS}s"

    # 7. Two runs with the same name at the same time do not meet.
    TMP2=$TMP/same
    mkdir -p "$TMP2"
    for s in 1 2; do
        "$BIN/wire-ref" --clients 2 --window 64 --lane-bytes 1 --seed $s --cycles 2000000 --out "$TMP2/ref$s.txt"
        "$BIN/wire-launch" --clients 2 --link same$$ \
            -- "$BIN/wire-node" --seed $s --cycles 2000000 --out "$TMP2/r$s-p{p}.txt" \
            -- "$BIN/wire-node" --seed $s --cycles 2000000 --out "$TMP2/r$s-p{p}.txt" 2> "$TMP2/l$s.err" &
        eval "L$s=\$!"
    done
    wait "$L1"; r1=$?
    wait "$L2"; r2=$?
    for s in 1 2; do
        cat "$TMP2"/r$s-p*.txt | grep -v '^#' | sort > "$TMP2/p$s.s"
        grep -v '^#' "$TMP2/ref$s.txt" | sort > "$TMP2/ref$s.s"
        cmp -s "$TMP2/p$s.s" "$TMP2/ref$s.s" || fail "same name: run $s differs from its reference"
    done
    [ "$r1$r2" = 00 ] || fail "same name: launcher exits $r1 $r2"
    echo "ok   two runs named same$$ at once: each equal to its own reference"
    rm -rf "$TMP2"
done

# 8. Under a debugger, through the launcher (gdb in batch mode stands in for a person at the terminal).
if command -v gdb >/dev/null; then
    NAME=gdb$$
    "$BIN/wire-ref" --clients 2 --window 64 --lane-bytes 1 --seed 3 --cycles 500000 --out "$TMP/gref.txt"
    # LeakSanitizer cannot run under ptrace: off for this step in the asan build.
    ASAN_OPTIONS=${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0 \
    "$BIN/wire-launch" --clients 2 --link $NAME --debug 0 --debugger "gdb -q -batch -ex run --args" \
        -- "$BIN/wire-node" --seed 3 --cycles 500000 --out "$TMP/g-p{p}.txt" \
        -- "$BIN/wire-node" --seed 3 --cycles 500000 --out "$TMP/g-p{p}.txt" > "$TMP/gdb.out" 2> "$TMP/$NAME.err"
    rc=$?
    cat "$TMP"/g-p*.txt | grep -v '^#' | sort > "$TMP/g.s"
    grep -v '^#' "$TMP/gref.txt" | sort > "$TMP/gref.s"
    if [ $rc = 0 ] && cmp -s "$TMP/g.s" "$TMP/gref.s" && grep -q "exited normally" "$TMP/gdb.out"; then
        echo "ok   the driver under gdb, through the launcher: equal to the reference"
    else
        fail "the driver under gdb (launcher exit $rc)"; sed 's/^/    /' "$TMP/$NAME.err" "$TMP/gdb.out" | tail -15
    fi
fi

shm_after=$(ls /dev/shm | grep -c meshsim || true)
[ "$shm_before" = "$shm_after" ] || fail "/dev/shm: $shm_before meshsim entries before, $shm_after after"
echo "ok   nothing in /dev/shm"
if [ $FAILS = 0 ]; then echo "stopping: all passed"; else echo "stopping: $FAILS failed"; exit 1; fi
