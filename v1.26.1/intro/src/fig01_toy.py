"""Figure 1: the toy sort, keys only, in five panels."""
from common import *
import toy

W = 940
CW, CH, CG, RG = 34, 26, 5, 4
PW = 4 * CW + 3 * CG
PG = (W - 48 - 5 * PW) / 4
Y = 132
f = Fig(W, 492, "Figure 1. Sorting 32 keys on 4 tiles",
        "Each tile ends with 8 keys, and the tiles are in global order. Equal keys keep their input order.")
titles = ["1. Input", "2. Local sort", "3. Split points", "4. All-to-all", "5. Local sort again"]
caps = [["8 keys per tile,", "unsorted"], ["each tile sorts", "its own keys"],
        ["colour = destination:", "ranks 0–7, 8–15, …"], ["each tile receives its", "colour, source by source"],
        ["each tile sorts what", "it received: done"]]
px = lambda i: 24 + i * (PW + PG)
for i in range(5):
    x = px(i)
    f.text(x + PW / 2, 94, titles[i], 13.5, "middle", "bold")
    for t in range(4):
        cx = x + t * (CW + CG)
        f.text(cx + CW / 2, Y - 9, f"T{t}", 12, "middle", fill=MUTED)
        for j in range(8):
            y = Y + j * (CH + RG)
            if i == 0:
                k, fl, st = toy.INP[t][j], BOX, EDGE
            elif i == 1:
                k, fl, st = toy.local[t][j][0], BOX, EDGE
            elif i == 2:
                r = toy.local[t][j]; k = r[0]; fl, st = TILE[toy.dest(r)]
            elif i == 3:
                k = toy.inbox[t][j][0]; fl, st = TILE[t]
            else:
                k = toy.final[t][j][0]; fl, st = TILE[t]
            cell(f, cx, y, CW, CH, f"{k:02d}", fl, st, 13)
        if i == 2:   # the cuts
            for j in range(1, 8):
                if toy.dest(toy.local[t][j]) != toy.dest(toy.local[t][j - 1]):
                    y = Y + j * (CH + RG) - RG / 2
                    f.line([(cx - 3, y), (cx + CW + 3, y)], INK, 2.2, end=False)
        if i == 3:   # source separators, and the source under each run
            for j in range(1, 8):
                if toy.inbox[t][j][1] != toy.inbox[t][j - 1][1]:
                    y = Y + j * (CH + RG) - RG / 2
                    f.line([(cx, y), (cx + CW, y)], MUTED, 1, end=False, dash="3 2")
    yb = Y + 8 * (CH + RG)
    f.lines(x + PW / 2, yb + 18, caps[i], 12, "middle", fill=INK)
    if i < 4:
        ya = Y + 4 * (CH + RG) - RG / 2
        f.line([(x + PW + 6, ya), (x + PW + PG - 6, ya)], INK, 1.8)
ly = 448
f.text(px(1) + 40, ly + 13, "destination:", 12, "end", fill=MUTED)
for q in range(4):
    x = px(1) + 52 + q * 74
    f.rect(x, ly, 22, 18, TILE[q][0], TILE[q][1], 1.2, rx=3)
    f.text(x + 28, ly + 13, f"T{q}", 12, "start")
f.text(px(3) - 10, ly + 13, "dashed lines in panel 4 separate the sources", 12, "start", fill=MUTED)
f.save("fig01-toy-sort.svg")
