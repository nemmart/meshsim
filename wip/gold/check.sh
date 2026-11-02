#!/bin/sh
# Stage 2's acceptance: writes the segment file for both workloads at 2^29 and both hard-cut tests, and checks each
# byte for byte against WORKER16's segment_writer output (jolt-asic-planning worker16-w3 at ca30b5e).
#   ./check.sh <jolt-asic-planning tracer dir> <dir with the reference .seg files> [out dir]
# The reference files are evm-zone-29.seg, cyclone-29.seg, evm-zone-23-hardcuts.seg, cyclone-22-hardcuts.seg as
# segment_writer writes them (WORKER18/NOTES.md, "Stage 2"); the hard-cut lists are meshsim's WORKER16/data/w3/*.cuts.
set -e
cd "$(dirname "$0")"
T=$1; R=$2; O=${3:-/tmp/gold-check}
CUTS=../../WORKER16/data/w3
[ -d build ] || ./build.sh
mkdir -p "$O"
E=evm-zone-4e3451c1d8c5
fail=0
run() { java -Xmx2g -cp build gold.Main segments "$@" || fail=1; }
run $T/$E $T/$E/inputs/trace-23-812ded164e49 $O/evm-zone-23-hardcuts.seg --cuts-file=$CUTS/evm-zone-23-hardcuts.cuts --compare=$R/evm-zone-23-hardcuts.seg
run $T/cyclone $T/cyclone/input/22/input-default $O/cyclone-22-hardcuts.seg --cuts-file=$CUTS/cyclone-22-hardcuts.cuts --compare=$R/cyclone-22-hardcuts.seg
run $T/$E $T/$E/inputs/trace-29-4599adc50712 $O/evm-zone-29.seg --compare=$R/evm-zone-29.seg
run $T/cyclone $T/cyclone/input/29/input-15961266 $O/cyclone-29.seg --compare=$R/cyclone-29.seg
[ $fail = 0 ] && echo "all four segment files identical" || { echo "SEGMENT FILES DIFFER"; exit 1; }
