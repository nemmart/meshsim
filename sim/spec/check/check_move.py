#!/usr/bin/env python3
"""The proof that v1.35's spec is v1.3's moved: run from the repository's root,  python3 sim/spec/check/check_move.py

Independent of build.py: it reads only the source files (v1.3/), the destinations (sim/spec/), map.tsv and edits.json.
  1. every non-blank source line is in exactly one destination range of map.tsv;
  2. each destination, its blank lines and its added lines set aside, is exactly its ranges' non-blank source lines in
     order, byte for byte, except edited lines, whose source line must equal the edit's old text and whose
     destination line its new text;
  3. every section number of v1.3's machine-spec.md and appendix is a heading in a destination; every item 1-159 is
     in section 13's index;
  4. every repository path cited in a non-history destination exists in v1.35's tree (sim/) or, starting with
     v1.3/, in the repository; a bare file name of ours names a file in sim/.
Exits non-zero on any failure. Blank lines are exempt (the owner, 2026-11-12).
"""
import json, os, re, sys
from collections import defaultdict

OUT, ROOT = 'sim/spec', 'sim'
fails = []


def lines(p):
    with open(p, encoding='utf-8') as f:
        L = f.read().split('\n')
    return L[:-1] if L and L[-1] == '' else L


ranges = defaultdict(list)          # dest -> [(src, a, b)] in order
for l in lines(os.path.join(OUT, 'check/map.tsv')):
    if l.startswith('#') or not l.strip():
        continue
    s, a, b, d = l.split('\t')
    ranges[d].append((s, int(a), int(b)))
ej = json.load(open(os.path.join(OUT, 'check/edits.json'), encoding='utf-8'))
edits = {(e[0], e[1]): e for e in ej['edits']}
added = defaultdict(set)
for d, n, t in ej['added']:
    added[d].add(n)
SRCS = sorted({s for r in ranges.values() for s, _, _ in r})
SL = {s: lines(s) for s in SRCS}

# 1. coverage
cover = defaultdict(int)
for d, r in ranges.items():
    for s, a, b in r:
        for i in range(a, b + 1):
            cover[(s, i)] += 1
n_src = 0
for s in SRCS:
    for i, l in enumerate(SL[s], 1):
        if not l.strip():
            continue
        n_src += 1
        if cover[(s, i)] != 1:
            fails.append(f'1: {s}:{i} is in {cover[(s, i)]} destinations')

# 2. destinations
n_dst = n_moved = 0
dests = sorted(set(ranges) | set(added))
for d in dests:
    D = lines(os.path.join(OUT, d))
    want = [(s, i) for s, a, b in ranges.get(d, []) for i in range(a, b + 1) if SL[s][i - 1].strip()]
    k = 0
    for n, l in enumerate(D, 1):
        if not l.strip():
            continue
        n_dst += 1
        if n in added[d]:
            continue
        if k >= len(want):
            fails.append(f'2: {d}:{n} is neither a source line nor added'); continue
        s, i = want[k]; k += 1
        src = SL[s][i - 1]
        e = edits.get((d, n))
        if e:
            if e[2] != s or e[3] != i or e[5] != src or e[6] != l:
                fails.append(f'2: {d}:{n}: edit does not match {s}:{i}')
        elif l != src:
            fails.append(f'2: {d}:{n} differs from {s}:{i}')
        else:
            n_moved += 1
    if k != len(want):
        fails.append(f'2: {d}: {len(want) - k} source lines missing at the end')

# 3. section numbers and items
heads = set()
for d in dests:
    for l in lines(os.path.join(OUT, d)):
        m = re.match(r'#+ ([0-9]+(?:\.[0-9]+)*|[A-I])[. ]', l)
        if m:
            heads.add((d.endswith('replayer-opcodes.md'), m.group(1)))
for s, isapp in (('v1.3/spec/machine-spec.md', False), ('v1.3/spec/replayer-opcodes.md', True)):
    for l in SL[s]:
        m = re.match(r'#+ ([0-9]+(?:\.[0-9]+)*|[A-I])[. ]', l)
        if m and (isapp, m.group(1)) not in heads:
            fails.append(f'3: section {m.group(1)} of {s} is no heading')
ms = '\n'.join(lines(os.path.join(OUT, 'machine-spec.md')))
idx = ms.split('**The index:**')[1].split('\n\n')[1] if '**The index:**' in ms else ''
for n in range(1, 160):
    if not re.search(rf'(^|; ){n} (closed|here|`)', idx):
        fails.append(f'3: item {n} not in the index')

# 4. paths
OURS = r'(spec|fw|src|tests|data|pipeline|planning|tools|sw|tables|meshsim|wip|results|diagrams|sort|v1\.3)/'
EXTERNAL = {'src/tracer/isa'}      # jolt-cpp's, cited at 4.19's ground truth
names = {f for _, _, fs in os.walk(ROOT) for f in fs}
n_paths = 0
for d in dests:
    if d.startswith('history/') and d != 'history/README.md':
        continue
    for n, l in enumerate(lines(os.path.join(OUT, d)), 1):
        for t in re.findall(r'`([^`\s]+)`', l):
            t0 = re.sub(r':[0-9–-]+$', '', t)
            if t0 in EXTERNAL or '*' in t0:
                continue
            if re.match(OURS, t0):
                n_paths += 1
                ok = os.path.exists(t0) if t0.startswith('v1.3/') else os.path.exists(os.path.join(ROOT, t0))
                if not ok:
                    fails.append(f'4: {d}:{n}: `{t}` does not exist')
            elif re.fullmatch(r'(CHANGES-[^/]+|DECISIONS-[^/]+|REGISTERS\.md|mesh\.h|RESULTS-[^/]+\.md)', t0):
                n_paths += 1
                if t0 not in names:
                    fails.append(f'4: {d}:{n}: `{t}` names no file in sim/')

print(f'sources {len(SRCS)}, non-blank source lines {n_src}; destinations {len(dests)}, non-blank lines {n_dst}: '
      f'{n_moved} moved unchanged, {len(edits)} edited, {sum(len(v) for v in added.values())} added; paths checked {n_paths}')
for f in fails:
    print('FAIL', f)
print('OK' if not fails else f'{len(fails)} failures')
sys.exit(1 if fails else 0)
