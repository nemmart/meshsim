"""Figure 6: LSD radix sort of 16 keys on one tile, with augmenting and count-on-arrival."""
from common import *

keys = [4821, 1307, 5263, 2830, 7714, 3962, 1307, 6045, 9158, 2876, 5203, 491, 8817, 3530, 7642, 4369]
dig = lambda k, d: (k // 10 ** (d - 1)) % 10            # d = 1 is the least significant digit
cols = [[(k, None) for k in keys], [(k, j) for j, k in enumerate(keys)]]
cur = cols[1]
for d in range(1, 5):
    cur = sorted(cur, key=lambda r: dig(r[0], d))        # stable, as mode 3 is
    cols.append(cur)
assert [r[0] for r in cols[5]] == sorted(keys)


def hist(rs, d):
    c = [0] * 10
    for r in rs: c[dig(r[0], d)] += 1
    return c, [sum(c[:b]) for b in range(10)]


W = 940
COLW, CW, CH, RG, X0, Y0 = 150, 92, 22, 3, 24, 150
yt = Y0 + 16 * (CH + RG) + 40
H = yt + 3 * 16 + 74
f = Fig(W, H, "Figure 6. The local sort on one tile: 16 keys, 4 decimal digits",
        "Pass 1 brings the keys in by augmenting, so each becomes key|position, and counts digit 1 (the ones). Each later pass buckets")
f.text(24, 74, "the records by one digit, stably, and counts the next digit. The two 1307s keep their input order, 01 before 06.", 12.5, "start", fill=MUTED)
titles = ["Input", "After pass 1", "After pass 2", "After pass 3", "After pass 4", "After pass 5"]
subs = ["raw keys", "augmented", "by digit 1", "by digit 2", "by digit 3", "by digit 4: sorted"]
CHW = 8.2


def keycell(x, y, r, sd, cd):
    f.rect(x, y, CW, CH, BOX, EDGE, 1, rx=3)
    s = f"{r[0]:04d}"
    tag = r[1] is not None
    kx = x + 10 if tag else x + (CW - 4 * CHW) / 2
    by = y + CH / 2 + 4.6
    for i, ch in enumerate(s):
        d = 4 - i; cx = kx + i * CHW
        if d == sd:
            f.rect(cx - 1, y + 3, CHW + 2, CH - 6, YELLOW_L, YELLOW_L, 0, rx=2)
            f.text(cx + CHW / 2, by, ch, 13, "middle", "bold", fill=YELLOW)
        else:
            f.text(cx + CHW / 2, by, ch, 13, "middle")
        if d == cd:
            f.line([(cx, by + 3), (cx + CHW, by + 3)], BLUE, 2, end=False)
    if tag:
        f.text(kx + 4 * CHW + 4, by, "|", 12, "start", fill=GREY)
        f.text(kx + 4 * CHW + 11, by, f"{r[1]:02d}", 12, "start", fill=MUTED)


for c in range(6):
    x = X0 + c * COLW
    cx = x + (COLW - CW) / 2 - 8
    f.text(cx + CW / 2, 112, titles[c], 13.5, "middle", "bold")
    f.text(cx + CW / 2, 129, subs[c], 12, "middle", fill=MUTED)
    sd = c - 1 if c >= 2 else None
    cd = c if 1 <= c <= 4 else None
    rs = cols[c]
    for j, r in enumerate(rs):
        y = Y0 + j * (CH + RG)
        keycell(cx, y, r, sd, cd)
        if sd and j > 0 and dig(r[0], sd) != dig(rs[j - 1][0], sd):
            f.line([(cx - 4, y - RG / 2), (cx + CW + 4, y - RG / 2)], INK, 1.4, end=False)
    if c < 5:
        ya = Y0 + 8 * (CH + RG) - RG / 2
        f.line([(cx + CW + 6, ya), (cx + COLW - 8, ya)], INK, 1.6)
    if cd:
        cnt, st = hist(rs, cd)
        bw = 14.4
        tx = cx + CW / 2 - 5 * bw
        f.text(cx + CW / 2, yt - 22, f"counts of digit {cd}", 11.5, "middle", fill=BLUE)
        f.rect(tx - 1, yt - 14, 10 * bw + 2, 3 * 16 + 6, BLUE_L, BLUE_L, 0, rx=3)
        for b in range(10):
            bx = tx + b * bw + bw / 2
            f.text(bx, yt, str(b), 10.5, "middle", "bold", fill=BLUE)
            f.text(bx, yt + 16, str(cnt[b]), 11, "middle")
            f.text(bx, yt + 32, str(st[b]), 10 if st[b] > 9 else 11, "middle", fill=INK, mono=False)
        if c == 1:
            for k, l in enumerate(["digit", "count", "start"]):
                f.text(tx - 6, yt + 16 * k, l, 10.5, "end", fill=MUTED)
yl = yt + 3 * 16 + 26
f.text(24, yl, "Under each column: the counts of the digit counted during that pass (HIST) and their exclusive prefix sum, which is where each", 12.5, "start")
f.text(24, yl + 18, "bucket starts in the next pass (PTR). Yellow: the digit a pass buckets by. Underlined in blue: the digit it counts.", 12.5, "start")
f.save("fig06-radix-16keys.svg")
