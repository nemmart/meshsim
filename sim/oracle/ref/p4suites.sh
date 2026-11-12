#!/bin/bash
# p4suites.sh -- the network, memory and engine suites, ref's side (WORKER22; P4, inventory C3-C7, C11, C17, G1).
# Needs a JDK. sim/ref is only read: ref's sources and test classes compile into a scratch directory, and each step runs
# in a scratch copy of ref/meshsim's layout (hops.txt, sw/bootrom.hex, data/, and sw/build/ with the C++ tree's
# committed images, fw/images, byte-identical to v1.3's builds: W19).
#
# Each step is run as v1.3's run_tests.sh runs it (stdout and stderr together, the same class and arguments); its log is
# the reference the C++ suite's output must equal (tests/suite_*.cpp, timing lines masked as W20's runner does).
# run_tests.sh's `net` step takes `quick` in quick mode only, so it has two logs: net-quick.log and net.log.
#
#   p4suites.sh --fixtures [STEP...]   (re)write tests/p4suites/<step>.log (default: every step) and PROVENANCE
#   p4suites.sh --check [STEP...]      regenerate into a scratch directory and compare with the committed fixtures
#   p4suites.sh --list                 the steps, their class and arguments, and whether they are long
#
# The long steps (marked L) run in minutes; they are made on the runner (a Quest task) when too slow locally.
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/p4suites"
REF="$SIM/ref/meshsim"
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)

# step | class and arguments | heap | L for long (run_tests.sh's full-only steps)
STEPS="
net-quick|NetTest -1 quick|3g|
net|NetTest -1|3g|
net-shuf|NetTest 5 quick|3g|
net-shuf2|NetTest 11 quick|3g|
skewtable|NetTest -1 skewtable 40|3g|L
mem|MemTest -1|3g|
mem-shuf|MemTest 3|3g|
mem-shuf2|MemTest 8|3g|
engine|EngineTest -1|3g|
eng-shuf|EngineTest 4|3g|
engine-full|EngineTest -1 full|4g|L
agtest|AgTest -1|3g|
agtest-shuf|AgTest 4|3g|
rdport|ReadPortGatherTest -1|3g|
rdport-shuf|ReadPortGatherTest 4|3g|
rdport-full|ReadPortGatherTest -1 full|4g|L
searchbench|SearchBench|3g|
reqbus|RequestBusTest -1|3g|
reqbus-shuf|RequestBusTest 4|3g|
agbench|AgBench|3g|L
"
steps() { echo "$STEPS" | awk -F'|' 'NF>=3{print $1}'; }
field() { echo "$STEPS" | awk -F'|' -v s="$1" -v f="$2" '$1==s{print $f}'; }

build() {    # B: ref's classes and the suites' classes, and the run directory
  local B=$1
  rm -rf "$B"; mkdir -p "$B/cls" "$B/run/sw/build"
  javac -nowarn -d "$B/cls" -sourcepath "$REF/test" "$REF"/src/*.java "$REF"/test/{NetTest,MemTest,EngineTest,AgTest,ReadPortGatherTest,SearchBench,RequestBusTest,AgBench}.java 2>&1 | grep -v '^Note:'
  for c in NetTest MemTest EngineTest AgTest ReadPortGatherTest SearchBench RequestBusTest AgBench; do
    [ -f "$B/cls/$c.class" ] || { echo "p4suites: $c did not compile"; exit 2; }
  done
  cp "$REF/hops.txt" "$B/run/"; cp "$REF/sw/bootrom.hex" "$B/run/sw/"; cp -r "$REF/data" "$B/run/data"
  cp "$SIM"/fw/images/*.bin "$SIM"/fw/images/*.sym "$B/run/sw/build/"
}
run_step() {    # B DIR STEP: one run_tests.sh step, its log as run_tests.sh writes it
  local B=$1 d=$2 s=$3 args heap cls t0
  args=$(field "$s" 2); heap=$(field "$s" 3); cls=${args%% *}
  [ -n "$args" ] || { echo "p4suites: no step $s"; exit 2; }
  t0=$(date +%s)
  ( cd "$B/run" && java -Xmx$heap -cp "$B/cls" $args ) > "$d/$s.log" 2>&1 || { tail -20 "$d/$s.log"; echo "FAIL ref's $s ($args)"; exit 1; }
  grep -q "0 failed$" "$d/$s.log" || [ "$cls" = AgBench ] || { tail -20 "$d/$s.log"; echo "FAIL ref's $s ($args)"; exit 1; }
  echo "$s: $(tail -1 "$d/$s.log")   [$(( $(date +%s) - t0 )) s]"
}
make_logs() {    # DIR STEP...
  local d=$1 B; shift; B=$(mktemp -d); build "$B"; mkdir -p "$d"
  for s in "$@"; do run_step "$B" "$d" "$s"; done
  rm -rf "$B"
}

cmd=${1:-}; shift || true
[ $# -gt 0 ] && SEL="$*" || SEL=$(steps)
case "$cmd" in
  --list) echo "$STEPS" | awk -F'|' 'NF>=3{printf "%-12s %-32s %s\n", $1, $2, $4}' ;;
  --fixtures)
    make_logs "$FIX" $SEL || exit 1
    { echo "tests/p4suites: made by oracle/ref/p4suites.sh --fixtures from sim/ref at $REFCOMMIT"
      echo "each <step>.log: v1.3's run_tests.sh step of that name (net-quick.log: the net step in quick mode),"
      echo "run in a scratch copy of ref/meshsim's layout with fw/images' images"
      echo "$STEPS" | awk -F'|' 'NF>=3{printf "  %-12s %s\n", $1, $2}'
      java -version 2>&1 | head -1; } > "$FIX/PROVENANCE"
    echo "p4suites: fixtures written to $FIX (ref $REFCOMMIT)" ;;
  --check)
    T=$(mktemp -d); make_logs "$T" $SEL > /dev/null || exit 1; fail=0
    for s in $SEL; do cmp -s "$FIX/$s.log" "$T/$s.log" || { echo "FAIL differs from ref: $s.log"; fail=1; }; done
    rm -rf "$T"; [ $fail = 0 ] && echo "p4suites --check: the fixtures equal ref's regeneration (ref $REFCOMMIT)"; exit $fail ;;
  *) echo "usage: p4suites.sh --fixtures [STEP...] | --check [STEP...] | --list"; exit 2 ;;
esac
