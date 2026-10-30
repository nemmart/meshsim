#!/usr/bin/env python3
"""Every code block in wip/sw/RECIPES.md preceded by <!-- recipe: name (file) --> equals that region of the file here."""
import re, os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
book = open(os.path.join(here, '../RECIPES.md')).read()
from regions import region  # noqa: E402
bad = 0
blocks = re.findall(r'<!-- recipe: (\S+) \((\S+)\) -->\n```\w*\n(.*?)\n```', book, re.S)
for name, f, code in blocks:
    if code != region(f, name):
        print('differs:', name, f); bad += 1
print('%d blocks checked, %d differ' % (len(blocks), bad))
sys.exit(1 if bad else 0)
