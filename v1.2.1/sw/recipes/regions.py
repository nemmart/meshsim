"""The RECIPE regions of the check sources: /* RECIPE name */ ... /* END */, de-indented."""
import re, os, sys
here = os.path.dirname(os.path.abspath(__file__))
def region(fname, name):
    text = open(os.path.join(here, fname)).read()
    m = re.search(r'/\* RECIPE %s \*/\n(.*?)\n\s*/\* END \*/' % re.escape(name), text, re.S)
    if not m: sys.exit('no region %s in %s' % (name, fname))
    lines = m.group(1).split('\n')
    ind = min(len(l) - len(l.lstrip()) for l in lines if l.strip())
    return '\n'.join(l[ind:] for l in lines)
