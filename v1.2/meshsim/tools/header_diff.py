#!/usr/bin/env python3
"""Compare the generated mesh.h with the draft header (wip/sw/mesh-v1.2-draft.h): every macro both define (numeric
value, or for function-like macros the value at sample arguments), every macro only one defines, and every struct
member's offset. Usage: header_diff.py GENERATED DRAFT. Exit status 0 even when they differ: the differences are a
report (WORKER4/REPORT.md), not a failure; RegDocTest asserts the ones that must agree."""
import re, subprocess, sys, tempfile, os
gen, draft = sys.argv[1], sys.argv[2]
def macros(h):
    out = subprocess.run(['gcc', '-E', '-dM', '-x', 'c', '-include', h, '/dev/null'], capture_output=True, text=True, check=True).stdout
    base = subprocess.run(['gcc', '-E', '-dM', '-x', 'c', '/dev/null'], capture_output=True, text=True, check=True).stdout
    skip = set(re.findall(r'#define (\w+)', base))
    m = {}
    for l in out.splitlines():
        r = re.match(r'#define (\w+)(\([^)]*\))? ?(.*)', l)
        if r and r.group(1) not in skip and not r.group(1).startswith('__') and not r.group(1).startswith('_STDINT') and not r.group(1).startswith('INT') and not r.group(1).startswith('UINT') and not r.group(1).startswith('WCHAR') and not r.group(1).startswith('SIG_') and not r.group(1).startswith('PTRDIFF') and not r.group(1).startswith('SIZE_MAX') and not r.group(1).startswith('WINT'):
            m[r.group(1)] = (r.group(2), r.group(3))
    return m
G, D = macros(gen), macros(draft)
skipnames = {'MESH_H', 'MESH_V12_DRAFT_H', 'MESH_RO', 'MESH_WO', 'MESH_RW', 'PERIPHERAL_STD_MEMBERS', 'tile', 'peripheral', 'replayer', 'cc1', 'tables', 'COMMAND', 'REPLY', 'MESH_H_VERSION', 'INTPTR_MAX', 'INTPTR_MIN', 'UINTPTR_MAX'}
both = sorted(n for n in set(G) & set(D) if n not in skipnames)
def args(params):
    k = len([p for p in params.strip('()').split(',') if p.strip()])
    return '(' + ', '.join(str(x) for x in [3, 5, 2, 1, 7][:k]) + ')'
def values(h, names):
    src = '#include <stdio.h>\n#include "%s"\nint main(void){\n' % os.path.abspath(h)
    for n in names:
        p = (G.get(n) or D.get(n))[0]
        e = n + (args(p) if p else '')
        src += '  printf("%s %%llx\\n", (unsigned long long)(%s));\n' % (n, e)
    src += 'return 0;}\n'
    d = tempfile.mkdtemp(); c = os.path.join(d, 't.c'); x = os.path.join(d, 't')
    open(c, 'w').write(src)
    r = subprocess.run(['gcc', '-w', '-o', x, c], capture_output=True, text=True)
    if r.returncode: return None, r.stderr
    return dict(l.split() for l in subprocess.run([x], capture_output=True, text=True).stdout.splitlines()), None
# keep only names whose values compile in both
ok = []
for n in both:
    vg, e1 = values(gen, [n]); vd, e2 = values(draft, [n])
    if vg and vd: ok.append((n, vg[n], vd[n]))
    else: print('UNCOMPARABLE', n)
diff = [(n, a, b) for n, a, b in ok if a != b]
print('macros in both: %d, compared: %d, differing: %d' % (len(both), len(ok), len(diff)))
for n, a, b in diff: print('  DIFFERS %-22s generated 0x%s, draft 0x%s' % (n, a, b))
for n in sorted(set(D) - set(G) - skipnames): print('  ONLY IN DRAFT', n)
for n in sorted(set(G) - set(D) - skipnames): print('  ONLY GENERATED', n)
# struct members
def members(h):
    t = open(h).read(); out = []
    for body, name in re.findall(r'typedef struct \{(.*?)\} (\w+);', t, re.S):
        body = body.replace('PERIPHERAL_STD_MEMBERS;', ' '.join('MESH_X %s;' % w for w in ['PARAMS','STATUS','ERRORS','PULSE_RUN','PULSE_DMA','EVENTS','EVENTS_PEEK','EVENTS_MASK','DMA_HDR','DMA_ADDR']))
        body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
        for decl in re.findall(r'(?:MESH_\w+|uint32_t)\s+([^;]+);', body):
            for v in decl.split(','):
                v = v.strip(); v = re.sub(r'\[.*', '', v)
                if v and not v.startswith('_'): out.append((name, v))
        for u in re.findall(r'union \{[^}]*\}', body):
            for v in re.findall(r'MESH_\w+ (\w+);', u): out.append((name, v))
    return sorted(set(out))
mg, md = members(gen), members(draft)
src = '#include <stdio.h>\n#include <stddef.h>\n#include "%s"\nint main(void){\n'
body = ''.join('  printf("%s.%s %%zx\\n", offsetof(%s, %s));\n' % (s, m, s, m) for s, m in sorted(set(mg) & set(md)))
res = {}
for h in (gen, draft):
    d = tempfile.mkdtemp(); c = os.path.join(d, 't.c'); x = os.path.join(d, 't')
    open(c, 'w').write((src % os.path.abspath(h)) + body + 'return 0;}\n')
    r = subprocess.run(['gcc', '-w', '-o', x, c], capture_output=True, text=True)
    if r.returncode: print(r.stderr[:2000]); sys.exit(1)
    res[h] = dict(l.split() for l in subprocess.run([x], capture_output=True, text=True).stdout.splitlines())
nd = [k for k in res[gen] if res[gen][k] != res[draft][k]]
print('struct members in both: %d, differing offsets: %d' % (len(res[gen]), len(nd)))
for k in nd: print('  OFFSET DIFFERS', k, res[gen][k], res[draft][k])
for s, m in sorted(set(md) - set(mg)): print('  MEMBER ONLY IN DRAFT', s, m)
for s, m in sorted(set(mg) - set(md)): print('  MEMBER ONLY GENERATED', s, m)
