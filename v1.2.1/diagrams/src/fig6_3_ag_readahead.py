# Figure 6.3: the address generator's read-ahead — the GROUP and STREAM readers, the tables across the island, the two queues.
# The scheme (spec §4.2): no slot working registers; STREAM read on every visit.
from common import *

W, H = 1300, 1000
f = Fig(W, H, "Figure 6.3. The address generator, reader side: GROUP and STREAM read ahead of the timetable",
        "v1.0 (spec/machine-spec.md §4.2, §4.10) — figure 6.4 is the visit side that consumes the two queues")

CTL = DASH["ctrl"]
REG_FILL, REG_EDGE = SAND, ORANGE


def dot(x, y, c=INK):
    f.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{c}"/>')


def queue(x, y, cells, cw, ch, labels, title, fill):
    f.text(x, y - 10, title, 12.5, "start", "bold")
    for k in range(cells):
        f.rect(x + k * cw, y, cw, ch, fill, EDGE, 1, rx=0)
        f.text(x + k * cw + cw / 2, y + ch / 2 + 4, labels[k], 11, "middle", fill=MUTED)
    f.text(x + cells * cw + 6, y - 6, "head", 10.5, "start", fill=MUTED)


# ---------------- left: the run and the two readers -----------------------------------------
MC = (30, 92, 330, 54)
f.box(*MC, "DMA_CONFIG: the run (§8.5)", ["kind (stream run or group run), start, count"], fill=REG_FILL, stroke=REG_EDGE, size=11, tsize=12.5)

CR = (30, 196, 330, 104)
f.box(*CR, "GROUP reader (group runs only)", [
    "group index g = start … start + count − 1",
    "reads GROUP[g], one read per cycle,",
    "while the GROUP queue has a free credit",
], fill=BOX, stroke=INK, size=11, tsize=12.5)

SR = (30, 384, 330, 162)
f.box(*SR, "STREAM reader", [
    ("Group run: for the group at its reader pointer,", {}),
    ("  rr = 0 … R − 1, i = 0 … t − 1:", {"mono": False}),
    ("  read STREAM[start stream + i]", {"weight": "bold"}),
    ("  R = 0: no reads, the pointer moves on", {}),
    ("Stream run: k = 0 … count − 1: read STREAM[start + k]", {}),
    ("one read per cycle, while a STREAM credit is free", {}),
], fill=BOX, stroke=INK, size=11, tsize=12.5, align="start", pad=12)

# DMA_CONFIG into both readers
f.line([(60, MC[1] + MC[3]), (60, CR[1] - 2)], ORANGE, 1.8, end=True, dash=CTL)
dot(60, 172, ORANGE)
f.line([(60, 172), (16, 172), (16, 470), (CR[0] - 2, 470)], ORANGE, 1.8, end=True, dash=CTL)
f.text(68, 182, "run fields", 10.5, "start", fill=ORANGE)

# ---------------- middle: the control island with the two tables --------------------------
IS = (420, 172, 230, 384)
f.rect(*IS, "white", PURPLE, 1.4, rx=12, dash="7 4")
f.text(IS[0] + IS[2] / 2, IS[1] + IS[3] - 12, "control island (§12)", 11.5, "middle", "bold", fill=PURPLE)
CM = (455, 212, 160, 72)
SM = (455, 420, 160, 72)
f.box(*CM, "GROUP[64]", ["64-bit entries", "one read per cycle"], fill=PURPLE_L, stroke=PURPLE, size=10.5, tsize=13)
f.box(*SM, "STREAM[256]", ["64-bit entries", "one read per cycle"], fill=PURPLE_L, stroke=PURPLE, size=10.5, tsize=13)

# reader -> table -> queue
f.line([(CR[0] + CR[2], 240), (CM[0] - 2, 240)], INK, 2, end=True)
f.text(CR[0] + CR[2] + 8, 232, "g", 11, "start", "bold")
f.text(CR[0] + CR[2] + 8, 256, "+ L_ts", 10.5, "start", fill=MUTED)
f.line([(SR[0] + SR[2], 456), (SM[0] - 2, 456)], INK, 2, end=True)
f.text(SR[0] + SR[2] + 8, 448, "index", 11, "start", "bold")
f.text(SR[0] + SR[2] + 8, 472, "+ L_ts", 10.5, "start", fill=MUTED)

QX, CW, CH = 740, 58, 46
CQ_Y, SQ_Y = 226, 434
f.line([(CM[0] + CM[2], 249), (QX - 2, 249)], INK, 2, end=True)
f.text(IS[0] + IS[2] + 10, 241, "entry", 11, "start", "bold")
f.text(IS[0] + IS[2] + 10, 265, "+ L_ts", 10.5, "start", fill=MUTED)
f.line([(SM[0] + SM[2], 457), (QX - 2, 457)], INK, 2, end=True)
f.text(IS[0] + IS[2] + 10, 449, "entry", 11, "start", "bold")
f.text(IS[0] + IS[2] + 10, 473, "+ L_ts", 10.5, "start", fill=MUTED)

# ---------------- right: the two queues -------------------------------------------------------
queue(QX, CQ_Y, 4, CW, CH, ["g + 3", "g + 2", "g + 1", "g"], "GROUP queue", BOX)
queue(QX, SQ_Y, 4, CW, CH, ["…", "…", "…", "next"], "STREAM queue", BOX)
QR = QX + 4 * CW
f.lines(QX, CQ_Y + CH + 30, ["ag_lookahead entries (4 shown), each", "R (18), start stream (8), t − 1 (4), gap (8)"], 10.5, fill=MUTED, lh=14)
f.lines(QX, SQ_Y + CH + 30, ["ag_lookahead entries (4 shown), each the whole", "entry: source, cm field, count, d, mode, size, gap after"], 10.5, fill=MUTED, lh=14)

# outputs to figure 6.4
O1 = (1060, 210, 210, 66)
O2 = (1060, 420, 210, 66)
f.box(*O1, "timetable (figure 6.4)", ["the head is the current group:", "R, t, gap; popped at its end"], fill="white", stroke=INK, size=10.5, tsize=12)
f.box(*O2, "visit datapath (figure 6.4)", ["one entry per visit;", "popped as each visit is made"], fill="white", stroke=INK, size=10.5, tsize=12)
f.line([(QR, CQ_Y + CH / 2), (O1[0] - 2, CQ_Y + CH / 2)], INK, 2, end=True)
f.line([(QR, SQ_Y + CH / 2), (O2[0] - 2, SQ_Y + CH / 2)], INK, 2, end=True)

# the STREAM reader's pointer into the GROUP queue (a second read pointer)
RPX = QX + 3 * CW + CW / 2 + 8
f.line([(RPX, CQ_Y + CH), (RPX, 340), (300, 340), (300, SR[1] - 2)], INK, 1.6, end=True)
dot(RPX, CQ_Y + CH + 2)
f.text(RPX - 8, 334, "reader pointer, at or after the head: the STREAM reader's group", 10.5, "end")
f.text(306, 372, "start stream, t, R", 10.5, "start")

# credits back to the readers
f.line([(QX + 2 * CW, CQ_Y), (QX + 2 * CW, 186), (330, 186), (330, CR[1] - 2)], ORANGE, 1.6, end=True, dash=CTL)
f.text(QX + 2 * CW + 8, 180, "credit back when its group ends", 10.5, "start", fill=ORANGE)
f.line([(QX + 2 * CW, SQ_Y + CH + 64), (QX + 2 * CW, 582), (200, 582), (200, SR[1] + SR[3] + 2)], ORANGE, 1.6, end=True, dash=CTL)
f.text(470, 576, "credit back when a visit uses its entry", 10.5, "start", fill=ORANGE)

# ---------------- example: read order = visit order ---------------------------------------
EY = 640
f.text(30, EY, "Example: a group run of three groups. The STREAM reads come out in visit order, so the queue is simply consumed from its head.", 13, "start", "bold")
f.text(30, EY + 22, "g0: streams a, b, c (t = 3), R = 2;  g1: streams d, e (t = 2), R = 0;  g2: streams f, g, h, k (t = 4), R = 1", 12, "start", fill=MUTED)
seq = [("a", 0), ("b", 0), ("c", 0), ("a", 0), ("b", 0), ("c", 0), ("f", 2), ("g", 2), ("h", 2), ("k", 2)]
SX, SY, SW_, SH = 150, EY + 44, 74, 34
f.text(30, SY + 22, "STREAM reads", 12, "start", "bold")
f.text(30, SY + 96, "GROUP reads", 12, "start", "bold")
fills = {0: GREEN_L, 2: BLUE_L}
for k, (s, g) in enumerate(seq):
    x = SX + k * SW_ + (40 if g == 2 else 0)
    f.rect(x, SY, SW_, SH, fills[g], EDGE, 1, rx=0)
    f.text(x + SW_ / 2, SY + 22, f"STREAM[{s}]", 10.5, "middle")
# group brackets under the strip
def bracket(x0, x1, y, label):
    f.raw(f'<path d="M{x0},{y} v6 H{x1} v-6" fill="none" stroke="{MUTED}" stroke-width="1"/>')
    f.text((x0 + x1) / 2, y + 20, label, 10.5, "middle", fill=MUTED)
bracket(SX + 2, SX + 3 * SW_ - 2, SY + SH + 4, "g0, rr = 0")
bracket(SX + 3 * SW_ + 2, SX + 6 * SW_ - 2, SY + SH + 4, "g0, rr = 1")
f.text(SX + 6 * SW_ + 20, SY + 15, "g1", 10.5, "middle", "bold", fill=ORANGE)
f.text(SX + 6 * SW_ + 20, SY + 29, "R = 0", 10, "middle", fill=ORANGE)
bracket(SX + 6 * SW_ + 42, SX + 10 * SW_ + 38, SY + SH + 4, "g2, rr = 0")
for k, g in enumerate(["GROUP[g0]", "GROUP[g1]", "GROUP[g2]"]):
    f.rect(SX + k * 92, SY + 76, 92, 30, "white", EDGE, 1, rx=0)
    f.text(SX + k * 92 + 46, SY + 96, g, 11, "middle")
f.lines(SX + 300, SY + 88, ["the GROUP reader runs ahead of the STREAM reader; g1's entry is read but", "passed by the reader pointer (no STREAM reads), and takes one cycle on the timetable"], 11, fill=MUTED, lh=14)

# ---------------- notes -------------------------------------------------------------------------
NY = SY + 146
notes = [
    "The read order depends only on the entries, never on the data, so both readers run ahead freely; nothing in the visit side feeds back except the credits.",
    "An entry holds its place from the cycle its read is issued until its visit uses it (a GROUP entry: until its group ends); that place is the credit.",
    "A table read is a round trip of 2·L_ts + 1 cycles (L_ts = ts_engine_latency each way, one in the memory), so ag_lookahead = 2·L_ts + 2 keeps both queues full whenever "
    "every group lasts at least 2 cycles: 4 at the default. Back-to-back one-visit groups would need 4·L_ts + 3, since each STREAM read waits for its GROUP entry; no group of the sort is that short.",
    "Groups start one per cycle: a zero-length group (R = 0, gap 0) takes one cycle. A group start or visit waiting for its entries waits the same on every tile, since the reads depend "
    "only on the tables and ts_engine_latency: that wait is part of the timetable, not lateness, and at ag_lookahead's bound it doesn't happen "
    "[meas: meshsim]. Only an SRAM read queue refusal makes a tile late: the drift condition (figure 6.4).",
    "Start begins the readers. A group run's first request goes out once group start's GROUP entry and its first STREAM entry are in: two round trips: 4·L_ts + 3 cycles after the start store (4·L_ts + 2 after the cycle the start takes effect); a stream run's after one: 2·L_ts + 2 (2·L_ts + 1). "
    "A stream run uses only the STREAM reader and queue, and is ready at once.",
    "Nothing is written back: STREAM and GROUP stay as the controller wrote them, so groups can share streams and a run can be repeated.",
]
y = NY
for n in notes:
    for j, s in enumerate(wrap(n, 1230, 11.5)):
        f.text(30 + (12 if j else 0), y, ("• " if j == 0 else "") + s, 11.5, "start")
        y += 15
    y += 4

# ---------------- legend ---------------------------------------------------------------------
# ---------------- interface (moved here from the interface figure, 12 October) -----------------
from rtl import R as _R
IY = y + 18
f.text(30, IY + 4, "Interface", 14, "start", "bold")
IY = _R(f).table(30, IY + 16, [30, 250, 340, 640, 1270], [["signal", "width", "from → to", "meaning"],
    ["start, kind, start field, count", "1, 1, 8, 9", "DMA_PULSE, DMA_CONFIG (fig 6.2) → here", "begin the readers"],
    ["GROUP read: addr, data", "6, 64", "here and the GROUP table", "a round trip of 2·L_ts + 1 cycles"],
    ["STREAM read: addr, data", "8, 64", "here and the STREAM table", "a round trip of 2·L_ts + 1 cycles"],
    ["GROUP head, valid; STREAM head, valid", "64, 1; 64, 1", "here → sequencer (fig 6.2), visit (fig 6.4)", "ag_lookahead entries each"],
    ["pop_group, pop_stream", "1, 1", "sequencer (fig 6.2) → here", "return a credit"]])
LY = IY + 26
f.line([(30, LY), (70, LY)], INK, 2, end=True)
f.text(78, LY + 4, "table reads and entries", 11.5, "start", fill=MUTED)
f.line([(260, LY), (300, LY)], ORANGE, 1.8, end=True, dash=CTL)
f.text(308, LY + 4, "control: run fields, credits", 11.5, "start", fill=MUTED)
f.raw(f'<circle cx="530" cy="{LY}" r="3.2" fill="{INK}"/>')
f.text(540, LY + 4, "junction (lines that cross without a dot don't connect)", 11.5, "start", fill=MUTED)

H = LY + 92
f.h = H
f.caption(["Two readers walk the tables ahead of the timetable: GROUP entries in group order, STREAM entries in visit order (each group's streams, R times over).",
           "Two queues of ag_lookahead entries absorb the tables' round trip. The visit side (figure 6.4) pops them and returns a credit for each entry."],
          y=H - 38, source="Source: spec/machine-spec.md v1.0, §4.2, §4.10, §10, §12.")
f.save("fig6_3_ag_readahead.svg")
