# speed-body.sh (WORKER14): the Tracer's speed alone, C++ against ref, on T1's segments (AUDIT-M3 section 5: C&C 3's
# Tracer decides M3's wall time). Not optimising: a measurement for the owner (D31's spirit).
# 1. The traces (not timing; both programs in parallel): ref's TracerT1 on segments SEGS through TracerConf into a FIFO,
#    converted by tracer_play into a compact call trace (the calls only: no proxy, no comparison; Quest 163 showed the
#    C++ Tracer equal to ref on these calls, cycle by cycle).
# 2. Timing (alone, sequential, one P core; D21's machine record): tracer_play run (C++, clang release, D18) and
#    TracerPlay (ref's Tracer, one JVM on the core and its hyperthread), REPS in-process repetitions each, per segment
#    and in all, plus each harness's own decoding cost; every pass checked to end on ref's last rows and record hash.
# 3. perf (cycles) on tracer_play (clang release + -g): the report and the hot lines of the Tracer's step.
SEGS=${SEGS:-0-3}; REPS=${REPS:-5}
sec "machine (D21)"; . "$KS/tools/machine.sh" "${QUEST_P_CPUS:-}" "${QUEST_E_CPUS:-}"; machine_record "$RES/machine.txt"; uptime
sec "build (clang release; and clang release + -g for perf, its own directory)"
( cd "$KS" && make -j8 CC_NAME=clang MODE=release build/clang-release/tracer_play > "$RES/build.log" 2>&1 \
  && make -j8 CC_NAME=clang MODE=release B=build/prof RELEASE="-O2 -DNDEBUG -g" build/prof/tracer_play >> "$RES/build.log" 2>&1 ) \
  && verdict build PASS || { tail -20 "$RES/build.log"; verdict build FAIL; exit 1; }
TP=$KS/build/clang-release/tracer_play
sec "inputs"
OKIN=1
declare -A M26=([evm-zone-29]=57dc0ffe7574ab3e11f79f8414501bd1ceb2fd75faab0b03f3b4a6c2c61cad2d [cyclone-29]=ace78f61efbb9c0e418a0b1d7f54a79f6e299b7ff45ad7f09537b11961b95fba)
declare -A M9=([evm-zone-29]=7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f [cyclone-29]=f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982)
for P in evm-zone-29 cyclone-29; do
  h=$(sha256sum "$D26/$P/manifest.sha256" 2>/dev/null | cut -d' ' -f1)
  [ "$h" = "${M26[$P]}" ] && (cd "$D26/$P" && sha256sum -c --quiet manifest.sha256) || { echo "$P: w26 inputs missing or differ"; OKIN=0; }
  h=$(sha256sum "$D9/$P.manifest.json" 2>/dev/null | cut -d' ' -f1)
  [ "$h" = "${M9[$P]}" ] || { echo "$P: delivery manifest missing or differs"; OKIN=0; }
done
[ $OKIN = 1 ] && verdict inputs PASS || { verdict inputs FAIL; exit 1; }
sec "1. the traces: segments $SEGS (ref through TracerConf, converted; both programs in parallel)"
PIDS=""
for P in evm-zone-29 cyclone-29; do
  S=${P%%-*}; f=$W/$S.fifo; rm -f "$f"; mkfifo "$f"
  ( cd "$KR" && java -Xmx6g -Dreplay.tables=$KT -cp "$KC" TracerConf t1 "$f" "$D9" "$P" --w26 "$D26/$P" --segments "$SEGS" --quiet ) > "$RES/$S-ref.log" 2>&1 &
  ( "$TP" convert "$f" "$W/$S.trace" ) > "$RES/$S-convert.log" 2>&1 & PIDS="$PIDS $!:$S"
done
for X in $PIDS; do wait ${X%%:*}; R=$?; S=${X#*:}; cat "$RES/$S-convert.log"; tail -1 "$RES/$S-ref.log"
  [ $R = 0 ] && grep -qE 'windows checked .* 0 failed' "$RES/$S-ref.log" && verdict "trace-$S" PASS || verdict "trace-$S" FAIL; done
wait
ls -la $W/*.trace
P1=$(take 1 "$P_ONE")
ONE=$(keep "$(expand "$(cat /sys/devices/system/cpu/cpu$P1/topology/thread_siblings_list 2>/dev/null || echo "$P1")")" "$P_CPUS")
sec "2. timing: C++ on CPU $P1, ref's JVM on CPUs $ONE; $REPS repetitions each; nothing else running"
for S in evm cyclone; do
  [ -f "$W/$S.trace" ] || continue
  taskset -c "$P1" "$TP" run "$W/$S.trace" "$REPS" > "$RES/cpp-$S.txt" 2>&1 && verdict "cpp-$S" PASS || { tail -3 "$RES/cpp-$S.txt"; verdict "cpp-$S" FAIL; }
  cat "$RES/cpp-$S.txt"
  ( cd "$KR" && taskset -c "$ONE" java -Xmx6g -cp "$KC" TracerPlay "$W/$S.trace" "$REPS" ) > "$RES/ref-$S.txt" 2>&1 && verdict "ref-$S" PASS || { tail -3 "$RES/ref-$S.txt"; verdict "ref-$S" FAIL; }
  cat "$RES/ref-$S.txt"
done
for S in evm cyclone; do
  c=$(grep -o 'median ns_per_cycle=[0-9.]*' "$RES/cpp-$S.txt" | cut -d= -f2); r=$(grep -o 'median ns_per_cycle=[0-9.]*' "$RES/ref-$S.txt" | cut -d= -f2)
  ch=$(grep -o '^harness .*ns_per_cycle=[0-9.]*' "$RES/cpp-$S.txt" | grep -o '[0-9.]*$'); rh=$(grep -o '^harness .*ns_per_cycle=[0-9.]*' "$RES/ref-$S.txt" | grep -o '[0-9.]*$')
  awk -v s=$S -v c="$c" -v r="$r" -v ch="$ch" -v rh="$rh" 'BEGIN { if (c > 0) printf "SPEED %s: C++ %.2f ns/cycle (harness %.2f), ref %.2f ns/cycle (harness %.2f): ref/C++ %.2fx\n", s, c, ch, r, rh, r / c }'
done | tee "$RES/SPEED.txt"
sec "3. perf (cycles) on tracer_play, evm-zone-29, one repetition"
if command -v perf > /dev/null && [ -f "$W/evm.trace" ]; then
  cd "$KS"
  taskset -c "$P1" perf record -q -e cycles -o "$W/p.data" -- ./build/prof/tracer_play run "$W/evm.trace" 1 > "$RES/perf-run.txt" 2>&1
  perf report -i "$W/p.data" --stdio --no-children --percent-limit 0.3 --sort symbol 2>/dev/null | grep -v '^#' | grep -v '^$' | head -40 | tee "$RES/perf-report.txt"
  perf annotate -i "$W/p.data" --stdio -l 2>/dev/null > "$W/ann.txt"
  SYMS="Tracer::step|Tracer::row|Tracer::settle|Tracer::fetch|Tracer::data_line|Tracer::lookup|Tracer::drain_cycle|Tracer::push_|LruIndex|MemSystem|LruCoder|EventCounts|FileStore::walk|pass<true>"
  awk -v syms="$SYMS" -v th="0.5" '
    BEGIN { n = split(syms, pat, "|") }
    /Percent \|/ { insym = 0; sym = ""; next }
    sym == "" && match($0, /<[^>]*(<[^>]*>)*[^>]*>:[ \t]*$/) {
      sym = substr($0, RSTART + 1, RLENGTH - 3); insym = 0
      for (i = 1; i <= n; i++) if (index(sym, pat[i])) insym = 1
      if (insym) { print ""; print "==== " sym }
      next
    }
    !insym { next }
    $1 ~ /^[0-9]+\.[0-9]+$/ && $2 == ":" { if ($1 + 0 >= th) { if (src != "") print "      src: " src; print; } src = ""; next }
    $1 == ":" && NF > 1 { src = $0; sub(/^[ \t]*:[ \t]*/, "", src) }
  ' "$W/ann.txt" > "$RES/annotate.txt"
  head -200 "$RES/annotate.txt"
  [ -s "$RES/perf-report.txt" ] && verdict perf PASS || verdict perf FAIL
  cd - > /dev/null
else
  echo "no perf on this machine"; verdict perf FAIL
fi
