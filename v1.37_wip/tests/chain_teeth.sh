#!/bin/bash
# chain_teeth.sh: the milestone's planted bugs in the chain (v1.37 stage 4, the INTEGRATOR), each of which the tests
# must catch. Not in make check (a rebuild per bug). The simulator and the link's sources (v1.37_wip/ and
# ../updater_wip/link/, which the simulator builds in place) are copied into a scratch directory side by side, each bug
# planted there by sed, the tests rebuilt (clang release) and run: test_ingress_chain (the chain with no chips),
# test_ingress_link (one chip's link) and test_ingress_egress. A bug is caught when a test fails; the table names the
# tests that failed. The link's own plants are WORKER8's (HL_PLANT, link_params.h), switched on by turning the test
# into `true`.
#
# Usage: tests/chain_teeth.sh [FILTER] (from anywhere): only the bugs whose name contains FILTER; exit 1 if a bug goes
# uncaught or its sed plants nothing. JOBS sets make's -j (2); TEETH_DIR the scratch directory (default: a new one).
set -u
SIM=$(cd "$(dirname "$0")/.." && pwd)
LINK=$(cd "$SIM/../updater_wip/link" && pwd)
FILTER=${1:-}
JOBS=${JOBS:-2}
if [ -n "${TEETH_DIR:-}" ]; then T=$TEETH_DIR; rm -rf "$T"; mkdir -p "$T"; else T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; fi
mkdir -p "$T/v1.37_wip" "$T/updater_wip/link"
(cd "$SIM" && tar cf - --exclude=./build --exclude=./results .) | (cd "$T/v1.37_wip" && tar xf -)
(cd "$LINK" && tar cf - --exclude=./build .) | (cd "$T/updater_wip/link" && tar xf -)
W=$T/v1.37_wip
B=build/clang-release
TESTS="test_ingress_chain test_ingress_link test_ingress_egress"
build() { (cd "$W" && make -s CC_NAME=clang MODE=release -j"$JOBS" $(for t in $TESTS; do echo $B/$t; done) > "$W/build.log" 2>&1); }
run() {   # prints the failing tests, "-" if none
  local f="" t rc
  for t in $TESTS; do
    (cd "$W" && timeout 1800 ./$B/$t > "$W/$t.out" 2>&1) 2> /dev/null
    rc=$?
    [ $rc = 0 ] && continue
    f="$f $(grep -oE '^FAIL +[a-z0-9_]+' "$W/$t.out" | awk '{print $2}' | tr '\n' ' ')"
    [ $rc -ge 124 ] && f="$f$t(died:$rc) "
  done
  echo "${f:- -}"
}
build || { tail -30 "$W/build.log"; echo "teeth: the pristine tree didn't build"; exit 2; }
r=$(run)
[ "$r" = " -" ] || { echo "teeth: the pristine tree fails: $r"; exit 2; }
echo "pristine: all pass"
L=../updater_wip/link
H=src/units/ingress_egress.h
# name ~ file ~ sed expression [~ file ~ sed expression ...]   (files relative to v1.37_wip/)
BUGS=(
  "hops not counted along the chain~$L/node.cpp~s/HL_PLANT(p, HOPS_NOT_COUNTED)/true/g"
  "a forwarded link packet's header not sealed again~$L/link_end.cpp~s/HL_PLANT(p, NOT_SEALED_AGAIN)/true/"
  "an ASIC's own link packet before a forwarded one~$L/node.cpp~s/HL_PLANT(p, OWN_BEFORE_FORWARDED)/true/"
  "a stop bit ignored~$L/link_end.cpp~s/HL_PLANT(p, STOP_IGNORED)/true/"
  "forwarded link packets out of order (the newest first)~$L/node.cpp~s/const Entry\& e = ring\[head\];/const Entry\& e = ring[(head + count - 1) % ring.size()];/"
  "the pin chain_next ignored (no link down)~$H~s/node = std::make_unique<::hostlink::Node>(chain_next, z);/node = std::make_unique<::hostlink::Node>(false, z);/"
  "clear_error() called by the chip~$H~s/^            link_cycles++;/            link_cycles++; node->clear_error();/"
)
miss=0
printf '%-72s %s\n' "planted bug" "caught by"
for b in "${BUGS[@]}"; do
  IFS='~' read -r -a f <<< "$b"
  what=${f[0]}
  case "$what" in *"$FILTER"*) ;; *) continue ;; esac
  planted=1; files=""
  for ((k = 1; k + 1 < ${#f[@]}; k += 2)); do
    file=${f[k]}; expr=${f[k + 1]}
    cp "$W/$file" "$W/$file.orig"; files="$files $file"
    sed -i "$expr" "$W/$file"
    cmp -s "$W/$file" "$W/$file.orig" && planted=0
  done
  if [ $planted = 0 ]; then r="NOT PLANTED (a sed matched nothing)"; miss=1
  elif build; then r=$(run); else r=" (no build: $(grep -m1 -oE 'error: .{0,60}' "$W/build.log"))"; fi
  for file in $files; do mv "$W/$file.orig" "$W/$file"; touch "$W/$file"; done
  printf '%-72s %s\n' "$what" "$r"
  case "$r" in " -"|" (no build"*) miss=1 ;; esac
done
[ $miss = 0 ] && echo "teeth: every planted bug caught" || echo "teeth: a planted bug went uncaught"
exit $miss
