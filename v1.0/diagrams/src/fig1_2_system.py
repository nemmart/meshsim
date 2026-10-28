# Figure 1.2: the system — 64 tiles on the circulant mesh, C&C on tile 0's fifth port, global nets.
from common import *

W, H = 1300, 1032
f = Fig(W, H, "Figure 1.2. The system: 64 tiles on the circulant mesh, C&C and the global nets",
        "v1.0 (spec/machine-spec.md §2, §3, §4.13, §4.16, §6) — logical view: tile idx at column idx mod 8, row idx div 8")

TW, TH = 66, 46          # tile box
PX, PY = 96, 80          # pitch
X0, Y0 = 452, 128        # top-left tile
def tx(c): return X0 + c * PX
def ty(r): return Y0 + r * PY

# enclosure for "all 64 tiles"
EX0, EY0, EX1, EY1 = X0 - 74, Y0 - 50, tx(7) + TW + 74, ty(7) + TH + 50
f.rect(EX0, EY0, EX1 - EX0, EY1 - EY0, "white", GREY, 1, rx=12, dash="5 4")
f.text(EX1 - 10, EY1 - 10, "64 tiles", 12, "end", fill=MUTED, italic=True)

LINK = INK
# horizontal links inside rows (bidirectional)
for r in range(8):
    for c in range(7):
        y = ty(r) + TH / 2
        f.line([(tx(c) + TW + 3, y), (tx(c + 1) - 3, y)], LINK, 1.4, end=True, start=True)
# vertical links inside columns
for r in range(7):
    for c in range(8):
        x = tx(c) + TW / 2
        f.line([(x, ty(r) + TH + 3), (x, ty(r + 1) - 3)], LINK, 1.4, end=True, start=True)

# ring stubs: the ±1 ring runs row to row (7–8, 15–16, …, 63–0)
for r in range(8):
    y = ty(r) + TH / 2
    last = r * 8 + 7
    nxt = (last + 1) % 64
    xr = tx(7) + TW
    f.line([(xr + 3, y), (xr + 30, y)], LINK, 1.4, end=True, start=True)
    f.text(xr + 35, y + 4.5, f"{nxt}", 12, "start", "bold", fill=MUTED)
    first = r * 8
    prv = (first - 1) % 64
    xl = tx(0)
    f.line([(xl - 30, y - (8 if r == 0 else 0)), (xl - 3, y - (8 if r == 0 else 0))], LINK, 1.4, end=True, start=True)
    f.text(xl - 35, y + 4.5 - (8 if r == 0 else 0), f"{prv}", 12, "end", "bold", fill=MUTED)
# column wrap stubs: tile 56+m is above tile m
for c in range(8):
    x = tx(c) + TW / 2
    f.line([(x, ty(0) - 26), (x, ty(0) - 3)], LINK, 1.4, end=True, start=True)
    f.text(x, ty(0) - 31, f"{56 + c}", 12, "middle", "bold", fill=MUTED)
    f.line([(x, ty(7) + TH + 3), (x, ty(7) + TH + 26)], LINK, 1.4, end=True, start=True)
    f.text(x, ty(7) + TH + 41, f"{c}", 12, "middle", "bold", fill=MUTED)

# tiles
for r in range(8):
    for c in range(8):
        i = r * 8 + c
        fill = SAND if i == 0 else BOX
        f.rect(tx(c), ty(r), TW, TH, fill, EDGE, 1.3 if i == 0 else 1)
        f.text(tx(c) + TW / 2, ty(r) + TH / 2 + 6, str(i), 16, "middle", "bold")

# "3 cycles" call-outs on two links
def callout(x, y, s, anchor="start"):
    f.rect(x - (0 if anchor == "start" else 6.3 * len(s) + 10), y - 12, 6.3 * len(s) + 10, 17, "white", GREY, 0.8, rx=4)
    f.text(x + (5 if anchor == "start" else -5), y + 1, s, 11, anchor, fill=INK)
mx = (tx(3) + TW + tx(4)) / 2
f.line([(mx, ty(2) + TH / 2 - 2), (mx + 16, ty(2) + TH / 2 - 26)], MUTED, 0.8, end=False)
callout(mx + 16, ty(2) + TH / 2 - 30, "3 cycles each way")
my = (ty(4) + TH + ty(5)) / 2
f.line([(tx(6) + TW / 2 + 2, my), (tx(6) + TW / 2 + 26, my - 2)], MUTED, 0.8, end=False)
callout(tx(6) + TW / 2 + 26, my + 2, "3 cycles each way")

# ---- C&C and tile 0's fifth port ---------------------------------------------------
CX, CY, CW, CH = 30, 96, 330, 124
f.box(CX, CY, CW, CH, "C&C (address 64)",
      ["one RV32I + Zmmul + WFI controller (§6.6)", "sequences the phases: RUN/HALT,",
       "mailbox commands, READY bits, GO", "the error bit (sticky); program RAM", "16–32 KB (proposal)"],
      fill=ORANGE_L, stroke=ORANGE, size=11.5)
# fifth port: C&C <-> tile 0
p5y = ty(0) + TH - 10
f.line([(CX + CW, p5y + 0), (tx(0) - 3, p5y)], ORANGE, 2.2, end=True, start=True)
f.line([(CX + CW, p5y), (CX + CW - 1, p5y)], ORANGE, 2.2, end=False)

# memory subsystem: hangs off C&C; no mesh address
MX, MY, MW, MH = 30, 252, 232, 100
f.box(MX, MY, MW, MH, "memory subsystem",
      ["hangs off C&C, not the mesh:", "no mesh address (65 unused);", "C&C moves its data (§7)"],
      fill=BOX, stroke=INK, sw=1.4, size=11.5)
f.tag(MX + MW - 6, MY + MH + 16, "open: its link to C&C (§13 item 9)", "end")
f.line([(MX + 60, MY), (MX + 60, CY + CH + 2)], INK, 1.6, end=False)
f.text((CX + CW + tx(0) - 30) / 2, p5y - 9, "fifth port", 11.5, "middle", fill=ORANGE)
f.text((CX + CW + tx(0) - 30) / 2, p5y + 19, "src/dst 64", 11.5, "middle", fill=ORANGE)

# ---- global nets ---------------------------------------------------------------------
# verticals leave C&C's bottom; the leftmost goes deepest so nothing crosses
gy = lambda r: ty(r) + TH + (PY - TH) / 2          # between row r and r+1
nets = [
    (272, gy(7), True, ["RUN/HALT: one line to", "every tile, fixed latency", "run_latency (§6.1)"]),
    (298, gy(6), True, ["GO: one global line,", "balanced so every tile sees", "the pulse on the same cycle"]),
    (324, gy(5), False, ["READY: one line per tile,", "64 in all, equal latency", "ready_latency (§6.3)"]),
]
for x, y, into, lab in nets:
    if into:
        f.line([(x, CY + CH), (x, y), (EX0 - 2, y)], ORANGE, 2, end=True, dash=DASH["ctrl"])
    else:
        f.line([(EX0 - 2, y), (x, y), (x, CY + CH + 2)], ORANGE, 2, end=True, dash=DASH["ctrl"])
    f.lines(265, y - 18, lab, 11.5, anchor="end", fill=ORANGE, lh=15)
    f.text(EX0 + 6, y - 5, "to all 64" if into else "from all 64", 10.5, "start", fill=ORANGE)

# the error line: each tile's "TILE_CONDITIONS != 0", an OR tree, C&C's sticky error bit
ex, ey = 350, gy(4)
gx0, gx1 = 386, 414                     # OR gate, output to the left
f.line([(gx0, ey), (ex, ey), (ex, CY + CH + 2)], ORANGE, 2, end=True, dash=DASH["ctrl"])
f.path(f"M{gx1},{ey - 11} Q{gx1 - 8},{ey} {gx1},{ey + 11} Q{gx0 + 8},{ey + 11} {gx0},{ey} Q{gx0 + 8},{ey - 11} {gx1},{ey - 11} Z",
       ORANGE, 1.4, end=False, fill="white")
f.text((gx0 + gx1) / 2 + 3, ey + 3.5, "≥1", 9, "middle", "bold", fill=ORANGE)
for dy in (-6, 0, 6):
    f.line([(gx1 + 14, ey + dy), (gx1 - 3 + (2 if dy else 0), ey + dy)], ORANGE, 1.2, end=False)
f.text(gx0 - 2, ey - 16, "OR tree", 10.5, "start", fill=ORANGE)
f.text(gx0 - 2, ey + 24, "from all 64", 10.5, "start", fill=ORANGE)
f.lines(265, ey - 22, ["error: each tile's \"conditions ≠ 0\",", "ORed by a tree (no equal latency)", "into C&C's sticky error bit, which", "wakes C&C's WFI (§4.13, §6.6)"],
        11.5, anchor="end", fill=ORANGE, lh=15)

# direction key
KX, KY = 140, 868
f.rect(KX - 26, KY - 18, 52, 36, BOX, EDGE)
f.text(KX, KY + 5, "idx", 13, "middle", "bold")
for (dx, dy, s, a) in [(1, 0, "right: idx + 1", "start"), (-1, 0, "left: idx − 1", "end"),
                       (0, 1, "down: idx + 8", "middle"), (0, -1, "up: idx − 8", "middle")]:
    x1, y1 = KX + dx * 28, KY + dy * 20
    x2, y2 = KX + dx * 52, KY + dy * 40
    f.line([(x1, y1), (x2, y2)], INK, 1.4, end=True)
    tx_, ty_ = x2 + dx * 6, y2 + dy * 6 + (4 if dy == 0 else (10 if dy > 0 else -2))
    f.text(tx_, ty_, s, 11.5, a)
f.text(KX, KY - 62, "link names, all mod 64 (§2)", 11.5, "middle", fill=MUTED)

# legend / notes on the right of the key
NX = 360
notes = [
    ("Links (§3).", {"weight": "bold"}),
    "Every arrow is a bidirectional link: one 102-bit packet per cycle each way. Every link direction takes exactly",
    "3 cycles (uniform; shorter links are padded with pipeline stages). Minimum latency 3 + 4h cycles for h hops. No backpressure.",
    "The ±1 links form one ring in index order (… 7–8 …, 63–0); the ±8 links are columns with wraparound.",
    "Unicast routes: horizontal first, then vertical, from a 64-entry offset table (§4.6). Broadcast is dst 126.",
    "Order: one sender's unicasts to a tile arrive in order, and so do its broadcasts, but a unicast and a broadcast may pass each other (§3.2).",
    "Addresses: 0–63 tiles, 64 C&C, 126 broadcast, 127 none (idle packet); 65 is unused.",
]
f.lines(NX, 818, notes, 11.5, lh=16)

f.caption(["The 64 tiles are identical except tile 0, whose router has a fifth port to C&C. C&C holds the tiles in HALT or RUN,",
           "writes commands into their mailboxes over the mesh, collects one READY pulse per tile and answers with GO. Each tile's error line (any condition set)",
           "is ORed into one sticky error bit at C&C, so C&C learns at once that something is wrong; REPORT then collects the tiles' conditions (§6.6)."],
          y=H - 54, source="Source: spec/machine-spec.md v1.0, §2, §3, §4.6, §4.16, §6, §7.")
f.save("fig1_2_system.svg")
