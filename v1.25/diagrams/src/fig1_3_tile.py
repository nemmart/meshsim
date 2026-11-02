# Figure 1.3: the tile's engines — the SRAM read queue, the SRAM write queue, and the tables and registers each engine reads.
from common import *

W, H = 1300, 920
f = Fig(W, H, "Figure 1.3. The tile: the engines, the tables and registers that drive them, and the big SRAM",
        "v1.0 (spec/machine-spec.md §4.1–§4.10) — the controller and its memories are figure 3.1; here they appear only as the tables and stubs")

SW = 2.2
f.raw('<g transform="translate(0,16)">')
TBL = (PURPLE, "white")         # table-store / counter-store entries
REG = (ORANGE, SAND)            # configuration registers

def inp(x, y, w, name, sub, kind, both=False, to_y=None, ns=11.5):
    c, fill = TBL if kind == "t" else REG
    f.rect(x, y, w, 42, fill, c, 1.3, rx=4, dash=None if kind == "t" else None)
    f.text(x + w / 2, y + 18, name, ns, "middle", "bold", mono=(kind == "r"))
    f.text(x + w / 2, y + 33, sub, 10, "middle", fill=MUTED)
    f.line([(x + w / 2, y + 44), (x + w / 2, to_y - 2)], c, 1.6, end=True, start=both, dash=None if kind == "t" else DASH["ctrl"])

# ---------------- top row: director, stubs, injection point ---------------------
DIR = (40, 100, 260, 98)
INJ = (1000, 100, 260, 98)
f.line([(DIR[0] + 130, 72), (DIR[0] + 130, DIR[1] - 2)], INK, 2, end=True)
f.text(DIR[0], 66, "from the router's eject (figures 4.1, 4.2)", 11.5, "start")
f.line([(INJ[0] + 130, INJ[1]), (INJ[0] + 130, 74)], GREEN, 2.4, end=True)
f.text(INJ[0] + INJ[2], 66, "to the router's injection route block (figures 4.1, 4.2)", 11.5, "end", fill=GREEN)
f.box(*DIR, "traffic director", ["the only path into the tile;", "routes each packet by its mode", "(figure 1.4); decrements the", "down-counters as it accepts"],
      fill=BOX, stroke=INK, size=11, tsize=13)
f.box(*INJ, "injection point", ["one packet per cycle:", "1. read returns", "2. the controller's send port"], fill=GREEN_L, stroke=GREEN, size=11, tsize=13)
# stubs to and from the controller (figure 3.1)
f.line([(DIR[0] + DIR[2], 128), (560, 128)], PURPLE, SW, end=True)
f.lines(316, 120, ["port B: modes 5, 6, 7 → the controller's"], 11, fill=PURPLE)
f.lines(316, 146, ["instruction and data memory (figure 3.1)"], 11, fill=PURPLE)
f.line([(720, 172), (INJ[0] - 2, 172)], PURPLE, SW, end=True)
f.text(INJ[0] - 10, 164, "the controller's send port (figure 3.1)", 11, "end", fill=PURPLE)

# ---------------- engines --------------------------------------------------------------
# The three readers are drawn right to left from highest to lowest priority:
# the GATHER engine (1), the address generator (2), the controller read port (3).
EY = 330
WE = (40, EY, 400, 104)
RP = (480, EY, 180, 104)
AG = (690, EY, 316, 104)
GA = (1036, EY, 196, 104)
f.box(*WE, "DMA write engine (§4.9)", ["modes 2, 4: at PTR[src];  mode 3: at PTR[bucket digit];  then += 2",
                                      "mode 4: high 32 bits ← ordinal from DMA_RECORD_BASE_ADDRESS",
                                      "count-on-arrival: HIST[histogram digit] += 1"], fill=BLUE_L, stroke=BLUE, size=11, tsize=13)
f.box(*AG, "address generator (§4.2)", ["walks STREAM (stream runs) or", "GROUP (group runs);", "one request per visit (figure 6.1)"],
      fill=GREEN_L, stroke=GREEN, size=11, tsize=13)
f.box(*RP, "controller read port (§4.3)", ["≥ 64-entry request queue;", "32-bit reads queued by", "the controller's stores"], fill=GREEN_L, stroke=GREEN, size=11, tsize=12.5)
f.box(*GA, "GATHER engine (§4.4)", ["mode 1: k reads at", "DMA_RECORD_BASE_ADDRESS +", "2k·ordinal (figure 7.4)"], fill=GREEN_L, stroke=GREEN, size=11, tsize=13)

# inputs: tables (purple) and registers (orange)
IY = 256
inp(118, IY, 82, "PTR[256]", "read-modify-write", "t", both=True, to_y=EY)
inp(210, IY, 82, "HIST[256]", "read-modify-write", "t", both=True, to_y=EY)
inp(302, IY, 132, "DMA_CONFIG", "bucket, histogram digits", "r", to_y=EY)
inp(448, IY, 236, "SRAM_RD_ADDR_QUEUE", "the read port's queue; SRAM_RD_DEST_ADDR", "r", to_y=EY, ns=10)
inp(694, IY, 80, "STREAM[256]", "stream entries", "t", to_y=EY, ns=10.5)
inp(778, IY, 70, "GROUP[64]", "group entries", "t", to_y=EY, ns=10.5)
inp(852, IY, 80, "DMA_CONFIG", "the run", "r", to_y=EY, ns=9.5)
inp(936, IY, 70, "DMA_PULSE", "start", "r", to_y=EY, ns=9)
inp(1064, IY, 174, "DMA_RECORD_BASE_ADDRESS, k", "k is in DMA_CONFIG", "r", to_y=EY, ns=10.5)
f.text(118, IY - 10, "DMA_RECORD_BASE_ADDRESS: the ordinals' origin for mode 4 (one base register per run, v1.0)", 10.5, "start", fill=ORANGE)

# director -> engines (mode lines run above the inputs)
f.line([(DIR[0] + 40, DIR[1] + DIR[3]), (DIR[0] + 40, WE[1] - 2)], BLUE, SW, end=True, dash=DASH["write"])
f.text(DIR[0] + 48, 312, "modes 2–4", 11, "start", fill=BLUE)
f.line([(DIR[0] + 230, DIR[1] + DIR[3]), (DIR[0] + 230, 214), (GA[0] + 20, 214), (GA[0] + 20, GA[1] - 2)], INK, 1.8, end=True)
f.text(GA[0] + 12, 298, "mode 1", 11, "end")

# ---------------- SRAM read queue and big SRAM ------------------------------------------------
# the SRAM read queue is a priority multiplexer: one request a cycle, highest-priority input first
MX, MT, MB = AG[0] + AG[2] / 2, 482, 530
f.raw(f'<polygon points="{MX - 90},{MT} {MX + 90},{MT} {MX + 50},{MB} {MX - 50},{MB}" fill="{GREEN_L}" stroke="{GREEN}" stroke-width="1.4"/>')
f.text(MX, MT + 38, "priority mux", 11.5, "middle", "bold")
for box_, px, jog, xo in [(RP, MX - 60, 462, 40), (AG, MX, 470, 0), (GA, MX + 60, 462, 0)]:
    x = box_[0] + box_[2] / 2 + xo
    f.line([(x, box_[1] + box_[3]), (x, jog), (px, jog), (px, MT - 2)], GREEN, SW, end=True)
for pr, px in [("3", MX - 60), ("2", MX), ("1", MX + 60)]:
    f.text(px, MT + 17, pr, 11.5, "middle", "bold", fill=GREEN)
f.lines(MX - 100, MT + 14, ["SRAM read queue (§4.5): one request a cycle,", "fixed priority 1 > 2 > 3"], 11, anchor="end", fill=GREEN, lh=14)
SR = (40, 600, 1220, 70)
f.rect(*SR, BOX, EDGE, 1.3, rx=8)
f.text(SR[0] + SR[2] / 2, SR[1] + 28, "big SRAM: 8 areas of 128 bits × 32K rows = 4 MB (figure 7.1)", 13.5, "middle", "bold")
f.text(SR[0] + SR[2] / 2, SR[1] + 50, "the write engine is its only writer; the SRAM read queue is its only reader; the controller reads it only through the read port", 11.5, "middle", fill=MUTED)
f.line([(WE[0] + 200, WE[1] + WE[3]), (WE[0] + 200, SR[1] - 2)], BLUE, SW, end=True, dash=DASH["write"])
f.text(WE[0] + 208, 566, "SRAM write queue", 11, "start", fill=BLUE)
f.line([(MX, MB), (MX, SR[1] - 2)], GREEN, SW, end=True)
f.text(MX + 8, 572, "read requests", 11, "start", fill=GREEN)
RX = 1246
f.line([(RX, SR[1] - 2), (RX, INJ[1] + INJ[3] + 2)], GREEN, SW + 0.4, end=True)
f.lines(RX - 8, 452, ["read returns", "become packets,", "in request order;", "a read-port result is", "a mode-7 packet to", "the tile itself"], 11, anchor="end", fill=GREEN, lh=14)

# ---------------- legend ---------------------------------------------------------------
LY = 712
f.rect(40, LY - 12, 30, 18, "white", PURPLE, 1.3, rx=4)
f.text(78, LY + 2, "a table: its own memory, in the control island (figure 3.1, §4.10); every engine access crosses ts_engine_latency each way", 11.5, "start", fill=MUTED)
f.rect(40, LY + 12, 30, 18, SAND, ORANGE, 1.3, rx=4)
f.text(78, LY + 26, "a register written by the controller: configuration registers between phases (static during a phase); the read port's at any time", 11.5, "start", fill=MUTED)
f.line([(820, LY + 22), (856, LY + 22)], GREEN, SW, end=True); f.text(862, LY + 26, "read", 11.5, "start", fill=MUTED)
f.line([(910, LY + 22), (946, LY + 22)], BLUE, SW, end=True, dash=DASH["write"]); f.text(952, LY + 26, "write", 11.5, "start", fill=MUTED)
f.line([(1004, LY + 22), (1040, LY + 22)], PURPLE, SW, end=True); f.text(1046, LY + 26, "controller", 11.5, "start", fill=MUTED)

f.text(40, LY + 52, "Software rule (§4.3, invariant 14): no packet from outside the tile, other than C&C's mode-6 command words, arrives while read-port results are outstanding.", 11.5, "start", fill=ORANGE)
f.text(40, LY + 68, "The network has no flow control: the hardware tolerates only 15 results against a stream at one packet per cycle, or 32 per burst at half rate [meas: meshsim].", 11.5, "start", fill=ORANGE)
f.raw("</g>")
f.caption(["Packets enter through the traffic director and leave through the injection point. The GATHER engine, the address generator and the controller read port share one",
           "SRAM read queue at fixed priority, drawn right to left from highest to lowest. Every read return becomes a packet: a read-port result is a mode-7 packet to the tile itself,",
           "back through the loopback and the director. The write engine is the only writer."],
          y=H - 54, source="Source: spec/machine-spec.md v1.0, §4.1–§4.10, §8, §12.")
f.save("fig1_3_tile.svg")
