# body.sh -- the v1.35 freeze check's body (WORKER22; PLAN-V135 step 4), inlined into the task tests/freeze/make_task.sh
# writes (KIT, KS = the kit's sim/, RES, W, DATA, ITEMS set by the task's head). From the existing task bodies: W12's sort (Quest 154), W16's T2
# checked runs (Quest 171), W17's chains (Quest 174), W25's calibration (tests/acceptance). run_tests.sh, the
# calibration, the sort's measurement kit and make check each run alone, so their times are unloaded (for RUNNING.md
# and BUILDING.md); the chains run in the background while the sort and T2 run. Not a timing task otherwise: each
# item's time is recorded (TIMES), and run_tests.sh's per step (run_tests/.step-times, STEP_TIMES=1).
# ITEMS (the task's head; default "main kit check"): which groups run. The machine and the toolchain always.
#   main:
#   1 run_tests    ./run_tests.sh all --check, alone: PASS when it passes (its PENDING steps listed, not failures)
#   2 sort         test_sort's full-size run (SORT_SIZE=64k) on one thread: every sort64k-fp point, the result, the
#                  phases, TOTAL 1,229,687; and bench/w10's bench_sort on the same keys, one thread and 6 threads by
#                  tiles (each tile a domain, grouped in order, as Quests 157/158): the digests at L2's all-READY equal,
#                  the span 1,229,687 at both
#   3 t2           T2 on evm-zone-29 and cyclone-29 segment 10: every point against golden-m3's unit lines, the rows
#                  (tests/freeze/t2-rows.txt), the exits, lag_cycles 0; one domain and 6
#   4 chains       segments 0-127 of both workloads: every start against golden-chain, every TracerT2 check, the ends
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
sec "build: clang release (test_sort, bench_sort, t2, chain, test_chain); the acceptance kit's programs (clang suite)"
t=$(date +%s)
if ( cd "$KS" && make -j"$(nproc)" CC_NAME=clang MODE=release build/clang-release/test_sort build/clang-release/bench_sort build/clang-release/t2 \
       build/clang-release/chain build/clang-release/test_chain ) > "$RES/build-release.log" 2>&1 \
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

sec "4. the chains, segments 0-127 of both workloads (in the background)"
D9=$DATA/w9-deliveries; D26=$DATA/w26-jumpmap
declare -A S9=([evm-zone-29]=7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f [cyclone-29]=f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982)
declare -A S26=([evm-zone-29]=57dc0ffe7574ab3e11f79f8414501bd1ceb2fd75faab0b03f3b4a6c2c61cad2d [cyclone-29]=ace78f61efbb9c0e418a0b1d7f54a79f6e299b7ff45ad7f09537b11961b95fba)
checkin() {   # W17's: the inputs against their COMPLETE markers and hashes
  local P=$1 A B
  A=$(sha256sum "$D9/$P.manifest.json" 2>/dev/null | cut -d' ' -f1); B=$(sha256sum "$D26/$P/manifest.sha256" 2>/dev/null | cut -d' ' -f1)
  [ "$A" = "${S9[$P]}" ] && [ "$(cat "$D9/$P.COMPLETE" 2>/dev/null)" = "$A" ] || { echo "$P: the delivery isn't complete or differs"; return 1; }
  [ "$B" = "${S26[$P]}" ] && [ "$(cat "$D26/$P.COMPLETE" 2>/dev/null)" = "$B" ] || { echo "$P: the jumpmap inputs aren't complete or differ"; return 1; }
  (cd "$D26/$P" && sha256sum -c --quiet manifest.sha256) || { echo "$P: the jumpmap files differ from their manifest"; return 1; }
  echo "$P: inputs checked"
}
declare -A PROG=([evm]=evm-zone-29 [cyclone]=cyclone-29)
T4=$(date +%s); P_CH=""
if ( cd "$KS" && ./build/clang-release/test_chain ) > "$RES/test_chain.log" 2>&1 && checkin evm-zone-29 && checkin cyclone-29; then
  for S in evm cyclone; do
    ( ( cd "$KS" && ./build/clang-release/chain --w26 "$D26/${PROG[$S]}" --delivery "$D9" --program "${PROG[$S]}" --segments 0-127 \
        --oracle "tests/golden-chain/$S.starts" ) > "$RES/chain-$S.log" 2>&1; R=$?; date +%s > "$W/chain-end-$S"; exit $R ) & P_CH="$P_CH $!:$S"
  done
else tail -5 "$RES/test_chain.log"; verdict chain-inputs FAIL; fi

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

sec "3. T2 on segment 10 of both workloads, every point against golden-m3, one domain and 6 (parallel)"
T3=$(date +%s)
G=$DATA/w13-golden-m3
cat > "$W/h3" <<'H'
9ffec285e4f9c612157b7e51bc72b4dd82733716c058ebf9c74f2ccb7b999166  evm.fpchip.gz
bf53765324ee4388b64296f241c53508b540083374674b2d86e726cd322ec231  cyclone.fpchip.gz
dcf71fc01b5596c0aea3eb8366942376243eadabfb52ff3d5177639143490d24  evm.files.gz
8317810b72e8b07c25abc62c7f4bbbd4be54e64683e40a72eb9a91abaa40c9db  cyclone.files.gz
64838bcdf9bdfaa29067ccd2bc5a8610e9ba569d0a3415abfa34d466dd28f406  evm.sram.gz
a228f9c394514c92365e2e2bd3c1b1b363f510d9b04b86ca77786bc7b30d6f61  cyclone.sram.gz
H
if (cd "$G" && sha256sum -c --quiet "$W/h3"); then
  declare -A END=([evm]=23940706 [cyclone]=28319575) EXIT=([evm]=23694305 [cyclone]=28106741)
  PIDS=""
  for S in evm cyclone; do for N in 1 6; do
    ( cd "$KS" && ./build/clang-release/t2 --sram "$G/$S.sram.gz" --files "$G/$S.files.gz" --segment --parts $N --fp-points "$G/$S.fpchip.gz" \
        --rows-ref "$KS/tests/freeze/t2-rows.txt" --program "${PROG[$S]}" --hint-exit ${EXIT[$S]} --hint-end ${END[$S]} ) > "$RES/t2-$S-$N.log" 2>&1 & PIDS="$PIDS $!:$S-$N"
  done; done
  for X in $PIDS; do wait ${X%%:*}; R=$?; L=$RES/t2-${X#*:}.log
    grep -E "^boot-end|^end:|^points:|^tr_results|^exits" "$L" | sed "s/^/${X#*:}: /"
    [ $R = 0 ] && grep -q "points: [0-9]* checked, 0 with a difference.*lag_cycles 0;" "$L" && grep -q "rows: 64 of 64" "$L" && grep -q "exits: 64 of 64" "$L" \
      && verdict "t2-${X#*:}" PASS || { grep -v "^\.\.\.\| 0 differ" "$L" | head -20; verdict "t2-${X#*:}" FAIL; }
  done
else verdict t2-inputs FAIL; fi
took t2 $T3

sec "4. the chains: waiting"
for X in $P_CH; do wait ${X%%:*}; R=$?; S=${X#*:}; L=$RES/chain-$S.log
  head -1 "$L"; tail -1 "$L"; grep -A3 "segment [0-9]*: FAIL" "$L" | head -40
  N=$(grep -c "^${PROG[$S]} segment [0-9]*: OK  start equal" "$L")
  echo "$S: $N of 128 segments OK with the start memory equal to ref's"
  [ $R = 0 ] && [ "$N" = 128 ] && verdict "chain-$S" PASS || verdict "chain-$S" FAIL
  echo "chain-$S $(( $(cat "$W/chain-end-$S" 2>/dev/null || date +%s) - T4 )) s" >> "$RES/TIMES"   # its own end, not the wait's
done

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
( cd "$W/clean-sim" && make -j"$(nproc)" check ) > "$RES/make-check.log" 2>&1; R=$?
grep "^====" "$RES/make-check.log"; tail -3 "$RES/make-check.log"
[ $R = 0 ] && tail -1 "$RES/make-check.log" | grep -q "make check: all passed" && verdict make-check PASS || verdict make-check FAIL
took make-check $t
fi

sec "verdicts"
cat "$RES/VERDICTS"; echo; cat "$RES/TIMES"
rm -rf "$W"
FREEZE_RC=$FAIL
[ $FAIL = 0 ] && echo "ALL PASS (PENDING steps aside)" || echo "SOME FAILED"
