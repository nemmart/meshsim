import re, sys
s = open(sys.argv[1]).read()
s = re.sub(r'<header id="title-block-header">.*?</header>', '', s, flags=re.S)
items = [(int(m.group(1)), m.group(2), re.sub(r'\s+', ' ', m.group(3)))
         for m in re.finditer(r'<h([23]) id="([^"]+)"[^>]*>(.*?)</h\1>', s, re.S)]
out, sub = ['<nav id="TOC"><h2>Contents</h2><ul>'], False
for lvl, i, t in items:
    if lvl == 2:
        if sub: out.append('</ul></li>'); sub = False
        elif len(out) > 1: out.append('</li>')
        out.append(f'<li><a href="#{i}">{t}</a>')
    else:
        if not sub: out.append('<ul>'); sub = True
        out.append(f'<li><a href="#{i}">{t}</a></li>')
out += ['</ul></li>' if sub else '</li>', '</ul></nav>']
i = s.index('<h2 id="0-about-this-document"')
open(sys.argv[2], 'w').write(s[:i] + '\n'.join(out) + s[i:])
