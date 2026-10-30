#!/usr/bin/env python3
"""Fill RECIPES.tmpl.md's @@name file@@ lines with the RECIPE regions of the check sources -> wip/sw/RECIPES.md.
check_book.py confirms afterwards that every block in the book equals its region."""
import re, sys, os
from regions import region, here
out = []
for line in open(os.path.join(here, 'RECIPES.tmpl.md')).read().split('\n'):
    m = re.fullmatch(r'@@(\S+) (\S+)@@', line)
    if m:
        name, f = m.groups()
        lang = 'asm' if f.endswith('.S') else 'c'
        out.append('<!-- recipe: %s (%s) -->\n```%s\n%s\n```' % (name, f, lang, region(f, name)))
    else:
        out.append(line)
open(os.path.join(here, '../../wip/sw/RECIPES.md'), 'w').write('\n'.join(out))
print('wrote wip/sw/RECIPES.md')
