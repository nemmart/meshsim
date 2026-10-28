"""Figure 12: the skew switch and the four transfer paths (sort/skew/skew-v2.md, spec 14)."""
from common import *

W, H = 940, 570
f = Fig(W, H, "Figure 12. Skewed data: the switch and the four transfer paths",
        "Every tile prices the paths from the same reduced values (the group maxima and a few totals), so every tile makes the same")
f.text(24, 74, "choice without a message. Planning is paid only as far as the data needs it.", 12.5, "start", fill=MUTED)

# left: the switch
LX, LW = 24, 400
def stage(y, h, title, body, fill=BOX, st=EDGE):
    f.rect(LX, y, LW, h, fill, st, 1, rx=6)
    f.text(LX + 14, y + 21, title, 13, "start", "bold")
    f.lines(LX + 14, y + 41, body, 12, "start")
def out(y, text, col):
    f.line([(LX + LW, y), (LX + LW + 34, y)], col, 1.6)
    f.text(LX + LW + 4, y - 6, text, 11, "start", fill=col)
stage(108, 104, "Stage 1 (every sort, ≈ 1K cycles)",
      ["Price direct and LB from the group maxima.",
       "Floor: a lower bound on any two-hop path.",
       "If min(direct, LB) ≤ floor: take the cheaper one."])
stage(244, 104, "Stage 2 (≈ 15–19K)",
      ["Build TH's window table. If TH needs one layer,",
       "its cost is exact: take TH if it is the cheapest.",
       "Else estimate BT; if it wouldn't win: direct or LB."], SAND, OPEN_C)
stage(380, 104, "Stage 3 (BT's planning, ≈ 150K)",
      ["Cut the runs into pieces, give each piece an",
       "intermediate tile (a sweep), count the layers.",
       "Take BT if its exact cost wins; else direct or LB."], PURPLE_L, PURPLE)
for y0, y1, lab in ((212, 244, "else"), (348, 380, "else")):
    f.line([(LX + 60, y0), (LX + 60, y1)], INK, 1.6)
    f.text(LX + 68, (y0 + y1) / 2 + 4, lab, 11.5, "start", fill=MUTED)
# right: the paths
RX, RW = 470, 446
paths = [
    ("direct", "the 16-group all-to-all of section 3", "uniform, Zipf ≤ 0.8", BOX, EDGE, 108),
    ("LB: loopback as a local copy", "each tile's records for itself (offset 0) leave group 15 and run as an extra one-stream group at full rate, so they no longer stretch a group on every tile",
     "sorted, all equal, 99 % one value", BOX, EDGE, 178),
    ("TH: two hops through windows", "each owner's buffer is cut into 64 equal windows, each carried by a different tile: hop 1 to the carriers, hop 2 one window to each owner; only when hop 1 is one layer",
     "reverse sorted", SAND, OPEN_C, 286),
    ("BT: balanced two hops", "every run is cut into pieces of w records; each piece gets an intermediate tile so that no pair of tiles carries too many; both hops run in layers at R = w",
     "Zipf 1.0, all nine trace sets", PURPLE_L, PURPLE, 394),
]
for name, what, used, fl, st, y in paths:
    lines = wrap(what, RW - 28, 12)
    h = 46 + 16 * len(lines)
    f.rect(RX, y, RW, h, fl, st, 1, rx=6)
    f.text(RX + 14, y + 21, name, 13, "start", "bold")
    f.lines(RX + 14, y + 40, lines, 12, "start")
    f.text(RX + RW - 12, y + 21, used, 11.5, "end", fill=MUTED, italic=True)
# arrows from stages to paths
f.line([(LX + LW, 160), (RX - 4, 128)], INK, 1.3)
f.line([(LX + LW, 168), (RX - 4, 200)], INK, 1.3)
f.line([(LX + LW, 296), (RX - 4, 310)], OPEN_C, 1.3)
f.line([(LX + LW, 432), (RX - 4, 420)], PURPLE, 1.3)
f.lines(24, H - 62, [
    "TH and BT move every record twice, but hop 2 lands each record exactly where the direct path would have (augmented, and with",
    "its digit counted on arrival), so the second local sort and the gather don't change. Simplified: the cost formulas, the floor",
    "and BT's limits are in sort/skew/skew-v2.md sections 2 and 6."], 12.5, "start")
f.save("fig12-skew-switch.svg")
