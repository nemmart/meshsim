#!/bin/bash
# chain_proc_teeth.sh: planted bugs in the chain of processes (v1.37 stage 6, the INTEGRATOR), each of which
# tests/chain_run.sh must catch on the fixture kin-raw (the stand-ins, the chain of two twice, the chain of one, the
# reply that never comes). Stage 4's bugs in the link and in Ingress/Egress (tests/chain_teeth.sh's, there caught with
# no chips), now across processes; and the driver's and the wire's own: the replies sorted by hops wrongly, a reply
# that never comes not counted in cycles, a cycle without its exchange. Not in make check (a rebuild per bug).
# v1.37_wip/ and ../updater_wip/{link,wire} are copied into a scratch directory with their builds, each bug planted
# there by sed and the programs rebuilt.
#   tests/chain_proc_teeth.sh [FILTER]    (FILTER: an extended regular expression on the bugs' names)
# Exit 1 if a bug goes uncaught or its sed plants nothing. JOBS sets make's -j (2).
set -u
SIM=$(cd "$(dirname "$0")/.." && pwd)
FILTER=${1:-}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cp -a "$SIM" "$T/v1.37_wip"; mkdir -p "$T/updater_wip"; cp -a "$SIM/../updater_wip/link" "$SIM/../updater_wip/wire" "$T/updater_wip/"
W=$T/v1.37_wip
L=../updater_wip/link
H=src/units/ingress_egress.h
(cd "$W" && JOBS=${JOBS:-2} bash tests/chain_run.sh kin-raw > "$T/pristine.log" 2>&1) || { cat "$T/pristine.log"; echo "teeth: the pristine chain fails"; exit 2; }
echo "pristine: all pass"
BUGS=(
  "link: hops not counted along the chain~$L/node.cpp~s/HL_PLANT(p, HOPS_NOT_COUNTED)/true/g"
  "link: a forwarded link packet's header not sealed again~$L/link_end.cpp~s/HL_PLANT(p, NOT_SEALED_AGAIN)/true/"
  "link: an ASIC's own link packet before a forwarded one~$L/node.cpp~s/HL_PLANT(p, OWN_BEFORE_FORWARDED)/true/"
  "link: a stop bit ignored~$L/link_end.cpp~s/HL_PLANT(p, STOP_IGNORED)/true/"
  "link: forwarded link packets out of order (the newest first)~$L/node.cpp~s/const Entry\& e = ring\[head\];/const Entry\& e = ring[(head + count - 1) % ring.size()];/"
  "chip: the pin chain_next ignored (no link down)~$H~s/node = std::make_unique<::hostlink::Node>(chain_next, z);/node = std::make_unique<::hostlink::Node>(false, z);/"
  "chip: clear_error() called by the chip~$H~s/^            link_cycles++;/            link_cycles++; node->clear_error();/"
  "driver: the replies sorted by hops the wrong way round~tests/driver_main.cpp~s/while (end.take_from(h, word, \&last)) {/while (end.take_from(chips - 1 - h, word, \&last)) {/"
  "driver: a reply that never comes not counted in cycles~tests/driver_main.cpp~s/for (; complete < chips \&\& t < limit; t++) {/for (; complete < chips; t++) {/"
  "wire: a chip cycle without its exchange~tests/wire_ends.h~s/        WireEnds\& w = \*static_cast<WireEnds\*>(self);/        WireEnds\& w = *static_cast<WireEnds*>(self); if (w.cycles == 60000) { w.cycles++; return; }/"
)
bad=0
printf '%-72s %s\n' "bug" "caught by"
for entry in "${BUGS[@]}"; do
  IFS='~' read -r name file expr <<< "$entry"
  [ -n "$FILTER" ] && ! [[ "$name" =~ $FILTER ]] && continue
  f="$W/$file"
  cp -p "$f" "$T/orig"
  sed -i "$expr" "$f"
  if cmp -s "$f" "$T/orig"; then printf '%-72s %s\n' "$name" "NOT PLANTED (the sed matched nothing)"; bad=1; continue; fi
  (cd "$W" && JOBS=${JOBS:-2} timeout 1800 bash tests/chain_run.sh kin-raw > "$T/run.log" 2>&1)
  rc=$?
  if [ $rc = 0 ]; then printf '%-72s %s\n' "$name" "UNCAUGHT"; bad=1
  elif grep -q "didn't build" "$T/run.log"; then printf '%-72s %s\n' "$name" "the build"
  else printf '%-72s %s\n' "$name" "$(grep -E '^FAIL' "$T/run.log" | head -2 | cut -c1-60 | tr '\n' ';')"; fi
  cp -p "$T/orig" "$f"; touch "$f"
done
[ $bad = 0 ] || { echo "teeth: FAILED"; exit 1; }
echo "teeth: every bug caught"
