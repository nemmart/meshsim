# body.sh -- the v1.35 freeze check's body (PLAN-V135 step 4), inlined into the task tests/freeze/make_task.sh
# writes (KIT, KS = the kit's sim/, RES, W, DATA, ITEMS set by the task's head). From the existing task bodies: the sort (Quest 154),
# the calibration (tests/acceptance). (v1.35's items 3, T2, and 4, the chains, are retired with T2: v1.36's segment on
# the chip is acceptance C, tests/test_rtl_segment.cpp.) run_tests.sh, the
# calibration, the sort's measurement kit and make check each run alone, so their times are unloaded (for RUNNING.md
# and BUILDING.md). Not a timing task otherwise: each
# item's time is recorded (TIMES), and run_tests.sh's per step (run_tests/.step-times, STEP_TIMES=1).
# ITEMS (the task's head; default "main kit check"): which groups run. The machine and the toolchain always.
#   main:
#   1 run_tests    ./run_tests.sh all --check, alone: PASS when it passes (its PENDING steps listed, not failures)
#   2 sort         test_sort's full-size run (SORT_SIZE=64k) on one thread: every sort64k-fp point, the result, the
#                  phases, TOTAL 1,229,687; and bench/sort's bench_sort on the same keys, one thread and 6 threads by
#                  tiles (each tile a domain, grouped in order, as Quests 157/158): the digests at L2's all-READY equal,
#                  the span 1,229,687 at both
#   5 calibration  the acceptance kit's calibrate.sh (128 synthetic windows, seed 11; 1, 4, 8, 16 processes), alone
#   kit:
#   7 sort-kit     the sort's measurement kit (tests/sort/kit) at full size, alone: every run exits 0 or is refused as
#                  recorded (F3's six ts-latency-3, ts-latency-4 and corner points, D47's two ready-1 points: no
#                  CSV), SUMMARY.md written, p4/uniform-k128's TOTAL 1,229,687
#   check:
#   8 make-check   make check from a clean copy of the kit's sim/ (copied before anything is built), alone
#   always:
#   6 machine      tools/machine.sh's record, and the firmware toolchain's versions (riscv64-unknown-elf-gcc, binutils,
#                  picolibc) and the locale the image builds run under (run_tests.sh runs under LC_ALL=C), in
#                  toolchain.txt and appended to machine.txt
FAIL=0; T0=$(date +%s)
sec() { echo; echo "==== $* ($(date -u +%T), +$(( $(date +%s) - T0 )) s)"; }
verdict() { echo "VERDICT $1: $2"; echo "$1 $2" >> "$RES/VERDICTS"; case $2 in PASS|PENDING*) ;; *) FAIL=1;; esac; }
took() { echo "$1 $(( $(date +%s) - $2 )) s" >> "$RES/TIMES"; }
want() { [[ " ${ITEMS:-main kit check} " == *" $1 "* ]]; }
: > "$RES/VERDICTS"; : > "$RES/TIMES"
want check && cp -a "$KS" "$W/clean-sim"   # make check's clean tree, before anything is built
echo "items: ${ITEMS:-main kit check}"
echo "kit: $(cat "$KIT/VERSION"); host $(hostname); $(nproc) CPUs; $(clang++ --version | head -1); $(java -version 2>&1 | head -1)"

sec "6. the machine (D21)"
. "$KS/tools/machine.sh" "${QUEST_P_CPUS:-}" "${QUEST_E_CPUS:-}"; machine_record "$RES/machine.txt"; uptime
[ -s "$RES/machine.txt" ] && verdict machine PASS || verdict machine FAIL
{ echo "== the firmware toolchain (the image builds: run_tests.sh's build steps, fw/build.sh)"
  echo "riscv64-unknown-elf-gcc: $(riscv64-unknown-elf-gcc --version 2>/dev/null | head -1)"
  echo "binutils: $(riscv64-unknown-elf-as --version 2>/dev/null | head -1)"
  for p in gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf picolibc-riscv64-unknown-elf; do
    echo "package $p: $(dpkg-query -W -f '${Version}' $p 2>/dev/null || echo "not a dpkg package")"; done
  PV=$(echo '#include <picolibc.h>' | riscv64-unknown-elf-gcc -specs=picolibc.specs -E -dM - 2>/dev/null | sed -n 's/^#define __PICOLIBC_VERSION__ //p')
  echo "picolibc: ${PV:-not found}"
  echo "locale of the image builds: LC_ALL=$(LC_ALL=C sh -c 'echo $LC_ALL'), charmap $(LC_ALL=C locale charmap 2>/dev/null)"
} > "$RES/toolchain.txt"; cat "$RES/toolchain.txt"; cat "$RES/toolchain.txt" >> "$RES/machine.txt"
grep -q '^riscv64-unknown-elf-gcc: .' "$RES/toolchain.txt" && grep -q '^binutils: .' "$RES/toolchain.txt" \
  && ! grep -q '^picolibc: not found' "$RES/toolchain.txt" && verdict toolchain PASS || verdict toolchain FAIL

if want main; then
sec "build: clang release (test_sort, bench_sort); the acceptance kit's programs (clang suite)"
t=$(date +%s)
if ( cd "$KS" && make -j"$(nproc)" CC_NAME=clang MODE=release build/clang-release/test_sort build/clang-release/bench_sort ) > "$RES/build-release.log" 2>&1 \
   && ( cd "$KIT" && ./build.sh ) > "$RES/build-acceptance.log" 2>&1; then verdict build PASS
else tail -20 "$RES/build-release.log" "$RES/build-acceptance.log"; verdict build FAIL; FREEZE_RC=1; return 0 2>/dev/null || exit 1; fi
took build $t

sec "1. ./run_tests.sh all --check, alone (LC_ALL=C, the image builds' locale; STEP_TIMES=1)"
T1=$(date +%s)
( cd "$KS" && LC_ALL=C STEP_TIMES=1 JOBS=8 ./run_tests.sh all --check ) > "$RES/run_tests.log" 2>&1; RC=$?
cp -r "$KS/results" "$RES/run_tests" 2>/dev/null
tail -25 "$RES/run_tests.log"
NP=$(grep -c "^PENDING" "$RES/run_tests.log"); ND=$(grep -c "^LOG DIFFERS" "$RES/run_tests.log")
echo "run_tests.sh: exit $RC; $NP steps PENDING; $ND logs differing"
grep "^LOG DIFFERS\|^FAIL" "$RES/run_tests.log" | head -20
echo "the slowest steps (all: run_tests/.step-times):"; sort -k2 -n -r "$RES/run_tests/.step-times" 2>/dev/null | head -12
[ $RC = 0 ] && verdict run_tests PASS || verdict run_tests FAIL
[ "$NP" -gt 0 ] && verdict "run_tests-pending-$NP-steps" PENDING
took run_tests $T1

sec "2. the full-size sort (p4 uniform-k128): every fingerprint on one thread; one thread and 6 by tiles, the digests"
T2S=$(date +%s)
FP64=$DATA/w7-golden-m2/fb6f21e/sort64k-fp.fp
H=$(sha256sum "$FP64" 2>/dev/null | cut -d' ' -f1)
KD=$(cd "$KS" && tests/sort/keys.sh 65536 uniform 2> "$RES/keys.log")
KH=$(sha256sum "$KS/$KD/uniform.keys" 2>/dev/null | cut -d' ' -f1)
if [ "$H" = 6e923d6c9823c0d758104f81e7706cd4598a771ba89aad39609e78985c1d3846 ] && [ "$KH" = a7636b687a6db39cac8fbf63c2f16f686fd1ce858fe8a7ed5d98621e452c9b7a ]; then
  t=$(date +%s)
  ( cd "$KS" && env SORT_SIZE=64k SORT_PARTS=1 SORT_KEYS="$KS/$KD/uniform.keys" SORT_FP="$FP64" ./build/clang-release/test_sort sort_runs ) > "$RES/sort-one.log" 2>&1
  rc=$?; grep -E '^sort' "$RES/sort-one.log"
  [ $rc = 0 ] && grep -q '^ok   sort_runs_equal_to_refs' "$RES/sort-one.log" && verdict sort-one PASS || { tail -5 "$RES/sort-one.log"; verdict sort-one FAIL; }
  took sort-one $t
  # 6 threads by tiles (test_sort splits in row bands only: 1, 2, 4 or 8), against one thread on the same keys
  t=$(date +%s); DG=""; OK=1
  for spec in "1 one" "6 tiles"; do set -- $spec
    ( cd "$KS" && ./build/clang-release/bench_sort --keys "$KS/$KD/uniform.keys" --threads $1 --partition $2 --reps 1 ) > "$RES/sort-split6-t$1.log" 2>&1
    rc=$?; L=$(grep '^sort ' "$RES/sort-split6-t$1.log" | tail -1); echo "${L%% times=*}"
    D=$(echo "$L" | grep -o 'digest=[0-9a-f]*'); DG="$DG $D"
    [ $rc = 0 ] && [ -n "$D" ] && echo "$L" | grep -q ' span=1229687 ' || { tail -5 "$RES/sort-split6-t$1.log"; OK=0; }
  done
  set -- $DG; echo "digests:$DG"
  [ $OK = 1 ] && [ $# = 2 ] && [ "$1" = "$2" ] && verdict sort-split6 PASS || verdict sort-split6 FAIL
  took sort-split6 $t
else echo "the sort's inputs: sort64k-fp '${H:-missing}', keys '${KH:-missing}' ($(cat "$RES/keys.log"))"; verdict sort-inputs FAIL; fi

sec "5. the acceptance kit's calibration (alone)"
t=$(date +%s)
( cd "$KIT" && ./calibrate.sh "$RES/calib" "$W" ) > "$RES/calibrate.log" 2>&1 && verdict calibration PASS || verdict calibration FAIL
tail -30 "$RES/calibrate.log"
took calibration $t
fi

if want kit; then
sec "7. the sort's measurement kit at full size (tests/sort/kit, 92 runs), alone"
t=$(date +%s); SK=$W/sortkit; KO=$SK/results
KD=$(cd "$KS" && tests/sort/keys.sh 65536 uniform 2>> "$RES/keys.log")   # the uniform set checked; the kit makes the rest
if ( cd "$KS" && tests/sort/kit/assemble.sh "$SK" "$KS/build" ) > "$RES/sortkit-assemble.log" 2>&1; then
  ( cd "$SK" && ./run.sh -j 10 -o results ) > "$RES/sortkit-run.log" 2>&1; R=$?
  tail -4 "$RES/sortkit-run.log"
  REFUSED=" sweep/trace100-30-k128/ts-latency-3 sweep/trace100-30-k128/ts-latency-4 sweep/trace100-30-k128/corner sweep/trace100-30-k128/ready-1 sweep/uniform-k128/ts-latency-3 sweep/uniform-k128/ts-latency-4 sweep/uniform-k128/corner sweep/uniform-k128/ready-1 "
  NRUN=0; NOK=0; NREF=0; BAD=""
  while read -r id rc; do NRUN=$((NRUN + 1)); f=$KO/$(echo "$id" | tr / _).csv
    if [[ $REFUSED == *" $id "* ]]; then [ "$rc" != 0 ] && [ ! -f "$f" ] && NREF=$((NREF + 1)) || BAD="$BAD $id(exit $rc, expected refused)"
    else [ "$rc" = 0 ] && [ -f "$f" ] && NOK=$((NOK + 1)) || BAD="$BAD $id(exit $rc)"; fi
  done < <(sed -nE 's/^[0-9:]+ ([^ ]+): exit ([0-9]+),.*/\1 \2/p' "$RES/sortkit-run.log")
  TU=$(awk -F, '$10 == "TOTAL" { print $11 }' "$KO/p4_uniform-k128.csv" 2>/dev/null)
  for id in $REFUSED; do echo "refused $id: $(grep -m1 FATAL "$KO/$(echo "$id" | tr / _).log" 2>/dev/null)"; done
  echo "sort kit: $NRUN runs; $NOK with a result, $NREF refused as recorded; p4/uniform-k128 TOTAL ${TU:-missing}${BAD:+; unexpected:$BAD}"
  mkdir -p "$RES/sortkit"; cp "$KO/SUMMARY.md" "$KO/all.csv" "$RES/sortkit/" 2>/dev/null
  tar -czf "$RES/sortkit/results.tgz" -C "$SK" results 2>/dev/null
  [ $R = 0 ] && [ "$NRUN" = 92 ] && [ "$NOK" = 84 ] && [ "$NREF" = 8 ] && [ -z "$BAD" ] && [ -s "$KO/SUMMARY.md" ] && [ "$TU" = 1229687 ] \
    && verdict sort-kit PASS || verdict sort-kit FAIL
else tail -10 "$RES/sortkit-assemble.log"; verdict sort-kit FAIL; fi
took sort-kit $t
fi

if want check; then
sec "8. make check from a clean copy of the kit's sim/, alone"
t=$(date +%s)
# setarch -R: ThreadSanitizer refuses the address randomisation of newer kernels (vm.mmap_rnd_bits above 28)
( cd "$W/clean-sim" && setarch "$(uname -m)" -R make -j"$(nproc)" check ) > "$RES/make-check.log" 2>&1; R=$?
grep "^====" "$RES/make-check.log"; tail -3 "$RES/make-check.log"
[ $R = 0 ] && tail -1 "$RES/make-check.log" | grep -q "make check: all passed" && verdict make-check PASS || verdict make-check FAIL
took make-check $t
fi

sec "verdicts"
cat "$RES/VERDICTS"; echo; cat "$RES/TIMES"
rm -rf "$W"
FREEZE_RC=$FAIL
[ $FAIL = 0 ] && echo "ALL PASS (PENDING steps aside)" || echo "SOME FAILED"
