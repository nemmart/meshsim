# Shared helpers for the introduction's figures (stdlib only).
# A copy of diagrams/common.py (palette, fonts, primitives) with save() writing into ../svg/
# and a few helpers the introduction's figures share (cells, tile colours).
import html

FONT = "Helvetica, Arial, 'Liberation Sans', sans-serif"
MONO = "'Courier New', 'Liberation Mono', monospace"

INK = "#444441"      # text and plain arrows
MUTED = "#6b6a64"    # secondary text
EDGE = "#888780"     # box outlines
GREY = "#B4B2A9"
BOX = "#F1EFE8"      # neutral box fill
GREEN, GREEN_L = "#1D9E75", "#E1F5EE"      # SRAM read queue
BLUE, BLUE_L = "#3B6EA5", "#E3ECF6"        # SRAM write queue
ORANGE, ORANGE_L = "#D85A30", "#FAECE7"    # control (C&C nets, wake, done)
PURPLE, PURPLE_L = "#7F77DD", "#EEEDFE"    # controller and its memories
SAND = "#FAEEDA"
OPEN_C = "#B5651D"                          # 'open' marker colour

DASH = {"read": None, "write": "9 3", "ctrl": "4 3", "proposed": "6 4", "open": "2 3"}


def esc(s):
    return html.escape(str(s), quote=False)


class Fig:
    def __init__(self, w, h, title, subtitle=None):
        self.w, self.h = w, h
        self.o = []
        self.colours = set()
        self.text(24, 34, title, 18, "start", "bold")
        if subtitle:
            self.text(24, 56, subtitle, 12.5, "start", fill=MUTED)

    # ---- primitives -------------------------------------------------------
    def raw(self, s):
        self.o.append(s)

    def text(self, x, y, s, size=12, anchor="middle", weight="normal", fill=INK,
             italic=False, mono=False):
        st = ' font-style="italic"' if italic else ""
        fam = f' font-family="{MONO}"' if mono else ""
        self.o.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" text-anchor="{anchor}" '
                      f'font-weight="{weight}" fill="{fill}"{st}{fam}>{esc(s)}</text>')

    def lines(self, x, y, items, size=12, anchor="start", lh=None, fill=INK, weight="normal"):
        """items: list of str or (str, dict(options))"""
        lh = lh or size * 1.32
        for i, it in enumerate(items):
            if isinstance(it, tuple):
                s, kw = it
                kw = dict(dict(size=size, anchor=anchor, fill=fill, weight=weight), **kw)
            else:
                s, kw = it, dict(size=size, anchor=anchor, fill=fill, weight=weight)
            self.text(x, y + i * lh, s, **kw)
        return y + len(items) * lh

    def rect(self, x, y, w, h, fill=BOX, stroke=EDGE, sw=1, rx=6, dash=None, opacity=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        op = f' fill-opacity="{opacity}"' if opacity is not None else ""
        self.o.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{rx}" '
                      f'fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}{op}/>')

    def box(self, x, y, w, h, title=None, body=(), fill=BOX, stroke=EDGE, sw=1, dash=None,
            size=12, tsize=13, align="middle", valign="center", pad=10):
        self.rect(x, y, w, h, fill, stroke, sw, dash=dash)
        lh = size * 1.32
        n = (1 if title else 0) + len(body)
        total = (tsize * 1.35 if title else 0) + len(body) * lh
        if valign == "center":
            cy = y + (h - total) / 2 + (tsize if title else size) * 0.95
        else:
            cy = y + pad + (tsize if title else size) * 0.8
        cx = x + w / 2 if align == "middle" else x + pad
        if title:
            self.text(cx, cy, title, tsize, align, "bold")
            cy += tsize * 1.35
        for s in body:
            if isinstance(s, tuple):
                st, kw = s
                kw = dict(dict(size=size, anchor=align), **kw)
                self.text(cx, cy, st, **kw)
            else:
                self.text(cx, cy, s, size, align)
            cy += lh
        return n

    # Arrowheads are drawn as explicit triangles, not SVG markers: PDF renderers differ in how they orient
    # markers (WeasyPrint draws them all pointing one way), and a polygon looks the same everywhere.
    def _head(self, x0, y0, x1, y1, c, w):
        import math
        d = math.hypot(x1 - x0, y1 - y0) or 1.0
        ux, uy = (x1 - x0) / d, (y1 - y0) / d
        L, half = 5.5 + 1.6 * w, 2.6 + 0.9 * w
        bx, by = x1 - ux * L, y1 - uy * L
        pts = [(x1, y1), (bx - uy * half, by + ux * half), (bx + uy * half, by - ux * half)]
        self.o.append('<polygon points="' + " ".join(f"{x:.1f},{y:.1f}" for x, y in pts) + f'" fill="{c}"/>')
        return bx, by

    def line(self, pts, c=INK, w=1.5, end=True, start=False, dash=None):
        if len(pts) == 2 and not isinstance(pts[0], (tuple, list)):
            raise ValueError("pts must be a list of points")
        pts = [tuple(p) for p in pts]
        if end:
            pts[-1] = self._head(*pts[-2], *pts[-1], c, w)
        if start:
            pts[0] = self._head(*pts[1], *pts[0], c, w)
        p = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.o.append(f'<polyline points="{p}" fill="none" stroke="{c}" stroke-width="{w}" '
                      f'stroke-linejoin="round"{d}/>')

    def path(self, d, c=INK, w=1.5, end=True, dash=None, fill="none"):
        """d: an M/L/C path in absolute coordinates; the head follows the last segment's direction."""
        import re
        if end:
            nums = [float(v) for v in re.findall(r"-?\d+(?:\.\d+)?", d)]
            x0, y0, x1, y1 = nums[-4:]
            self._head(x0, y0, x1, y1, c, w)
        da = f' stroke-dasharray="{dash}"' if dash else ""
        self.o.append(f'<path d="{d}" fill="{fill}" stroke="{c}" stroke-width="{w}"{da}/>')

    def tag(self, x, y, kind, anchor="start"):
        """Small status pill: kind in {'proposed', 'open', 'likely to change', ...}."""
        label = kind
        col = OPEN_C if kind.startswith("open") else (BLUE if "decided" in kind else PURPLE)
        w = 7 + 6.2 * len(label)
        x0 = x if anchor == "start" else (x - w if anchor == "end" else x - w / 2)
        self.o.append(f'<rect x="{x0:.1f}" y="{y - 11:.1f}" width="{w:.1f}" height="15" rx="7.5" '
                      f'fill="white" stroke="{col}" stroke-width="1"/>')
        self.text(x0 + w / 2, y, label, 10.5, "middle", fill=col, italic=True)
        return w

    def legend_status(self, x, y):
        """Explains the proposed / open markings."""
        self.rect(x, y - 12, 34, 18, "white", PURPLE, 1.2, dash=DASH["proposed"], rx=4)
        self.text(x + 42, y + 1, "proposed: suggested, not yet ruled on", 11.5, "start", fill=MUTED)
        self.rect(x + 300, y - 12, 34, 18, "white", OPEN_C, 1.4, dash=DASH["open"], rx=4)
        self.text(x + 342, y + 1, "open: still to be settled", 11.5, "start", fill=MUTED)

    def caption(self, lines, y=None, source=None):
        y = y or self.h - 18 - 16 * (len(lines) - 1) - (16 if source else 0)
        self.o.append(f'<line x1="24" y1="{y - 22}" x2="{self.w - 24}" y2="{y - 22}" stroke="{GREY}" stroke-width="0.8"/>')
        for i, s in enumerate(lines):
            self.text(24, y + i * 16, s, 12.5, "start", fill=INK)
        if source:
            self.text(24, y + len(lines) * 16, source, 11, "start", fill=MUTED, italic=True)

    # ---- output -----------------------------------------------------------
    def svg(self):
        defs = []
        for c in []:
            # orient="auto" with a separate reversed marker for line starts: PDF renderers that don't know
            # "auto-start-reverse" (WeasyPrint) would otherwise draw every arrowhead pointing one way
            defs.append(f'<marker id="m{c[1:]}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" '
                        f'markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="{c}"/></marker>')
            defs.append(f'<marker id="s{c[1:]}" viewBox="0 0 10 10" refX="1" refY="5" markerWidth="7" '
                        f'markerHeight="7" orient="auto"><path d="M10,0 L0,5 L10,10 z" fill="{c}"/></marker>')
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                f'viewBox="0 0 {self.w} {self.h}" font-family="{FONT}">')
        return "\n".join([head, "<defs>" + "".join(defs) + "</defs>",
                          f'<rect width="{self.w}" height="{self.h}" fill="white"/>'] + self.o + ["</svg>"])

    def save(self, name):
        import os
        here = os.path.dirname(os.path.abspath(__file__))
        out = os.path.join(here, "..", "svg")
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, name), "w") as f:
            f.write(self.svg())
        print("wrote", name)


def bitbar(f, x, y, fields, scale, h=40, label_size=12, show_bits=True, msb_first=True):
    """Draw a register/packet layout. fields: list of (name, bits, fill, extra) left = most significant.
    Returns total width."""
    cx = x
    for name, bits, fill, *rest in fields:
        w = bits * scale
        dash = rest[0] if rest else None
        stroke = PURPLE if dash == DASH["proposed"] else (OPEN_C if dash == DASH["open"] else EDGE)
        f.rect(cx, y, w, h, fill, stroke, 1.2 if dash else 1, rx=0, dash=dash)
        if w > 34:
            f.text(cx + w / 2, y + h / 2 - 2, name, label_size, "middle", "bold")
            f.text(cx + w / 2, y + h / 2 + 13, f"{bits}", 11, "middle", fill=MUTED)
        else:
            f.text(cx + w / 2, y + h / 2 + 4, f"{bits}", 11, "middle", fill=MUTED)
        cx += w
    return cx - x


def wrap(s, width_px, size=12):
    """Greedy word wrap using an average glyph width (Helvetica-like ≈ 0.52 em)."""
    maxc = max(4, int(width_px / (0.52 * size)))
    out, cur = [], ""
    for w in s.split(" "):
        if cur and len(cur) + 1 + len(w) > maxc:
            out.append(cur)
            cur = w
        else:
            cur = (cur + " " + w) if cur else w
    if cur:
        out.append(cur)
    return out


# ---- the introduction's additions -------------------------------------------
# One colour pair per destination tile in the toy examples (fill, edge), from the palette above.
TILE = [(BLUE_L, BLUE), (ORANGE_L, ORANGE), (GREEN_L, GREEN), (PURPLE_L, PURPLE)]
YELLOW_L, YELLOW = "#FFF4CC", "#C99A06"   # highlight (the digit a pass sorts by, a close-up)


def cell(f, x, y, w, h, text, fill=BOX, stroke=EDGE, size=13, weight="normal", sub=None, fill_text=INK,
         dash=None, sw=1.1):
    """A record cell: main text centred, optional small grey text at the right (the ordinal)."""
    f.rect(x, y, w, h, fill, stroke, sw, rx=3, dash=dash)
    if sub is None:
        f.text(x + w / 2, y + h / 2 + size * 0.36, text, size, "middle", weight, fill=fill_text)
    else:
        f.text(x + w * 0.42, y + h / 2 + size * 0.36, text, size, "end", weight, fill=fill_text)
        f.text(x + w * 0.42 + 3, y + h / 2 + size * 0.36, "|", size - 1, "start", fill=GREY)
        f.text(x + w * 0.42 + 10, y + h / 2 + size * 0.36, sub, size - 1, "start", fill=MUTED)


def arrow(f, x0, y0, x1, y1, c=INK, w=1.6, dash=None):
    f.line([(x0, y0), (x1, y1)], c, w, dash=dash)
