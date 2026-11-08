# Task body (WORKER10): the hot lines of any unit or framework function, from one task. perf record (cycles) on a
# bench_chip run built clang release + -g (its own build directory), then perf annotate --stdio: for each symbol whose
# name contains one of SYMS ('|'-separated), its lines at THRESH percent of the symbol or more, with the source lines
# perf interleaves. ARGS: the bench_chip arguments (default the sort's boot, one thread, 9 machines).
# For the sort: ARGS="--prog sort16k ..." SYMS="WriteEngine|Gather|..." (the units' class names).
ARGS=${ARGS:---prog sort --reps 9}
SYMS=${SYMS:-run_form<units::TileCore|run_form<units::Router|deliver_link<3u>|Engine::near_slot|units::Router::step|TileCore::step}
THRESH=${THRESH:-0.5}
sec "build (clang release + -g, its own directory)"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release B=build/prof RELEASE="-O2 -DNDEBUG -g" build/prof/bench_chip > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
done
P1=$(take 1 "$P_ONE")
for k in $KITS; do
  cd "$W/kit/$k/sim"
  sec "perf record: bench_chip $ARGS ($k)"
  if ! taskset -c "$P1" perf record -q -e cycles -o "$W/p.data" -- ./build/prof/bench_chip $ARGS > "$RES/run-$k.txt" 2>&1; then
    verdict "perf record ($k)" FAIL; cd - > /dev/null; continue
  fi
  perf report -i "$W/p.data" --stdio --no-children --percent-limit 0.5 --sort symbol 2>/dev/null | grep -v '^#' | grep -v '^$' | head -40 | tee "$RES/report-$k.txt"
  perf annotate -i "$W/p.data" --stdio -l 2>/dev/null > "$W/ann.txt"
  head -120 "$W/ann.txt" > "$RES/annotate-raw-head-$k.txt"            # the format, for the parse below
  # a block starts at a "Percent |" line; its symbol is the first "<name>:" line after it; lines "P : ..." with P a
  # percent of the symbol's samples; source lines (perf -l) are kept between them when a hot line follows
  awk -v syms="$SYMS" -v th="$THRESH" '
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
  ' "$W/ann.txt" | tee "$RES/annotate-$k.txt" | head -300
  lines=$(grep -c ' : ' "$RES/annotate-$k.txt")
  [ "$lines" -gt 0 ] && verdict "annotate found hot lines ($k: $lines)" PASS || verdict "annotate found hot lines ($k)" FAIL
  cd - > /dev/null
done
