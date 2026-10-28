"""Figure 11: one tile's all-to-all as a group run: groups, rotations, and the table entries behind them."""
from common import *

W, H = 940, 530
f = Fig(W, H, "Figure 11. One tile's keys all-to-all: 16 groups, run as one group run",
        "Every tile runs the same timetable, started on the same cycle; only the data differs. A group runs R rotations, R being its")
f.text(24, 74, "largest stream anywhere on the mesh, and the next group starts on the same cycle everywhere.", 12.5, "start", fill=MUTED)
X0, Y0 = 24, 120
GW = (W - 2 * X0) / 16
f.text(X0, Y0 - 10, "16 groups, one after another (uniform keys: R ≈ 1,110 in every group, ≈ 4,450 cycles each)", 12.5, "start")
for g in range(16):
    x = X0 + g * GW
    hl = g == 12
    f.rect(x, Y0, GW - 3, 30, YELLOW_L if hl else BOX, YELLOW if hl else EDGE, 1.6 if hl else 1, rx=3)
    f.text(x + GW / 2 - 1.5, Y0 + 19, f"g{g}", 11.5, "middle", "bold" if hl else "normal")
f.text(W - X0, Y0 + 46, "time →", 11, "end", fill=MUTED)
# zoom
zy = 226
x12 = X0 + 12 * GW
f.path(f"M{x12},{Y0 + 30} L{X0},{zy - 34} M{x12 + GW - 3},{Y0 + 30} L{W - X0},{zy - 34}", YELLOW, 1, end=False, dash="4 3")
f.text(W / 2, zy - 12, "group 12 in detail: offsets +14, +18, +46, +50, one cycle per visit", 12.5, "middle", "bold")
CW = 22
labels = ["+14", "+18", "+46", "+50"]
cl = [(ORANGE_L, ORANGE), (GREEN_L, GREEN), (BLUE_L, BLUE), (PURPLE_L, PURPLE)]
seq = [("rot", "0", [1] * 4), ("rot", "1", [1] * 4), ("rot", "2", [1] * 4), ("dots",),
       ("rot", "R−3", [1, 1, 0, 1]), ("rot", "R−2", [1, 1, 0, 1]), ("rot", "R−1", [1, 0, 0, 1]), ("next",)]
x = X0
for it in seq:
    if it[0] == "rot":
        _, name, act = it
        for i in range(4):
            if act[i]:
                f.rect(x + i * CW, zy, CW - 1, 34, cl[i][0], cl[i][1], 1, rx=0)
                f.text(x + i * CW + CW / 2 - 0.5, zy + 21, labels[i], 9.5, "middle")
            else:
                f.rect(x + i * CW, zy, CW - 1, 34, "white", GREY, 1, rx=0, dash="3 2")
                f.text(x + i * CW + CW / 2 - 0.5, zy + 21, "idle", 8.5, "middle", fill=MUTED)
        f.path(f"M{x},{zy + 42} v6 h{4 * CW - 1} v-6", MUTED, 1, end=False)
        f.text(x + 2 * CW, zy + 62, f"rotation {name}", 10.5, "middle", fill=MUTED)
        x += 4 * CW + 8
    elif it[0] == "dots":
        f.text(x + 24, zy + 22, "· · ·", 16, "middle", fill=MUTED); x += 50
    else:
        f.rect(x, zy, W - X0 - x, 34, BOX, EDGE, 1, rx=0)
        f.text(x + (W - X0 - x) / 2, zy + 21, "group 13: next cycle (gap 0)", 11.5, "middle")
# the tables behind it
ty = 326
f.text(24, ty, "What the controller wrote for it (spec 4.2)", 13, "start", "bold")
f.rect(24, ty + 10, 300, 92, PURPLE_L, PURPLE, 1, rx=5)
f.lines(36, ty + 30, [("GROUP[12]", dict(weight="bold")), "R = the group's global maximum count",
                      "start stream = s, streams t = 4, gap = 0", ("the R field is rewritten every all-to-all", dict(fill=MUTED))], 12, "start")
f.rect(340, ty + 10, 576, 92, PURPLE_L, PURPLE, 1, rx=5)
f.lines(352, ty + 30, [("STREAM[s … s + 3], one per offset", dict(weight="bold")),
                       "source = the start of this tile's run for (me + d) mod 64, count = its length,",
                       "d = 14, 18, 46, 50 (relative, the same on every tile), mode 4 (keys) or 2 (values),",
                       "read size 64 bits (keys) or 128 bits (values: two packets per visit, period 8)"], 12, "start")
f.lines(24, ty + 128, [
    "A rotation sends one element of each stream that still has one; a stream that has run out (+46 here) leaves its cycle idle. One",
    "large stream anywhere therefore stretches its group on every tile: this is where skew costs (section 4). The whole all-to-all takes",
    "Σ 4 × R_g cycles for the keys (8 × R_g for 128-bit values) plus a short start and tail."], 12.5, "start")
f.save("fig11-a2a-timeline.svg")
