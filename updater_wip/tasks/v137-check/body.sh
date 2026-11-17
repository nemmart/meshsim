# body.sh -- the body of the v1.37_wip check's Quest tasks (make_task.sh inlines it after a head that sets REPO, RES, W,
# KIT, KS = the kit's v1.37_wip/, REV and ITEM). v136-check's body, for v1.37_wip. ITEM:
#   suites   1 convert_delivery (clang release); 2 the small inputs (EVM Zone trace-23, Cyclone input/22) built from the
#            pinned sources with pipeline/inputs/build.sh, jolt-asic-planning from Quest task 111's kit, gold's and
#            jumpmap's classes from this kit, each build against its manifest (check.sh); 3 ./run_tests.sh all --check with
#            those inputs and the kit's link vectors (PACKET_VECTORS). A step run_tests.sh prints as SKIPPED (the firmware
#            toolchain is not the runner's; no javac for the fixtures) is listed and is not a failure; if the inputs can't
#            be built the real-data steps are skipped, and that is one. Between 2 and 3, 2b: the host's side of v1.37's
#            proof steps (pipeline/frames/build.sh matrix real 29, with the kit's classes of the frames' gold, each packet
#            file also against the tests' writer), into the same inputs' directory; after 3, 4: the proof runs counted
#            (3 recorded segments, 24 of the matrix, 10 at 2^29: each must have passed) and their figures
#            (tests/proof_numbers.py) in proof-numbers.md.
#   check    make check from a clean tree: every test, debug and release, gcc and clang, and the TSan runs.
#   suites-cache  (WORKER5) as suites, with CACHE_CTL=1: the proof steps on the cache's controller (proof_run --cache)
#            and the step tracer-block-cache; every other step as suites runs it.
# VERDICTS: one line per item. TIMES: each item's wall time. Scratch in /tmp, removed first and at the end.
FAIL=0; T0=$(date +%s); N=$(nproc)
sec() { echo; echo "==== $* ($(date -u +%T), +$(( $(date +%s) - T0 )) s)"; }
verdict() { echo "VERDICT $1: $2"; echo "$1 $2" >> "$RES/VERDICTS"; case $2 in PASS*|SKIPPED*) ;; *) FAIL=1;; esac; }
took() { echo "$1 $(( $(date +%s) - $2 )) s" >> "$RES/TIMES"; }
: > "$RES/VERDICTS"; : > "$RES/TIMES"

sec "the machine"
{ echo "kit: $(cat "$KIT/VERSION")"; echo "host $(hostname); $N CPUs; $(uname -sr)"
  clang++ --version 2>&1 | head -1; g++ --version 2>&1 | head -1; make --version 2>&1 | head -1
  java -version 2>&1 | grep -i version | head -1; cmake --version 2>&1 | head -1; python3 --version 2>&1
  for t in riscv-none-elf-gcc riscv64-unknown-elf-gcc; do echo "$t: $("$t" --version 2>/dev/null | head -1)"; done
  free -g | sed -n 2p; df -BG --output=avail /tmp | tail -1
} 2>&1 | tee "$RES/machine.txt"
verdict machine PASS
[ -n "${DRY:-}" ] && { echo "DRY: stopping after the kit and the machine"; rm -rf "$W"; exit 0; }

CACHE=""; [ "$ITEM" = suites-cache ] && CACHE=1
if [ "$ITEM" = suites ] || [ "$ITEM" = suites-cache ]; then
  sec "1. convert_delivery (clang release), which the inputs' build needs"
  t=$(date +%s)
  if ( cd "$KS" && make -j"$N" MODE=release CC_NAME=clang build/clang-release/convert_delivery ) > "$RES/build-convert.log" 2>&1
  then verdict build-convert PASS; else tail -20 "$RES/build-convert.log"; verdict build-convert FAIL; fi
  took build-convert "$t"

  sec "2. the small inputs, from the pinned sources (jolt-asic-planning from Quest task 111's kit)"
  t=$(date +%s); IN=$W/inputs; INOK=1; K111=$REPO/tasks/111-w27-inputs-evm-zone-29.sh
  if [ -f "$K111" ] && sed -n '/^__KIT_BEGIN__$/,/^__KIT_END__$/p' "$K111" | sed '1d;$d' | base64 -d > "$W/kit111.tgz" \
     && [ "$(sha256sum "$W/kit111.tgz" | cut -d' ' -f1)" = 3ddbcebf08ff8cf3aaf75c1b88fd5eceb93a2a62ce2c942b42ef55f14c0a00b3 ] \
     && mkdir -p "$W/i" && tar -xzf "$W/kit111.tgz" -C "$W/i" k/jap k/libelf; then
    rm -f "$W/kit111.tgz"
    [ -e /usr/include/libelf.h ] || [ -e /usr/include/libelf/libelf.h ] || { export LIBELF_PREFIX=$W/i/k/libelf; echo "no system libelf.h: task 111's kit's"; }
    for w in evm-zone cyclone; do
      OUT=$IN JAP_DIR=$W/i/k/jap CLASSES=$KIT/classes GOLD_FORMAT="v1.2 v1.3" MESHSIM_COMMIT=$REV JOBS=$N \
        bash "$KS/pipeline/inputs/build.sh" "$w" small > "$RES/inputs-$w.log" 2>&1 || { tail -15 "$RES/inputs-$w.log"; INOK=0; }
      grep -v INFO "$RES/inputs-$w.log" | tail -3
    done
    for r in evm-zone-23 cyclone-22; do
      OUT=$IN bash "$KS/pipeline/inputs/check.sh" "$r" > "$RES/inputs-check-$r.txt" 2>&1 || INOK=0; tail -1 "$RES/inputs-check-$r.txt"
    done
    du -sh "$IN" 2>/dev/null
  else echo "Quest task 111's kit is not at $K111, or differs"; INOK=0; fi
  [ $INOK = 1 ] && verdict inputs-small PASS || verdict inputs-small FAIL
  took inputs-small "$t"

  sec "2b. the host's side of the proof steps: gold's frames, packets and references (pipeline/frames/build.sh)"
  t=$(date +%s)
  ( cd "$KS" && make -j"$N" MODE=suite CC_NAME=clang build/clang-suite/packetize ) > "$RES/build-packetize.log" 2>&1 || tail -5 "$RES/build-packetize.log"
  if [ $INOK = 1 ] && OUT=$IN JAP_DIR=$W/i/k/jap GOLD_CLASSES=$KIT/classes-frames PACKETIZE=$KS/build/clang-suite/packetize \
       bash "$KS/pipeline/frames/build.sh" matrix real 29 > "$RES/frames.log" 2>&1
  then grep -v JAVA_TOOL "$RES/frames.log" | tail -16; verdict frames PASS
  else tail -8 "$RES/frames.log" 2>/dev/null; verdict frames FAIL; fi
  cat "$IN"/v137/*/SHA256SUMS > "$RES/frames-sha256.txt" 2>/dev/null
  took frames "$t"

  sec "3. ./run_tests.sh all --check (JOBS=$N; MESHSIM_INPUTS=$IN; CACHE_CTL=$CACHE)"
  t=$(date +%s)
  ( cd "$KS" && CACHE_CTL=$CACHE MESHSIM_INPUTS=$IN PACKET_VECTORS=$KIT/updater_wip/pipeline/gold/test/packets STEP_TIMES=1 JOBS=$N ./run_tests.sh all --check ) > "$RES/run_tests.log" 2>&1; R=$?
  took run_tests "$t"
  tail -45 "$RES/run_tests.log"
  cp "$KS/results/.step-times" "$RES/step-times.txt" 2>/dev/null; cp "$KS/results/.differs" "$RES/differs.txt" 2>/dev/null
  tar -czf "$RES/run_tests-logs.tgz" -C "$KS" results 2>/dev/null
  [ "$(stat -c %s "$RES/run_tests-logs.tgz" 2>/dev/null || echo 0)" -le 20000000 ] || rm -f "$RES/run_tests-logs.tgz"
  LAST=$(grep '^== ' "$RES/run_tests.log" | tail -1); echo "exit $R; $LAST"
  if [ $R = 0 ] && [[ "$LAST" == "== all passed"* ]]; then verdict run_tests PASS; else verdict run_tests FAIL; fi
  sec "4. the proof runs, counted, and their figures"
  for p in real:3 matrix:24 29:10; do
    L=$KS/results/proof-${p%%:*}.log; want=${p##*:}
    got=$(grep -c '^PASS proof_run' "$L" 2>/dev/null); bad=$(grep -c '^FAIL' "$L" 2>/dev/null)
    cp "$L" "$RES/" 2>/dev/null
    if [ "${got:-0}" = "$want" ] && [ "${bad:-0}" = 0 ]; then verdict "proof-${p%%:*}" "PASS-$got-runs"; else verdict "proof-${p%%:*}" "FAIL-${got:-0}-of-$want-passed"; fi
  done
  ( cd "$KS" && python3 tests/proof_numbers.py results/proof-real.log results/proof-matrix.log results/proof-29.log ) > "$RES/proof-numbers.md" 2>&1
  [ -z "$CACHE" ] || { cp "$KS/results/tracer-block-cache.log" "$RES/" 2>/dev/null; grep -h 'the cache:\|LPDDR:' "$KS"/results/proof-*.log > "$RES/cache-figures.txt" 2>/dev/null; }
  tail -3 "$RES/proof-numbers.md"
  SK=$(grep -c 'SKIPPED (' "$RES/run_tests.log")
  [ "$SK" = 0 ] || verdict run_tests-skipped "SKIPPED-$SK-steps: $(grep 'SKIPPED (' "$RES/run_tests.log" | sed 's/^[^a-z]*//; s/ .*//' | sort -u | tr '\n' ' ')"
fi

if [ "$ITEM" = check ]; then
  sec "make check from a clean tree (-j$N; setarch -R: ThreadSanitizer refuses newer kernels' address randomisation)"
  t=$(date +%s)
  ( cd "$KS" && setarch "$(uname -m)" -R make -j"$N" check ) > "$RES/make-check.log" 2>&1; R=$?
  took make-check "$t"
  tail -12 "$RES/make-check.log"
  if [ $R = 0 ] && tail -3 "$RES/make-check.log" | grep -q 'make check: all passed'; then verdict make-check PASS; else verdict make-check FAIL; fi
fi

sec "end"
cat "$RES/VERDICTS"; cat "$RES/TIMES"
rm -rf "$W"
[ $FAIL = 0 ] && echo "ALL PASS" || echo "FAILED"
exit $FAIL
