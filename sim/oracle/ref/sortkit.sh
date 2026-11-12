#!/bin/bash
# sortkit.sh -- v1.3's measurement kit (sim/ref/meshsim/kit), assembled from ref and run, for the C++ kit's references
# (WORKER24, P6, row D8). Needs a JDK. sim/ref is only read.
#
#   sortkit.sh OUTDIR [run.sh options...]     e.g.  sortkit.sh /tmp/k -n 2000 -j 8 -m 2g
#
# OUTDIR/kit is v1.3's kit as kit/assemble.sh makes it, with the two changes v1.3's own sort needs (NOTES, F2):
#   1. the image files v1.3's sort build makes (Inputs.IMAGE: sort.code.bin, sort.data.bin, sort.sym), not v1.1's list
#      (loader.bin, sort.ovinit.bin, sort.stage.bin no longer exist: v1.2's image has no overlays);
#   2. sw/bootrom.hex beside the jar (Controller.bootRom reads it from the working directory since v1.2; v1.1 had no boot
#      ROM), listed in HASHES so the Inputs check covers it.
# Everything else is assemble.sh's: the jar's classes, run.sh, tools/runjob.sh, jobs.txt, HASHES' form (KeyGen's 20 sets
# at 64 x 65,536 and 64 x 2,000). The images are the C++ tree's committed ones (byte-identical to v1.3's build,
# oracle/fw_crosscheck.sh). Then `run.sh OPTIONS -o results` in the kit (OUTDIR/kit/results); SUMMARY.md and all.csv are SortSummary's.
set -eu
export LC_ALL=C
SIM=$(cd "$(dirname "$0")/../.." && pwd); M=$SIM/ref/meshsim
O=${1:?usage: sortkit.sh OUTDIR [run.sh options]}; shift
mkdir -p "$O"; O=$(cd "$O" && pwd); K=$O/kit
rm -rf "$K" "$O/cls"; mkdir -p "$K/tools" "$K/image" "$K/sw" "$O/cls"
cp "$M/kit/run.sh" "$M/kit/README.md" "$K/"; cp "$M/kit/tools/runjob.sh" "$M/kit/tools/make_jobs.py" "$K/tools/"
javac --release 17 -nowarn -d "$O/cls" "$M"/src/*.java "$M"/test/{T,SortTest,SortSummary,SortCompare,PhaseNeutral,KeyGen,Inputs}.java 2>&1 | grep -v '^Note:' || true
(cd "$O/cls" && jar cf "$K/meshsim.jar" *.class)
cp "$M/hops.txt" "$K/"; cp "$M/sw/bootrom.hex" "$K/sw/"
for f in sort.code.bin sort.data.bin sort.sym; do cp "$SIM/tests/golden-m2/images/$f" "$K/image/"; done
cp "$M/kit/jobs.txt" "$K/"
(cd "$K" && for n in 65536 2000; do java -Xmx2g -cp meshsim.jar KeyGen keys-$n --n $n > /dev/null; done
 { echo "# meshsim v1.1 (spec 26n) kit: SHA-256 of every input a run reads (java Inputs check HASHES)"
   java -cp meshsim.jar Inputs hash meshsim.jar sw/bootrom.hex image/sort.code.bin image/sort.data.bin image/sort.sym hops.txt \
     keys-65536/*.keys keys-2000/*.keys; } > HASHES
 rm -rf keys-65536 keys-2000)
echo "kit assembled in $K: $(du -sh "$K" | cut -f1)"
# relative: SortSummary names the directory in SUMMARY.md's first line
cd "$K" && ./run.sh "$@" -o results
