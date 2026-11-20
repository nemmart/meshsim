#!/bin/sh
# run.sh BIN OUT: every tests/NN-name.in through the host build (with NN-name.seed if there is one), its console
# bytes diffed with NN-name.out; the emulation's call log in OUT/NN-name.log. A failure prints the diff (cat -v).
bin=$1
out=$2
mkdir -p "$out"
n=0
fails=0
for in in tests/*.in; do
    t=${in%.in}
    name=${t##*/}
    n=$((n + 1))
    if [ -f "$t.seed" ]; then
        "$bin" -s "$t.seed" -i "$in" -l "$out/$name.log" >"$out/$name.actual" 2>"$out/$name.err"
    else
        "$bin" -i "$in" -l "$out/$name.log" >"$out/$name.actual" 2>"$out/$name.err"
    fi
    rc=$?
    if [ $rc -eq 0 ] && cmp -s "$out/$name.actual" "$t.out"; then
        echo "ok   $name"
    else
        fails=$((fails + 1))
        echo "FAIL $name (exit $rc)"
        cat "$out/$name.err"
        cat -v "$t.out" >"$out/$name.expected.v" 2>/dev/null
        cat -v "$out/$name.actual" >"$out/$name.actual.v"
        diff "$out/$name.expected.v" "$out/$name.actual.v" | head -40
    fi
done
echo "$n tests, $fails failed"
[ $fails -eq 0 ]
