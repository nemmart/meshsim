# Figure 4.3: the router datapath — a route block's decision (hops ROM, horizontal progress, direction, eject, the broadcast
# tree), an output's queue set and round-robin selector, router overflow, the link pipeline, and tile 0's C&C port. Item 1.
from rtl import *

W = 1300
f = Fig(W, 2000, "Figure 4.3. The router: the route decision, the output queues and round-robin selectors, and the link pipeline",
        "v1.0 (spec/machine-spec.md §3.2, §4.6, §4.7, §4.16) — figures 4.1 and 4.2 are the block view; this is what a route block and an output do")
r = R(f)

AY = 76
r.frame(16, AY, W - 32, 330, "A. A route block: one per link input, one for the loopback (injection), and on tile 0 one from C&C (§4.6)")
steps = [("k = (dst − src) mod 64", ["dst 64 → 0", "src 64 (C&C) → 0"]),
         ("hops ROM [k] → (b, a)", ["64 entries, hard-wired (§4.6)", "|b|, |a| ≤ 4; offsets 29–32 down,", "33–35 up"]),
         ("hb, horizontal progress", ["b ≥ 0: (col(me) − col(src)) mod 8", "b < 0: −((col(src) − col(me)) mod 8)"]),
         ("decide", ["hb ≠ b: horizontal, sign(b)", "else me ≠ dst: vertical, sign(a)", "else eject (tile 0, dst 64: to C&C)"])]
x = 30
for i, (t, body) in enumerate(steps):
    f.box(x, AY + 50, 280, 96, t, body, fill=BOX if i != 1 else GREEN_L, stroke=INK if i != 1 else GREEN, sw=1.4, size=10, tsize=11.5)
    if i < 3:
        f.line([(x + 280, AY + 98), (x + 312, AY + 98)], INK, 1.6, end=True)
    x += 312
f.text(30, AY + 172, "Unicast: the result is one push enable, into this block's FIFO in the chosen output's queue set (B). Idle packets are discarded. One cycle.", 10.5, "start", fill=MUTED)
f.text(30, AY + 196, "Broadcast (dst 126): copies by arriving direction, s = src (64 → 0):", 11.5, "start", "bold")
rows = [["arriving", "eject", "right", "left", "down", "up"],
        ["injection (or C&C on tile 0)", "yes (not to C&C)", "yes", "yes", "yes", "yes"],
        ["from left", "yes", "(col(me) − col(s)) mod 8 ≠ 4", "—", "yes", "yes"],
        ["from right", "yes", "—", "(col(s) − col(me)) mod 8 ≠ 3", "yes", "yes"],
        ["from up / from down", "yes", "—", "—", "from up: (row(me) − row(s)) mod 8 ≠ 4", "from down: (row(s) − row(me)) mod 8 ≠ 3"]]
r.table(30, AY + 206, [30, 230, 340, 540, 740, 1000, 1270], rows, rh=20, size=10)

# B. an output
BY = AY + 350
r.frame(16, BY, W - 32, 330, "B. One output: its queue set, the round-robin selector, overflow, and the link pipeline (§4.7, §3.2)")
fd = ["injection", "from-left", "from-right", "from-up"]
for i, s in enumerate(fd):
    y = BY + 54 + i * 50
    f.rect(170, y, 170, 36, BOX, INK, 1.3, rx=4)
    f.text(255, y + 15, f"FIFO: {s}", 10.5, "middle", "bold")
    f.text(255, y + 29, "16 × 102 bits", 9.5, "middle", fill=MUTED)
    f.line([(120, y + 18), (169, y + 18)], INK, 1.5, end=True)
    f.text(116, y + 22, s if i else "injection", 10, "end", fill=MUTED)
    f.line([(340, y + 18), (400, y + 18), (400, BY + 160), (439, BY + 160)] if False else [(340, y + 18), (420, y + 18)], INK, 1.5, end=False)
f.text(30, BY + 270, "to-down shown; feeders per output:", 10.5, "start", fill=MUTED)
f.lines(30, BY + 286, ["to-right: injection, from-left · to-left: injection, from-right",
                       "to-up: injection, from-left, from-right, from-down · eject: loopback + all four"], 10, lh=13, fill=MUTED)
mo, _, _ = r.mux(420, BY + 54, 186, w=40, labels=("", ""))
f.text(440, BY + 150, "sel", 10, "middle", fill=ORANGE)
RP = (420, BY + 254, 220, 52)
r.reg(*RP, "round-robin pointer", ["serve the first non-empty FIFO at or", "after it; then pointer ← served + 1"], fill="white", stroke=ORANGE, size=9.5, tsize=11)
f.line([(440, RP[1] - 1), (440, BY + 232)], ORANGE, 1.3, end=True, dash=CTL)
# link pipeline
LS = [(520, "selector", "output register"), (650, "link stage 2", ""), (780, "link stage 3", "")]
f.line([mo, (519, mo[1])], INK, 2, end=True)
for x0, t, b in LS:
    r.reg(x0, mo[1] - 26, 110, 52, t, [b] if b else [], fill=BLUE_L if x0 > 520 else SAND)
    f.line([(x0 + 110, mo[1]), (x0 + 129, mo[1])], INK, 2, end=True)
r.reg(910, mo[1] - 26, 150, 52, "route block, far tile", ["and its FIFO write"], fill=BOX)
f.text(1070, mo[1] - 30, "a hop = 3 link stages", 10.5, "start", "bold")
f.text(1070, mo[1] - 14, "+ 1 route-and-FIFO cycle", 10.5, "start", "bold")
f.text(1070, mo[1] + 2, "= 4 cycles (§3.2)", 10.5, "start", "bold")
f.text(1070, mo[1] + 22, "broadcast bound 8 × (link_latency + 1)", 10, "start", fill=MUTED)
f.text(1070, mo[1] + 36, "[meas: meshsim]", 10, "start", fill=MUTED)
# overflow
OV = r.gate_or(820, BY + 260, 2, "router overflow")
r.lbl(800, OV[0], "push & full, any FIFO of the tile", c=INK)
r.lbl(800, OV[1], "(tile 0: the C&C FIFOs too)", c=MUTED)
f.line([(806, OV[0]), (824, OV[0])], INK, 1.3, end=False); f.line([(806, OV[1]), (824, OV[1])], INK, 1.3, end=False)
f.line([(858, BY + 260), (900, BY + 260)], RED, 1.6, end=True)
r.lbl(906, BY + 260, "set[1]: the packet is lost (figure 2.4)", "start", c=RED, weight="bold")

# C. tile 0
CY = BY + 350
r.frame(16, CY, W - 32, 150, "C. Tile 0's fifth port (§4.16)")
f.lines(30, CY + 50, [
    "From C&C: a sixth route block, deciding as an injection at tile 0 (unicast as from tile 0; a broadcast rooted at tile 0, not delivered back to C&C).",
    "Its packets enter a \"from C&C\" FIFO on each of tile 0's five outputs (four links and eject), each selector gaining one input: C&C gets a fair share.",
    "To C&C: five FIFOs (from left, right, up, down and tile 0's injection) with their own round-robin selector, one packet a cycle; every packet for dst 64 leaves here.",
    "C&C accepts one packet a cycle (no back-pressure, §4.16); its receive FIFO is C&C's side. Address 65 is unused: the memory subsystem hangs off C&C (§7).",
], 10.5, lh=16)

# D. interface
DY = CY + 170
f.text(30, DY, "D. Interface of one route block and one output", 14, "start", "bold")
rows = [["signal", "width", "from → to", "meaning"],
        ["packet in", "102", "link stage 3 / injection (fig 7.3) / C&C → route block", "src 7, dst 7, mode 4, addr 20, payload 64; idle packets dropped"],
        ["me", "6", "tile index (TILE_STATUS) → every route block", "col(me) = me mod 8, row(me) = me div 8"],
        ["push enables", "1 per output", "route block → its FIFO in each output's set", "one for unicast; up to five for a broadcast"],
        ["full", "1 per FIFO", "FIFO → overflow OR", "count = 16 before the cycle's pop"],
        ["packet out", "102", "selector → link stage / traffic director (eject)", "one per cycle per output"],
        ["set[1]", "1", "here → TILE_CONDITIONS (fig 2.4)", "router overflow"]]
y = r.table(30, DY + 12, [30, 170, 290, 680, 1270], rows)
y = r.notes(y + 26, ["No flow control anywhere: a correct program never overflows a FIFO; the fan-in limits are §3.2's, and the schedule's queue bounds are spec/schedule.md's. "
                     "Packets in one FIFO are never reordered, so one source's packets to one destination arrive in the order sent."])
finish(f, r, y, ["Each route block decides in one cycle from src, dst and me: horizontal first by the offset table, then vertical, then eject; a broadcast",
                 "fans out by its arriving direction. Each output takes one packet a cycle from its FIFOs round-robin; a link adds three register stages,",
                 "so a hop is four cycles. A push into a full FIFO loses the packet and sets router overflow."],
       "Source: spec/machine-spec.md v1.0, §3.2, §4.6, §4.7, §4.16.")
f.save("fig4_3_router.svg")
