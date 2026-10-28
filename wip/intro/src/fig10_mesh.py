"""Figure 10: the 8 x 8 mesh and one tile's four streams in group 12 (spec/schedule.md)."""
from common import *

W, H = 940, 540
f = Fig(W, H, "Figure 10. The mesh, and one tile's four streams in group 12",
        "64 tiles, numbered row by row. Each has four links: right and left (±1), which make one ring through all 64 tiles, and down")
f.text(24, 74, "and up (±8), which wrap around the columns. A stream to offset k always takes the same route: along the row, then the column.",
       12.5, "start", fill=MUTED)
X0, Y0, S, B = 62, 118, 50, 34
pos = lambda t: (X0 + (t % 8) * S + B / 2, Y0 + (t // 8) * S + B / 2)
for t in range(64):
    x, y = pos(t)
    if t % 8 < 7: f.line([(x + B / 2, y), (x + S - B / 2, y)], GREY, 2, end=False)
    if t // 8 < 7: f.line([(x, y + B / 2), (x, y + S - B / 2)], GREY, 2, end=False)
for r in range(8):
    x, y = pos(8 * r + 7)
    f.line([(x + B / 2, y), (x + B / 2 + 9, y)], GREY, 2, end=False)
    f.text(x + B / 2 + 12, y + 4, f"→{(8 * r + 8) % 64}", 9.5, "start", fill=MUTED)
    x, y = pos(8 * r)
    f.line([(x - B / 2 - 9, y), (x - B / 2, y)], GREY, 2, end=False)
    f.text(x - B / 2 - 12, y + 4, f"{(8 * r - 1) % 64}←", 9.5, "end", fill=MUTED)
src = 27
streams = [(14, -2, 2), (18, 2, 2), (46, -2, -2), (50, 2, -2)]
cols = [ORANGE, GREEN, BLUE, PURPLE]
dests = {(src + k) % 64: i for i, (k, h, v) in enumerate(streams)}
off = [(-4, -4), (4, -4), (-4, 4), (4, 4)]
for i, (k, h, v) in enumerate(streams):
    sx, sy = pos(src); dx, dy = off[i]
    mx, my = pos(src + h); ex, ey = pos((src + k) % 64)
    sgn = 1 if v > 0 else -1
    f.line([(sx + dx, sy + dy), (mx + dx, my + dy), (ex + dx, ey - sgn * (B / 2 + 3))], cols[i], 3)
for t in range(64):
    x, y = pos(t)
    if t == src:
        f.rect(x - B / 2, y - B / 2, B, B, INK, INK, 1.5, rx=5)
        f.text(x, y + 4.5, str(t), 12.5, "middle", "bold", fill="white")
    elif t in dests:
        f.rect(x - B / 2, y - B / 2, B, B, "white", cols[dests[t]], 2.2, rx=5)
        f.text(x, y + 4.5, str(t), 12.5, "middle", "bold")
    else:
        f.rect(x - B / 2, y - B / 2, B, B, BOX, EDGE, 1, rx=5)
        f.text(x, y + 4.5, str(t), 12, "middle")
LX = 540
f.text(LX, Y0 + 12, "Group 12, sent by tile 27", 14, "start", "bold")
rows = [("offset", "route", "to tile")] + [(f"+{k}", f"{'right' if h > 0 else 'left'} {abs(h)}, {'down' if v > 0 else 'up'} {abs(v)}",
                                             str((src + k) % 64)) for k, h, v in streams]
for j, (a, b, c) in enumerate(rows):
    y = Y0 + 44 + j * 24
    if j: f.rect(LX, y - 9, 14, 5, cols[j - 1], cols[j - 1], 0, rx=0)
    for xx, s in ((LX + 24, a), (LX + 84, b), (LX + 224, c)):
        f.text(xx, y, s, 12.5, "start", "bold" if j == 0 else "normal", fill=MUTED if j == 0 else INK)
f.lines(LX, Y0 + 44 + 5 * 24 + 14, [
    "Together the four streams make 4 hops right,",
    "4 left, 4 down and 4 up.",
    "",
    "Every tile sends the same four offsets at the",
    "same time, so every link direction carries",
    "4 packets per 4 cycles: every link busy on every",
    "cycle, none asked for more.",
    "",
    "The 16 groups cover all 64 offsets (+0 is the",
    "tile itself) in 16 × 4 = 64 cycles per round,",
    "the least any schedule can take (schedule.md 4)."], 12.5, "start")
f.save("fig10-mesh-routes.svg")
