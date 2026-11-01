"""Figure 4: values follow their keys by ordinal: tile T1, steps 4 to 6."""
from common import *
import toy

q = 1
inbox, final = toy.inbox[q], toy.final[q]
W, H = 940, 530
f = Fig(W, H, "Figure 4. How each key finds its value",
        "Tile T1 after the two all-to-alls. Key i and value i landed at position i of two buffers, so after the second sort each")
f.text(24, 74, "key's ordinal says where its value is, and a gather fetches the values in key order. (val a4: the value of T0's input record 4.)",
       12.5, "start", fill=MUTED)
Y0, RH, CH, CWD = 168, 34, 26, 78
XK, XS, XV, XG = 46, 236, 500, 700
cols = [(XK, "Keys landed", "key | ordinal"), (XS, "Keys sorted again", "key | ordinal"),
        (XV, "Values landed", "128-bit values (k = 2)"), (XG, "Values gathered", "in key order")]
for x, t, s in cols:
    f.text(x + CWD / 2, 116, t, 13.5, "middle", "bold")
    f.text(x + CWD / 2, 133, s, 12, "middle", fill=MUTED)
hl = {0: YELLOW, 4: YELLOW}
for i in range(8):
    y = Y0 + i * RH
    r, s = inbox[i], final[i]
    o = toy.ord_rx[s]
    cell(f, XK, y, CWD, CH, f"{r[0]:02d}", BOX, EDGE, 13, sub=str(i))
    cell(f, XS, y, CWD, CH, f"{s[0]:02d}", BOX, YELLOW if i in hl else EDGE, 13, sub=str(o), sw=2 if i in hl else 1.1)
    hv = any(toy.ord_rx[final[j]] == i for j in hl)
    cell(f, XV, y, CWD, CH, "val " + toy.value(r), BOX, YELLOW if hv else EDGE, 12.5, sw=2 if hv else 1.1)
    f.text(XV - 8, y + 17.5, f"V + {4 * i}", 12, "end", fill=MUTED, mono=True)
    cell(f, XG, y, CWD, CH, "val " + toy.value(s), BOX, YELLOW if i in hl else EDGE, 12.5, sw=2 if i in hl else 1.1)
    f.text(XG + CWD + 8, y + 17.5, f"from V + {4 * o}", 11.5, "start", fill=MUTED, mono=True)
# the source runs of the landed keys
for p in range(4):
    idx = [i for i, r in enumerate(inbox) if r[1] == p]
    y0, y1 = Y0 + idx[0] * RH, Y0 + idx[-1] * RH + CH
    f.line([(XK - 6, y0 + 2), (XK - 12, y0 + 2), (XK - 12, y1 - 2), (XK - 6, y1 - 2)], MUTED, 1.4, end=False)
    f.text(XK - 16, (y0 + y1) / 2 + 4, f"T{p}", 11.5, "end", fill=MUTED)
f.line([(XK + CWD + 12, Y0 + 4 * RH - 4), (XS - 12, Y0 + 4 * RH - 4)], INK, 1.8)
f.text((XK + CWD + XS) / 2, Y0 + 4 * RH - 14, "sort", 12, "middle", fill=MUTED)
# lookups
for j in hl:
    s = final[j]; o = toy.ord_rx[s]
    ya, yv = Y0 + j * RH + CH / 2, Y0 + o * RH + CH / 2
    f.path(f"M{XS + CWD + 4},{ya} C{XS + CWD + 60},{ya} {XV - 120},{yv} {XV - 58},{yv}", YELLOW, 2)
    f.path(f"M{XV + CWD + 4},{yv} C{XV + CWD + 50},{yv} {XG - 50},{ya} {XG - 4},{ya}", YELLOW, 2)
f.text((XS + CWD + XV - 58) / 2, Y0 + 8 * RH + 4, "two of the eight lookups", 12, "middle", fill=YELLOW, italic=True)
f.lines(24, H - 62, [
    "The values all-to-all (mode 2) lands each sender's values at its own PTR[src], in the same order as its keys, so value i belongs to",
    "key i. The gather streams the sorted (key, ordinal) records to the tile itself; for each, the GATHER engine reads k records at",
    "V + 2k · ordinal (here V + 4 · ordinal) and writes them in order. Step 2 is the same operation on the input, before the split."],
    12.5, "start")
f.save("fig04-gather.svg")
