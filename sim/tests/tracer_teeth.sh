#!/bin/bash
# tracer_teeth.sh: planted bugs, one per area, each of which the Tracer's suites must catch. Not in
# make check (a rebuild per bug). The tree is copied into a scratch directory (ref/ linked, for the tables), each bug
# is planted there by sed, the suites rebuilt (clang release) and test_tracer_suites (the three suites against ref's
# logs) and test_tracer_rig (the rig's calls against ref's rig's; Java's HashMap order) run. A bug is caught when
# either fails; the table says which. Usage: tests/tracer_teeth.sh (from anywhere); exit 1 if a bug goes uncaught.
set -u
SIM=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
(cd "$SIM" && tar cf - --exclude=./build --exclude=./ref .) | (cd "$W" && tar xf -)
ln -s "$SIM/ref" "$W/ref"
B=build/clang-release
TARGETS="$B/test_tracer_suites $B/test_tracer_rig"
build() { (cd "$W" && make -s CC_NAME=clang MODE=release -j"$(nproc)" $TARGETS > "$W/build.log" 2>&1); }
run() {   # prints the failing tests, "-" if none
  local f=""
  (cd "$W" && ./$B/test_tracer_suites > "$W/s.out" 2>&1) || f="$f $(grep -oE '^FAIL +[a-z_]+' "$W/s.out" | awk '{print $2}' | tr '\n' ' ')"
  (cd "$W" && ./$B/test_tracer_rig > "$W/r.out" 2>&1) || f="$f $(grep -oE '^FAIL +[a-z_]+' "$W/r.out" | awk '{print $2}' | tr '\n' ' ')"
  echo "${f:- -}"
}
build || { cat "$W/build.log"; echo "teeth: the pristine tree didn't build"; exit 2; }
[ "$(run)" = " -" ] || [ "$(run)" = "-" ] || { echo "teeth: the pristine tree fails: $(run)"; exit 2; }
echo "pristine: all pass"
# area | file | sed expression
BUGS=(
  "memory system: an LPDDR miss 81 cycles, not 80|src/units/mem_system.h|s/int hit = 6, miss = 80,/int hit = 6, miss = 81,/"
  "FileStore: a path not owned reads as a hole|src/units/file_store.cpp|s/w.status = owned ? ST_OK : ST_NOT_OWNED;/w.status = owned ? ST_OK : ST_HOLE;/"
  "RVC: c_imm6 drops immediate bit 4|src/units/rvc.h|s/((h >> 2) \& 0x1f); }/((h >> 2) \& 0x0f); }/"
  "Tracer rows: 5 cycles a row, not 4|src/units/tracer.h|s/int row_cycles = 4,/int row_cycles = 5,/"
  "a stall: the map probe's compare 3 cycles, not 2|src/units/tracer.cpp|s/t = resp_.ready_at + probe_compare;/t = resp_.ready_at + probe_compare + 1;/"
  "an ERRORS bit: LD aligned on 4 bytes|src/units/tracer.cpp|s/if ((mem_a \& 7) != 0) bad |= 1 << ERR_MISALIGNED;/if ((mem_a \& 3) != 0) bad |= 1 << ERR_MISALIGNED;/"
  "the output path: a transfer one word past its credits|src/units/tracer.cpp|s/} else if (words > oq\[q\].covered()) {/} else if (words > oq[q].covered() + 1) {/"
  "the rig: service always from queue 1 (round-robin lost)|tests/tracer_rig.cpp|s/rr = q % 3 + 1;/rr = 1;/"
  "the rig: computeIfAbsent appends (the page order)|tests/tracer_rig.cpp|s/b.insert(b.begin(), key);/b.push_back(key);/"
)
miss=0
printf '%-58s %s\n' "planted bug" "caught by"
for b in "${BUGS[@]}"; do
  IFS='|' read -r what file expr <<< "$b"
  cp "$W/$file" "$W/$file.orig"
  sed -i "$expr" "$W/$file"
  if cmp -s "$W/$file" "$W/$file.orig"; then printf '%-58s %s\n' "$what" "NOT PLANTED (the sed matched nothing)"; miss=1; continue; fi
  if build; then r=$(run); else r=" (no build)"; fi
  mv "$W/$file.orig" "$W/$file"; touch "$W/$file"
  printf '%-58s %s\n' "$what" "$r"
  case "$r" in " -"|"-") miss=1 ;; esac
done
build
[ $miss = 0 ] && echo "teeth: every planted bug caught" || echo "teeth: a planted bug went uncaught"
exit $miss
