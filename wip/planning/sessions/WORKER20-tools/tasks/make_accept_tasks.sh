#!/usr/bin/env bash
# make_accept_tasks.sh KIT.tgz OUTDIR [EZ_SHA CY_SHA]: WORKER20's tasks 097-101, reading the deliveries from persistent data
# ($DATA/w9-deliveries, written by tasks 103/104) and converting them into scratch. The SHA-256s are the per-program
# manifests' (default: WORKER9's 079/080, which 103/104 reproduce if the writer is deterministic).
set -eu
K=$1; T=$2; EZ=${3:-7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f}; CY=${4:-f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982}
HERE=$(cd "$(dirname "$0")" && pwd); MS=$(cd "$HERE/../../wip/meshsim" && pwd)
setup() {
  echo 'REPO=${RES%/results/*}'
  cat "$HERE/data_dir.inc"
  cat <<EOF
# v1.26 (WORKER20): WORKER9's v1.25 deliveries, regenerated into persistent data by tasks 103/104 (079/080's writer and
# inputs), converted into scratch by ConvertDelivery: the state records rewritten to 68 words (DECISIONS-v1.26 items 1, 2,
# 8), every other file a link (the delivery is only read). Scratch is rm -rf'ed at the start: safe to re-run after a reboot.
SRC=\$DATA/w9-deliveries
echo "deliveries: \$SRC"
for PS in $1; do
  P=\${PS%%:*}; SM=\${PS#*:}
  [ -f "\$SRC/\$P.COMPLETE" ] || { echo "\$SRC/\$P.COMPLETE missing: run tasks 103/104 (the deliveries) first"; rm -rf "\$W"; exit 2; }
  GOT=\$(sha256sum "\$SRC/\$P.manifest.json" 2>/dev/null | cut -d' ' -f1)
  [ "\$GOT" = "\$SM" ] && [ "\$GOT" = "\$(cat "\$SRC/\$P.COMPLETE")" ] || { echo "\$SRC/\$P.manifest.json sha256 '\$GOT', expected \$SM"; rm -rf "\$W"; exit 2; }
  java -Xmx2g -cp meshsim.jar ConvertDelivery "\$SRC" "\$W/v126" "\$P" > "\$RES/convert-\$P.log" 2>&1; CRC=\$?
  cat "\$RES/convert-\$P.log"; [ \$CRC = 0 ] || { echo "conversion of \$P failed"; rm -rf "\$W"; exit 1; }
done
EOF
}
setup "evm-zone-29:$EZ cyclone-29:$CY" > /tmp/setup-both.sh; setup "evm-zone-29:$EZ" > /tmp/setup-ez.sh; setup "cyclone-29:$CY" > /tmp/setup-cy.sh
sh_() { echo "./run_shard.sh -d \"\$W/v126\" -p $1 -M \"\$W/v126/$1.manifest.json\" $2 \"\$RES/$3\""; }
MT=$MS/acceptance/make_task.sh
$MT $K 097-w20-v126-smoke "$(sh_ evm-zone-29 '-f 0 -n 64 -j 16 -x 16' evm-zone-29) && $(sh_ cyclone-29 '-f 0 -n 64 -j 16 -x 16' cyclone-29) && $(sh_ evm-zone-29 '-f 0 -n 64 -j 16 -s B -q 512' evm-zone-29-8k) && $(sh_ cyclone-29 '-f 0 -n 64 -j 16 -s B -q 512' cyclone-29-8k)" /tmp/setup-both.sh > $T/097-w20-v126-smoke.sh
$MT $K 098-w20-v126-accept-evm-zone-29 "$(sh_ evm-zone-29 '-f 0 -n 8192 -j 16 -x 64' evm-zone-29)" /tmp/setup-ez.sh > $T/098-w20-v126-accept-evm-zone-29.sh
$MT $K 099-w20-v126-accept-cyclone-29 "$(sh_ cyclone-29 '-f 0 -n 8192 -j 16 -x 64' cyclone-29)" /tmp/setup-cy.sh > $T/099-w20-v126-accept-cyclone-29.sh
$MT $K 100-w20-v126-8k-evm-zone-29 "$(sh_ evm-zone-29 '-f 0 -n 8192 -j 16 -s B -q 512' evm-zone-29-8k)" /tmp/setup-ez.sh > $T/100-w20-v126-8k-evm-zone-29.sh
$MT $K 101-w20-v126-8k-cyclone-29 "$(sh_ cyclone-29 '-f 0 -n 8192 -j 16 -s B -q 512' cyclone-29-8k)" /tmp/setup-cy.sh > $T/101-w20-v126-8k-cyclone-29.sh
echo "wrote 097-101 into $T"
