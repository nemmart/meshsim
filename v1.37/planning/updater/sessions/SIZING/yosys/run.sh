#!/bin/sh
# run.sh: synthesize the three models to simple gates and count them (yowasp-yosys, the WebAssembly build of yosys).
cd "$(dirname "$0")"
Y=${YOSYS:-yowasp-yosys}
for m in upd_decoder cache_tagrow cache_line; do
  $Y -q -p "read_verilog $m.v; synth -flatten -top $m; abc -g AND,NAND,OR,NOR,XOR,XNOR,ANDNOT,ORNOT,MUX; opt_clean; tee -q -o $m.stat stat" > /dev/null 2>&1
done
python3 ge.py | tee crosscheck.md
