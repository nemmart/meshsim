#!/usr/bin/env python3
"""fw_cycles.py: the instructions and cycles of the firmware outlines in fw/*.S, and what they come to beside the two
controllers' own cycles and the link's rate (SIZING/REPORT.md, part 5).

A PATH's cycles: one an instruction, with core.md's costs ("The core's cycle costs"): a taken branch (T) 3 more, a
jump (J) 2 more, a use of a load's result just after it (U) 1 more, a window load (W) 1 more (WINDOW_LOAD_LATENCY 2).
The waits for memory (wait:NAME) are not counted: the controller waits as long for the same burst or reply.

    python3 fw_cycles.py          # prints the tables, writes FW.md
"""
import json
import os
import re
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
EXTRA = {'T': 3, 'J': 2, 'U': 1, 'W': 1}
TAKEN = EXTRA['T']


def parse(path):
    paths = OrderedDict()
    cur = None
    for line in open(path):
        m = re.match(r'#\s*PATH\s+(\w+):\s*(.*)', line)
        if m:
            cur = m.group(1)
            paths[cur] = dict(desc=m.group(2).strip(), n=0, cycles=0, waits=[], marks={})
            continue
        if re.match(r'#\s*END', line):
            cur = None
            continue
        if cur is None:
            continue
        code, _, comment = line.partition('#')
        code = re.sub(r'^\s*\w+:', '', code).strip()          # a label
        if not code or code.startswith('.'):
            continue
        p = paths[cur]
        p['n'] += 1
        p['cycles'] += 1
        marks = comment.split()[:2] if comment else []
        for mk in marks:
            if mk.startswith('wait:'):
                p['waits'].append(mk[5:])
                continue
            for ch in mk:
                if ch in EXTRA and mk == mk.upper() and len(mk) <= 2:
                    p['cycles'] += EXTRA[ch]
                    p['marks'][ch] = p['marks'].get(ch, 0) + 1
    return paths


U = parse(os.path.join(HERE, 'fw', 'updater_fw.S'))
C = parse(os.path.join(HERE, 'fw', 'cache_fw.S'))


def cyc(P, name):
    return P[name]['cycles']


def ins(P, name):
    return P[name]['n']


# ---- the LRU code mix: lru_stats.py on the repository's frame files (small fixtures), and two other points
def lru_mix():
    import subprocess, sys
    root = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
    files = [os.path.join(root, 'v1.37_wip', 'tests', 'updater', f) for f in
             ('divs.frames', 'jumps-base.frames', 'kin-big.frames', 'kin-lru.frames', 'kin-raw.frames')]
    files.append(os.path.join(root, 'updater_wip', 'pipeline', 'gold', 'test', 'packets', 'small.frames'))
    files = [f for f in files if os.path.exists(f)]
    out = subprocess.run([sys.executable, os.path.join(HERE, 'lru_stats.py')] + files, capture_output=True, text=True,
                         check=True).stdout
    return json.loads(out), [os.path.relpath(f, root) for f in files]


def updater_costs():
    mix, files = lru_mix()
    refill = cyc(U, 'lru_refill32') / 32.0                       # a refill's cycles a bit of code
    hit0 = cyc(U, 'lru_hit0') + 6 * refill
    hit_i = lambda i: (cyc(U, 'lru_hit0') - 4 + TAKEN + cyc(U, 'lru_move_setup') + i * cyc(U, 'lru_move1')
                       + 6 * refill)
    m34 = cyc(U, 'lru_miss34') + 34 * refill
    m66 = cyc(U, 'lru_miss66') + 66 * refill
    place, header = cyc(U, 'lru_place'), cyc(U, 'lru_header')
    hist = mix['hit_index_hist']
    hits = sum(hist)
    codes = mix['codes']
    mean_decode = (sum(n * (hit0 if i == 0 else hit_i(i)) for i, n in enumerate(hist)) + mix['m34'] * m34
                   + mix['m66'] * m66) / codes
    rows = OrderedDict()
    rows['raw: a header word'] = (cyc(U, 'raw_header'), 1, 64)
    rows['raw: a value into the page held, the general path'] = (cyc(U, 'raw_value'), 1, 64)
    rows['raw: a value, the fast path (a stretch of a run in the page, unrolled by 4)'] = (cyc(U, 'raw_fast4') / 4.0, 1, 64)
    rows['LRU: a hit at index 0, a value'] = (hit0 + place, 1, 6)
    rows['LRU: a hit at index 8, a value'] = (hit_i(8) + place, 1, 6)
    rows['LRU: a 34-bit miss, a value'] = (m34 + place, 1, 34)
    rows['LRU: a 66-bit miss, a value'] = (m66 + place, 1, 66)
    rows['LRU: a header word (a 66-bit miss)'] = (m66 + header, 1, 66)
    rows['LRU: the fixtures\' mix (%d codes: %.0f%% hits, mean index %.2f; %.1f bits a code)' % (
        codes, 100 * mix['hit_share'], mix['mean_hit_index'], mix['bits_a_code'])] = (mean_decode + place, 1,
                                                                                    mix['bits_a_code'])
    return rows, mix, files, dict(refill=refill, hit0=hit0, m34=m34, m66=m66, place=place)


def updater_page_costs():
    new_page = cyc(U, 'new_page')
    clear = 256 * cyc(U, 'fault_clear4')
    level = cyc(U, 'fault_level')
    return OrderedDict([
        ('a value in another page: the page held flushed in place, the next looked up and held',
         (new_page, U['new_page']['waits'], 6)),
        ('a fault\'s step at an index level: source read, owned bits cleared (1,024 entries), page taken, written, '
         'entry updated', (level + clear, U['fault_level']['waits'], 5)),
        ('the same at the data level (no owned bits to clear)', (level, U['fault_level']['waits'], 5)),
    ])


def cache_costs():
    take, reply = cyc(C, 'c_take'), cyc(C, 'c_reply')
    probe = cyc(C, 'c_probe')
    level = lambda k: cyc(C, 'c_level') + k * probe + TAKEN
    lru = cyc(C, 'c_touch_lru')
    rows = OrderedDict()
    for k, what in ((1, 'found at the first way probed'), (4.5, 'found at the mean way, 4.5 probes')):
        rows['a look-up, a depth-2 file, both lines in the cache, %s; no replacement order kept' % what] = (
            take + 2 * level(k) + reply, 6)
        rows['the same, true LRU kept (23.6.1)' + ('' if k == 1 else ', 4.5 probes')] = (
            take + 2 * (level(k) + lru) + reply, 6)
    fill = take + 3 * (level(4.5) + lru) + level(4.5) + lru + 4 * cyc(C, 'c_fill_line8') + 2
    rows['a fill read, a depth-3 file, its four lines in the cache, 4.5 probes, true LRU; the line\'s 32 words copied'] = (
        fill, 10)
    miss_own = cyc(C, 'c_miss') + 3.5 * cyc(C, 'c_victim1') + 8 * probe   # the victim search; the 8 probes that missed
    rows['a line missing: the probes that miss, the victim, the fetch, the install (besides the wait for LPDDR)'] = (
        miss_own, 13)
    rows['a dirty victim\'s write-back, besides its wait'] = (cyc(C, 'c_writeback'), 3)
    return rows


def tables():
    L = []
    p = L.append
    p('# FW.md: the firmware outlines counted (generated by fw_cycles.py; do not edit)\n')
    p('Costs a cycle (core.md, "The core\'s cycle costs"): an instruction 1; a taken branch 3 more; a JAL 2 more; a '
      'load\'s result used at once 1 more; a load from a window 1 more. Memory waits apart.\n')
    for nm, P in (('updater_fw.S', U), ('cache_fw.S', C)):
        p('## %s\n' % nm)
        p('| path | instructions | cycles | marks | waits | what |')
        p('|---|---|---|---|---|---|')
        for k, v in P.items():
            p('| %s | %d | %d | %s | %s | %s |' % (k, v['n'], v['cycles'], ' '.join('%s%d' % kv for kv in v['marks'].items()),
                                                 ', '.join(v['waits']), v['desc']))
        p('')
    rows, mix, files, parts = updater_costs()
    p('## The Updater: an update word, decoded and placed\n')
    p('The link delivers a byte a cycle (host-link.md; 17.6.5): a raw update word every 8 cycles; an LRU code every '
      'bits/8 cycles. The controller takes one a cycle. "Core share" is the firmware\'s cycles over the link\'s: above '
      '1, one core cannot keep up.\n')
    p('| update word | firmware, cycles | controller, cycles | the link\'s cycles for it | core share |')
    p('|---|---|---|---|---|')
    res = OrderedDict()
    for k, (f, hw, bits) in rows.items():
        link = bits / 8.0
        res[k] = dict(fw=f, hw=hw, link=link, share=f / link)
        p('| %s | %.1f | %d | %.2f | %.2f |' % (k, f, hw, link, f / link))
    p('')
    p('The LRU mix is lru_stats.py\'s over ' + ', '.join(files) + ': small test fixtures, not proof runs.\n')
    p('| page work | firmware, cycles besides the waits | the waits (the same for the controller) | controller\'s own cycles |')
    p('|---|---|---|---|')
    pg = updater_page_costs()
    for k, (f, waits, hw) in pg.items():
        p('| %s | %d | %s | about %d |' % (k, f, ', '.join(waits), hw))
    p('')
    p('## The cache: a request answered\n')
    p('| request | firmware, cycles | controller, cycles (23.6.7) |')
    p('|---|---|---|')
    cr = cache_costs()
    for k, (f, hw) in cr.items():
        p('| %s | %.0f | %d |' % (k, f, hw))
    p('')
    return '\n'.join(L), res, pg, cr, mix


if __name__ == '__main__':
    text, res, pg, cr, mix = tables()
    open(os.path.join(HERE, 'FW.md'), 'w').write(text + '\n')
    json.dump(dict(updater=res, cache={k: dict(fw=v[0], hw=v[1]) for k, v in cr.items()},
                   pages={k: dict(fw=v[0], hw=v[2]) for k, v in pg.items()}, lru_mix={k: v for k, v in mix.items()}),
              open(os.path.join(HERE, 'fw.json'), 'w'), indent=1)
    print(text)
