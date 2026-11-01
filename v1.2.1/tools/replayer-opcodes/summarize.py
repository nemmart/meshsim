#!/usr/bin/env python3
"""Turn enum's raw output into summary.txt (per row opcode: last-row status and slot forms with
their source instructions), and grep the tracer for static emit sites into sites.json.
Usage: python3 summarize.py summary enum.txt > summary.txt
       python3 summarize.py sites <tracer>/tracer/src/tracer > sites.json"""
import sys, re, collections, glob, json
def summary(path):
    d = collections.OrderedDict(); cur = None
    for l in open(path):
        if l.startswith('== '): cur = l[3:].strip(); d[cur] = []; continue
        m = re.match(r'\s+(.*?) \| src=(\S+)(.*?) \| (\w+)\s+x(\d+)', l)
        if m: d[cur].append((m.group(1), m.group(2), m.group(4)))
    for op, rows in d.items():
        forms = collections.OrderedDict(); lasts = set(); lastsrc = collections.defaultdict(set)
        for f, src, last in rows:
            forms.setdefault(f, set()).add(src); lasts.add(last); lastsrc[last].add(src)
        L = 'always' if lasts == {'LAST'} else 'never' if lasts == {'notlast'} else 'sometimes'
        extra = f"  last in: {', '.join(sorted(lastsrc['LAST']))}" if L == 'sometimes' else ''
        print(f"{op}  [last: {L}]{extra}")
        for f, srcs in forms.items(): print(f"    {f}: {', '.join(sorted(srcs))}")
def sites(root):
    names = [m.group(2) for m in re.finditer(r'HANDLE_(INSTR|VIRTUAL)\((\w+)\)', open(root + '/isa/riscv.inc').read()) if m.group(2) != 'op_code']
    out = collections.defaultdict(list)
    for f in [root + '/isa/virtual.cpp'] + sorted(glob.glob(root + '/inlines/*')):
        for i, l in enumerate(open(f).read().split('\n'), 1):
            if not ('emit' in l or 'expand_load' in l or 'expand_narrow_store' in l or '?' in l): continue
            for m in re.finditer(r'\b(?:isa::)?(\w+)\b', l):
                if m.group(1) in names: out[m.group(1)].append(f"{f.split('/')[-1]}:{i}")
    print(json.dumps({k: sorted(set(v), key=lambda s: (s.split(':')[0], int(s.split(':')[1]))) for k, v in out.items()}))
if __name__ == '__main__':
    {'summary': summary, 'sites': sites}[sys.argv[1]](sys.argv[2])
