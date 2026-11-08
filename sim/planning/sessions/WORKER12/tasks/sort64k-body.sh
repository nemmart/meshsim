# Body (WORKER12): the full-size sort (p4 uniform, 64 x 65536 keys, k128) against its references, end to end: every
# sort64k-fp point (Quest 135's file in $DATA, sha256 from HASHES), the result (M2Sort's verification), the phases and
# TOTAL (sort64k.out, sort64k.phases.csv: 1,229,687), on one thread and on 8 domains (row bands). Also the 16K sort on 8
# domains (sort16k-fp regenerated from ref here), which the one-CPU container could not run.
DATA=/home/questrunner/data; [ -d "$DATA/w7-golden-m2" ] || DATA=$REPO/data
FP64=$DATA/w7-golden-m2/fb6f21e/sort64k-fp.fp
sec "inputs: the full-size golden ($FP64), the key sets (KeyGen), the 16K golden (regen.sh)"
H=$(sha256sum "$FP64" 2>/dev/null | cut -d' ' -f1)
[ "$H" = 6e923d6c9823c0d758104f81e7706cd4598a771ba89aad39609e78985c1d3846 ] && verdict "sort64k-fp.fp present, sha256 as HASHES" PASS \
  || { verdict "sort64k-fp.fp present, sha256 as HASHES (got '${H:-missing}')" FAIL; exit 1; }
mkdir -p "$W/cls" "$W/keys"
javac -nowarn -d "$W/cls" "$KS"/ref/meshsim/src/*.java "$KS"/ref/meshsim/test/KeyGen.java 2>&1 | grep -v '^Note:'
for n in 16384 65536; do ( cd "$W/keys" && java -Xmx2g -cp "$W/cls" KeyGen "$W/keys/keys-$n" --n $n --only uniform > /dev/null ); done
k16=$(sha256sum "$W/keys/keys-16384/uniform.keys" | cut -d' ' -f1); k64=$(sha256sum "$W/keys/keys-65536/uniform.keys" | cut -d' ' -f1)
[ "$k16" = 3db703f3e170ee6fa337eb7ee539800b371a3d6fcf924225baade1a8386aeb40 ] && [ "$k64" = a7636b687a6db39cac8fbf63c2f16f686fd1ce858fe8a7ed5d98621e452c9b7a ] \
  && verdict "key sets as HASHES" PASS || verdict "key sets as HASHES ($k16, $k64)" FAIL
( cd "$KS/tests/golden-m2" && KEEP="$W/fp16" ./regen.sh --check 'sort16k-fp' > "$RES/regen16k.log" 2>&1 ) \
  && verdict "sort16k-fp regenerated (regen.sh --check: SAME)" PASS || { tail -5 "$RES/regen16k.log"; verdict "sort16k-fp regenerated" FAIL; }
sec "build (clang release, D18)"
( cd "$KS" && make -j8 CC_NAME=clang MODE=release build/clang-release/test_sort > "$RES/build.log" 2>&1 ) \
  && verdict "build" PASS || { tail -20 "$RES/build.log"; verdict "build" FAIL; exit 1; }
# srun TAG CPUS ENV...: test_sort's full run (sort_runs_equal_to_refs) under taskset, its log kept
srun() {
  local tag=$1 cpus=$2; shift 2
  local t0=$(date +%s)
  ( cd "$KS" && env "$@" taskset -c "$cpus" ./build/clang-release/test_sort sort_runs > "$RES/$tag.log" 2>&1 )
  local rc=$?
  grep -E '^sort' "$RES/$tag.log"
  if [ $rc = 0 ] && grep -q '^ok   sort_runs_equal_to_refs' "$RES/$tag.log"; then verdict "$tag ($(( $(date +%s) - t0 )) s)" PASS
  else tail -5 "$RES/$tag.log"; verdict "$tag (rc $rc)" FAIL; fi
}
P1=$(take 1 "$P_ONE"); C8=$(take 8 "$PLACE")
sec "the full size, one thread (CPU $P1)"
srun sort64k-one "$P1" SORT_SIZE=64k SORT_KEYS="$W/keys/keys-65536/uniform.keys" SORT_FP="$FP64"
sec "the full size, 8 domains (CPUs $C8)"
srun sort64k-p8 "$C8" SORT_SIZE=64k SORT_PARTS=8 SORT_KEYS="$W/keys/keys-65536/uniform.keys" SORT_FP="$FP64"
sec "16K, 8 domains (CPUs $C8)"
srun sort16k-p8 "$C8" SORT_PARTS=8 SORT_KEYS="$W/keys/keys-16384/uniform.keys" SORT_FP="$W/fp16/sort16k-fp.fp"
