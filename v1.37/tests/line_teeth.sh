#!/bin/bash
# line_teeth.sh: planted bugs in v1.37's line between the special tiles and their DMAs on south (16.1 to 16.4; v1.37
# stage 2, the INTEGRATOR), each of which the tests must catch. Not in make check (a rebuild per bug). As joined_teeth.sh:
# the tree copied into a scratch directory, each bug planted by an exact replacement, the tests rebuilt (clang release)
# and run: test_special_router (the line's own tests), test_memory (the memory run, whose answers ride the line),
# test_master (the guests: the master's loads into the tracer tile ride the line) and test_segment (v1.36's segments).
#
# Usage: tests/line_teeth.sh [FILTER] (from anywhere): only the bugs whose name contains FILTER; exit 1 if a bug goes
# uncaught or plants nothing. JOBS sets make's -j (2); TEETH_DIR the scratch directory (default: a new temporary one).
set -u
SIM=$(cd "$(dirname "$0")/.." && pwd)
FILTER=${1:-}
JOBS=${JOBS:-2}
if [ -n "${TEETH_DIR:-}" ]; then W=$TEETH_DIR; rm -rf "$W"; mkdir -p "$W"; else W=$(mktemp -d); trap 'rm -rf "$W"' EXIT; fi
(cd "$SIM" && tar cf - --exclude=./build --exclude=./results .) | (cd "$W" && tar xf -)
B=build/clang-release
TESTS="test_special_router test_memory test_master test_segment"
build() { (cd "$W" && make -s CC_NAME=clang MODE=release -j"$JOBS" $(for t in $TESTS; do echo $B/$t; done) > "$W/build.log" 2>&1); }
run() {   # prints the failing tests, "-" if none
  local f="" rc t
  for t in $TESTS; do
    (cd "$W" && timeout 1200 ./$B/$t > "$W/$t.out" 2>&1) 2> /dev/null
    rc=$?
    local g
    g="$(grep -oE '^FAIL +[a-z0-9_]+' "$W/$t.out" | awk '{print $2}' | tr '\n' ' ')"
    [ $rc -ge 124 ] && g="$g$t(died:$rc) "
    [ $rc != 0 ] && [ -z "$g" ] && g="$t(exit:$rc) "
    f="$f$g"
  done
  echo "${f:- -}"
}
# plant FILE OLD NEW: replace every occurrence of OLD in the scratch copy of FILE; status 1 if it is not there
plant() {
  python3 -I - "$W/$1" "$2" "$3" <<'EOF'
import sys
p, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(p).read()
if s.count(old) < 1: sys.exit(1)
open(p, "w").write(s.replace(old, new))
EOF
}
fw() {   # rebuild master.elf in the scratch tree
  (cd "$W" && PARTS=cc bash fw/build.sh > "$W/fw.log" 2>&1)
}
build || { tail -30 "$W/build.log"; echo "teeth: the pristine tree didn't build"; exit 2; }
r=$(run)
[ "$r" = " -" ] || [ "$r" = "-" ] || { echo "teeth: the pristine tree fails: $r"; exit 2; }
echo "pristine: all pass"

# name | file | old | new   (fields separated by a line of three tildes)
BUGS=$(cat <<'EOF'
L1 the ring's rule reversed: a packet for a special tile above the tile's id sent west
~~~
src/units/router.h
~~~
    unsigned line_output(unsigned dst) const { return dst > me ? unsigned(TO_RIGHT) : unsigned(TO_LEFT); }
~~~
    unsigned line_output(unsigned dst) const { return dst > me ? unsigned(TO_LEFT) : unsigned(TO_RIGHT); }
~~~~~~
L2 a packet arriving on the line sent back the way it came
~~~
src/units/router.h
~~~
        route_mask(c, p == FROM_LEFT ? M_RIGHT : M_LEFT, p, d, a, cw, taken);
~~~
        route_mask(c, p == FROM_LEFT ? M_LEFT : M_RIGHT, p, d, a, cw, taken);
~~~~~~
L3 a packet for another special tile, from local, by the attach port and not the line
~~~
src/units/router.h
~~~
        else if (lw::controller(dst)) {
            const unsigned o = line_output(dst);
~~~
        else if (false) {
            const unsigned o = line_output(dst);
~~~~~~
L4 an attach port's arrival for another special tile put on the line (a compute tile's packet taking the line)
~~~
src/units/router.h
~~~
        else if (lw::dst(a) == me) route_mask(c, M_EJ, FROM_CNC, d, a, cw, taken);
    }
    if (c.ready(inj)) {                           // from local: for the tile; another special tile by the line; else attach
~~~
        else if (lw::dst(a) == me) route_mask(c, M_EJ, FROM_CNC, d, a, cw, taken);
        else if (lw::controller(lw::dst(a))) route_mask(c, line_output(lw::dst(a)) == TO_RIGHT ? M_RIGHT : M_LEFT, FROM_CNC, d, a, cw, taken);
    }
    if (c.ready(inj)) {                           // from local: for the tile; another special tile by the line; else attach
~~~~~~
L5 the slot's DMA judging its room as the west input's (v1.36's)
~~~
src/units/periph_slot.h
~~~
    unsigned dma_input = 3;
~~~
    unsigned dma_input = 0;
~~~~~~
L6 the line's west output's queues in the wrong order (the DMA before the line)
~~~
src/units/router.h
~~~
        {LOOPBACK, FROM_LEFT, FROM_DOWN}, {LOOPBACK, FROM_RIGHT, FROM_DOWN}, {}, {},
~~~
        {LOOPBACK, FROM_LEFT, FROM_DOWN}, {LOOPBACK, FROM_DOWN, FROM_RIGHT}, {}, {},
~~~~~~
L7 a DMA's packet for another special tile by the attach port
~~~
src/units/router.h
~~~
        const unsigned o = dst == me ? unsigned(EJECT) : line && lw::controller(dst) ? line_output(dst) : unsigned(TO_CNC);
~~~
        const unsigned o = dst == me ? unsigned(EJECT) : unsigned(TO_CNC);
EOF
)
uncaught=0
printf "%-100s %s\n" "bug" "caught by"
python3 -I -c '
import sys
for b in sys.stdin.read().split("\n~~~~~~\n"):
    f = b.split("\n~~~\n")
    print("\x1f".join(x.strip("\n") for x in f).replace("\n", "\x1e"))
' <<< "$BUGS" > "$W/bugs.txt"
while IFS=$'\x1f' read -r name file old new; do
  old=${old//$'\x1e'/$'\n'}; new=${new//$'\x1e'/$'\n'}
  [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
  if ! plant "$file" "$old" "$new"; then printf "%-100s %s\n" "$name" "NOT PLANTED (the text is not there once)"; uncaught=1; continue; fi
  if [ "${file#fw/}" != "$file" ]; then
    if ! command -v riscv-none-elf-gcc > /dev/null; then printf "%-100s %s\n" "$name" "SKIPPED (no riscv-none-elf-gcc)"; cp "$SIM/$file" "$W/$file"; continue; fi
    fw || { printf "%-100s %s\n" "$name" "(the firmware does not build)"; uncaught=1; cp "$SIM/$file" "$W/$file"; continue; }
  fi
  if ! build; then printf "%-100s %s\n" "$name" "(does not build)"; uncaught=1; cp "$SIM/$file" "$W/$file"; continue; fi
  r=$(run)
  if [ "$r" = " -" ] || [ "$r" = "-" ]; then printf "%-100s %s\n" "$name" "UNCAUGHT"; uncaught=1; else printf "%-100s %s\n" "$name" "$r"; fi
  cp "$SIM/$file" "$W/$file"
  [ "${file#fw/}" != "$file" ] && cp -r "$SIM/fw/images/." "$W/fw/images/"
done < "$W/bugs.txt"
exit $uncaught
