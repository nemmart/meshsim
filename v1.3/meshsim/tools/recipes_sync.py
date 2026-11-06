#!/usr/bin/env python3
"""Rewrite every code block in sw/RECIPES.md from the RECIPE region it names (the snippet check's sources:
sw/tests/rcp*.{c,h}, sw/tests/selftest.h, src/CnC.java). RecipeTest still checks the two agree."""
import re, os, sys, textwrap
root = sys.argv[1] if len(sys.argv) > 1 else '.'
src = {}
files = [os.path.join(root, 'sw/tests', f) for f in sorted(os.listdir(os.path.join(root, 'sw/tests')))
         if (f.startswith('rcp') and f[-2:] in ('.c', '.h')) or f == 'selftest.h'] + [os.path.join(root, 'src/CnC.java')]
for f in files:
    for m in re.finditer(r'/\* RECIPE (\S+) \*/\n(.*?)\n[ \t]*/\* END \*/', open(f).read(), re.S):
        src[m.group(1)] = textwrap.dedent(m.group(2)).rstrip()
p = os.path.join(root, '../sw/RECIPES.md')
if not os.path.exists(p): p = os.path.join(root, 'sw/RECIPES.md')
md = open(p).read()
def rep(m):
    n = m.group(1)
    if n not in src: sys.exit('no region ' + n)
    return '<!-- recipe: %s (%s) -->\n```%s\n%s\n```' % (n, m.group(2), m.group(3), src[n])
md = re.sub(r'<!-- recipe: (\S+) \((\S+)\) -->\s*```(c|java)\n.*?```', rep, md, flags=re.S)
open(p, 'w').write(md)
quoted = set(re.findall(r'<!-- recipe: (\S+) ', md))
missing = [n for n in src if n not in quoted]
print('synced; regions not quoted:', missing)
