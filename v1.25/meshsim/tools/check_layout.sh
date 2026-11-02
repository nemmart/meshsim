#!/bin/sh
# In the v1.0 layout (meshsim/ beside sw/ and sort/): the copies meshsim builds from must equal the published sources.
#   ../sw/mesh.h                     = sw/mesh.h (generated; RegDocTest checks it too)
#   ../sort/program/sw/              = sw/ext/sort/sw/ (the sort program)
#   ../sort/skew/planner + planner.patch        = sw/ext/skew-planner/
#   ../sort/split/{split.c,split.h,split_regs.h} (+ split-barrier.patch on split.c) = sw/ext/split-bisect/
cd "$(dirname "$0")/.."
[ -d ../sort ] && [ -d ../sw ] || { echo "check_layout: not in the v1.0 layout (no ../sort, ../sw); nothing to check"; exit 0; }
T=$(mktemp -d); bad=0
cmp -s ../sw/mesh.h sw/mesh.h || { echo "DIFF ../sw/mesh.h sw/mesh.h"; bad=1; }
diff -r ../sort/program/sw sw/ext/sort/sw > /dev/null || { echo "DIFF ../sort/program/sw sw/ext/sort/sw"; bad=1; }
mkdir -p $T/sort/skew $T/sort/split && cp -r ../sort/skew/planner $T/sort/skew/ && cp ../sort/split/split.c $T/sort/split/
patch -s -p0 -d $T < ../sort/program/patches/planner.patch && patch -s -p0 -d $T < ../sort/program/patches/split-barrier.patch || bad=1
for f in sw/ext/skew-planner/*; do cmp -s $f $T/sort/skew/planner/$(basename $f) || { echo "DIFF planner: $(basename $f) (../sort/skew/planner + planner.patch)"; bad=1; }; done
cmp -s $T/sort/split/split.c sw/ext/split-bisect/split.c || { echo "DIFF split.c (../sort/split + split-barrier.patch)"; bad=1; }
for f in split.h split_regs.h; do cmp -s ../sort/split/$f sw/ext/split-bisect/$f || { echo "DIFF $f"; bad=1; }; done
rm -rf $T
[ $bad = 0 ] && echo "check_layout: meshsim's copies equal ../sw and ../sort" || { echo "check_layout: FAIL"; exit 1; }
