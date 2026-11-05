"""Figure 2: the toy sort as (key, ordinal) records."""
from common import *
import toy

W = 940
CW, CH, CG, RG = 60, 26, 6, 4
PW = 4 * CW + 3 * CG
PG = (W - 48 - 3 * PW) / 2
X0 = 24
Y = 150
f = Fig(W, 500, "Figure 2. The same sort, as (key, ordinal) records",
        "Each cell is key|ordinal. Step 1 stamps each key's input position; the keys all-to-all stamps each record's position")
f.text(24, 74, "in the receiving buffer instead. The five 42s (bold) stay in (tile, position) order throughout.",
       12.5, "start", fill=MUTED)
titles = ["After the local sort", "Landed at the owner", "After the local sort again"]
subs = ["ordinal = input position", "ordinal = position in the buffer", "ordinal = position in the buffer"]
px = lambda i: X0 + i * (PW + PG)
for i in range(3):
    x = px(i)
    f.text(x + PW / 2, 108, titles[i], 13.5, "middle", "bold")
    f.text(x + PW / 2, 125, subs[i], 12, "middle", fill=MUTED)
    for t in range(4):
        cx = x + t * (CW + CG)
        f.text(cx + CW / 2, Y - 8, f"T{t}", 12, "middle", fill=MUTED)
        col = [toy.local, toy.inbox, toy.final][i][t]
        for j, r in enumerate(col):
            y = Y + j * (CH + RG)
            fl, st = TILE[toy.dest(r)] if i == 0 else TILE[t]
            o = toy.ord_in[r] if i == 0 else toy.ord_rx[r]
            cell(f, cx, y, CW, CH, f"{r[0]:02d}", fl, st, 13, "bold" if r[0] == 42 else "normal", sub=str(o),
                 sw=2 if r[0] == 42 else 1.1)
        if i == 0:
            for j in range(1, 8):
                if toy.dest(col[j]) != toy.dest(col[j - 1]):
                    y = Y + j * (CH + RG) - RG / 2
                    f.line([(cx - 3, y), (cx + CW + 3, y)], INK, 2.2, end=False)
        if i == 1:
            for j in range(1, 8):
                if col[j][1] != col[j - 1][1]:
                    y = Y + j * (CH + RG) - RG / 2
                    f.line([(cx, y), (cx + CW, y)], MUTED, 1, end=False, dash="3 2")
    if i < 2:
        ya = Y + 4 * (CH + RG) - RG / 2
        f.line([(x + PW + 6, ya), (x + PW + PG - 6, ya)], INK, 1.8)
yb = Y + 8 * (CH + RG) + 26
notes = [
    "Left: equal keys stay in input order on each tile, because every radix pass is stable. The 42s are T0's 42|3 and 42|7, T1's 42|1,",
    "T2's 42|2 and T3's 42|5; the boundary at rank 16 sends the first four of them to T1 and the last to T2.",
    "Middle: T1's buffer fills source by source (dashed lines): T0's run, then T1's, T2's and T3's. So the positions 1, 2, 4 and 6 of",
    "its four 42s are in (tile, position) order, and the stable second sort keeps them in that order (right).",
]
f.lines(24, yb, notes, 12.5, "start", fill=INK)
f.save("fig02-toy-records.svg")
