#!/usr/bin/env python3
"""ge.py: the yosys cells of each *.stat in GE, with count.py's costs, beside count.py's estimate of the same pieces."""
import os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import count
C = count.COST
W = {'$_AND_': C['and2'], '$_OR_': C['and2'], '$_NAND_': 1.0, '$_NOR_': 1.0, '$_XOR_': C['xor'], '$_XNOR_': C['xor'],
     '$_ANDNOT_': C['and2'], '$_ORNOT_': C['and2'], '$_MUX_': C['mux'], '$_NOT_': 0.67}
def FF(name):
    k = name.split('_')[1]                 # DFF, DFFE, SDFF, SDFFE, ...
    return C['ff'] + (C['mux'] if k.endswith('E') else 0) + (C['and2'] if k.startswith(('S', 'A')) or 'R' in k else 0)

def stat(path):
    cells = {}
    for line in open(path):
        m = re.match(r'\s+(\d+)\s+(\S+)$', line) or re.match(r'\s+(\$\S+)\s+(\d+)$', line)
        if not m: continue
        a, b = m.groups()
        n, c = (int(a), b) if a.isdigit() else (int(b), a)
        if c.startswith('$_'): cells[c] = cells.get(c, 0) + n
    return cells

def ge(cells):
    logic = sum(n * W[c] for c, n in cells.items() if c in W)
    ffs = {c: n for c, n in cells.items() if 'DFF' in c or 'SDFF' in c}
    ff = sum(n * FF(c) for c, n in ffs.items())
    return logic, ff, sum(ffs.values())

ops = {o['name']: count.op_ge(o) for u in count.OPS for o in count.OPS[u]}
def est(names): return sum(ops[n] for n in names)
EST = {
 'upd_decoder': ('logic', est([n for n in ops if n.startswith('decoder:') and 'LOW' not in n and 'WORD_COUNT' not in n
                               and 'min' not in n and 'type' not in n and 'cleared at START, bit' not in n]),
                 'the decoder\'s operators (HIGH) but the run counters and the LRU clear, which yosys puts in its flip-flops'),
 'cache_tagrow': ('logic', est([n for n in ops if n.startswith(('tag compare', 'the victim', 'touch', 'tag row write',
                                                                'the slot\'s way'))]),
                  'tag compare, victim, touch, tag row write'),
 'cache_line': ('logic', est(['the entry: 32 of the line\'s 1,024 bits, 32:1', 'the update\'s patch: the entry into its 32 bits of the line',
                              'the update\'s patch: the 5 -> 32 decode']), 'entry_of and the patch'),
}
print('| model | yosys logic, GE | count.py\'s estimate, GE | yosys flip-flops (GE, with their enables and resets) | what count.py\'s figure covers |')
print('|---|---|---|---|---|')
for m in ('upd_decoder', 'cache_tagrow', 'cache_line'):
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), m + '.stat')
    if not os.path.exists(p): continue
    cells = stat(p); lg, ff, nff = ge(cells)
    kind, e, what = EST[m]
    print('| %s | %s | %s | %d (%s) | %s |' % (m, '{:,}'.format(round(lg)), '{:,}'.format(round(e)), nff, '{:,}'.format(round(ff)), what))
# the decoder's flip-flops with their enables and clear, as count.py costs them (HIGH): the bit buffer, its count, the LRU
ff_est = (194 + 8 + 2048) * C['ff'] + (2048 + 64) * C['mux'] + 2048 * C['and2'] + (194 + 8) * C['and2']
print()
print('upd_decoder\'s flip-flops with their enables and clears, count.py\'s way (HIGH): %s GE' % '{:,}'.format(round(ff_est)))
