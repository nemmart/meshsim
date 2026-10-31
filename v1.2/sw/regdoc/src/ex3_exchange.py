# Worked example 3 (REGISTERS.md 7.3): the one-word exchange through the send port.
# Every tile sends and receives at once, so one tile's map shows both: outgoing (OUT) and incoming (IN), with an inset
# of two tiles joined by the mesh.
from examples_tile import *

OUT, OUT_L = "#B83280", "#FBE7F1"     # this tile's words, going out
IN, IN_L = "#C98A0C", "#FCF1DC"       # the other tiles' words, coming in

H = 1410
f = Fig(W, H, "Example 3. A one-word exchange: every tile sends one word to every tile, through the send port",
        "REGISTERS.md 7.3 — mode 7 from the send port, over the mesh, into data memory by port B: every tile does both, outgoing and incoming at once")
t = TileMap(f,
    active={"router", "send", "inj", "inject", "rt_out", "mesh_out", "loopback", "mesh_in", "rt_in", "eject", "dir", "portB", "cmem", "wake"},
    stores={"dir": [1], "send": [2, 3]},
    steps={"send": "a", "inject": "b", "mesh_out": "c", "mesh_in": "d", "eject": "e", "portB": "f", "wake": "g"},
    bodies={"cmem": ["in[0..63]: one word from each tile"], "inj": ["one packet per cycle:", "1. read returns (none here)", "2. the send port"]},
    colours={"send": OUT, "inject": OUT, "rt_out": OUT, "mesh_out": OUT, "loopback": OUT,
             "mesh_in": IN, "rt_in": IN, "eject": IN, "portB": IN})
t.draw(program("ex3"))                  # the program block in REGISTERS.md 7.3
f.tag(RTR[0] + RTR[2] / 2, RTR[1] + 50, "every tile does both", "middle")

LY = SR[1] + SR[3] + 34
t.legend(LY)
for i, (c, s) in enumerate([(OUT, "outgoing: this tile's words to the others"), (IN, "incoming: the others' words to this tile")]):
    x = 40 + 420 * i
    f.line([(x, LY + 22), (x + 30, LY + 22)], c, SW, end=True)
    f.text(x + 38, LY + 27, s, 11.5, "start", fill=MUTED)

# what happens (left) and the inset (right)
f.text(40, LY + 64, "What happens", 13, "start", "bold")
t.step_list(LY + 90, [
    ("a", OUT, "A store to ROUTER_SEND_LO puts the packet {dst d, mode 7, addr at, payload out[d]} into the send port's holding register; the store stalls only while the previous packet still waits there."),
    ("b", OUT, "The injection point takes it into the router, one packet per cycle (read returns would go first)."),
    ("c", OUT, "The router sends it towards tile d over the mesh, horizontal first, then vertical. The word to this tile itself (d = me) takes the loopback instead."),
    ("d", IN, "At the same time the other tiles' words arrive from the mesh."),
    ("e", IN, "The router ejects every packet with dst = me to the traffic director."),
    ("f", IN, "The director routes mode 7 to port B and decrements DMA_REPLY_REMAINING; port B writes the word at the sender's addr: in[s] for sender s."),
    ("g", ORANGE, "The 64th word in: DMA_REPLY_REMAINING = 0 and cm_idle, so the local wake fires and WFI returns."),
], cols=1, colw=620)

IX, IYY, IW, IH = 690, LY + 52, 570, 262
f.rect(IX, IYY, IW, IH, "white", EDGE, 1, rx=8)
f.text(IX + 14, IYY + 22, "Inset: two tiles, me and d, joined by the mesh (every tile does both)", 12.5, "start", "bold")
def tile(x, label, mirror, lands, lc):
    f.rect(x, IYY + 38, 210, 210, BOX, EDGE, 1.2, rx=6)
    f.text(x + 105, IYY + 56, label, 12, "middle", "bold")
    sx = x + 88 if mirror else x + 12
    rxx = x + 12 if mirror else x + 138
    f.box(sx, IYY + 70, 110, 32, None, ["send port"], fill=PURPLE_L, stroke=PURPLE, size=11)
    f.box(rxx, IYY + 112, 60, 68, None, ["router"], fill="white", stroke=INK, size=11)
    f.box(sx, IYY + 196, 110, 40, None, ["data memory", (lands, dict(size=10, fill=lc))], fill=PURPLE_L, stroke=PURPLE, size=11)
    return sx, rxx
asx, arx = tile(IX + 16, "tile me", False, "in[d] ← d's word", IN)
bsx, brx = tile(IX + 344, "tile d", True, "in[me] ← me's word", OUT)
# outgoing: me -> d
f.line([(asx + 110, IYY + 86), (arx + 30, IYY + 86), (arx + 30, IYY + 110)], OUT, SW)
f.line([(arx + 60, IYY + 128), (brx - 2, IYY + 128)], OUT, SW)
f.line([(brx + 30, IYY + 180), (brx + 30, IYY + 216), (bsx - 2, IYY + 216)], OUT, SW)
# incoming: d -> me
f.line([(bsx, IYY + 86), (brx + 30, IYY + 86), (brx + 30, IYY + 110)], IN, SW)
f.line([(brx, IYY + 162), (arx + 62, IYY + 162)], IN, SW)
f.line([(arx + 30, IYY + 180), (arx + 30, IYY + 216), (asx + 112, IYY + 216)], IN, SW)
mx = (arx + 60 + brx) / 2
f.text(mx, IYY + 120, "me's word to d", 10.5, "middle", fill=OUT)
f.text(mx, IYY + 177, "d's word to me", 10.5, "middle", fill=IN)
f.text(mx, IYY + 146, "the mesh: one or more hops", 10.5, "middle", fill=MUTED, italic=True)

f.caption(["A one-word exchange uses no engine: the program sends through the send port, and the traffic director hands every arriving word to port B, which writes it where",
           "its sender said (REGISTERS.md 2.5). Every tile does both at once, so the counter must be set before any word can arrive: it is the command's first store, by identical",
           "code on every tile. 64 words go out and 64 come in, the word to itself through the loopback."],
          y=H - 54, source="Source: REGISTERS.md 2.2, 2.3.3, 2.5; RECIPES.md 7 (xchg(), send_words()); spec/machine-spec.md v1.0, §3.1, §3.2, §4.11, §4.12; the tile map is figure 1.3's.")
save(f, "ex3_exchange.svg")
