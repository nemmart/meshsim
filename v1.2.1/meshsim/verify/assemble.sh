#!/bin/sh
# verify/assemble.sh OUTDIR M11_TREE M11_KIT_TGZ [REF_RESULTS]: makes M12's verification kit (worker 13, from worker 12's M11 kit), Java only.
#   m11/   the M11 sort kit as shipped (M11_KIT_TGZ, design_v2/sort/kit/mesh-sort-kit.tar.gz), its jar rebuilt with the
#          accounting-only m11-phaselog.patch (DEV 125); the split jar is built from the same patched tree
#   m12/  the M12 sort kit (kit/assemble.sh from this tree; REF_RESULTS = design_v2/sort/results-p4 for its ref-m7/)
#   m11/split/, m12/split/  split.jar (that tree's simulator with SplitRun and Mach; M12's also SplitSets, VerifySummary),
#         hops.txt and sw/build/{loader.bin, split.code.bin, split.data.bin, split.sym} (build both trees' sw/ and split first)
#   HASHES-SPLIT  SHA-256 of both split directories' files and the 25 split sets (made here with SplitSets, then deleted)
#   run.sh, tools/runjob.sh, README.md
set -e
M=$(cd "$(dirname "$0")/.." && pwd); V=${1:?usage: verify/assemble.sh OUTDIR M11_TREE M11_KIT_TGZ [REF_RESULTS]}; M11=$(cd "${2:?}" && pwd); TGZ=${3:?}; REF=$4
mkdir -p "$V/tools"; V=$(cd "$V" && pwd)
cp $M/verify/run.sh $M/verify/README.md "$V/"; cp $M/verify/tools/runjob.sh "$V/tools/"
# the M11 kit, as shipped
rm -rf /tmp/vk9 && mkdir -p /tmp/vk9 && tar -xzf "$TGZ" -C /tmp/vk9 && rm -rf "$V/m11" && mv /tmp/vk9/* "$V/m11"
# M12 (DEV 125, the lead's agreement of 9 October): M11's PhaseLog with the slowest tile's instruction and busy columns,
# an accounting-only patch (m11-phaselog.patch), so that PhaseNeutral can trace each phase's cycles on both versions.
# The M11 tree is copied and patched; m11/meshsim.jar is rebuilt from it and m11/HASHES' jar line regenerated.
# The images, jobs and options are the shipped kit's.
rm -rf /tmp/m11a && cp -r "$M11" /tmp/m11a && (cd /tmp/m11a && patch -p1 -s < $M/verify/m11-phaselog.patch)
M11=/tmp/m11a
rm -rf /tmp/kitcls11 && mkdir -p /tmp/kitcls11
javac --release 17 -d /tmp/kitcls11 $M11/src/*.java $M11/test/T.java $M11/test/SortTest.java $M11/test/SortSummary.java $M11/test/SortCompare.java $M11/test/KeyGen.java $M11/test/Inputs.java
rm -f "$V/m11/meshsim.jar" && (cd /tmp/kitcls11 && jar cf "$V/m11/meshsim.jar" *.class)
(cd "$V/m11" && NEW=$(java -cp meshsim.jar Inputs hash meshsim.jar) && awk -v n="$NEW" '$2 == "meshsim.jar" { print n; next } { print }' HASHES > HASHES.new && mv HASHES.new HASHES)
cp $M/verify/m11-phaselog.patch "$V/"
# the M12 kit
rm -rf "$V/m12" && sh $M/kit/assemble.sh "$V/m12" $REF > /dev/null
# the split directories
for x in m11 m12; do
  T=$M11; [ $x = m12 ] && T=$M
  S="$V/$x/split"; rm -rf "$S" /tmp/vkcls; mkdir -p "$S/sw/build" /tmp/vkcls
  EXTRA=; [ $x = m12 ] && EXTRA="$T/test/SplitSets.java $T/test/VerifySummary.java $T/test/PhaseNeutral.java $T/test/SortCompare.java $T/test/KeyGen.java"
  javac --release 17 -d /tmp/vkcls $T/src/*.java $T/test/T.java $T/test/Mach.java $T/test/SplitRun.java $T/test/Inputs.java $EXTRA
  (cd /tmp/vkcls && jar cf "$S/split.jar" *.class)
  cp $T/hops.txt "$S/"
  for f in loader.bin split.code.bin split.data.bin split.sym; do cp $T/sw/build/$f "$S/sw/build/"; done
done
# HASHES-SPLIT (root-relative names, as tools/runjob.sh writes them)
(cd "$V" && java -Xmx3g -cp m12/split/split.jar SplitSets sets-65536 > /dev/null
 { echo "# M12 verification kit: SHA-256 of every input a split run reads (java -cp m12/split/split.jar Inputs check HASHES-SPLIT)"
   for x in m11 m12; do java -cp m12/split/split.jar Inputs hash $x/split/split.jar $x/split/sw/build/loader.bin $x/split/sw/build/split.code.bin \
     $x/split/sw/build/split.data.bin $x/split/sw/build/split.sym $x/split/hops.txt; done
   java -cp m12/split/split.jar Inputs hash sets-65536/*.bin; } > HASHES-SPLIT
 rm -rf sets-65536)
echo "verification kit assembled in $V: $(du -sh "$V" | cut -f1)"
