"""Figure 3: augmenting (mode 4) at the toy's tile T1 during the keys all-to-all."""
from common import *
import toy

q = 1
inbox = toy.inbox[q]
W, H = 940, 500
f = Fig(W, H, "Figure 3. Augmenting: each record is stamped with the position it lands at",
        "Tile T1 in the keys all-to-all. Packets from different senders interleave; each lands at PTR[src], and the write engine")
f.text(24, 74, "replaces its high word by the ordinal, (landing address − DMA_RECORD_BASE_ADDRESS) / 2.", 12.5, "start", fill=MUTED)

# arrival order: one record per sender per round, senders in a fixed order (an illustration of interleaving)
runs = {p: [r for r in inbox if r[1] == p] for p in range(4)}
order, rnd = [], 0
while len(order) < len(inbox):
    for p in (0, 2, 3, 1):
        if rnd < len(runs[p]): order.append(runs[p][rnd])
    rnd += 1
slot = {r: i for i, r in enumerate(inbox)}

Y0, RH = 150, 36
# left: arriving packets
f.text(150, 112, "Packets arriving, in order", 13.5, "middle", "bold")
f.text(150, 129, "mode 4; payload = the sender's record", 12, "middle", fill=MUTED)
for a, r in enumerate(order):
    y = Y0 + a * RH
    fl, st = TILE[r[1]]
    f.text(40, y + 18, f"{a + 1}.", 12, "end", fill=MUTED)
    f.rect(48, y, 108, 26, fl, st, 1.1, rx=3)
    f.text(56, y + 17.5, f"from T{r[1]}", 12, "start")
    cell(f, 166, y, 62, 26, f"{r[0]:02d}", BOX, EDGE, 13, sub=str(toy.ord_in[r]))
    f.text(238, y + 17.5, f"arrival count {a}", 11.5, "start", fill=MUTED)

# right: the buffer
BX, BW = 690, 64
f.text(BX + BW / 2 + 10, 112, "T1's receive buffer", 13.5, "middle", "bold")
f.text(BX + BW / 2 + 10, 129, "base B; PTR as set before the run", 12, "middle", fill=MUTED)
for i, r in enumerate(inbox):
    y = Y0 + i * RH
    fl, st = TILE[r[1]]
    cell(f, BX, y, BW, 26, f"{r[0]:02d}", fl, st, 13, "bold", sub=str(i))
    f.text(BX - 10, y + 17.5, f"B + {2 * i}", 12, "end", fill=MUTED, mono=True)
# PTR starts per source
starts = {}
for i, r in enumerate(inbox):
    starts.setdefault(r[1], i)
for p, i in starts.items():
    cnt = len(runs[p])
    y0, y1 = Y0 + i * RH, Y0 + (i + cnt - 1) * RH + 26
    st = TILE[p][1]
    f.line([(BX + BW + 8, y0 + 2), (BX + BW + 14, y0 + 2), (BX + BW + 14, y1 - 2), (BX + BW + 8, y1 - 2)], st, 1.6, end=False)
    f.text(BX + BW + 20, y0 + 17, f"PTR[{p}] = B + {2 * i}", 12, "start", fill=st)

# middle: the write engine
MX, MW = 370, 190
f.box(MX, Y0 + 2 * RH - 10, MW, 132, "write engine, mode 4",
      ["target = PTR[src]", "PTR[src] += 2", "high word = (target − B) / 2", "low word (the key) unchanged"],
      fill=BLUE_L, stroke=BLUE, size=12.5)
for a, r in enumerate(order):
    ya = Y0 + a * RH + 13
    ys = Y0 + slot[r] * RH + 13
    st = TILE[r[1]][1]
    f.line([(330, ya), (MX - 4, Y0 + 2 * RH - 10 + 66 + (a - 3.5) * 9)], st, 1.1, end=False)
    f.line([(MX + MW + 4, Y0 + 2 * RH - 10 + 66 + (slot[r] - 3.5) * 9), (BX - 66, ys)], st, 1.1)
f.lines(24, H - 46, [
    "The ordinal counts positions in the buffer, not arrivals: the 39 from T2 arrives second but lands at position 5, the start",
    "of T2's run. Each sender's run is contiguous, so key i and a value that lands at position i of a matching buffer belong together."],
    12.5, "start")
f.save("fig03-augmenting.svg")
