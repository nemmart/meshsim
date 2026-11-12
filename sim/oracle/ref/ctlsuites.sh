#!/bin/bash
# ctlsuites.sh -- the control suites, programs and recipes, ref's side (WORKER23; P5, inventory C8, C10, C12-C15, C18,
# G2, G3). Needs a JDK (and python3 with gcc for RegDocTest's header_diff, as v1.3's run had them). Nothing in sim/ref is
# written: sim/ref is copied to a scratch directory, v1.35's committed images are put where v1.3's builds put them
# (meshsim/sw/build/; they are byte-identical to v1.3's, W19's fw_crosscheck), and each step runs there exactly as
# v1.3's run_tests.sh runs it (from meshsim/, stdout and stderr together).
#
#   ctlsuites.sh --fixtures [quick|full|STEP...]   (re)write tests/ctlsuites/<step>.log (and the phases CSVs)
#   ctlsuites.sh --check    [quick|full|STEP...]   regenerate into a scratch directory and compare
#
# quick (the default): the steps make check runs; full: the steps v1.3 runs only in `all` mode (long ones go to the
# runner). The logs carry no provenance line (the C++ suites' output is compared with them as they are): PROVENANCE.
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
FIX="$SIM/tests/ctlsuites"
REFCOMMIT=$(git -C "$SIM" log -1 --format=%H -- ref 2>/dev/null || echo unknown)
# step: log name, then the class and its arguments (run_tests.sh's lines)
STEPS_QUICK=(
  "cccores|CcCoresTest -1"      "cccores-shuf|CcCoresTest 4"
  "ctrl|CtrlTest -1"            "ctrl-shuf|CtrlTest 4"
  "hooks|HooksTest -1"          "hooks-shuf|HooksTest 4"
  "regdoc|RegDocTest"
  "cond|CondTest -1"            "cond-shuf|CondTest 4"
  "accounting|AccountingTest -1"
  "recipes|RecipeTest -1"       "recipes-shuf|RecipeTest 4"
)
STEPS_FULL=( "ccbench|CcBench" "s1bench|S1Bench" "accounting-full|AccountingTest -1 full" "ctrl-full|CtrlTest -1 full" )
JX="-Xmx4g"   # run_tests.sh: $J is -Xmx3g, the big runs (ctrl-full, accounting-full) $JX
setup() {   # B: the scratch tree and its classes
  local B=$1
  cp -r "$SIM/ref" "$B/ref"; chmod -R u+w "$B/ref"
  local M="$B/ref/meshsim"; mkdir -p "$M/sw/build" "$M/build" "$M/results"; rm -rf "$M"/results/*
  for d in tests/m3-images tests/golden/images tests/golden-m2/images fw/images; do
    cp "$SIM/$d"/*.bin "$SIM/$d"/*.sym "$M/sw/build/" 2>/dev/null
  done
  ( cd "$M" && javac -nowarn -d build src/*.java 2>&1 | grep -v '^Note:'
    javac -nowarn -d build -cp build test/T.java test/Mach.java test/Net.java test/A2A.java test/SelfTest.java \
      test/CcCoresTest.java test/CcBench.java test/CtrlTest.java test/HooksTest.java test/RegDocTest.java \
      test/CondTest.java test/AccountingTest.java test/RecipeTest.java test/S1Bench.java 2>&1 | grep -v '^Note:' )
  for c in CcCoresTest CcBench CtrlTest HooksTest RegDocTest CondTest AccountingTest RecipeTest S1Bench; do
    [ -f "$M/build/$c.class" ] || { echo "ctlsuites: $c did not compile"; exit 2; }
  done
}
run_step() {   # B OUT "log|Class args"
  local B=$1 out=$2 log=${3%%|*} cmd=${3#*|} M="$1/ref/meshsim" heap="-Xmx3g" t0
  case $log in ctrl-full|accounting-full) heap=$JX;; esac
  t0=$(date +%s)
  ( cd "$M" && java $heap -cp build $cmd ) > "$out/$log.log" 2>&1; local rc=$?
  echo "$log: exit $rc, $(( $(date +%s) - t0 )) s: $(tail -1 "$out/$log.log")"
  [ $rc = 0 ] || return 1
  case $log in accounting) cp "$M/results/phases-a2a.csv" "$out/";; accounting-full) cp "$M/results/phases-a2a-full.csv" "$out/";; esac
}
select_steps() {
  SEL=()
  [ $# = 0 ] && set -- quick
  for a in "$@"; do
    case $a in
      quick) SEL+=("${STEPS_QUICK[@]}");;
      full) SEL+=("${STEPS_FULL[@]}");;
      *) local s found=0; for s in "${STEPS_QUICK[@]}" "${STEPS_FULL[@]}"; do [ "${s%%|*}" = "$a" ] && { SEL+=("$s"); found=1; }; done
         [ $found = 1 ] || { echo "ctlsuites: no step $a"; exit 2; };;
    esac
  done
}
mode=${1:-}; shift || true
select_steps "$@"
B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
setup "$B"
case $mode in
  --fixtures)
    mkdir -p "$FIX"; fail=0
    for s in "${SEL[@]}"; do run_step "$B" "$FIX" "$s" || fail=1; done
    { echo "tests/ctlsuites: made by oracle/ref/ctlsuites.sh --fixtures from sim/ref at $REFCOMMIT"
      echo "<step>.log: v1.3's run_tests.sh step of the same name, run as run_tests.sh runs it (stdout and stderr)"
      echo "phases-a2a.csv, phases-a2a-full.csv: AccountingTest's results/phases-a2a*.csv of the same runs"
      echo "images: v1.35's committed images (fw/images, tests/golden*/images, tests/m3-images) in sw/build/"
      java -version 2>&1 | head -1; } > "$FIX/PROVENANCE"
    exit $fail ;;
  --check)
    mkdir -p "$B/out"; fail=0
    for s in "${SEL[@]}"; do
      log=${s%%|*}; run_step "$B" "$B/out" "$s" || fail=1
      cmp -s "$B/out/$log.log" "$FIX/$log.log" || { echo "FAIL differs from the fixture: $log.log"; fail=1; }
    done
    for f in phases-a2a.csv phases-a2a-full.csv; do
      [ -f "$B/out/$f" ] && { cmp -s "$B/out/$f" "$FIX/$f" || { echo "FAIL differs from the fixture: $f"; fail=1; }; }
    done
    [ $fail = 0 ] && echo "ctlsuites --check: equal to the committed fixtures (ref $REFCOMMIT)"; exit $fail ;;
  *) echo "usage: ctlsuites.sh --fixtures|--check [quick|full|STEP...]"; exit 2 ;;
esac
