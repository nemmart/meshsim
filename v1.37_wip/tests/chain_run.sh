#!/bin/bash
# chain_run.sh: v1.37's chain of processes (MILESTONE-v1.37.md; the INTEGRATOR's stage 6) on the master tile's
# fixtures (tests/updater/): the driver and the chips, each a process, on WORKER10's wire (wire_launch).
#   tests/chain_run.sh [FIXTURE...]      (default: kin-raw kin-lru jumps-base divs kin-big)
# For each fixture:
#   1. the stand-ins: the driver and two stand-in ASICs (standin_asic): the driver's checks of the replies, the wire;
#   2. the chain of two: the driver, ASIC0 on segment 0 and ASIC1 on segment 1, each chip with every check of the
#      proof runs (gold's images, its last record against record s + 1, the master's results) and the driver's of
#      both replies, sorted by hops;
#   3. the same again: every line of every process the same, the wire's waits and the real time apart;
#   4. a chain of one: the driver and ASIC0, the single chip's checks;
#   5. once, a reply that never comes (a stand-in ASIC 1 that stays silent): the driver must fail at its limit, a
#      count of cycles, and name it.
# Exit 1 on any failure. B=DIR: the binaries (default build/clang-release, built here first); JOBS for make.
set -u
cd "$(dirname "$0")/.."
B=${B:-build/clang-release}
if [ "$B" = build/clang-release ]; then
  make -s CC_NAME=clang MODE=release -j"${JOBS:-2}" $B/wire_launch $B/driver $B/standin_asic $B/proof_run > /tmp/chain_run.make.log 2>&1 \
    || { cat /tmp/chain_run.make.log; echo "chain_run: the programs didn't build"; exit 2; }
fi
FIXTURES=${*:-kin-raw kin-lru jumps-base divs kin-big}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
fail=0
mask() { sed -E 's/after [0-9.]+ s/after - s/; s/waits [0-9]+ \([0-9.]+ s\)/waits -/; s/pid [0-9]+/pid -/; s/run run[0-9]+/run -/; s/[0-9.]+ s of simulation, [0-9]+ cycles a second/-/' "$1" | sort; }
run() {   # run LOG CLIENTS DRIVER-ARGS -- CLIENT-COMMAND...
  local log=$1 n=$2; shift 2
  timeout 3600 $B/wire_launch --clients "$n" -- "$@" > "$log" 2>&1
}
for f in $FIXTURES; do
  F=tests/updater/$f.frames
  D="$B/driver $F --rows-traced 50 --limit 20000000"
  C="$B/proof_run $F --asics 64 --windows 1 --rows 50 --gold tests/updater/$f-s{k}.images.txt --quiet"
  run "$W/$f-standin.log" 2 $D -- $B/standin_asic $F --rows-traced 50 \
    && echo "$f: stand-ins: $(grep -c 'as gold.s files give' "$W/$f-standin.log") replies right" \
    || { echo "FAIL $f: the stand-ins"; tail -8 "$W/$f-standin.log"; fail=$((fail + 1)); }
  if run "$W/$f-2a.log" 2 $D -- $C && run "$W/$f-2b.log" 2 $D -- $C; then
    if cmp -s <(mask "$W/$f-2a.log") <(mask "$W/$f-2b.log"); then
      echo "$f: a chain of two: both chips pass, both replies right ($(grep -o 'at cycle [0-9]*' "$W/$f-2a.log" | tr '\n' ' ')); twice, the same to the cycle"
    else
      echo "FAIL $f: a chain of two, run twice, differs:"; diff <(mask "$W/$f-2a.log") <(mask "$W/$f-2b.log") | head -10; fail=$((fail + 1))
    fi
  else
    echo "FAIL $f: a chain of two"; grep -E "FAIL|wire-launch" "$W/$f-2a.log" "$W/$f-2b.log" 2>/dev/null | head -12; fail=$((fail + 1))
  fi
  run "$W/$f-1.log" 1 $D -- $C \
    && echo "$f: a chain of one: the chip passes, its reply right ($(grep -o 'at cycle [0-9]*' "$W/$f-1.log"))" \
    || { echo "FAIL $f: a chain of one"; grep -E "FAIL|wire-launch" "$W/$f-1.log" | head -8; fail=$((fail + 1)); }
done
F=tests/updater/kin-raw.frames
if timeout 300 $B/wire_launch --clients 2 -- $B/driver $F --rows-traced 50 --limit 400000 -- $B/standin_asic $F --rows-traced 50 --silent 1 > "$W/silent.log" 2>&1; then
  echo "FAIL a reply that never comes: the run passed"; fail=$((fail + 1))
elif grep -q "ASIC 1's reply (segment 1): not come in 400000 cycles" "$W/silent.log" && grep -q "ASIC 0's reply (segment 0): 73 words" "$W/silent.log"; then
  echo "a reply that never comes: the driver fails at its limit, 400,000 cycles, and names ASIC 1"
else
  echo "FAIL a reply that never comes: not as expected"; grep -E "FAIL|wire-launch" "$W/silent.log" | head -6; fail=$((fail + 1))
fi
[ $fail = 0 ] && echo "chain_run: all ok" || { echo "chain_run: $fail FAILED"; exit 1; }
