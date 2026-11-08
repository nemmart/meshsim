# Task body (WORKER10): where the time inside the framework's run loop goes on the boot (one thread): perf record
# (cycles) on bench_chip --prog sort --reps 9 built with -g (release flags otherwise), perf annotate of the run loop's
# instantiations for TileCore, Router and Director, and of deliver_link<3> and near_slot: the hot lines.
sec "build (clang release + -g, its own directory)"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release B=build/prof RELEASE="-O2 -DNDEBUG -g" build/prof/bench_chip > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
done
P1=$(take 1 "$P_ONE")
for k in $KITS; do
  cd "$W/kit/$k/sim"
  taskset -c "$P1" perf record -q -e cycles -o "$W/p.data" -- ./build/prof/bench_chip --prog sort --reps 9 > "$RES/run-$k.txt" 2>&1 || { echo "perf record failed"; continue; }
  perf report -i "$W/p.data" --stdio --no-children --percent-limit 0.5 --sort symbol 2>/dev/null | grep -v '^#' | grep -v '^$' | head -40 > "$RES/report-$k.txt"
  cat "$RES/report-$k.txt"
  perf annotate -i "$W/p.data" --stdio -l 2>/dev/null > "$W/ann.txt"
  for sym in 'run_form<units::TileCore' 'run_form<units::Router' 'run_form<units::Director' 'deliver_link<3u>' 'Engine::near_slot' 'Engine::after_far'; do
    echo "==== $sym (lines at 0.5% or more of the symbol)"
    awk -v s="$sym" 'index($0, "Percent |") && index($0, s) { on = 1; print; next } index($0, "Percent |") { on = 0 } on && $1 ~ /^[0-9.]+$/ && $1 + 0 >= 0.5' "$W/ann.txt" | head -60
  done | tee "$RES/annotate-$k.txt"
  cd - > /dev/null
done
verdict "annotate done" PASS
