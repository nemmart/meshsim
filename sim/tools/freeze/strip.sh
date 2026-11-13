#!/bin/bash
# tools/freeze/strip.sh [DIR | --apply] [--make-check]: P8's one-time cleanup, run once after the ports finish (proposal
# rule 4, D45). Three changes, then their proof:
#   1. the moves: the benches' folders take their subjects' names (bench/w10's kept benches to bench/sort/, w10.mk to
#      sort.mk; bench/w14 to bench/tracer/, w14.mk to tracer.mk), and the three audits in tests/ go to planning/audits/
#      (names unchanged); every path to them in a source file or a document outside planning/ and oracle/ follows (not
#      fixtures, nor the oracle, whose writers put the audit's old path in the fixtures' headers);
#   2. the comments: worker names and planning codes out (strip-rules.txt: cstrip's @untag pass, then the hand-reviewed
#      entries for what it can't decide); references to ref stay; only comments change, and no line is added or removed;
#   3. the messages and usage lines (messages.txt, hand-reviewed: FILE<TAB>old<TAB>new, each old found exactly once).
# The proof: every C++ translation unit preprocessed (g++ -E -P, in the debug, release and suite builds' defines) byte
# for byte as before, except the files messages.txt edits (their differences listed) and the renamed benches' own
# paths; the firmware images byte-identical (fw/build.sh --check, where the toolchain is); pipeline/check.sh (a JDK
# needed); every other edited file's
# code unchanged (cstrip's own check) and each shell script still parsing (bash -n). Then the tags left are listed: in
# comments, and in code (strings and names), less keep.txt's (what stays on purpose: register words, fixtures' lines).
# --make-check: also make check and run_tests.sh quick --check in the result.
#   DIR       (the default, /tmp/strip-dry) a dry run: sim/'s tracked files (less ref/) copied into DIR and changed there
#   --apply   the real run, in sim/ itself (git mv for the renames); commit it as one change with the freeze task's update
set -eu
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
SIM=$(cd "$(dirname "$0")/../.." && pwd); F=$SIM/tools/freeze
DIR=/tmp/strip-dry; APPLY=0; MC=0
for a in "$@"; do case $a in --apply) APPLY=1;; --make-check) MC=1;; *) DIR=$a;; esac; done
cd "$SIM"
mkdir -p build/strip
g++ -std=c++20 -O2 -Wall -Wextra -Werror "$F/cstrip.cpp" -o build/strip/cstrip
CS=$SIM/build/strip/cstrip
BEFORE=$SIM   # the tree before, for the proof
if [ $APPLY = 1 ]; then
  [ -z "$(git status --porcelain -- .)" ] || { echo "strip.sh --apply: sim/ has uncommitted changes"; exit 1; }
  W=$SIM; BEFORE=$(mktemp -d); trap 'rm -rf "$BEFORE"' EXIT
  git ls-files -- . | grep -v '^ref/' | tar -cf - --no-recursion -T - | tar -xf - -C "$BEFORE"
else
  rm -rf "$DIR"; mkdir -p "$DIR"
  git ls-files -- . | grep -v '^ref/' | tar -cf - --no-recursion -T - | tar -xf - -C "$DIR"
  W=$(cd "$DIR" && pwd)
fi
R=$W/build/strip-report; mkdir -p "$R"; : > "$R/summary"
say() { echo "$*" | tee -a "$R/summary"; }

# ---- 1. the bench renames
MOVES="bench/w10/bench_sort.cpp:bench/sort/bench_sort.cpp bench/w10/bench_chip.cpp:bench/sort/bench_chip.cpp
bench/w10/trace_filter.cpp:bench/sort/trace_filter.cpp bench/w10/trace.sh:bench/sort/trace.sh bench/w10/w10.mk:bench/sort/sort.mk
bench/w14/tracer_play.cpp:bench/tracer/tracer_play.cpp bench/w14/w14.mk:bench/tracer/tracer.mk
tests/AUDIT-M1.md:planning/audits/AUDIT-M1.md tests/AUDIT-M2.md:planning/audits/AUDIT-M2.md tests/AUDIT-M3.md:planning/audits/AUDIT-M3.md"
cd "$W"
for m in $MOVES; do
  a=${m%%:*}; b=${m#*:}; mkdir -p "$(dirname "$b")"
  if [ $APPLY = 1 ]; then git mv "$a" "$b"; else mv "$a" "$b"; fi
done
# the paths to them, outside the history (planning/) and the frozen-out oracle's own copies
grep -rlE 'bench/w1[04]\b|\bw1[04]\.mk\b|tests/AUDIT-M[123]\.md' --exclude-dir=planning --exclude-dir=build --exclude-dir=.git . 2>/dev/null |
  sed 's|^\./||' | grep -Ev '^(tools/freeze|oracle)/' | grep -E '(\.(md|h|hpp|cpp|c|S|sh|mk|py|inc|java|ld)|/Makefile|^Makefile)$' > "$R/renamed-refs" || true
while IFS= read -r f; do
  sed -i -E 's#bench/w10/w10\.mk#bench/sort/sort.mk#g; s#bench/w14/w14\.mk#bench/tracer/tracer.mk#g; s#bench/w10/#bench/sort/#g; s#bench/w14/#bench/tracer/#g; s#\bw10\.mk\b#sort.mk#g; s#\bw14\.mk\b#tracer.mk#g; s#bench/w10\b#bench/sort#g; s#bench/w14\b#bench/tracer#g; s#(sim/)?tests/AUDIT-(M[123])\.md#planning/audits/AUDIT-\2.md#g' "$f"
done < "$R/renamed-refs"
say "1. moves: $(echo "$MOVES" | wc -w) files moved; paths updated in $(wc -l < "$R/renamed-refs") files ($(paste -sd' ' "$R/renamed-refs"))"

# ---- 2. the comments
"$F/strip_files.sh" > "$R/files"
# strip_files.sh lists sim/'s paths; in the dry run's copy the renamed benches have their new names
for m in $MOVES; do sed -i "s#\t${m%%:*}\$#\t${m#*:}#" "$R/files"; done
: > "$R/used"; n=0; ch=0
while IFS=$'\t' read -r lang f; do
  [ -f "$f" ] || continue
  n=$((n + 1))
  "$CS" apply "$lang" "$F/strip-rules.txt" "$f" > "$R/out" 2>> "$R/used" || { echo "cstrip failed on $f"; exit 1; }
  cmp -s "$R/out" "$f" || { ch=$((ch + 1)); echo "$f" >> "$R/changed"; cat "$R/out" > "$f"; }
done < "$R/files"
grep -v '^USED ' "$R/used" > "$R/cstrip-errors" || true
[ ! -s "$R/cstrip-errors" ] || { cat "$R/cstrip-errors"; exit 1; }
unused=$(grep '^=' "$F/strip-rules.txt" | while IFS= read -r e; do grep -qxF "USED $e" "$R/used" || echo "$e"; done)
[ -z "$unused" ] || { echo "hand entries not found (re-review them):"; echo "$unused"; exit 1; }
say "2. comments: $n files read, $ch changed; $(grep -c '^=' "$F/strip-rules.txt" || true) hand entries, all used"

# ---- 3. the messages
m=0
while IFS=$'\t' read -r f old new; do
  case $f in ''|'#'*) continue;; esac
  for mv in $MOVES; do [ "$f" = "${mv%%:*}" ] && f=${mv#*:}; done
  c=$(grep -cF -- "$old" "$f" || true)
  [ "$c" = 1 ] || { echo "messages.txt: '$old' is in $f $c times (1 expected)"; exit 1; }
  OLD=$old NEW=$new perl -0pi -e 's/\Q$ENV{OLD}\E/$ENV{NEW}/' "$f"
  echo "$f" >> "$R/messages-files"; m=$((m + 1))
done < "$F/messages.txt"
sort -u -o "$R/messages-files" "$R/messages-files" 2>/dev/null || : > "$R/messages-files"
say "3. messages: $m edits in $(wc -l < "$R/messages-files") files"

# ---- the proof: preprocessed output
COMMON="-std=c++20 -fno-exceptions -fno-rtti -Isrc -pthread"
: > "$R/pp-differ"; tu=0
for f in $(cd "$W" && git -C "$SIM" ls-files -- 'src/*.cpp' 'src/**/*.cpp' 'tests/*.cpp' 'tests/**/*.cpp' 'tools/*.cpp' 'tools/**/*.cpp' 'bench/*.cpp' 'bench/**/*.cpp' | grep -v '^src/sketch/\|^tools/freeze/' | sort -u); do
  g=$f; for mv in $MOVES; do [ "$f" = "${mv%%:*}" ] && g=${mv#*:}; done
  [ -f "$W/$g" ] || continue
  tu=$((tu + 1))
  for defs in "-O1 -DSIM_DEBUG" "-O2 -DNDEBUG" "-O2 -DNDEBUG -DSIM_CHECKS"; do
    (cd "$BEFORE" && g++ $COMMON $defs -E -P "$f" 2>/dev/null | sed "s#$f#$g#g") > "$R/a.i" || true
    (cd "$W" && g++ $COMMON $defs -E -P "$g" 2>/dev/null) > "$R/b.i" || true
    [ -s "$R/a.i" ] || { echo "$f [$defs]: does not preprocess"; exit 1; }
    cmp -s "$R/a.i" "$R/b.i" || { echo "$g [$defs]: $(diff "$R/a.i" "$R/b.i" | grep -c '^[<>]') lines" >> "$R/pp-differ"; }
  done
done
# a difference is expected only in a translation unit including a file messages.txt edits
unexp=$(cut -d' ' -f1 "$R/pp-differ" | sort -u | while read -r t; do
  inc=$(cd "$W" && g++ $COMMON -MM "$t" 2>/dev/null | tr ' \\' '\n\n' | grep -v ':$' | grep . || true)
  echo "$inc" | grep -qxF -f "$R/messages-files" || echo "$t"; done)
say "proof: $tu translation units x 3 builds preprocessed; $(cut -d' ' -f1 "$R/pp-differ" | sort -u | wc -l) differ, all through messages.txt's files${unexp:+ BUT NOT: $unexp}"
[ -z "$unexp" ] || exit 1
if command -v riscv64-unknown-elf-gcc > /dev/null; then (cd "$W" && bash fw/build.sh --check | tail -1 | tee -a "$R/summary")
else say "proof: no riscv64-unknown-elf-gcc: the firmware sources' images not rebuilt"; fi
# the pipeline: its pinned files untouched and its own check passing (a JDK is needed: apt install openjdk-21-jdk-headless)
command -v javac > /dev/null || { echo "strip.sh: no JDK, so pipeline/check.sh can't run (apt install openjdk-21-jdk-headless)"; exit 1; }
(cd "$W" && bash pipeline/check.sh > "$R/pipeline-check.log" 2>&1) && say "proof: pipeline/check.sh: $(tail -1 "$R/pipeline-check.log")" ||
  { tail -5 "$R/pipeline-check.log"; say "proof: pipeline/check.sh FAILED"; exit 1; }
bad=0; for f in $(grep '\.sh$' "$R/changed" 2>/dev/null || true); do bash -n "$f" || { echo "bash -n: $f"; bad=1; }; done
[ $bad = 0 ] && say "proof: every changed shell script parses (bash -n)"

# ---- the tags left
grep -vE '^\s*(#|$)' "$F/keep.txt" > "$R/keep" || true
while IFS=$'\t' read -r lang f; do [ -f "$f" ] && "$CS" list "$lang" "$f" | sed "s|^|$f:|"; done < "$R/files" |
  grep -E '(^|[^A-Za-z0-9_-])(WORKER[0-9]+|W([1-9]|[12][0-9]|30)(-[0-9]+)?)\b' | grep -vF -f "$R/keep" > "$R/left-comments" || true
while IFS=$'\t' read -r lang f; do [ -f "$f" ] && "$CS" code "$lang" "$f" | grep -nE '(^|[^A-Za-z0-9_-])(WORKER[0-9]+|W([1-9]|[12][0-9]|30)(-[0-9]+)?)\b' | sed "s|^|$f:|"; done < "$R/files" |
  grep -vF -f "$R/keep" > "$R/left-code" || true
say "left: $(wc -l < "$R/left-comments") comment lines and $(wc -l < "$R/left-code") code lines still name a worker ($R/left-*)"

(cd "$W" && bash tools/doccheck.sh > "$R/doccheck.txt") || true
say "doccheck after the moves: $(tail -1 "$R/doccheck.txt")"

if [ $MC = 1 ]; then
  (cd "$W" && make check > "$R/make-check.log" 2>&1) && say "make check: passed" || { say "make check: FAILED ($R/make-check.log)"; exit 1; }
  (cd "$W" && ./run_tests.sh quick --check > "$R/run_tests.log" 2>&1) && say "run_tests.sh quick --check: $(tail -1 "$R/run_tests.log")" || { say "run_tests.sh quick --check: FAILED"; exit 1; }
fi
say "strip.sh: done in $W (report: $R)"
