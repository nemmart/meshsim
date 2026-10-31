# RTL drawing helpers shared by figures 2.2, 2.5, 3.3, 3.4, 4.3, 7.3 and 8.1, and by the interface tables of 5.1, 5.2, 6.3, 6.4:
# gates, mux, clocked register, frame, label, table, waveform. Figures 2.3, 2.4, 3.2, 6.2, 7.2 and 7.5 carry their own copies.
from common import *

CTL = DASH["ctrl"]
RED, RED_L = "#C0392B", "#FBE9E7"


class R:
    def __init__(self, f):
        self.f = f

    def dot(self, x, y, c=INK):
        self.f.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{c}"/>')

    def bubble(self, x, y, c=INK):
        self.f.raw(f'<circle cx="{x - 4:.1f}" cy="{y:.1f}" r="4" fill="white" stroke="{c}" stroke-width="1.3"/>')

    def gate_and(self, x, yc, n, label=None, c=INK, w=36):
        h = max(28, 14 * n + 6); y0, y1 = yc - h / 2, yc + h / 2
        self.f.raw(f'<path d="M{x},{y0} L{x + w - h / 2},{y0} A{h / 2},{h / 2} 0 0 1 {x + w - h / 2},{y1} L{x},{y1} Z" '
                   f'fill="white" stroke="{c}" stroke-width="1.4"/>')
        if label:
            self.f.text(x + w / 2 - 4, y0 - 4, label, 10, "middle", fill=MUTED)
        return [yc + (i - (n - 1) / 2) * 14 for i in range(n)]

    def gate_or(self, x, yc, n, label=None, c=INK, w=38):
        h = max(28, 14 * n + 6); y0, y1 = yc - h / 2, yc + h / 2
        self.f.raw(f'<path d="M{x},{y0} Q{x + w * 0.55},{y0} {x + w},{yc} Q{x + w * 0.55},{y1} {x},{y1} Q{x + 9},{yc} {x},{y0} Z" '
                   f'fill="white" stroke="{c}" stroke-width="1.4"/>')
        if label:
            self.f.text(x + w / 2, y0 - 4, label, 10, "middle", fill=MUTED)
        return [yc + (i - (n - 1) / 2) * 14 for i in range(n)]

    def mux(self, x, y, h, w=30, labels=("0", "1")):
        self.f.raw(f'<polygon points="{x},{y} {x + w},{y + 10} {x + w},{y + h - 10} {x},{y + h}" fill="white" stroke="{INK}" stroke-width="1.4"/>')
        self.f.text(x + 7, y + h * 0.25 + 4, labels[0], 10, "start", "bold")
        self.f.text(x + 7, y + h * 0.75 + 4, labels[1], 10, "start", "bold")
        return (x + w, y + h / 2), y + h * 0.25, y + h * 0.75

    def reg(self, x, y, w, h, title, body=(), fill=SAND, stroke=INK, size=10.5, tsize=11.5):
        self.f.box(x, y, w, h, title, body, fill=fill, stroke=stroke, sw=1.5, size=size, tsize=tsize)
        cx = x + 16
        self.f.raw(f'<polyline points="{cx - 6},{y + h} {cx},{y + h - 8} {cx + 6},{y + h}" fill="none" stroke="{stroke}" stroke-width="1.2"/>')

    def frame(self, x, y, w, h, title=None):
        self.f.rect(x, y, w, h, "white", GREY, 1, rx=10)
        if title:
            self.f.text(x + 14, y + 24, title, 14, "start", "bold")

    def lbl(self, x, y, s, anchor="end", c=INK, size=10.5, weight="normal"):
        self.f.text(x, y + 4, s, size, anchor, weight, fill=c)

    def table(self, x0, y0, cxs, rows, rh=21, size=10.5, bold_col=0, fills=None):
        for i, row in enumerate(rows):
            y = y0 + i * rh
            for c in range(len(cxs) - 1):
                fill = BOX if i == 0 else ((fills or {}).get(i, "white"))
                self.f.rect(cxs[c], y, cxs[c + 1] - cxs[c], rh, fill, EDGE, 0.8, rx=0)
                self.f.text(cxs[c] + 6, y + rh / 2 + 4, row[c], size, "start", "bold" if (i == 0 or c == bold_col) else "normal")
        return y0 + len(rows) * rh

    def wave(self, x0, y0, LX, cw, ncyc, rows, start_label="t"):
        """rows: (label, kind 'bus'|'lvl', data, colour). Returns the y below the last row."""
        f = self.f
        for k in range(ncyc):
            cx = LX + k * cw
            f.text(cx + cw / 2, y0 + 12, start_label if k == 0 else f"{start_label}+{k}", 10.5, "middle", "bold", fill=MUTED)
            f.raw(f'<line x1="{cx}" y1="{y0 + 18}" x2="{cx}" y2="{y0 + 18 + 26 * len(rows)}" stroke="#ECEAE3" stroke-width="1"/>')
        for r_, (label, kind, data, c) in enumerate(rows):
            y = y0 + 20 + r_ * 26
            f.text(x0, y + 15, label, 10.5, "start", "bold")
            hi, lo = y + 4, y + 20
            if kind == "bus":
                for k, v in enumerate(data):
                    xa, xb = LX + k * cw, LX + (k + 1) * cw
                    f.raw(f'<polygon points="{xa},{y + 12} {xa + 4},{hi} {xb - 4},{hi} {xb},{y + 12} {xb - 4},{lo} {xa + 4},{lo}" '
                          f'fill="{c if v else "white"}" stroke="{EDGE}" stroke-width="1"/>')
                    f.text((xa + xb) / 2, y + 16, v, 10, "middle")
            else:
                pts = []
                for k, v in enumerate(data):
                    yy = hi if v else lo; xa = LX + k * cw
                    if pts:
                        pts.append((xa, pts[-1][1]))
                    pts += [(xa, yy), (xa + cw, yy)]
                f.line(pts, c, 1.8, end=False)
        return y0 + 20 + len(rows) * 26

    def notes(self, y, items, width=1230):
        for n in items:
            for j, s in enumerate(wrap(n, width, 11.5)):
                self.f.text(30 + (12 if j else 0), y, ("• " if j == 0 else "") + s, 11.5, "start")
                y += 15
            y += 4
        return y

    def legend(self, y, items):
        x = 30
        for kind, c, s in items:
            if kind == "dot":
                self.f.raw(f'<circle cx="{x + 4}" cy="{y}" r="3.2" fill="{c}"/>'); x += 14
            else:
                self.f.line([(x, y), (x + 40, y)], c, 1.8, end=True, dash=(CTL if kind == "ctrl" else (DASH["write"] if kind == "write" else None))); x += 48
            self.f.text(x, y + 4, s, 11.5, "start", fill=MUTED)
            x += 9 * len(s) * 0.62 + 30
        return y


def finish(f, r, y, caption, source, legend=None):
    LY = y + 14
    r.legend(LY, legend or [("line", INK, "datapath"), ("ctrl", ORANGE, "control"), ("line", RED, "condition"), ("dot", INK, "junction (crossings without a dot don't connect); wedge: clocked register")])
    f.h = int(LY + 96)
    f.caption(caption, y=f.h - 54, source=source)
