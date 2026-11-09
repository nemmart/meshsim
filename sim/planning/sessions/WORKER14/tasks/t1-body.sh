# t1-body.sh (WORKER14): T1 on the runner, and TracerConf's long sweep; the C++ Tracer against ref's, cycle by cycle.
# T1: ref's TracerT1 (the Tracer, its memory system and the ideal controller on segments SEGS of evm-zone-29 and
#   cyclone-29, in order on one memory, every window checked against WORKER9's delivery and Stage A) through TracerConf's
#   recording proxy, written into a FIFO that test_tracer replays as it is written: every call's result, every cycle's
#   observation, the fingerprint (AUDIT-M3 6.6) every 100,000 cycles and at the end, the store hashed at each record
#   load. Both programs in parallel (two JVMs, two replays).
# Sweep: TracerConf's seeded guests beyond make check's: mix seeds 41-SWEEP, wb seeds 4-16, wbover seeds 2-6, each a
#   JVM writing a FIFO and a replay reading it, JOBS at a time.
SEGS=${SEGS:-0-3}; SWEEP=${SWEEP:-200}; JOBS=${JOBS:-6}
sec "build (clang release)"
( cd "$KS" && make -j8 CC_NAME=clang MODE=release build/clang-release/test_tracer > "$RES/build.log" 2>&1 ) \
  && verdict build PASS || { tail -20 "$RES/build.log"; verdict build FAIL; exit 1; }
TT=$KS/build/clang-release/test_tracer
# run1 TAG JAVA-ARGS...: ref into a FIFO, test_tracer replaying it; both logs kept; the replay's verdict
run1() {
  local tag=$1; shift
  local f=$W/$tag.fifo; rm -f "$f"; mkfifo "$f"
  ( cd "$KR" && java -Xmx${HEAP:-5g} -Dreplay.tables=$KT -cp "$KC" TracerConf "$1" "$f" "${@:2}" ) > "$W/$tag.ref" 2>&1 &
  local jp=$!
  ( cd "$KS" && TRACERCONF_STREAM="$f" "$TT" stream ) > "$W/$tag.cpp" 2>&1
  local rc=$?
  kill $jp 2>/dev/null; wait $jp; local jr=$?
  echo "$tag ref-exit $jr cpp-exit $rc :: $(grep -E 'all equal|test_tracer.cpp' "$W/$tag.cpp" | head -2 | tr '\n' ' ')" >> "$W/sweep.txt"
  return $rc
}
sec "inputs"
OKIN=1
declare -A M26=([evm-zone-29]=57dc0ffe7574ab3e11f79f8414501bd1ceb2fd75faab0b03f3b4a6c2c61cad2d [cyclone-29]=ace78f61efbb9c0e418a0b1d7f54a79f6e299b7ff45ad7f09537b11961b95fba)
declare -A M9=([evm-zone-29]=7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f [cyclone-29]=f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982)
for P in evm-zone-29 cyclone-29; do
  h=$(sha256sum "$D26/$P/manifest.sha256" 2>/dev/null | cut -d' ' -f1)
  [ "$h" = "${M26[$P]}" ] && (cd "$D26/$P" && sha256sum -c --quiet manifest.sha256) || { echo "$P: w26 inputs missing or differ ($h)"; OKIN=0; }
  h=$(sha256sum "$D9/$P.manifest.json" 2>/dev/null | cut -d' ' -f1)
  [ "$h" = "${M9[$P]}" ] && [ "$(cat "$D9/$P.COMPLETE" 2>/dev/null | cut -d' ' -f1)" = "$h" ] || { echo "$P: delivery manifest missing or differs ($h)"; OKIN=0; }
done
[ $OKIN = 1 ] && verdict inputs PASS || verdict inputs FAIL
if [ $OKIN = 1 ]; then
  sec "T1: segments $SEGS of both programs, ref through TracerConf into test_tracer"
  PIDS=""
  for P in evm-zone-29 cyclone-29; do
    T=t1-${P%%-*}
    ( HEAP=6g run1 $T t1 "$D9" "$P" --w26 "$D26/$P" --segments "$SEGS" --quiet ) & PIDS="$PIDS $!:$T"
  done
  for X in $PIDS; do
    wait ${X%%:*}; R=$?; T=${X#*:}
    cp "$W/$T.ref" "$RES/$T.ref.log"; cp "$W/$T.cpp" "$RES/$T.cpp.log"
    grep -E 'segment [0-9]+:|windows checked' "$W/$T.ref" | cut -c1-200
    tail -3 "$W/$T.cpp"
    grep -qE 'windows checked .* 0 failed' "$W/$T.ref" && ! grep -q 'segment [0-9]*: FAIL' "$W/$T.ref" && verdict "$T-ref-against-delivery" PASS || verdict "$T-ref-against-delivery" FAIL
    [ $R = 0 ] && grep -q '^ok   stream_from_tracerconf_stream' "$W/$T.cpp" && verdict "$T-cpp-equals-ref" PASS || verdict "$T-cpp-equals-ref" FAIL
  done
fi
sec "the sweep: mix 41-$SWEEP, wb 4-16, wbover 2-6 ($JOBS at a time)"
: > "$W/sweep.txt"
LIST=""; for s in $(seq 41 $SWEEP); do LIST="$LIST mix:$s"; done
for s in $(seq 4 16); do LIST="$LIST wb:$s"; done; for s in 2 3 4 5 6; do LIST="$LIST wbover:$s"; done
n=0
for x in $LIST; do
  f=${x%%:*}; s=${x#*:}
  ( run1 "$f-s$s" gen "$s" "$f" ) &
  n=$((n + 1)); [ $((n % JOBS)) = 0 ] && wait
done
wait
cp "$W/sweep.txt" "$RES/sweep.txt"
TOT=$(wc -l < "$W/sweep.txt"); OK=$(grep -c 'cpp-exit 0 .*all equal' "$W/sweep.txt")
echo "sweep: $OK of $TOT replays equal to ref"
grep -v 'cpp-exit 0 .*all equal' "$W/sweep.txt" | head -20
[ "$TOT" -gt 0 ] && [ "$OK" = "$TOT" ] && verdict "sweep-$TOT-logs" PASS || verdict "sweep-$OK-of-$TOT" FAIL
