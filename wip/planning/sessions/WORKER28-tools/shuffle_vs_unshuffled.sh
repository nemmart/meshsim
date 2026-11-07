#!/bin/bash
# shuffle_vs_unshuffled.sh DIR (prove_skip.sh's output): the shuffled steps with stage 1 on against their unshuffled base with stage 1 off (seed banners masked)
cd ${1:-/tmp/pq2}
mask() { sed -E 's/[0-9]+(\.[0-9]+)? ?(s|ms|µs|us)\b/#s/g; s/[0-9]+ rows per JVM-second/# rows per JVM-second/g; s/ \(shuffled, seed [0-9]+\)//; /^shuffled child order, seed [0-9]+$/d' "$1"; }
for p in net:net-shuf net:net-shuf2 mem:mem-shuf mem:mem-shuf2 engine:eng-shuf ctrl:ctrl-shuf rdport:rdport-shuf agtest:agtest-shuf recipes:recipes-shuf cond:cond-shuf hooks:hooks-shuf periph:periph-shuf reqbus:reqbus-shuf cccores:cccores-shuf ${EXTRA:-}; do
  a=${p%%:*}; b=${p#*:}; [ -f $a.false.log ] && [ -f $b.true.log ] || { echo "$b: missing"; continue; }
  n=$(diff <(mask $a.false.log) <(mask $b.true.log) | grep -c '^[<>]'); echo "$b (on) vs $a (off): $([ $n = 0 ] && echo SAME || echo "$n lines differ")"
done
