# Figure M.1: the memory system: LPDDR, the bus driver, the cache and their consumers (a first sketch; the design
# notes of 2026-11-15). Uses the shared helpers of the main figure set, wip/diagrams/src/common.py; writes
# ../svg/fig_m1_lpddr_path.svg.
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "wip", "diagrams", "src"))
from common import Fig, DASH, EDGE, GREY, INK, MUTED, ORANGE  # noqa: E402

BH = 52                   # every box's height
GAP = 44                  # the room an arrow takes between two rows
STUB = 36                 # a station's connection to a bus
SG = 20                   # the gap between two stations
UX, UW = 100, 282         # the Updater
BW = 220                  # LPDDR and the memory controller
SW = 160                  # the bus driver
KW = 166                  # the cache
NW = 116                  # the LRU and the two queues
FW = 84                   # the free page queue, narrower still
LC, RC, XC = UX + 64, UX + 170, UX + 234   # the columns under the Updater: segment data, free pages, state
QC = UX + UW - 16         # the Updater's line to the look-up bus

# The stations under the left bus, after the Updater: (title, body, width).
LEFT = [("RW engine", ["file pages, for the tiles"], 150), ("Binder", ["not yet defined"], 120)]
GREYED = {"Binder"}        # stations drawn in grey
LIGHT = "#A09E96"          # their text
TRACER = ["Data cache", "Instruction cache", "Jump-table read"]   # the Tracer's three stations, under the right bus
TW, TG, TP = 128, 10, 12  # a Tracer station's width, the gap between two, the outline's padding

x = UX + UW + SG
places = []
for title, body, w in LEFT:
    places.append((x, w, title, body))
    x += w + SG
KX = x                    # the cache, the last station of the row before the Tracer
TX = KX + KW + 30         # the Tracer's outline
TOW = 3 * TW + 2 * TG + 2 * TP
W, H = TX + TOW + 40, 852
CX = (UX + KX) / 2        # the column above the left bus: LPDDR, the memory controller, the bus driver

f = Fig(W, H, "The memory system: the bus driver, the cache and their consumers", "A first sketch")


def bus(p, q, label=None, lx=0, ly=0):
    """A connection that carries data both ways: a two-headed arrow from p to q, with a small label beside it."""
    f.line([p, q], INK, 2, end=True, start=True)
    if label:
        f.text((p[0] + q[0]) / 2 + lx, (p[1] + q[1]) / 2 + ly, label, 11, "start" if lx else "middle", fill=MUTED)


def arrow(p, q, words=(), dx=9, anchor="start", colour=INK, dash=None):
    """A one-way arrow from p to q, with a small label of one or two lines beside its middle."""
    f.line([p, q], colour, 2, dash=dash)
    if words:
        f.lines((p[0] + q[0]) / 2 + dx, (p[1] + q[1]) / 2 - 3 - 7 * (len(words) - 2), list(words), 11, anchor,
                fill=MUTED if colour == INK else colour)


# Above the left bus, top to bottom: LPDDR, the memory controller, the bus driver.
y = 80
f.box(CX - BW / 2, y, BW, BH, "LPDDR memory", ["4 GB"])
bus((CX, y + BH), (CX, y + BH + GAP), "LPDDR pins", lx=12, ly=4)
y += BH + GAP
f.box(CX - BW / 2, y, BW, BH, "Memory controller", ["with its PHY"])
bus((CX, y + BH), (CX, y + BH + GAP), "AXI", lx=12, ly=4)
y += BH + GAP
f.box(CX - SW / 2, y, SW, BH, "Bus driver", ["the only station on AXI"])

# Two buses at one height: the left one for the bus driver and the page movers, the right one for the Tracer. The cache
# is on both, by a line up to each.
sy = y + BH + STUB        # both buses
bus((CX, y + BH), (CX, sy))
KL, KR = KX + KW / 4, KX + 3 * KW / 4   # the cache's two lines
f.line([(UX, sy), (KL, sy)], INK, 3, end=False)
f.line([(KR, sy), (TX + TOW, sy)], INK, 3, end=False)
f.text(CX + 14, sy - 9, "the left bus: the AXI width, data only", 11.5, "start", fill=MUTED)
f.text(KR + 14, sy - 9, "the right bus: the Tracer's fill bus, 1,024 bits", 11.5, "start", fill=MUTED)

y = sy + STUB             # the stations' row
UC = UX + UW / 2
bus((UC, sy), (UC, y))
f.box(UX, y, UW, BH, "Updater", ["one 4 KB page memory"])
for sx, w, title, body in places:
    if title in GREYED:   # not yet designed: the box, its text and its lines in grey
        f.line([(sx + w / 2, sy), (sx + w / 2, y)], GREY, 2, end=True, start=True)
        f.rect(sx, y, w, BH, "#F6F5F1", GREY)
        f.text(sx + w / 2, y + 23, title, 13, "middle", "bold", fill=LIGHT)
        f.text(sx + w / 2, y + 40, body[0], 12, "middle", fill=LIGHT)
    else:
        bus((sx + w / 2, sy), (sx + w / 2, y))
        f.box(sx, y, w, BH, title, body)
bus((KL, sy), (KL, y))
bus((KR, sy), (KR, y))
f.box(KX, y, KW, BH, "Cache", ["512 KB of 128 B lines,", "and the translator"], size=11.5)
f.rect(TX, y - 12, TOW, BH + 44, "none", EDGE, 1.2, dash="5 4")
f.text(TX + TOW / 2, y + BH + 22, "Tracer", 13, "middle", "bold")
for i, title in enumerate(TRACER):
    tx = TX + TP + i * (TW + TG)
    bus((tx + TW / 2, sy), (tx + TW / 2, y))
    f.box(tx, y, TW, BH, title)

# The look-up bus, narrow: the Updater, the RW engine and the Binder ask the cache where a file's pages are.
ky = y + BH + 30
f.line([(QC, ky), (KX + KW / 2, ky)], INK, 1.5, end=False)
f.line([(QC, ky), (QC, y + BH)], INK, 1.5, end=True, start=True)
f.line([(KX + KW / 2, ky), (KX + KW / 2, y + BH)], INK, 1.5, end=True, start=True)
RWX = places[0][0] + places[0][1] / 2   # the RW engine's centre: under it, left to right, its look-up line, the
RWD = 45                                # peripheral bus's line in and its packets out, RWD apart
for sx, w, title, _ in places:
    qx = sx + w / 2 - (RWD if title == "RW engine" else 0)
    f.line([(qx, ky), (qx, y + BH)], GREY if title in GREYED else INK, 1.5, end=True, start=True)
f.text(RWX + RWD + 14, ky + 16, "the look-up bus (narrow): a file, its depth and a page number in; an entry and a level back",
       11, "start", fill=MUTED)

# Under the Updater, and narrower than it. Left: the segment queue feeds the LRU, and the LRU feeds the Updater on
# three lines. Right: the free page queue feeds the Updater.
ly_ = y + BH + GAP        # the LRU's row
qy = ly_ + BH + GAP       # the queues' row
f.box(LC - NW / 2, ly_, NW, BH, "LRU")
arrow((LC - 42, ly_), (LC - 42, y + BH), ["32-bit", "address"], -8, "end")
arrow((LC, ly_), (LC, y + BH), ["64-bit", "value"], 5)
arrow((LC + 42, ly_), (LC + 42, y + BH), ["type"], 7)
f.box(LC - NW / 2, qy, NW, BH, "Segment queue", ["4 KB"], tsize=12.5)
arrow((LC, qy), (LC, ly_ + BH))
f.box(RC - FW / 2, qy, FW, BH, "Free page", [("queue", dict(size=12.5, weight="bold"))], tsize=12.5)
arrow((RC, qy), (RC, y + BH), ["page", "numbers"])

# The peripheral bus fills both queues, writes the Updater's state and goes into the RW engine; the segment queue's
# credits go to Ingress/Egress (not in this figure).
by = qy + BH + GAP
f.line([(LC - NW / 2, by), (RWX + 12, by)], INK, 3, end=False)
f.text((LC - NW / 2 + XC + 12) / 2, by + 18, "peripheral bus", 11, "middle", fill=MUTED)
arrow((RWX, by), (RWX, y + BH))
arrow((LC, by), (LC, qy + BH))
arrow((RC, by), (RC, qy + BH))
arrow((XC, by), (XC, y + BH), ["state"], 8)
# The RW engine's packets go straight down to C&C 2's router.
f.line([(RWX + RWD, y + BH), (RWX + RWD, by)], INK, 3)
f.lines(RWX + RWD + 12, by - 22, ["packets, to C&C 2's", "+8 router input"], 11, "start", fill=MUTED)
cy = qy + BH - 16
f.line([(LC - NW / 2, cy), (30, cy)], ORANGE, 2, dash=DASH["ctrl"])
f.lines(LC - NW / 2 - 6, cy - 24, ["credits, to", "Ingress/Egress"], 11, "end", fill=ORANGE)

f.caption(["The bus driver is the only station on the AXI bus to the memory controller. The left bus is as wide as AXI and carries data only, between the bus driver and its consumers: pages and half pages for the",
           "Updater, the RW engine and the Binder, and lines for the cache. Those three ask the cache where a file's pages are, and change the file's index, over the narrow look-up bus.",
           "The right bus is the Tracer's fill bus, between the cache and the Tracer's three stations: its data cache, its instruction cache and its jump-table read (figure 9.1). The",
           "peripheral bus fills the segment queue and the free page queue, writes the Updater's state and goes into the RW engine; the segment queue sends credits to Ingress/Egress. The LRU turns the segment",
           "data into addresses and values, with a type for each: words, a half page (2 KB) or a full page (4 KB). The RW engine sends its packets through C&C 2's +8 router input."],
          source="Source: v1.4_wip/planning/DESIGN-NOTES-2026-11-15.md, sections 2 to 4; the Tracer's stations are figure 9.1's.")

out = os.path.join(HERE, "..", "svg", "fig_m1_lpddr_path.svg")
with open(out, "w") as fh:
    fh.write(f.svg())
print("wrote", os.path.basename(out), W, H)
