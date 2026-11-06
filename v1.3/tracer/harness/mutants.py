#!/usr/bin/env python3
"""Negative tests for check_div: deliberately broken templates must be caught (WORKER7).

Each mutant changes one template; the harness runs it (words loaded from a .hex file) and must report a
failure in the expected check. Run after build.sh: python3 mutants.py [random-cases].
"""
import copy
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import templates as T  # noqa: E402
import gen  # noqa: E402


def mutate(name, fn):
    rows = copy.deepcopy(T.TEMPLATES[name])
    fn(rows)
    return name, rows


def drop(*idx):
    return lambda rows: [rows.pop(i) for i in sorted(idx, reverse=True)]


def setf(i, **kw):
    return lambda rows: rows[i].update(kw)


def swap(i, j):
    def f(rows):
        rows[i], rows[j] = rows[j], rows[i]
    return f


MUTANTS = [
    ('REM without the divide-by-zero fix-up', mutate('REM', drop(2, 3)), 'C1'),
    ('REMW without the divide-by-zero fix-up', mutate('REMW', drop(4, 5)), 'C1'),
    ('DIVUW without the zero-extension fix-up', mutate('DIVUW', drop(3)), 'C1'),
    ('DIVW as a native DIVW of the raw operands', mutate('DIVW', setf(2, op='DIVW', rs1='RS1', rs2='RS2')), 'C1'),
    ('DIV reading the wrong bank-1 register', mutate('DIV', setf(1, advice='b1')), 'C1'),
    ('DIVU with the untraced row after the advice row', mutate('DIVU', swap(0, 1)), 'C1'),
    ('REM with |DIVU| instead of |DIV|', mutate('REM', setf(0, op='DIVU')), 'C1'),
    ('DIV with a changed traced row', mutate('DIV', setf(12, rs1='t1')), 'C2'),
    # Advice that is wrong only when the divisor is 0: does Jolt's own assertion chain notice?
    ('REMU: DIVU advice replaced by 0 when rs2 = 0', mutate('REMU', lambda r: r.__setitem__(slice(0, 1), [
        T.U('DIVU', 'b0', 'RS1', 'RS2'), T.U('SLTU', 'b1', 'x0', 'RS2'), T.U('MUL', 'b0', 'b0', 'b1')])), 'C1'),
    ('REMUW: DIVUW advice replaced by 0 when rs2[31:0] = 0', mutate('REMUW', lambda r: r.__setitem__(slice(2, 4), [
        T.U('DIVUW', 'b0', 'RS1', 'RS2'), T.U('VirtualZeroExtendWord', 'b0', 'b0', 'K'),
        T.U('VirtualZeroExtendWord', 'b1', 'RS2', 'K'), T.U('SLTU', 'b1', 'x0', 'b1'), T.U('MUL', 'b0', 'b0', 'b1')])), 'C1'),
    ('DIVU: advice replaced by 0 when rs2 = 0', mutate('DIVU', lambda r: r.__setitem__(slice(0, 1), [
        T.U('DIVU', 'b0', 'RS1', 'RS2'), T.U('SLTU', 'b1', 'x0', 'RS2'), T.U('MUL', 'b0', 'b0', 'b1')])), 'C1'),
]


def main():
    nrand = sys.argv[1] if len(sys.argv) > 1 else '20000'
    ok = True
    print('# mutant\texpected\tcaught by (C1 advice, C2 rows, C3 values+asserts, C4 RV64 M)')
    for k, (what, (name, rows), expect) in enumerate(MUTANTS):
        path = f'/tmp/mutant_{k}_{name}.hex'
        with open(path, 'w') as f:
            for i, w in enumerate(gen.words(name, rows)):
                f.write(f'{name} {i} {w:032x}\n')
        out = subprocess.run(['/tmp/check_div', nrand, path], capture_output=True, text=True).stdout
        line = [l for l in out.splitlines() if l.startswith(name + '\t')][0].split('\t')
        caught = [c for c, n in zip(['C1', 'C2', 'C3', 'C4'], line[2:6]) if int(n) > 0]
        hit = expect in caught
        ok &= hit
        aborts = f' ({line[6]} cases fail a Jolt assertion)'
        print(f'{what}\t{expect}\t{",".join(caught) or "NOT CAUGHT"}{aborts}' + ('' if hit else '\t<-- FAIL'))
    print('# all mutants caught' if ok else '# SOME MUTANTS NOT CAUGHT')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
