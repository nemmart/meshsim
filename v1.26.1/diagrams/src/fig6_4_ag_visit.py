# Figure 6.4: the address generator, visit side — the timetable's counters, the STREAM entry of this visit,
# the element address (source + e << z: +1, +2 or +4 chunks per element), dst, the mode-5 word, the request and its token.
# The scheme (spec §4.2).
from common import *

W, H = 1300, 1100
f = Fig(W, H, "Figure 6.4. The address generator, visit side: one STREAM entry and one counter make each request",
        "v1.0 (spec/machine-spec.md §4.2, §4.5) — the entry comes from figure 6.3's STREAM queue; no per-stream state is kept")

CTL = DASH["ctrl"]
SHIFT = 30


def dot(x, y, c=INK):
    f.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{c}"/>')


def poly(pts, fill="white", stroke=INK, sw=1.4):
    p = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
    f.raw(f'<polygon points="{p}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')


def small(x, y, w, h, label, fill="white"):
    f.rect(x, y, w, h, fill, INK, 1.2, rx=3)
    f.text(x + w / 2, y + h / 2 + 4, label, 10.5, "middle", "bold")


f.raw(f'<g transform="translate(0,{SHIFT})">')
# ---------------- the STREAM entry of this visit (figure 6.3's STREAM queue head) -------------------
BX, BY, BS, BH = 480, 122, 12, 40
fields = [("source (chunk address)", 20, GREEN_L), ("cm field", 12, PURPLE_L), ("count", 18, BOX),
          ("d", 6, SAND), ("mode", 3, BOX), ("size", 2, BLUE_L), ("gap", 3, BOX)]
f.text(BX, BY - 26, "STREAM queue head: this visit's entry (figure 6.3)", 12.5, "start", "bold")
f.text(BX, BY - 8, "low word", 10.5, "start", fill=MUTED)
f.text(BX + 32 * BS, BY - 8, "high word", 10.5, "start", fill=MUTED)
bitbar(f, BX, BY, fields, BS, h=BH, label_size=11)
f.text(BX + 64 * BS - 18, BY - 8, "size", 10.5, "middle", "bold")
cx = {}
x = BX
for name, bits, *_ in fields:
    cx[name.split(" ")[0]] = x + bits * BS / 2
    x += bits * BS
BB = BY + BH                                   # bottom of the bar

# ---------------- timetable, counters, e mux (left) ------------------------------------------
TT = (30, 90, 420, 172)
f.box(*TT, "timetable (visit side)", [
    "Group run: the GROUP queue head is the group (R, t, gap).",
    "For r = 0 … R − 1, each of the t streams gets one visit of 1 cycle",
    "(2 for 128-bit) and pops its STREAM entry; then the group's gap.",
    "Stream run: j = 0, 1, … while send = 1; then gap after; pop.",
    "A visit with send = 1 waits until the SRAM read queue takes its",
    "request; a visit with send = 0 is idle for its visit time.",
], fill=BOX, stroke=INK, size=11, tsize=12.5, align="start", pad=12, valign="top")
RB = (175, 284, 120, 34)
JB = (175, 326, 120, 34)
f.box(*RB, "r: rotation", [], fill="white", stroke=INK, sw=1.6, tsize=11.5)
f.box(*JB, "j: element", [], fill="white", stroke=INK, sw=1.6, tsize=11.5)
f.line([(110, TT[1] + TT[3]), (110, 301), (RB[0] - 2, 301)], ORANGE, 1.4, end=True, dash=CTL)
f.line([(70, TT[1] + TT[3]), (70, 343), (JB[0] - 2, 343)], ORANGE, 1.4, end=True, dash=CTL)
f.text(118, 290, "count, clear", 10, "start", fill=ORANGE)
f.text(RB[0] + RB[2] / 2, RB[1] - 4, "18-bit counters", 10, "middle", fill=MUTED)
EM_X0, EM_X1 = 320, 360
poly([(EM_X0, 280), (EM_X1, 300), (EM_X1, 344), (EM_X0, 364)])
f.text(EM_X0 + 9, 305, "0", 10, "middle", "bold", fill=MUTED)
f.text(EM_X0 + 9, 347, "1", 10, "middle", "bold", fill=MUTED)
f.line([(RB[0] + RB[2], 301), (EM_X0 - 2, 301)], INK, 1.6, end=True)
f.line([(JB[0] + JB[2], 343), (EM_X0 - 2, 343)], INK, 1.6, end=True)
f.line([(EM_X0 + 20, TT[1] + TT[3]), (EM_X0 + 20, 287)], ORANGE, 1.4, end=True, dash=CTL)
f.text(EM_X0 + 26, 278, "kind", 10, "start", fill=ORANGE)
EBUS = 322
f.line([(EM_X1, EBUS), (918, EBUS)], INK, 2.2, end=True)
f.text(372, EBUS - 6, "e (18 bits): r in a group run, j in a stream run", 11, "start", "bold")

# ---------------- lt comp and size decode -------------------------------------------------------
LT = (920, 296, 110, 52)
f.box(*LT, "lt comp", ["e < count ?"], fill="white", stroke=INK, size=10.5, tsize=12)
f.line([(cx["count"], BB), (cx["count"], LT[1] - 2)], INK, 1.6, end=True)
SD = (1146, 296, 126, 52)
f.box(*SD, "size decode", ["z = 0, 1, 2; 00 = error"], fill="white", stroke=INK, size=10, tsize=12)
f.line([(cx["size"], BB), (cx["size"], SD[1] - 2)], INK, 1.6, end=True)
# send: up over the bar to the timetable
f.line([(LT[0] + LT[2], 322), (1046, 322), (1046, 58), (400, 58), (400, TT[1] - 2)], ORANGE, 1.6, end=True, dash=CTL)
f.text(1052, 286, "send", 11, "start", "bold", fill=ORANGE)
# gap after: to the timetable
f.line([(cx["gap"], BY), (cx["gap"], 76), (430, 76), (430, TT[1] - 2)], ORANGE, 1.4, end=True, dash=CTL)
f.text(480, 72, "gap after (stream runs)", 10.5, "start", fill=ORANGE)
f.text(480, 54, "send: in a stream run, send = 0 ends the stream", 10.5, "start", fill=ORANGE)

# ---------------- element address: source + (e << z) -----------------------------------------
ZB = 386                                        # z bus
SHY = 348
for k, (xx, lab) in enumerate([(445, "<< 0"), (490, "<< 1"), (535, "<< 2")]):
    dot(xx + 15, EBUS)
    f.line([(xx + 15, EBUS), (xx + 15, SHY - 2)], INK, 1.4, end=True)
    small(xx, SHY, 30, 22, lab)
MX0, MX1, MT, MB = 438, 572, 398, 426
poly([(MX0, MT), (MX1, MT), (MX1 - 22, MB), (MX0 + 22, MB)])
for xx in (460, 505, 550):
    f.line([(xx, SHY + 22), (xx, MT - 2)], INK, 1.4, end=True)
f.text(460, MT + 13, "0", 10, "middle", "bold", fill=MUTED)
f.text(505, MT + 13, "1", 10, "middle", "bold", fill=MUTED)
f.text(550, MT + 13, "2", 10, "middle", "bold", fill=MUTED)
f.line([(cx["size"] + 18, SD[1] + SD[3]), (cx["size"] + 18, ZB), (588, ZB), (588, 412), (MX1 - 9, 412)], ORANGE, 1.6, end=True, dash=CTL)
f.text(1000, ZB - 6, "z", 11, "start", "bold", fill=ORANGE)
AD = (470, 452, 170, 48)
f.line([(505, MB), (505, AD[1] - 2)], INK, 2, end=True)
f.text(512, 444, "e << z", 10.5, "start")
f.line([(cx["source"], BB), (cx["source"], AD[1] - 2)], INK, 2, end=True)
f.box(*AD, "+  (20-bit adder)", ["source + (e << z)"], fill=GREEN_L, stroke=GREEN, size=10.5, tsize=12)
f.lines(244, 392, [("per element:", {"weight": "bold"}), "z = 0: +1 chunk (32-bit)", "z = 1: +2 chunks (64-bit)", "z = 2: +4 chunks (128-bit)"], 11, lh=15)

# ---------------- mode-5 word: cm + (e << 1) or (e << 2) ---------------------------------------
for xx, lab in [(822, "<< 1"), (868, "<< 2")]:
    dot(xx + 15, EBUS)
    f.line([(xx + 15, EBUS), (xx + 15, SHY - 2)], INK, 1.4, end=True)
    small(xx, SHY, 30, 22, lab)
CM0, CM1 = 812, 910
poly([(CM0, MT), (CM1, MT), (CM1 - 16, MB), (CM0 + 16, MB)])
f.line([(837, SHY + 22), (837, MT - 2)], INK, 1.4, end=True)
f.line([(883, SHY + 22), (883, MT - 2)], INK, 1.4, end=True)
f.text(837, MT + 13, "0", 10, "middle", "bold", fill=MUTED)
f.text(883, MT + 13, "1", 10, "middle", "bold", fill=MUTED)
dot(940, ZB, ORANGE)
f.line([(940, ZB), (940, 412), (CM1 - 7, 412)], ORANGE, 1.4, end=True, dash=CTL)
f.text(946, 410, "z = 2", 10, "start", fill=ORANGE)
CA = (760, 452, 160, 48)
f.line([(861, MB), (861, CA[1] - 2)], INK, 1.6, end=True)
f.line([(cx["cm"], BB), (cx["cm"], CA[1] - 2)], INK, 1.6, end=True)
f.box(*CA, "+  (12-bit adder)", ["cm field + 2 per packet"], fill=PURPLE_L, stroke=PURPLE, size=10.5, tsize=12)
# addr mux: mode 5 ? word : 0
AM0, AM1, AMT, AMB = 790, 890, 524, 550
poly([(AM0, AMT), (AM1, AMT), (AM1 - 16, AMB), (AM0 + 16, AMB)])
f.line([(820, CA[1] + CA[3]), (820, AMT - 2)], INK, 1.6, end=True)
f.text(814, AMT + 12, "1", 10, "middle", "bold", fill=MUTED)
f.text(862, AMT - 6, "0", 10.5, "middle", "bold")
f.line([(862, AMT - 2), (862, AMT - 1)], INK, 1, end=False)
f.text(866, AMT + 12, "0", 10, "middle", "bold", fill=MUTED)

# ---------------- dst = (me + d) mod 64 --------------------------------------------------------
DA = (1060, 452, 96, 48)
f.line([(cx["d"], BB), (cx["d"], DA[1] - 2)], INK, 1.6, end=True)
f.box(*DA, "+  (6-bit)", ["(me + d) mod 64"], fill=SAND, stroke=ORANGE, size=10, tsize=12)
f.text(DA[0] - 6, 470, "me", 10.5, "end", "bold")
f.line([(DA[0] - 4, 476), (DA[0] - 2, 476)], INK, 1, end=False)

# mode: to the addr mux select and to the token
f.line([(cx["mode"], BB), (cx["mode"], 598)], INK, 1.6, end=True)
dot(cx["mode"], 537, ORANGE)
f.line([(cx["mode"], 537), (AM1 - 7, 537)], ORANGE, 1.4, end=True, dash=CTL)
f.text(1000, 532, "mode = 4", 10, "start", fill=ORANGE)

# ---------------- request, SRAM read queue, token, FIFO, packets ----------------------------------
RQ = (440, 600, 260, 58)
f.line([(555, AD[1] + AD[3]), (555, RQ[1] - 2)], INK, 2, end=True)
f.box(*RQ, "read request", ["chunk address (20), read size", "offered when send = 1"], fill="white", stroke=GREEN, sw=1.6, size=10.5, tsize=12)
f.line([(975, LT[1] + LT[3]), (975, 578), (680, 578), (680, RQ[1] - 2)], ORANGE, 1.4, end=True, dash=CTL)
RS = (440, 712, 260, 52)
f.line([(570, RQ[1] + RQ[3]), (570, RS[1] - 2)], GREEN, 2.4, end=True)
f.box(*RS, "SRAM read queue (figure 1.3, §4.5)", ["priority 2, below the GATHER engine"], fill=GREEN_L, stroke=GREEN, size=10.5, tsize=12)
# taken back to the timetable
f.line([(RS[0], 738), (18, 738), (18, 176), (TT[0] - 2, 176)], ORANGE, 1.6, end=True, dash=CTL)
f.text(30, 732, "taken: the visit is done; the timetable moves on", 10.5, "start", fill=ORANGE)

TK = (760, 600, 512, 58)
f.box(*TK, "token, pushed when the request is taken", ["dst (6), mode (3, zero-extended to 4), addr (12: the word, or 0), two packets (1: z = 2)"],
      fill="white", stroke=INK, sw=1.4, size=10.5, tsize=12)
f.line([(840, AMB), (840, TK[1] - 2)], INK, 1.6, end=True)
f.line([(1108, DA[1] + DA[3]), (1108, TK[1] - 2)], INK, 1.6, end=True)
f.line([(1250, SD[1] + SD[3]), (1250, TK[1] - 2)], ORANGE, 1.4, end=True, dash=CTL)
TF = (760, 712, 230, 52)
f.line([(875, TK[1] + TK[3]), (875, TF[1] - 2)], INK, 1.6, end=True)
f.box(*TF, "token FIFO, 64 entries", ["popped as each return arrives"], fill=BOX, stroke=INK, size=10.5, tsize=12)
PB = (1030, 712, 242, 52)
f.line([(TF[0] + TF[2], 738), (PB[0] - 2, 738)], INK, 1.6, end=True)
f.box(*PB, "packet builder", ["one packet; two for z = 2"], fill=GREEN_L, stroke=GREEN, size=10.5, tsize=12)
f.line([(570, RS[1] + RS[3]), (570, 792), (1080, 792), (1080, PB[1] + PB[3] + 2)], GREEN, 2, end=True)
f.text(600, 786, "read return: the element (32, 64 or 128 bits), in request order", 10.5, "start", fill=GREEN)
f.line([(1220, PB[1] + PB[3]), (1220, 812)], GREEN, 2.4, end=True)
f.text(1214, 826, "to injection (figure 1.3)", 10.5, "end", "bold", fill=GREEN)

# ---------------- increments table ----------------------------------------------------------------
TY = 868
f.text(30, TY, "The increments: element e of a stream, for each read size (source s, cm field c)", 13, "start", "bold")
cols = ["read size", "z", "e = 0", "e = 1", "e = 2", "e = 3", "mode-5 words (packets of element e)"]
rows = [
    ["32 bits", "0  (+1 chunk)", "s", "s + 1", "s + 2", "s + 3", "c + 2e"],
    ["64 bits", "1  (+2 chunks)", "s", "s + 2", "s + 4", "s + 6", "c + 2e"],
    ["128 bits", "2  (+4 chunks)", "s", "s + 4", "s + 8", "s + 12", "c + 4e, then c + 4e + 2"],
]
CX = [30, 150, 290, 400, 510, 620, 730, 1270]
RY, RH = TY + 12, 22
for i, row in enumerate([cols] + rows):
    y0 = RY + i * RH
    for c in range(7):
        f.rect(CX[c], y0, CX[c + 1] - CX[c], RH, BOX if i == 0 else "white", EDGE, 0.8, rx=0)
        f.text(CX[c] + 8, y0 + 15, row[c], 11.5, "start", "bold" if (i == 0 or c == 0) else "normal")

# ---------------- notes --------------------------------------------------------------------------
y = RY + 4 * RH + 26
notes = [
    "Nothing per stream is stored: each visit's address, dst and word come from its STREAM entry and the one counter, and the counters, the request and the token FIFO are the only state.",
    "The z = 2 packets: one request reads the 128-bit element from one row, or from two when the element crosses a row boundary; the packet builder sends it as two packets on consecutive cycles, low half then high, the second at addr + 2.",
    "A spanning read (any element that crosses a row boundary) costs a second row access (row + 1, possibly in the next area; figure 7.2). In a group run it costs a cycle and sets drift (TILE_CONDITIONS bit 0, §4.1), so a group run's streams are laid out to avoid it.",
    "A 32-bit element goes in the payload's low half with the high half 0 (§4.2). Read size 00 is an error even at count 0. A group's streams share one read size.",
    "A visit whose STREAM entry is not in the queue yet waits the same on every tile: part of the timetable, not lateness; with ag_lookahead at its bound it doesn't happen.",
    "A request the SRAM read queue does not take holds the visit until it is taken. In a group run that sets the drift condition (TILE_CONDITIONS bit 0, §4.13): the tile has fallen behind its timetable.",
]
for n in notes:
    for j, s in enumerate(wrap(n, 1230, 11.5)):
        f.text(30 + (12 if j else 0), y, ("• " if j == 0 else "") + s, 11.5, "start")
        y += 15
    y += 4

# ---------------- interface (moved here from the interface figure, 12 October) -----------------
from rtl import R as _R
IY = y + 16
f.text(30, IY + 4, "Interface", 14, "start", "bold")
IY = _R(f).table(30, IY + 16, [30, 250, 340, 640, 1270], [["signal", "width", "from → to", "meaning"],
    ["STREAM head", "64", "read-ahead (fig 6.3) → here", "count, d, mode, size, gap after, source, cm field"],
    ["e, offer", "18, 1", "sequencer (fig 6.2) → here", "the element (j or r); offer this cycle (a 128-bit visit takes 2 cycles)"],
    ["valid_2, request", "1, —", "here → SRAM read queue (fig 7.2)", "chunk = source + (e << z); dst = (me + d) mod 64; mode; width (one request per element)"],
    ["dest addr", "20", "here → token", "mode 5: cm + 2e (32, 64 bits) or cm + 4e (128: the builder adds 2 for the second packet); else 0"],
    ["taken_2", "1", "SRAM read queue → here, fig 6.2", "accepted this cycle"]])
LY = IY + 26
f.line([(30, LY), (70, LY)], INK, 2, end=True)
f.text(78, LY + 4, "datapath", 11.5, "start", fill=MUTED)
f.line([(170, LY), (210, LY)], GREEN, 2.4, end=True)
f.text(218, LY + 4, "SRAM read queue and its return", 11.5, "start", fill=MUTED)
f.line([(420, LY), (460, LY)], ORANGE, 1.6, end=True, dash=CTL)
f.text(468, LY + 4, "control and selects", 11.5, "start", fill=MUTED)
f.raw(f'<circle cx="640" cy="{LY}" r="3.2" fill="{INK}"/>')
f.text(650, LY + 4, "junction (lines that cross without a dot don't connect)", 11.5, "start", fill=MUTED)

f.raw("</g>")
H = LY + 92 + SHIFT
f.h = H
f.caption(["Each visit takes one STREAM entry and the counter e (the rotation r, or the element j): it sends element e if e < count, from source + (e << z),",
           "so the source steps by 1, 2 or 4 chunks per element for 32-, 64- or 128-bit reads. The token carries dst, mode and the mode-5 word to the packets."],
          y=H - 38, source="Source: spec/machine-spec.md v1.0, §4.2, §4.5, §4.13.")
f.save("fig6_4_ag_visit.svg")
