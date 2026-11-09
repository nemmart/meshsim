# regen-body.sh (WORKER13): sim/tests/golden-m3/regen.sh on the runner, from the kit (not timing; ~8 min).
sec "inputs"
OKIN=1; for P in evm-zone-29 cyclone-29; do checkin $P || OKIN=0; done
[ $OKIN = 1 ] || verdict inputs FAIL
if [ $OKIN = 1 ]; then
  sec "regen.sh"
  bash $W/kit/golden-m3/regen.sh "$KR" "$KC" "$KT" "$DATA" "$W/out" && verdict regen PASS || verdict regen FAIL
fi
