# Figure 7.3: the return path — the 64-entry token FIFO with its data slots, the row merge (a read from any chunk, one
# or two rows), the packet builder (the two packets of a 128-bit return), and the injection point (read returns first,
# then the send port). Item 8. No alignment (4.1); chunk selection from any starting chunk (13a).
from rtl import *

W = 1300
f = Fig(W, 2000, "Figure 7.3. The return path: the token FIFO, the packet builder and the injection point",
        "v1.0 (spec/machine-spec.md §4.1, §4.2, §4.5) — reads from any chunk; returns leave in request order, one packet a cycle")
r = R(f)

AY = 76
r.frame(16, AY, W - 32, 560, "A. Datapath (§4.1, §4.5, §4.5)")
# token push
r.lbl(30, AY + 70, "from the SRAM read queue (figure 7.2): taken", "start", c=ORANGE, weight="bold")
r.lbl(30, AY + 88, "token = {dst, mode, dest, width, chunk, span, rp, zero}", "start")
f.line([(30, AY + 100), (300, AY + 100)], INK, 1.8, end=True)
f.lines(30, AY + 120, ["chunk = chunk[1:0], the read's first chunk in its row.", "span: chunk + n > 4 (n = 1, 2 or 4 chunks), so the read",
                       "covers two rows (figure 7.2 sends parts 0 and 1).", "At the push: p0 = 0, p1 = ¬span.",
                       "rp: a read-port read (figure 3.3). zero: out of range,", "its slot is written 0, p0 = p1 = 1 at the push."], 10, lh=13, fill=MUTED)
# the FIFO
TF = (302, AY + 50, 344, 230)
f.rect(*TF, BOX, INK, 1.5, rx=6)
f.text(TF[0] + TF[2] / 2, TF[1] + 20, "token FIFO: 64 entries, circular", 12, "middle", "bold")
PARTS = {2: ("1", "0"), 3: ("1", "1"), 5: ("1", "1")}      # an entry waiting for its second row, a full one, the head
for k in range(6):
    y = TF[1] + 36 + k * 26
    nm = ["tail →", "", "", "", "", "head →"][k]
    full = k in (3, 5)
    f.rect(TF[0] + 70, y, 120, 22, "white", EDGE, 1, rx=0)
    f.text(TF[0] + 130, y + 15, "token" if k < 5 else "token (oldest)", 10, "middle")
    f.rect(TF[0] + 196, y, 90, 22, GREEN_L if full else ("#EAF2FB" if k == 2 else "white"), EDGE, 1, rx=0)
    f.text(TF[0] + 241, y + 15, "data 128" if full else ("part 0 in" if k == 2 else "—"), 10, "middle")
    p0, p1 = PARTS.get(k, ("0", "0" if k in (0,) else "1"))
    for j, v in enumerate((p0, p1)):
        f.rect(TF[0] + 292 + 22 * j, y, 20, 22, GREEN_L if full else "white", EDGE, 1, rx=0)
        f.text(TF[0] + 302 + 22 * j, y + 15, v, 10, "middle")
    if nm:
        f.text(TF[0] + 64, y + 15, nm, 10, "end", fill=MUTED)
f.text(TF[0] + 241, TF[1] + 34 - 4, "data slot", 9.5, "middle", fill=MUTED)
f.text(TF[0] + 313, TF[1] + 34 - 4, "p0 p1", 9.5, "middle", fill=MUTED)
f.text(TF[0] + 14, TF[1] + 212, "tail = the tag sent with the request; valid = p0 & p1", 10, "start", fill=MUTED)
# space back to figure 7.2
f.line([(TF[0] + 40, TF[1] + TF[3]), (TF[0] + 40, AY + 452), (30, AY + 452)], ORANGE, 1.4, end=True, dash=CTL)
r.lbl(30, AY + 466, "space = count < 64: a term of figure 7.2's \"ok\"", "start", c=ORANGE)
# the row merge
MG = (TF[0] + 70, AY + 316, 300, 106)
f.box(*MG, "row merge (write port of the slots)", ["slot chunk j ← row chunk (j + chunk) mod 4", "enable j: part 0: j < 4 − chunk; part 1: j ≥ 4 − chunk",
                                                   "(and j < n); sets p0 or p1 of entry rtag", "one rotator; the slot holds the data in order"],
      fill="white", stroke=GREEN, sw=1.5, size=10, tsize=11.5)
f.line([(TF[0] + 241, MG[1]), (TF[0] + 241, TF[1] + TF[3] + 1)], GREEN, 1.8, end=True)
f.text(TF[0] + 247, MG[1] - 10, "slot[rtag], p[rpart]", 9.5, "start", fill=GREEN)
f.line([(TF[0] + 241, AY + 470), (TF[0] + 241, MG[1] + MG[3] + 1)], GREEN, 1.8, end=True)
r.lbl(TF[0] + 241, AY + 482, "area returns: rdata[127:0], rtag, rpart", "middle", c=GREEN)
f.text(TF[0] + 241, AY + 498, "(any order; up to one per area per cycle; a span's two rows may come from two areas)", 10, "middle", fill=MUTED)
# the token's chunk to the merge
f.line([(TF[0] + 130, TF[1] + TF[3]), (TF[0] + 130, MG[1] - 1)], ORANGE, 1.3, end=True, dash=CTL)
f.text(TF[0] + 124, MG[1] - 10, "chunk[rtag]", 9.5, "end", fill=ORANGE)

# packet builder
PB = (712, AY + 50, 290, 230)
f.line([(TF[0] + TF[2], TF[1] + 36 + 5 * 26 + 11), (PB[0] - 1, TF[1] + 36 + 5 * 26 + 11)], GREEN, 2, end=True)
f.text(TF[0] + TF[2] + 6, TF[1] + 36 + 5 * 26 + 4, "head, valid", 9.5, "start", fill=GREEN)
f.box(*PB, "packet builder", ["the slot is already in order (row merge):", "32: slot[31:0], [63:32] = 0", "64: slot[63:0]",
                              "128: slot[63:0], then slot[127:64]", "", "addr: dest addr; a 128-bit mode-5", "return's second packet: addr + 2",
                              "half register h: pop after the last", "packet of the head token"], fill="white", stroke=INK, sw=1.5, size=10, tsize=11.5)
# injection mux
MX = 1040
mo, ma, mb = r.mux(MX, AY + 120, 90, labels=("1", "2"))
f.line([(PB[0] + PB[2], ma), (MX - 1, ma)], GREEN, 2, end=True)
f.text(PB[0] + PB[2] + 4, ma - 6, "return", 9.5, "start", fill=GREEN)
f.line([(MX - 40, AY + 330), (MX - 20, AY + 330), (MX - 20, mb), (MX - 1, mb)], PURPLE, 2, end=True)
r.lbl(MX - 44, AY + 330, "the send port's holding register (figure 3.4)", "end", c=PURPLE)
f.line([mo, (1250, mo[1])], INK, 2.2, end=True)
f.text(1250, mo[1] - 10, "to the injection", 10.5, "end", "bold")
f.text(1250, mo[1] + 16, "route block (fig 4.3)", 10.5, "end", "bold")
f.text(MX + 15, AY + 112, "select", 10, "middle", fill=ORANGE)
f.lines(MX + 46, AY + 236, ["injection: 1 = a return packet whenever", "the head is valid; 2 = the send port", "otherwise. No fairness: the send port",
                            "waits through a stream (§4.5)."], 10.5, lh=14, fill=MUTED)
# rp back to the read port
f.line([(PB[0] + 150, PB[1] + PB[3]), (PB[0] + 150, AY + 520), (PB[0] - 20, AY + 520)], ORANGE, 1.4, end=True, dash=CTL)
r.lbl(PB[0] - 24, AY + 520, "rp_inj: a read-port token injected → figure 3.3's in-flight count", "end", c=ORANGE)
f.text(30, AY + 546, "The injection point never stalls: the network has no flow control, and a full output FIFO is an overflow (condition 1, figure 4.3).",
       10.5, "start", fill=MUTED)

# B. reads from any chunk
BY = AY + 580
r.frame(16, BY, W - 32, 330, "B. Reads from any chunk (§4.1: no alignment): one or two rows, merged into the slot in order")
CW, CHH = 52, 24
P0C, P1C = GREEN_L, "#EAF2FB"


def rowbox(x, y, label, take, fill):
    f.text(x - 8, y + 16, label, 10, "end", "bold")
    for j in range(4):
        on = j in take
        f.rect(x + j * CW, y, CW, CHH, fill if on else "white", EDGE, 1, rx=0)
        f.text(x + j * CW + CW / 2, y + 16, f"c{j}", 10, "middle", "bold" if on else "normal", fill=INK if on else MUTED)


def slotbox(x, y, cells):
    f.text(x - 8, y + 16, "slot", 10, "end", "bold")
    for j in range(4):
        txt, fill = cells[j] if j < len(cells) else ("—", "white")
        f.rect(x + j * CW, y, CW, CHH, fill, EDGE, 1, rx=0)
        f.text(x + j * CW + CW / 2, y + 16, txt, 10, "middle")


EX = [("64 bits at chunk 1: one row", [1, 2], [], [("r.c1", P0C), ("r.c2", P0C)], "packet: slot[63:0]"),
      ("128 bits at chunk 1: two rows", [1, 2, 3], [0], [("r.c1", P0C), ("r.c2", P0C), ("r.c3", P0C), ("r+1.c0", P1C)],
       "packets: slot[63:0], then slot[127:64]"),
      ("64 bits at chunk 3: two rows", [3], [0], [("r.c3", P0C), ("r+1.c0", P1C)], "packet: slot[63:0]")]
for i, (title, t0, t1, cells, pk) in enumerate(EX):
    x = 110 + i * 410
    y = BY + 46
    f.text(x - 60, y + 4, title, 11.5, "start", "bold")
    rowbox(x, y + 18, "row r", t0, P0C)
    rowbox(x, y + 50, "row r+1", t1, P1C)
    f.line([(x + 2 * CW, y + 50 + CHH + 4), (x + 2 * CW, y + 98)], GREEN, 1.6, end=True)
    f.text(x + 2 * CW + 6, y + 92, "rotate by chunk", 9.5, "start", fill=GREEN)
    slotbox(x, y + 102, cells)
    f.text(x - 60, y + 146, pk, 10, "start", fill=MUTED)
f.text(30, BY + 212, "Chunks are 32 bits, chunk 0 is bits [31:0] of a row and of the slot. Green: part 0 (row r); blue: part 1 (row r + 1, the next area when r is an area's last row).", 10.5, "start", fill=MUTED)
rows_ = [["first chunk", "32-bit read", "64-bit read", "128-bit read"],
         ["0", "one row", "one row", "one row"],
         ["1", "one row", "one row", "two rows: 3 + 1 chunks"],
         ["2", "one row", "one row", "two rows: 2 + 2"],
         ["3", "one row", "two rows: 1 + 1", "two rows: 1 + 3"]]
r.table(30, BY + 228, [30, 180, 400, 620, 900], rows_, rh=19)
f.lines(920, BY + 244, ["A read covering two rows costs two area", "accesses and holds its token until both", "are in; the packets are the same as for", "a one-row read (§4.1, §4.5)."], 10.5, lh=15, fill=MUTED)

# C. an example of reordering
BY = BY + 350
r.frame(16, BY, W - 32, 220, "C. Out-of-order returns, in-order packets: four 64-bit reads to areas 2, 5, 2, 7 (one row each)")
rows = [["cycle", "t", "t+1", "t+2", "t+3", "t+4", "t+5", "t+6", "t+7"],
        ["taken (tag)", "0", "1", "2", "3", "", "", "", ""],
        ["area returns (tag)", "", "", "", "1", "0", "3", "2", ""],
        ["head", "0", "0", "0", "0", "0", "0", "1", "2"],
        ["packet injected", "", "", "", "", "", "tag 0", "tag 1", "tag 2"]]
r.table(30, BY + 40, [30, 180] + [180 + 120 * k for k in range(1, 9)], rows, rh=22)
f.text(30, BY + 172, "Tag 1 returns first but waits in its slot; tag 0's return (t+4) lets the head go at t+5, then 1 and 2 follow one a cycle (3 at t+8).", 10.5, "start", fill=MUTED)
f.text(30, BY + 188, "A slot is valid from the end of its last part's return cycle; the head is injected the next cycle (one register stage).", 10.5, "start", fill=MUTED)

# D. interface
CY = BY + 240
f.text(30, CY, "D. Interface", 14, "start", "bold")
rows = [["signal", "width", "from → to", "meaning"],
        ["push, token", "1, ~46", "SRAM read queue (fig 7.2) → here", "taken this cycle; tail is its tag"],
        ["space", "1", "here → SRAM read queue", "count < 64 (count before the cycle's pop)"],
        ["rvalid, rtag, rpart, rdata", "1, 6, 1, 128 per area", "areas → here", "a read's row (part 0 or 1), merged into slot[rtag]"],
        ["holding valid, packet", "1, 95", "send port (fig 3.4) → here", "a packet waiting in the holding register"],
        ["send_taken", "1", "here → send port", "the holding register was injected this cycle"],
        ["inj_valid, packet", "1, 95", "here → injection route block (fig 4.3)", "dst 7, mode 4, addr 20, payload 64; the router adds src"],
        ["rp_inj", "1", "here → read port (fig 3.3)", "a read-port token was injected"]]
y = r.table(30, CY + 12, [30, 230, 380, 680, 1270], rows)
y = r.notes(y + 26, ["The token FIFO absorbs a read's queueing at its area; how much a phase's placement needs is still open (§4.2), and 64 entries is the depth (§10).",
                     "In HALT the token FIFO is held empty (§4.5); no packet is in flight when RUN/HALT changes."])
finish(f, r, y, ["Every read taken pushes a token whose tail index is the read's tag. A read may start at any chunk: its one or two rows return in any order and are",
                 "merged into its slot in order; the head leaves when both parts are in, as one packet (two for 128 bits), so packets trail the requests in request",
                 "order. The injection point takes a return packet whenever one is ready, and the send port's packet otherwise."],
       "Source: spec/machine-spec.md v1.0.",
       legend=[("line", GREEN, "reads and returns"), ("line", PURPLE, "the controller"), ("ctrl", ORANGE, "control"), ("dot", INK, "junction")])
f.save("fig7_3_returns.svg")
