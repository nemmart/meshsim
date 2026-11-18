#!/bin/bash
# joined_teeth.sh: planted bugs at the joints of v1.37's memory side (v1.37 stage 1, the INTEGRATOR; WORKER9's J1 to J7
# and the stage's own), each of which the tests must catch. Not in make check (a rebuild per bug). The tree is copied
# into a scratch directory, each bug planted there by an exact replacement (a string that must occur once), the tests
# rebuilt (clang release) and run: test_updater_ctl (the joined bench among it), test_memory_system and test_master.
# A bug is caught when a test fails or dies; the table names the tests that failed. The firmware's bug rebuilds
# master.elf with the pinned toolchain (riscv-none-elf-gcc on PATH; else it is skipped and said so).
#
# Usage: tests/joined_teeth.sh [FILTER] (from anywhere): only the bugs whose name contains FILTER; exit 1 if a bug goes
# uncaught or plants nothing. JOBS sets make's -j (2); TEETH_DIR the scratch directory (default: a new temporary one).
set -u
SIM=$(cd "$(dirname "$0")/.." && pwd)
FILTER=${1:-}
JOBS=${JOBS:-2}
if [ -n "${TEETH_DIR:-}" ]; then W=$TEETH_DIR; rm -rf "$W"; mkdir -p "$W"; else W=$(mktemp -d); trap 'rm -rf "$W"' EXIT; fi
(cd "$SIM" && tar cf - --exclude=./build --exclude=./results .) | (cd "$W" && tar xf -)
B=build/clang-release
TESTS="test_updater_ctl test_memory_system test_master"
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

CS=src/units/cache_system.h
# name | file | old | new   (fields separated by a line of three tildes)
BUGS=$(cat <<'EOF'
J1 the Updater on the wrong client port (3)
~~~
src/units/cache_system.h
~~~
        unsigned client = axi::UPDATER;            // planted bugs move it (tests/joined_teeth.sh)
~~~
        unsigned client = axi::BINDER;
~~~~~~
J2 the Updater on the wrong look-up port (3)
~~~
src/units/cache_system.h
~~~
        unsigned port = lookup::UPDATER;
~~~
        unsigned port = lookup::BINDER;
~~~~~~
J3 an update sent before its page's write is answered
~~~
src/units/updater_ctl.h
~~~
if (q.wr_active || q.aw_valid || q.w_valid || q.w_pending) { d.stat[S_STALL_MEMORY]++; break; }
~~~
if (q.aw_valid || q.w_valid || q.w_pending) { d.stat[S_STALL_MEMORY]++; break; }
~~~~~~
J4 the cache answering from a line an update has since changed (LPDDR written, its own line not)
~~~
src/units/cache_ctl.cpp
~~~
            data_.write(t, DataMemory::row(set_, way_), wline_, 1u << ((i & 31) >> 2));
~~~
            (void)i;
~~~~~~
J5 the arbiter granting a second write piece while one is in turn
~~~
src/units/axi_arbiter.cpp
~~~
    if (!aw_busy && !w_cur_) {
~~~
    if (!aw_busy) {
~~~~~~
J7 the joint a cycle out: a client's outputs of t kept as its outputs of t + 1
~~~
src/units/cache_system.h
~~~
            edge(c);
            edged_ = c;
            clear_posted();
~~~
            edge(c);
            edged_ = c;
~~~~~~
J8 the memory system's error not on its ERRORS
~~~
src/units/memory_system.h
~~~
        if (sys->ctl.out().error) errors_ |= E_LPDDR_ERROR;
~~~
        (void)E_LPDDR_ERROR;
~~~~~~
J9 the reset not reaching the cache
~~~
src/units/memory_system.h
~~~
        if (sys) sys->reset();
~~~
        if (false) sys->reset();
~~~~~~
J10 the RW engine on the arbiter's client 3
~~~
src/units/cache_system.h
~~~
        unsigned client = axi::RW_ENGINE;
~~~
        unsigned client = axi::BINDER;
~~~~~~
J11 the Updater's bad root left as lpddr_error (17.10.1: UB)
~~~
src/units/updater_ctl.h
~~~
                    FATAL("the Updater: undefined behavior at %lld: a run's root that is no page of LPDDR, with bit 31 set, or RUN_CONFIG.root 3 "
~~~
                    { stop(ERR_LPDDR); break; } if (false) FATAL("the Updater: undefined behavior at %lld: a run's root that is no page of LPDDR, with bit 31 set, or RUN_CONFIG.root 3 "
~~~~~~
J12 the firmware's free_page_credit at EVENTS [3] (the units' at [2])
~~~
fw/include/host.h
~~~
#define UPD_EVENT_FREE_PAGE_CREDIT  (1 << 2)
~~~
#define UPD_EVENT_FREE_PAGE_CREDIT  (1 << 3)
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
