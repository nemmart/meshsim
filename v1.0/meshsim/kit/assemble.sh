#!/bin/sh
# kit/assemble.sh OUTDIR: makes the measurement kit: the simulator as a Java 17 jar (with SortTest, SortSummary,
# SortCompare, PhaseNeutral, KeyGen and Inputs), the sort's prebuilt images (run sw/ext/sort/build.sh first), the runner,
# the job list, and HASHES, the reference set: SHA-256 of the jar, the image, hops.txt and the 20 key sets at
# 64 x 65,536 and 64 x 2,000 (made here with KeyGen, then deleted; about 1 minute). Java only to run it.
set -e
M=$(cd "$(dirname "$0")/.." && pwd); K=${1:?usage: kit/assemble.sh OUTDIR}
mkdir -p $K/tools $K/image
cp $M/kit/run.sh $M/kit/README.md $K/; cp $M/kit/tools/runjob.sh $M/kit/tools/make_jobs.py $K/tools/
rm -rf /tmp/kitcls && mkdir -p /tmp/kitcls
javac --release 17 -d /tmp/kitcls $M/src/*.java $M/test/T.java $M/test/SortTest.java $M/test/SortSummary.java $M/test/SortCompare.java $M/test/PhaseNeutral.java $M/test/KeyGen.java $M/test/Inputs.java
(cd /tmp/kitcls && jar cf $K/meshsim.jar *.class)
cp $M/hops.txt $K/
for f in loader.bin sort.code.bin sort.data.bin sort.ovinit.bin sort.stage.bin sort.sym; do cp $M/sw/ext/sort/build/$f $K/image/; done
cp $M/kit/jobs.txt $K/
# the reference hashes (kit-relative names, the ones SortTest's inputs line uses)
(cd $K && for n in 65536 2000; do java -Xmx2g -cp meshsim.jar KeyGen keys-$n --n $n > /dev/null; done
 { echo "# meshsim v1.0 kit: SHA-256 of every input a run reads (java Inputs check HASHES)"
   java -cp meshsim.jar Inputs hash meshsim.jar image/loader.bin image/sort.code.bin image/sort.data.bin image/sort.ovinit.bin \
     image/sort.stage.bin image/sort.sym hops.txt keys-65536/*.keys keys-2000/*.keys; } > HASHES
 rm -rf keys-65536 keys-2000)
echo "kit assembled in $K: $(du -sh $K | cut -f1)"
