#!/bin/bash
# check.sh RUN: a build in OUT (default ~/meshsim-inputs) against manifest/RUN.sha256: every listed file, then every
# window file of the delivery, the selected windows (2^29) and the v1.26.1 conversion against their own manifests.
# Prints PASS or FAIL; exit 0 only on PASS.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); . "$HERE/pins.sh"
RUN=$1; run_info "$RUN" || exit 2
OUT=${OUT:-$HOME/meshsim-inputs}; F=$HERE/manifest/$RUN.sha256
[ -f "$F" ] || { echo "no $F (record.sh writes it)"; exit 2; }
cd "$OUT" || exit 2
bad=0
n=$(grep -vc '^#' "$F")
if grep -v '^#' "$F" | sha256sum -c --quiet --strict -; then echo "$RUN: $n files equal manifest/$RUN.sha256"; else echo "$RUN: files differ from manifest/$RUN.sha256"; bad=1; fi
for R in w9-deliveries v1261 $( [ -n "$SEL" ] && echo w9-deliveries-selected ); do
  python3 "$HERE/lib/inputs.py" verify-delivery "$R" "$RUN" | sed "s#^#$R: #" || bad=1
done
[ $bad = 0 ] && echo "$RUN: PASS" || { echo "$RUN: FAIL"; exit 1; }
