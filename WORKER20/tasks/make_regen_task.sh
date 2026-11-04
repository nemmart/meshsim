#!/usr/bin/env bash
# make_regen_task.sh NNN PROGRAM PROG_DIR INPUT_DIR SELECTED_WINDOWS V125_MANIFEST_SHA TRACER_TGZ > NNN-w20-regen-PROGRAM.sh
# WORKER20: a Quest task that regenerates one 2^29 replay delivery with WORKER9's delivery_writer into persistent data.
set -eu
N=$1; P=$2; PD=$3; IN=$4; SEL=$5; OLD=$6; TGZ=$7
HERE=$(cd "$(dirname "$0")" && pwd)
NAME=$N-w20-regen-$P; SHA=$(sha256sum "$TGZ" | cut -d' ' -f1)
cat <<HDR
#!/bin/bash
# Task $NAME -- meshsim WORKER20: regenerate the 2^29 replay delivery of $P after the runner's reboot wiped /tmp
# (WORKER9's tasks 079/080, same writer and inputs), into PERSISTENT data (\$DATA below; Quest PIPELINE.md).
# The embedded tarball (SHA-256 checked) is jolt-asic-planning tracer-standalone-main at 8d546e3 (its tracer/src and
# tracer/delivery equal 5ba7f0c's, which is 079/080's 121c271 plus their imm_signed patch), with only this program's
# input kept, the universal replayer tables (meshsim wip/tables/replayer) and a libelf fallback (Ubuntu 24.04 debs).
# Writes: \$DATA/w9-deliveries/{$P/, $P.manifest.json, manifest.json, $P.COMPLETE}
#         \$DATA/w9-deliveries-selected/{$P/, $P.manifest.json, manifest.json}, \$DATA/w9-selected-windows.tgz
# Reboot-safe: everything is written to a staging directory and moved into place only when complete; a re-run starts
# from scratch, and one that finds $P.COMPLETE matching its manifest skips the full delivery. Expected ~20 min.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD
RES=\$REPO/results/$NAME; mkdir -p "\$RES"
HDR
cat "$HERE/data_dir.inc"
cat <<BODY
echo "persistent data: \$DATA" | tee "\$RES/data_path.txt"
echo "unit: \$(systemctl show quest-runner -p ProtectSystem -p ProtectHome -p ReadWritePaths 2>/dev/null | tr '\n' ' ')" | tee -a "\$RES/data_path.txt"
findmnt -T "\$DATA" 2>/dev/null | tail -1 | tee -a "\$RES/data_path.txt"
AV=\$(df -BG --output=avail "\$DATA" | tail -1 | tr -dc 0-9); echo "free at \$DATA: \${AV} GB"
P=$P; PD=$PD; IN=$IN; SEL=$SEL; OLD=$OLD
D=\$DATA/w9-deliveries; S=\$DATA/w9-deliveries-selected; mkdir -p "\$D" "\$S"
skip=0
if [ -f "\$D/\$P.COMPLETE" ] && [ -f "\$D/\$P.manifest.json" ] && [ "\$(cat "\$D/\$P.COMPLETE")" = "\$(sha256sum "\$D/\$P.manifest.json" | cut -d' ' -f1)" ]; then
  echo "\$D/\$P is complete (\$(cat "\$D/\$P.COMPLETE")): the full delivery is not regenerated"; skip=1
else
  [ "\$AV" -ge 60 ] || { echo "need about 47 GB plus margin at \$DATA; only \${AV} GB free"; exit 4; }
fi
WANT=$SHA
W=/tmp/w20-$N; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__TGZ_BEGIN__\$/,/^__TGZ_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/w20-tracer.tgz" || exit 2
GOT=\$(sha256sum "\$W/w20-tracer.tgz" | cut -d' ' -f1); echo "tarball sha256 \$GOT"
[ "\$GOT" = "\$WANT" ] || { echo "tarball sha256 mismatch (want \$WANT)"; exit 2; }
tar -xzf "\$W/w20-tracer.tgz" -C "\$W" || exit 2
cd "\$W/w9/tracer" || exit 2
echo "start \$(date -u) on \$(hostname), \$(nproc) threads"
EXTRA=(); INC=""
if [ ! -e /usr/include/libelf.h ] && [ ! -e /usr/include/libelf/libelf.h ]; then
  echo "no system libelf.h: the tarball's fallback"
  EXTRA=(-DCMAKE_PREFIX_PATH="\$W/w9/libelf/usr" -DCMAKE_LIBRARY_PATH="\$W/w9/libelf/usr/lib/x86_64-linux-gnu")
  export LD_LIBRARY_PATH="\$W/w9/libelf/usr/lib/x86_64-linux-gnu:\${LD_LIBRARY_PATH:-}"
  INC="-I\$W/w9/libelf/usr/include -L\$W/w9/libelf/usr/lib/x86_64-linux-gnu"
fi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "\${EXTRA[@]}" > "\$RES/configure.log" 2>&1 || { tail -30 "\$RES/configure.log"; rm -rf "\$W"; exit 3; }
cmake --build build -j8 --target tracer > "\$RES/build.log" 2>&1 || { tail -30 "\$RES/build.log"; rm -rf "\$W"; exit 3; }
sed -i "s#-lelf#\$INC -lelf#" delivery/build.sh
bash delivery/build.sh "\$PWD" "\$PWD/build" "\$W/dw" >> "\$RES/build.log" 2>&1 || { tail -30 "\$RES/build.log"; rm -rf "\$W"; exit 3; }
TAB="\$W/w9/tables"
merge() {  # a replay-delivery root: manifest.json listing every <name>.manifest.json's program (as 079/080)
  python3 - "\$1" <<'PY'
import json, glob, os, sys
root = sys.argv[1]; progs = []
for f in sorted(glob.glob(os.path.join(root, '*.manifest.json'))):
    m = json.load(open(f)); progs += m['programs']; base = m
base['programs'] = progs
json.dump(base, open(os.path.join(root, 'manifest.json.tmp'), 'w'), indent=1)
os.replace(os.path.join(root, 'manifest.json.tmp'), os.path.join(root, 'manifest.json'))
print('root manifest:', [p['name'] for p in progs])
PY
}
# write into \$1/.staging-\$P, check, then move into \$1 (a crash leaves only a staging directory, removed next time)
write() {  # write ROOT WINDOWS LOG
  local R=\$1 ST=\$1/.staging-\$P; rm -rf "\$ST"; mkdir -p "\$ST"
  local T0=\$(date +%s)
  "\$W/dw" "\$PD" "\$IN" "\$TAB" "\$ST" "\$P" --windows=\$2 --keep=all > "\$RES/\$3" 2>&1; local rc=\$?
  echo "\$3: delivery_writer exit \$rc after \$(( \$(date +%s) - T0 )) s"; grep -v INFO "\$RES/\$3" | tail -3
  [ \$rc = 0 ] && [ -f "\$ST/\$P.manifest.json" ] || { echo "\$3: the writer failed"; return 1; }
  python3 -c "import json,sys;W=json.load(open('\$ST/\$P.manifest.json'))['programs'][0]['window_entries'];n=sum(w['replay']=='ok' for w in W);print(len(W),'windows,',n,'replayed ok');sys.exit(0 if n==len(W) else 1)" | tee -a "\$RES/summary.txt" || return 1
  cp "\$TAB"/*.bin "\$ST/\$P/"
  rm -f "\$R/\$P.COMPLETE"; rm -rf "\$R/\$P" "\$R/\$P.manifest.json"
  mv "\$ST/\$P" "\$R/\$P" && mv "\$ST/\$P.manifest.json" "\$R/\$P.manifest.json" && rmdir "\$ST" || return 1
  merge "\$R"
}
RC=0
if [ \$skip = 0 ]; then
  write "\$D" all delivery.log || RC=5
  [ \$RC = 0 ] && sha256sum "\$D/\$P.manifest.json" | cut -d' ' -f1 > "\$D/\$P.COMPLETE"
fi
write "\$S" "\$SEL" selected.log || RC=6
# the selected windows, packed as 079/080 did (top directory w9-deliveries-selected), atomically
if [ \$RC = 0 ]; then
  tar -czf "\$DATA/w9-selected-windows.tgz.tmp" -C "\$DATA" --exclude '.staging-*' w9-deliveries-selected && mv "\$DATA/w9-selected-windows.tgz.tmp" "\$DATA/w9-selected-windows.tgz" || RC=7
fi
du -sb "\$D/\$P" 2>/dev/null | tee "\$RES/size.txt"; du -sh "\$D/\$P" 2>/dev/null | tee -a "\$RES/size.txt"
(cd "\$DATA" && sha256sum w9-deliveries/\$P.manifest.json w9-deliveries/manifest.json w9-deliveries-selected/\$P.manifest.json w9-selected-windows.tgz 2>/dev/null) | tee "\$RES/manifest.sha256"
NEW=\$(sha256sum "\$D/\$P.manifest.json" 2>/dev/null | cut -d' ' -f1)
if [ "\$NEW" = "\$OLD" ]; then echo "\$P.manifest.json equals WORKER9's ($OLD): the delivery is reproduced exactly" | tee -a "\$RES/summary.txt"
else echo "\$P.manifest.json \$NEW differs from WORKER9's \$OLD" | tee -a "\$RES/summary.txt"; fi
df -h "\$DATA" | tail -1
cd "\$REPO"; rm -rf "\$W"
echo "end \$(date -u), exit \$RC"
exit \$RC
__TGZ_BEGIN__
BODY
base64 -w 76 "$TGZ"
echo "__TGZ_END__"
