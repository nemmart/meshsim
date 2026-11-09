#!/bin/bash
# replayconf.sh -- the replayer's conformance, ref's side (WORKER15; tests/ref/ReplayConf.java, tests/test_replayer_*.cpp).
# Needs a JDK. Nothing in sim/ref is written: ref and the driver are compiled into a scratch directory.
#
#   replayconf.sh --fixtures   (re)write tests/replayer/: alu-report.txt (ReplayerTest's ALU report, lines 1-29) and
#                              alu-sweep.txt (ReplayConf alu 1 2000: a digest per opcode, and RowCore's)
#                              and the traces: synthetic deliveries made by ref's SyntheticDelivery (seeded), one window
#                              each through ReplayConf trace (ReplayDriver's ideal controller, every call recorded):
#                              NAME.script.gz and NAME.obs.gz, as TRACES below (some with mutations: the error paths)
#   replayconf.sh --check      the ALU fixtures regenerated and compared; every committed script replayed on ref (ReplayConf
#                              run) and its observations compared with the committed ones. (The scripts aren't
#                              regenerated: SyntheticDelivery's last window differs run to run, so a trace through it does.)
#   replayconf.sh --long OUT   every window of the four deliveries in all three variants into OUT (not committed), for
#                              build/<cc>-<mode>/replayconf (tests/replayconf_main.cpp) to compare with
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/replayer"
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
build() {
  rm -rf "$1"; mkdir -p "$1"
  javac --release 17 -nowarn -d "$1" "$SIM"/ref/meshsim/src/*.java "$SIM"/ref/meshsim/test/*.java 2>&1 | grep -v '^Note:'
  javac --release 17 -nowarn -d "$1" -cp "$1" "$SIM"/tests/ref/ReplayConf.java 2>&1 | grep -v '^Note:'
  [ -f "$1/ReplayConf.class" ] || { echo "replayconf: the driver did not compile"; exit 2; }
}
fixtures() {
  local d=$1 B; B=$(mktemp -d); build "$B"; mkdir -p "$d"
  (cd "$SIM/ref/meshsim" && java -cp "$B" ReplayerTest data/vec-821e1bd.tsv ../tables/opcode_table.txt) > "$B/rt.out" || { echo "FAIL ref's ReplayerTest"; exit 1; }
  sed -n '1,29p' "$B/rt.out" > "$d/alu-report.txt"
  java -cp "$B" ReplayConf alu 1 2000 > "$d/alu-sweep.txt" || exit 1
  deliveries "$B"
  local t; for t in "${TRACES[@]}"; do set -- $t; trace "$B" "$d" "$@"; done
  rm -rf "$B"
}
# the synthetic deliveries (SyntheticDelivery: windows, rows, seed[, inline weight])
DELIVS=("syn1 3 2048 7" "syn2 4 1024 11 6" "syn3 3 1024 3" "syn4 2 8192 5")
deliveries() {   # B: the deliveries go to B/NAME
  local B=$1 d; for d in "${DELIVS[@]}"; do set -- $d
    (cd "$SIM/ref/meshsim" && java -cp "$B" SyntheticDelivery "$B/$1" --windows "$2" --rows "$3" --seed "$4" ${5:+--inline-weight $5}) > /dev/null 2>&1 \
      || { echo "FAIL SyntheticDelivery $d"; exit 1; }
  done
}
# NAME DELIVERY VARIANT WINDOW [mutations]
TRACES=("s1-lru-w0 syn1 state-lru 0" "s4-pick-w1 syn4 state-pick 1" "s2-pick-w0 syn2 state-pick 0"
        "m-extra-code syn4 state 1 --extra-code 40" "m-flood syn4 state 1 --flood 300" "m-passes syn3 state-pick 2 --passes 2 --rows 600"
        "m-under-rlog syn3 state-pick 2 --values 1 -5" "m-left-rlog syn3 state-pick 2 --values 1 5"
        "m-under-wlog syn3 state-pick 2 --values 2 -3" "m-left-wlog syn2 state-pick 2 --values 2 4"
        "m-under-code syn2 state 2 --values 0 -3" "m-bad-decode syn2 state 0 --bad-decode" "m-rows0 syn2 state 3 --rows 0")
trace() {   # B DIR NAME DELIVERY VARIANT WINDOW [mutations]
  local B=$1 d=$2 n=$3 dl=$4 v=$5 w=$6; shift 6
  (cd "$B" && java -cp "$B" ReplayConf trace "$B/$dl" "$v" "$w" "$B/$n" "$@") || { echo "FAIL ReplayConf trace $n"; exit 1; }
  java -cp "$B" ReplayConf run "$B/$n.script" | cmp -s - "$B/$n.obs" || { echo "FAIL ref's run of $n differs from its trace"; exit 1; }
  { sed -n '1,2p' "$B/$n.script"; echo "# ref $REFCOMMIT"; sed '1,2d' "$B/$n.script"; } | gzip -n -9 > "$d/$n.script.gz"
  gzip -n -9 < "$B/$n.obs" > "$d/$n.obs.gz"
}
FILES="alu-report.txt alu-sweep.txt"
for t in "${TRACES[@]}"; do FILES="$FILES ${t%% *}.script.gz ${t%% *}.obs.gz"; done
case "${1:-}" in
  --fixtures) fixtures "$FIX"; echo "replayconf: fixtures written to $FIX (ref $REFCOMMIT)"; exit 0 ;;
  --check)
    T=$(mktemp -d); build "$T"; fail=0
    (cd "$SIM/ref/meshsim" && java -cp "$T" ReplayerTest data/vec-821e1bd.tsv ../tables/opcode_table.txt) | sed -n '1,29p' | cmp -s - "$FIX/alu-report.txt" || { echo "FAIL differs from ref: alu-report.txt"; fail=1; }
    java -cp "$T" ReplayConf alu 1 2000 | cmp -s - "$FIX/alu-sweep.txt" || { echo "FAIL differs from ref: alu-sweep.txt"; fail=1; }
    for t in "${TRACES[@]}"; do n=${t%% *}
      gzip -dc "$FIX/$n.script.gz" > "$T/$n.script"
      java -cp "$T" ReplayConf run "$T/$n.script" | cmp -s - <(gzip -dc "$FIX/$n.obs.gz") || { echo "FAIL ref's observations differ: $n"; fail=1; }
    done
    rm -rf "$T"; [ $fail = 0 ] && echo "replayconf --check: the ALU fixtures and ${#TRACES[@]} traces equal ref (ref $REFCOMMIT)"; exit $fail ;;
  --long)
    OUT=$(mkdir -p "${2:?--long OUT}" && cd "$2" && pwd); B="$OUT/build"; build "$B"; deliveries "$B"
    for d in "${DELIVS[@]}"; do set -- $d; dl=$1; nw=$2
      for v in state state-lru state-pick; do for ((w = 0; w < nw; w++)); do trace "$B" "$OUT" "$dl-$v-w$w" "$dl" "$v" "$w"; done; done
    done
    echo "replayconf --long: traces in $OUT"; exit 0 ;;
  *) echo "usage: replayconf.sh --fixtures | --check | --long OUT"; exit 2 ;;
esac
