# Figures 4.1 and 4.2: the router of tile 0 (with the C&C port, spec 4.17) and of tiles 1–63 (spec 4.6, 4.7).
# Adapted from design_v1/diagrams/figures-new/fig4.py: same geometry, 102-bit packets, v2 names.
# draw(True) writes figure 4.1 (tile 0); fig3_router.py calls draw(False) for figure 4.2.
from common import *

WIRE, MINT, LAV, FIFO = "#9a9993", GREEN_L, PURPLE_L, BOX
K = 1.6            # v1 coordinates are scaled by K inside the drawing group
GX, GY0 = 30, 76   # where the drawing group sits on the page


def draw(TILE0):
    VW, VH = 760, (726 if TILE0 else 611)          # v1 drawing size, plus room for the two-part tile box
    W, H = 1300, int(GY0 + VH * K + 84)
    if TILE0:
        f = Fig(W, H, "Figure 4.1. The router of tile 0: the router of tiles 1–63 plus a fifth port for C&C",
                "v1.0 (spec/machine-spec.md §4.6, §4.7, §4.16) — tiles 1–63 plus the fifth port; 102-bit packets")
    else:
        f = Fig(W, H, "Figure 4.2. The router of tiles 1–63: five route blocks, output queue sets and round-robin selectors",
                "v1.0 (spec/machine-spec.md §4.6, §4.7) — tiles 1–63; 102-bit packets")
    f.raw(f'<g transform="translate({GX},{GY0}) scale({K})">')

    def t(x, y, s, size=11, anchor="middle", weight="normal", fill=INK):
        f.text(x, y + size * 0.35, s, size, anchor, weight, fill=fill)

    def rect(x, y, w, h, fill, rx=5, sw=0.8, stroke=EDGE):
        f.rect(x, y, w, h, fill, stroke, sw, rx=rx)

    def path(pts, c=INK, w=1.2, arrow=False):
        f.line(pts, c, w, end=arrow)

    def dot(x, y, c):
        f.raw(f'<circle cx="{x}" cy="{y}" r="2.2" fill="{c}"/>')

    # ---- input route blocks
    RX, RW, RH = 76, 58, 26
    src = {"C": 102, "L": 147, "R": 192, "U": 237, "D": 282} if TILE0 else {"L": 72, "R": 117, "U": 162, "D": 207}
    names = {"C": "from C&C", "L": "from left", "R": "from right", "U": "from up", "D": "from down"}
    for k, y in src.items():
        t(20, y - 12, names[k], 12, "start", fill=ORANGE if k == "C" else INK)
        path([(20, y + 4), (RX - 2, y + 4)], ORANGE if k == "C" else INK, 1.5, True)
        rect(RX, y - 9, RW, RH, MINT)
        t(RX + RW / 2, y + 4, "route", 12)
    GY = {k: y - 3 for k, y in src.items()}
    EY = {k: y + 9 for k, y in src.items()}

    # ---- output banks
    FX, FW, FH = 470, 72, 10
    if TILE0:
        banks = [("to C&C", [("from left", "L"), ("from right", "R"), ("from up", "U"), ("from down", "D"), ("injection", "P")], 50),
                 ("to right", [("from C&C", "C"), ("from left", "L"), ("injection", "P")], GY["L"] - 12),
                 ("to left", [("from C&C", "C"), ("from right", "R"), ("injection", "P")], GY["R"] - 12),
                 ("to up", [("from C&C", "C"), ("from left", "L"), ("from right", "R"), ("from down", "D"), ("injection", "P")], 320),
                 ("to down", [("from C&C", "C"), ("from left", "L"), ("from right", "R"), ("from up", "U"), ("injection", "P")], 410)]
    else:
        banks = [("to right", [("from left", "L"), ("injection", "P")], GY["L"]),
                 ("to left", [("from right", "R"), ("injection", "P")], GY["R"]),
                 ("to up", [("from left", "L"), ("from right", "R"), ("from down", "D"), ("injection", "P")], 250),
                 ("to down", [("from left", "L"), ("from right", "R"), ("from up", "U"), ("injection", "P")], 330)]
    rowy = {}
    for name, rows, y0 in banks:
        for i, (lab, s) in enumerate(rows):
            y = y0 + 12 * i
            rect(FX, y - FH / 2, FW, FH, ORANGE_L if s == "C" else FIFO, 2, 0.6)
            t(FX + FW / 2, y, lab, 7.5)
            rowy[(name, s)] = y
        yc = y0 + 12 * (len(rows) - 1) / 2
        path([(FX + FW, yc), (FX + FW + 20, yc)], INK, 1.2)
        rect(FX + FW + 20, yc - 13, 38, 26, LAV)
        t(FX + FW + 39, yc, "RR", 12)
        c = ORANGE if name == "to C&C" else INK
        path([(FX + FW + 58, yc), (FX + FW + 104, yc)], c, 1.5, True)
        t(FX + FW + 108, yc, name, 12, "start", fill=c)
    t(560, 12, "each grey bar is one FIFO, 16 entries × 102 bits (queue_depth);", 8.5)
    t(560, 24, "one FIFO per feeder, a round-robin selector per output", 8.5)

    # ---- straight-through and turning lines: one vertical bus per source
    chan = {"C": 330, "L": 310, "R": 290, "U": 270, "D": 250} if TILE0 else {"L": 300, "R": 280, "U": 260, "D": 240}
    for s in src:
        ys = GY[s]
        targets = [rowy[(b, s)] for b, *_ in banks if (b, s) in rowy]
        far = [y for y in targets if y != ys]
        if any(y == ys for y in targets):
            path([(RX + RW, ys), (FX - 1, ys)], WIRE, 1.0, True)
        else:
            path([(RX + RW, ys), (chan[s], ys)], WIRE, 1.0)
        if far:
            lo, hi = min(far + [ys]), max(far + [ys])
            path([(chan[s], lo), (chan[s], hi)], WIRE, 1.0)
            if any(y == ys for y in targets) or lo < ys < hi:
                dot(chan[s], ys, WIRE)
            for y in far:
                path([(chan[s], y), (FX - 1, y)], WIRE, 1.0, True)
                if lo < y < hi:
                    dot(chan[s], y, WIRE)

    # ---- injection: route block -> one FIFO per output, and the loopback
    IX = 452
    e0 = 545 if TILE0 else 430
    inj_y = e0 + 23
    rect(520, inj_y - 13, 60, 26, MINT)
    t(550, inj_y, "route", 12)
    p_rows = [rowy[(b, "P")] for b, *_ in banks]
    path([(520, inj_y), (IX, inj_y), (IX, min(p_rows))], INK, 1.2)
    for y in p_rows:
        path([(IX, y), (FX - 1, y)], INK, 1.2, True)
        if y != min(p_rows):
            dot(IX, y, INK)

    # ---- eject bank
    EX, EFW = 195, 72
    ej = [("loopback", None)] + ([("from C&C", "C")] if TILE0 else []) + [("from left", "L"), ("from right", "R"), ("from up", "U"), ("from down", "D")]
    NE = len(ej)
    EMID = e0 + 11 * (NE - 1) / 2
    for i, (lab, s) in enumerate(ej):
        y = e0 + 11 * i
        rect(EX, y - 5, EFW, 10, ORANGE_L if s == "C" else FIFO, 2, 0.6)
        t(EX + EFW / 2, y, lab, 7.5)
    path([(EX + EFW, EMID), (EX + EFW + 20, EMID)], INK, 1.2)
    rect(EX + EFW + 20, EMID - 13, 38, 26, LAV)
    t(EX + EFW + 39, EMID, "RR", 12)
    t(EX + 4, e0 - 34, "eject: dst = me, or broadcast (126)", 11, "start")
    t(EX + 4, e0 - 20, f"({NE} FIFOs, 16 deep each)", 9, "start")
    path([(IX, e0), (EX + EFW + 1, e0)], INK, 1.2, True)
    dot(IX, e0, INK)
    t(390, e0 - 8, "loopback (dst = me or 126)", 9)
    bus = {"C": 180, "L": 170, "R": 160, "U": 150, "D": 140} if TILE0 else {"L": 172, "R": 162, "U": 152, "D": 142}
    for s in src:
        y = e0 + 11 * [x[1] for x in ej].index(s)
        path([(RX + RW, EY[s]), (bus[s], EY[s]), (bus[s], y), (EX - 1, y)], INK, 1.2, True)

    # ---- the rest of the tile: traffic director (in) and injection point (out)
    PY = e0 + 96
    rect(150, PY, 540, 78, BOX, 6)
    t(420, PY + 13, "the rest of the tile (figure 1.3)", 10, weight="bold")
    rect(250, PY + 26, 190, 40, "white", 4, 0.8, stroke=INK)
    t(345, PY + 39, "traffic director", 10.5, weight="bold")
    t(345, PY + 54, "routes each packet by its mode", 8)
    rect(460, PY + 26, 190, 40, GREEN_L, 4, 0.8, stroke=GREEN)
    t(555, PY + 39, "injection point", 10.5, weight="bold")
    t(555, PY + 54, "read returns first, then the send port", 8)
    rx = EX + EFW + 58
    path([(rx, EMID), (345, EMID), (345, PY + 25)], INK, 1.2, True)
    t(351, PY - 26, "in: src 7 + mode 4 + addr 20 + payload 64", 8.5, "start")
    path([(550, PY + 26), (550, inj_y + 14)], INK, 1.2, True)
    t(556, PY - 32, "out: dst 7 + mode 4 + addr 20 + payload 64", 8.5, "start")
    t(556, PY - 20, "(the router adds src: 102 bits on the link)", 8.5, "start")

    if TILE0:
        t(20, 50, "C&C's route block routes as an injection at tile 0:", 8.5, "start", fill=ORANGE)
        t(20, 61, "it never waits behind tile 0's injection point", 8.5, "start", fill=ORANGE)
        t(FX + FW + 108, 50 + 24 + 16, "every packet for dst 64", 8.5, "start", fill=ORANGE)
    f.raw("</g>")

    if TILE0:
        cap = ["Tile 0's router is the router of tiles 1–63 (figure 4.2) plus a sixth route block for packets from C&C, a from-C&C FIFO (orange) on each of the",
               "five outputs, so C&C gets a fair round-robin share of each, and a sixth output, to C&C, with five FIFOs of its own: ten extra FIFOs in all."]
        src_ = "Source: spec/machine-spec.md v1.0, §4.6, §4.7, §4.16."
        name = "fig4_1_router_tile0.svg"
    else:
        cap = ["Each route block forwards an arriving packet in one cycle to output FIFOs, one FIFO per feeder; each output's round-robin selector sends one packet",
               "per cycle. Routes go horizontal first, then vertical, so to-right and to-left have two feeders each, to-up and to-down four, and eject five: 17 FIFOs."]
        src_ = "Source: spec/machine-spec.md v1.0, §4.6, §4.7."
        name = "fig4_2_router.svg"
    f.caption(cap, y=H - 38, source=src_)
    f.save(name)


if __name__ == "__main__":
    draw(True)
