#!/usr/bin/env python3
"""List the units of v1.3's spec that the restructuring places, with their line ranges.

Run from the repository's root:  python3 sim/spec/check/inventory.py [units|items|rows|files]

  units  every heading, "in brief" block and companion bullet of machine-spec.md, and every heading of
         replayer-opcodes.md and schedule.md, as  file  first-last  kind  label
  rows   every table row and bullet of sections 5, 8.1, 8.11, 9, 10, 11 and 12 and of the companion list,
         with whether it names the replayer or the Tracer (the Jolt-specific units)
  items  section 13's items: number, line range, group, title, and every status-table row that names it
  files  every source file with its line count

Reads only v1.3/ (frozen); writes nothing.
"""
import re
import sys

SPEC = 'v1.3/spec/machine-spec.md'
SOURCES = ['v1.3/spec/machine-spec.md', 'v1.3/spec/replayer-opcodes.md', 'v1.3/spec/schedule.md'] + \
    [f'v1.3/spec/CHANGES-{n}.md' for n in ('to-v1.25', 'v1.2', 'v1.25', 'v1.25-to-v1.26.1', 'v1.26',
                                         'v1.26.1', 'v1.26.1-to-v1.3', 'v1.3')] + \
    [f'v1.3/planning/DECISIONS-v1.{n}.md' for n in ('2', '25', '26', '3')]
JOLT = re.compile(r'replayer|Tracer|tracer|4\.19|4\.21|8\.16|jolt|Jolt|run log|expansion')


def lines(path):
    with open(path, encoding='utf-8') as f:
        text = f.read()
    out = text.split('\n')
    if out and out[-1] == '':
        out.pop()
    return out


def sections(L):
    """(line, level, number, title) for every heading."""
    out = []
    for i, l in enumerate(L, 1):
        m = re.match(r'^(#+) (.*)$', l)
        if m:
            num = re.match(r'([0-9]+(?:\.[0-9]+)*|[A-Z])\.?\s', m.group(2) + ' ')
            out.append((i, len(m.group(1)), num.group(1) if num else '', m.group(2)))
    return out


def ranges(L):
    s = sections(L)
    for k, (i, lev, num, title) in enumerate(s):
        end = len(L)
        for j, lev2, _, _ in s[k + 1:]:
            if lev2 <= lev:
                end = j - 1
                break
        yield i, end, lev, num, title


def short(s, n=110):
    return s if len(s) <= n else s[:n] + '…'


def cmd_files():
    for p in SOURCES:
        print(f'{len(lines(p)):6d}  {p}')


def cmd_units():
    L = lines(SPEC)
    # the preamble: companion bullets and the "in brief" blocks
    first = next(i for i, *_ in ranges(L) if i > 1)
    i = 1
    while i < first:
        l = L[i - 1]
        if l.startswith('- `') and i < 20:
            print(f'{SPEC}\t{i}-{i}\tcompanion\t{short(l, 80)}')
        m = re.match(r'^\*\*(v[0-9.]+ in brief)\*\*', l)
        if m:
            j = i
            while j < first and L[j] != '':
                j += 1
            print(f'{SPEC}\t{i}-{j}\tin-brief\t{m.group(1)}')
        i += 1
    for path in SOURCES[:3]:
        for a, b, lev, num, title in ranges(lines(path)):
            print(f'{path}\t{a}-{b}\th{lev}\t{short(title, 90)}')


def cmd_rows():
    L = lines(SPEC)
    want = {'5', '8.1', '8.11', '9', '10', '11', '12'}
    spans = [(1, 19, 'preamble')] + [(a, b, num) for a, b, lev, num, _ in ranges(L) if num in want]
    for a, b, tag in spans:
        for i in range(a, b + 1):
            l = L[i - 1]
            if (l.startswith('|') and not l.startswith('|---')) or re.match(r'^\s*(- |[0-9]+\. )', l):
                j = 'JOLT' if JOLT.search(l) else ''
                print(f'{tag}\t{i}\t{j}\t{short(l, 100)}')


def cmd_items():
    L = lines(SPEC)
    a, b = next((a, b) for a, b, lev, num, _ in ranges(L) if num == '13')
    tables, group, items = {}, '', []
    table = None
    for i in range(a, b + 1):
        l = L[i - 1]
        m = re.match(r'^\*\*(v[0-9.]+): the status of', l)
        if m:
            table = m.group(1)
            continue
        if table and l.startswith('| ') and not l.startswith('| item'):
            c = [x.strip() for x in l.strip('|').split('|')]
            tables.setdefault(int(c[0]), []).append((table, c[1], i, c[2]))
            continue
        if l.startswith('**') and l.endswith('**') or re.match(r'^\*\*[^*]+\*\* \(', l):
            group, table = l.strip('*'), None
            continue
        m = re.match(r'^([0-9]+)\. (.*)$', l)
        if m:
            t = re.match(r'(?:\*\([^)]*\)\*\s*)?\*\*(.*?)\*\*', m.group(2))
            items.append([int(m.group(1)), i, i, group, t.group(1) if t else short(m.group(2), 60)])
        elif items and l.startswith('    '):
            items[-1][2] = i
    for n, s, e, g, t in items:
        print(f'{n}\t{s}-{e}\t{g}\t{t}')
        for v, st, ln, how in tables.get(n, []):
            print(f'\t{v}\t{st}\tline {ln}\t{short(how, 140)}')


if __name__ == '__main__':
    {'files': cmd_files, 'units': cmd_units, 'rows': cmd_rows, 'items': cmd_items}[
        sys.argv[1] if len(sys.argv) > 1 else 'units']()
