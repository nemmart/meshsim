#!/bin/bash
# record.sh RUN [NOTE]: writes manifest/RUN.sha256, the checksums of one run's outputs in OUT (default ~/meshsim-inputs),
# for check.sh. Paths are relative to OUT. The deliveries' window files (and the converted records) are covered by their
# per-program manifests, which hold every file's size and SHA-256 (check.sh verifies each file against them); listed
# here are those manifests, the tables copied beside the windows, gold's files and every jumpmap file but report.json
# (it records run times). w9-selected-windows.tgz and the root manifest.json are left out: both depend on which
# programs share the directory.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd); . "$HERE/pins.sh"
RUN=$1; NOTE=${2:-first build}; run_info "$RUN"
OUT=${OUT:-$HOME/meshsim-inputs}; cd "$OUT"
F=$HERE/manifest/$RUN.sha256
for c in w9-deliveries v1261 gold w26-jumpmap; do [ -f "$c/$RUN.COMPLETE" ] || { echo "record: $OUT/$c/$RUN isn't complete"; exit 2; }; done
{
  echo "# $RUN: the input pipeline's outputs (meshsim wip/inputs), paths relative to OUT. Source of these checksums: $NOTE."
  echo "# jolt-asic-planning $JAP_COMMIT; meshsim ${MESHSIM_COMMIT:-$(git -C "$HERE" rev-parse --short HEAD 2>/dev/null)}; gold $(sed -n 's/.*"gold_commit": "\([^"]*\)".*/\1/p' w26-jumpmap/$RUN/proof.json)$( [ -f gold-v1.3/$RUN.COMPLETE ] && echo ", gold v1.3 ${GOLD_V13_SRC:0:7}"); recorded $(date -u +%F)"
  sha256sum "w9-deliveries/$RUN.manifest.json"
  for t in decode_table rom template_index templates; do sha256sum "w9-deliveries/$RUN/$t.bin"; done   # the tables beside the windows
  [ -z "$SEL" ] || sha256sum "w9-deliveries-selected/$RUN.manifest.json"
  sha256sum "v1261/$RUN.manifest.json"
  sha256sum gold/$RUN/segments.seg gold/$RUN/segments.seg.cuts gold/$RUN/segments.seg.json
  [ ! -f gold-v1.3/$RUN.COMPLETE ] || sha256sum gold-v1.3/$RUN/segments.seg gold-v1.3/$RUN/segments.seg.cuts gold-v1.3/$RUN/segments.seg.json
  (cd w26-jumpmap/$RUN && find . -type f ! -name report.json ! -name manifest.sha256 | sort | xargs sha256sum | sed "s#  \./#  w26-jumpmap/$RUN/#")
} > "$F.tmp"
mv "$F.tmp" "$F"
echo "recorded $(grep -vc '^#' "$F") checksums in $F"
