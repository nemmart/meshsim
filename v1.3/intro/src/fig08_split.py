"""Figures 8 and 9: the split on the toy example (the bisection of sort/split/split.c with its seed bracket)."""
from common import *
import toy

S = toy.SPLIT
runs = [[r[0] for r in toy.local[t]] for t in range(4)]
OWN = {1: TILE[1], 2: TILE[2], 3: TILE[3]}
NR = max(len(S[q]["rounds"]) for q in S)

# ---------------- Figure 8: seed and rounds ----------------
W = 940
X0, CWd, RH, Y0 = 80, 286, 50, 150
H = Y0 + (NR + 1) * RH + 96
f = Fig(W, H, "Figure 8. The split on the toy example, round by round",
        "Owner q looks for the key at which global rank 8q falls. The seed gives each owner a bracket from one sample per tile; then")
f.text(24, 74, "every round probes the bracket's midpoint, counts below and equal on every tile, and halves the bracket. Keys run 0–99.",
       12.5, "start", fill=MUTED)
for i, q in enumerate((1, 2, 3)):
    x = X0 + i * CWd
    f.text(x + CWd / 2 - 10, Y0 - 26, f"Boundary {q}: owner T{q}, rank {8 * q}", 13, "middle", "bold", fill=OWN[q][1])
HR = 4
f.rect(18, Y0 + HR * RH + 1, W - 36, RH - 2, YELLOW_L, YELLOW, 1.2, rx=4)
for r in range(NR + 1):
    y = Y0 + r * RH
    f.text(X0 - 12, y + RH / 2 + 4, "seed" if r == 0 else f"round {r}", 12, "end", "bold" if r == HR else "normal")
    if r == HR:
        f.text(X0 - 12, y + RH / 2 + 18, "figure 9", 10.5, "end", fill=YELLOW, italic=True)
    f.line([(18, y), (W - 18, y)], "#EEEDE8", 1, end=False)
    for i, q in enumerate((1, 2, 3)):
        x = X0 + i * CWd
        ax0, aw = x + 4, 118
        sx = lambda k: ax0 + aw * k / 99
        ay = y + 29
        fl, st = OWN[q]
        rds = S[q]["rounds"]
        if r > len(rds):
            f.text(ax0 + aw / 2, ay + 4, "done", 11.5, "middle", fill=GREY)
            continue
        f.line([(ax0, ay), (ax0 + aw, ay)], GREY, 1, end=False)
        if r == 0:
            lo, hi = S[q]["bracket"]
            f.rect(sx(lo), ay - 6, max(sx(hi) - sx(lo), 2), 12, fl, st, 1, rx=2)
            for k in S[q]["samples"]:
                f.raw(f'<circle cx="{sx(k):.1f}" cy="{ay:.1f}" r="3.2" fill="{st}"/>')
            f.text(ax0 + aw + 10, ay - 3, "samples " + ", ".join(map(str, S[q]["samples"])), 11.5, "start")
            f.text(ax0 + aw + 10, ay + 12, f"→ bracket [{lo}, {hi}]", 11.5, "start", fill=st)
            f.text(ax0, ay + 18, "0", 9.5, "middle", fill=MUTED)
            f.text(ax0 + aw, ay + 18, "99", 9.5, "middle", fill=MUTED)
            continue
        d = rds[r - 1]
        f.rect(sx(d["lo"]), ay - 6, max(sx(d["hi"]) - sx(d["lo"]), 2), 12, fl, st, 1, rx=2)
        f.line([(sx(d["v"]), ay - 11), (sx(d["v"]), ay + 11)], INK, 2, end=False)
        f.text(sx(d["v"]), ay - 14, str(d["v"]), 11, "middle", "bold")
        E = d["U"] - d["L"]
        f.text(ax0 + aw + 10, ay - 3, f"below {d['L']}" + (f", equal {E}" if E else ""), 11.5, "start")
        act = {"lower": "→ lower", "higher": "→ higher", "equal": "→ found", "tie": "→ found, inside the 42s"}[d["act"]]
        if "known" in d:
            act = f"→ found: {d['known']}, not probed"
        f.text(ax0 + aw + 10, ay + 12, act, 11.5, "start", "bold" if d["act"] in ("equal", "tie") or "known" in d else "normal",
               fill=st if d["act"] in ("equal", "tie") or "known" in d else MUTED)
yb = Y0 + (NR + 1) * RH + 22
f.lines(24, yb, [
    "Found: boundary 3 at 71 in round 1 (24 below); boundary 2 inside the five 42s in round 4 (12 below: the first four 42s, in tile order,",
    "stay below); boundary 1 at 27 after round 4, its bracket down to one key whose counts are already known from the probes either side.",
    "Records below each boundary, T0–T3: 3, 2, 1, 2; 6, 4, 3, 3; 7, 6, 5, 6. Their differences are figure 1's slices."], 12.5, "start")
f.save("fig08-split-rounds.svg")

# ---------------- Figure 9: round 4 close-up ----------------
RND = 4
W, H = 940, 590
f = Fig(W, H, "Figure 9. Round 4 of the toy split, close up",
        "One round is two exchanges, each started on every tile at once: the probes out, then the counts back. Between them, every")
f.text(24, 74, "tile counts its own run against each new probe with binary searches through the controller read port.", 12.5, "start", fill=MUTED)
PW, PG, YT = 288, 14, 96
titles = ["1. Owners send their probes", "2. Every tile counts", "3. Owners narrow or finish"]
for p in range(3):
    x = 24 + p * (PW + PG)
    f.rect(x, YT, PW, H - YT - 70, "white", GREY, 1, rx=8)
    f.text(x + PW / 2, YT + 24, titles[p], 13.5, "middle", "bold")
pr = {q: S[q]["rounds"][RND - 1] if RND <= len(S[q]["rounds"]) else None for q in (1, 2, 3)}
# panel 1
x = 24
y = YT + 50
for i, q in enumerate((1, 2, 3)):
    fl, st = OWN[q]
    yy = y + i * 52
    f.rect(x + 14, yy, PW - 28, 42, fl, st, 1.1, rx=5)
    if pr[q]:
        f.text(x + 26, yy + 17, f"T{q}: bracket [{pr[q]['lo']}, {pr[q]['hi']}]", 12, "start", "bold")
        f.text(x + 26, yy + 33, f"probe = midpoint = {pr[q]['v']}", 12, "start")
    else:
        last = S[q]["rounds"][-1]["v"]
        f.text(x + 26, yy + 17, f"T{q}: done in round {len(S[q]['rounds'])}", 12, "start", "bold")
        f.text(x + 26, yy + 33, f"sends {last} again: the done marker", 12, "start")
yy = y + 3 * 52 + 14
f.lines(x + 16, yy, [
    "Each owner sends its probe to every tile,",
    "one word each (mode 7), in the 16-group",
    "order, so every step is a permutation.",
    "",
    "A probe repeated from the last round",
    "means done: tiles skip it. When every",
    "owner is done, the rounds stop early.",
    "",
    "Tile 0 owns no boundary; on the machine",
    "tiles 1–63 own boundaries 1–63."], 12, "start")
# panel 2
x = 24 + PW + PG
CW, CH, RG = 46, 22, 3
cx0 = x + 44
y0 = YT + 62
probes = [q for q in (1, 2) if pr[q]]
for t in range(4):
    cx = cx0 + t * (CW + 16)
    f.text(cx + CW / 2, y0 - 8, f"T{t}", 12, "middle", "bold")
    for j, k in enumerate(runs[t]):
        yy = y0 + j * (CH + RG)
        fl, st = (TILE[2][0], TILE[2][1]) if k == 42 else (BOX, EDGE)
        cell(f, cx, yy, CW, CH, f"{k:02d}", fl, st, 12.5)
    for q in probes:
        v = pr[q]["v"]
        pos = toy.lb(runs[t], v)
        yy = y0 + pos * (CH + RG) - RG / 2
        f.line([(cx - 5, yy), (cx + CW + 5, yy)], OWN[q][1], 2.6, end=False)
ty = y0 + 8 * (CH + RG) + 26
f.text(cx0 - 6, ty, "probe", 11, "end", fill=MUTED)
for t in range(4):
    f.text(cx0 + t * (CW + 16) + CW / 2, ty, "lb, ub", 11, "middle", fill=MUTED)
for i, q in enumerate(probes):
    v = pr[q]["v"]
    yy = ty + 19 + i * 19
    f.text(cx0 - 6, yy, str(v), 12, "end", "bold", fill=OWN[q][1])
    for t in range(4):
        f.text(cx0 + t * (CW + 16) + CW / 2, yy, f"{toy.lb(runs[t], v)}, {toy.ub(runs[t], v)}", 12, "middle")
f.lines(x + 16, ty + 62, [
    "lb: keys below the probe; ub: keys up to it.",
    "Each is a binary search of the tile's run, inside",
    "the bracket it keeps for that owner. Each tile",
    "sends (lb, ub) to each owner as one 64-bit",
    "packet (mode 5)."], 12, "start")
# panel 3
x = 24 + 2 * (PW + PG)
for i, q in enumerate((1, 2, 3)):
    fl, st = OWN[q]
    yy = YT + 46 + i * 124
    f.rect(x + 12, yy, PW - 24, 112 if pr[q] else 76, fl, st, 1.1, rx=6)
    R = 8 * q
    if not pr[q]:
        c = S[q]["cuts"]
        f.text(x + 24, yy + 22, f"T{q}: rank {R}", 12.5, "start", "bold")
        f.text(x + 24, yy + 42, f"found in round 1: 24 keys below 71", 12, "start")
        f.text(x + 24, yy + 62, f"cuts: {', '.join(map(str, c))}", 12, "start", "bold")
        continue
    d = pr[q]; E = d["U"] - d["L"]
    f.text(x + 24, yy + 22, f"T{q}: rank {R}, probe {d['v']}", 12.5, "start", "bold")
    f.text(x + 24, yy + 42, f"below L = {d['L']}, up to U = {d['U']}", 12, "start")
    if d["act"] == "tie":
        f.text(x + 24, yy + 62, f"L < {R} < U: inside the {E} 42s; the first", 12, "start")
        f.text(x + 24, yy + 79, f"{R - d['L']} stay below, in tile order: 2, 1, 1, 0", 12, "start")
        f.text(x + 24, yy + 99, f"cuts = lb + taken: {', '.join(map(str, S[q]['cuts']))}", 12, "start", "bold")
    else:
        f.text(x + 24, yy + 62, f"U ≤ {R}: higher; the bracket is now [{d['v'] + 1}, {d['hi']}]:", 12, "start")
        f.text(x + 24, yy + 79, f"one key, and the {d['U']} keys up to {d['v']} fill rank {R}", 12, "start")
        f.text(x + 24, yy + 99, f"cuts = ub at 26: {', '.join(map(str, S[q]['cuts']))}", 12, "start", "bold")
f.lines(24, H - 42, [
    "After the last round each owner sends every tile its cut (mode 7), so each tile knows how many of its records go to each owner.",
    "Simplified: the rules for a bracket of one key and for equal keys are in sort/split/split.c."], 12.5, "start")
f.save("fig09-split-round.svg")
