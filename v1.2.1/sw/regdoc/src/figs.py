# Replace <img src="*.svg"> with a high-resolution PNG rendered by cairosvg (WeasyPrint ignores SVG marker orientation,
# so every arrowhead would point right). Paths resolve against the Markdown file's directory.
import re, sys, os, cairosvg
html_in, html_out, base, tmp = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
s = open(html_in).read()
def png(m):
    src = m.group(1)
    path = os.path.normpath(os.path.join(base, src))
    out = os.path.join(tmp, os.path.basename(path)[:-4] + ".png")
    cairosvg.svg2png(url=path, write_to=out, output_width=3900)
    return m.group(0).replace(src, "file://" + out)
s = re.sub(r'<img src="([^"]+\.svg)"', png, s)
open(html_out, "w").write(s)
