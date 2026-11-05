# Figure 9.2: the Tracer's output path (spec 4.21.6): the core's two streams into the FIFOs and set 1, the choice, the scans
# through set 2 and the packers into output queues 1-3 with their credits and service flags, queue 0 read in place, the DMA.
from common import *

W, H = 1300, 1120
f = Fig(W, H, "Figure 9.2. The Tracer's output path: the FIFOs, the two compressor sets, the packers and the output queues",
        "v1.3 (spec §4.21.6, §4.21.3) — block level; the stream's data in blue, the state record in purple, control in orange")

SW = 2.0
WR = DASH["write"]
CT = DASH["ctrl"]


def dot(x, y, c=INK):
    f.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{c}"/>')


def rot_text(x, y, s, size=11, fill=INK):
    f.raw(f'<text transform="rotate(-90 {x:.1f} {y:.1f})" x="{x:.1f}" y="{y:.1f}" font-size="{size}" '
          f'text-anchor="middle" fill="{fill}">{esc(s)}</text>')


def mux(c, y, label="raw | coded"):
    f.raw(f'<polygon points="{c - 95},{y} {c + 95},{y} {c + 70},{y + 28} {c - 70},{y + 28}" fill="white" '
          f'stroke="{BLUE}" stroke-width="1.4"/>')
    f.text(c, y + 18, label, 10.5, "middle", "bold")


CODE, READ, WRITE = 230, 560, 860
TRUNK = 1230                      # the state record, read in place

# ---------------- the core (figure 9.1) --------------------------------------------------------------------------
CORE = (150, 76, 1000, 46)
f.box(*CORE, "the RV64 core (figure 9.1)", ["one code value per instruction or padding row; at most one log word per row"],
      fill=BOX, stroke=INK, size=10.5, tsize=12.5)
f.line([(CODE, CORE[1] + CORE[3]), (CODE, 158)], BLUE, SW, end=True, dash=WR)
f.line([(1060, CORE[1] + CORE[3]), (1060, 158)], BLUE, SW, end=True, dash=WR)
f.text(CODE + 6, 136, "code value (32 bits)", 10.5, "start", fill=BLUE)
f.text(1054, 136, "log word: 64 bits + a 2-bit tag", 10.5, "end", fill=BLUE)
# the state record: part of the core, read in place as output queue 0
f.line([(CORE[0] + CORE[2], 99), (TRUNK, 99), (TRUNK, 688)], PURPLE, SW + 0.4, end=True)
rot_text(TRUNK + 14, 420, "the state record, read in place as output queue 0 (4.21.6)", 10.5, fill=PURPLE)

# ---------------- the FIFOs and set 1 ------------------------------------------------------------------------------
CF = (150, 160, 180, 140)
LF = (970, 160, 180, 140)
f.box(*CF, "code FIFO", ["65,536 × 32 bits", "256 KB: one window", "a code value per", "instruction or padding row"],
      fill=BLUE_L, stroke=BLUE, size=10.5, tsize=12.5)
f.box(*LF, "log FIFO", ["65,536 × 66 bits", "528 KB: one window", "a 64-bit word and a tag:", "0 load, 1 other read, 2 write"],
      fill=BLUE_L, stroke=BLUE, size=10.5, tsize=12.5)

S1 = (420, 150, 460, 150)
f.rect(*S1, BLUE_L, BLUE, 1.4, rx=6)
f.text(S1[0] + S1[2] / 2, S1[1] + 20, "set 1: sizing as the rows run (4.21.6)", 12.5, "middle", "bold")
for x, name, sub in [(436, "code", "16-entry LRU"), (586, "read log", "16-entry LRU"), (736, "write log", "15 LRU + E[15]")]:
    f.box(x, 186, 130, 50, name + " encoder", [sub], fill="white", stroke=BLUE, size=10, tsize=11)
f.text(S1[0] + S1[2] / 2, 258, "the coded bits are counted, then discarded;", 10.5, "middle")
f.text(S1[0] + S1[2] / 2, 273, "the tag picks the encoder; loads (tag 0) update E[15]", 10.5, "middle")
f.text(S1[0] + S1[2] / 2, 288, "reset to all zeros by PULSE_RUN", 10, "middle", fill=MUTED)
# taps into set 1, beside the FIFOs' entries
dot(CODE, 146, BLUE)
f.line([(CODE, 146), (501, 146), (501, 184)], BLUE, 1.5, end=True, dash=WR)
dot(1060, 146, BLUE)
f.line([(1060, 146), (651, 146), (651, 184)], BLUE, 1.5, end=True, dash=WR)
dot(801, 146, BLUE)
f.line([(801, 146), (801, 184)], BLUE, 1.5, end=True, dash=WR)

# ---------------- the choice ----------------------------------------------------------------------------------------
CH = (420, 322, 460, 56)
f.box(*CH, "the choice, when the rows end", ["per stream: LRU-coded if fewer 128-bit words than raw, raw on a tie;",
                                             "only where RUN_CONFIG [28:26] enables it"],
      fill=SAND, stroke=ORANGE, size=10.5, tsize=12)
for x in (501, 651, 801):
    f.line([(x, S1[1] + S1[3]), (x, CH[1] - 2)], ORANGE, 1.4, end=True, dash=CT)
f.text(512, 314, "sizes", 9.5, "start", fill=ORANGE)
# the totals into the state record
f.line([(CH[0] + CH[2], 350), (TRUNK - 2, 350)], PURPLE, 1.8, end=True)
dot(TRUNK, 350, PURPLE)
f.text(888, 342, "the totals: word 1's three", 10, "start", fill=PURPLE)
f.text(888, 366, "output fields; words 37–39", 10, "start", fill=PURPLE)
# the select lines to set 2's muxes
f.line([(650, CH[1] + CH[3]), (650, 392)], ORANGE, 1.6, end=False, dash=CT)
f.line([(100, 392), (730, 392)], ORANGE, 1.6, end=False, dash=CT)
for c in (CODE, READ, WRITE):
    x = c - 130
    f.line([(x, 392), (x, 554), (c - 86, 554)], ORANGE, 1.6, end=True, dash=CT)

# ---------------- the scans, the tag demux ------------------------------------------------------------------------------
f.line([(CODE, CF[1] + CF[3]), (CODE, 456)], BLUE, SW, end=False, dash=WR)
f.text(CODE + 8, 318, "scan: one entry a cycle,", 10, "start", fill=BLUE)
f.text(CODE + 8, 331, "from the rows' end", 10, "start", fill=BLUE)
f.line([(1060, LF[1] + LF[3]), (1060, 398)], BLUE, SW, end=True, dash=WR)
f.text(1068, 318, "scan, in parallel", 10, "start", fill=BLUE)
DX = (940, 400, 240, 38)
f.box(*DX, "tag demux", ["0, 1 → read log; 2 → write log"], fill="white", stroke=BLUE, size=10, tsize=11)
f.line([(DX[0], 412), (READ, 412), (READ, 456)], BLUE, SW, end=False, dash=WR)
f.line([(DX[0], 428), (WRITE, 428), (WRITE, 456)], BLUE, SW, end=False, dash=WR)

# ---------------- set 2, the packers, output queues 1-3 ------------------------------------------------------------------
f.rect(110, 462, 856, 70, "none", BLUE, 1.2, dash="3 3", rx=6)
f.lines(976, 470, ["set 2: on the way out,", "from all zeros by the", "same rules: exactly", "set 1's sizes; every log", "word passes the write", "log's table (E[15])"], 10, fill=BLUE, lh=12.5)
cols = [(CODE, "code", "16-entry LRU", 1, "code", 8), (READ, "read log", "16-entry LRU", 2, "read log", 9),
        (WRITE, "write log", "15 LRU + E[15]: every log", 3, "write log", 10)]
for c, name, sub, q, qname, ev in cols:
    dot(c, 456, BLUE)
    f.line([(c, 456), (c - 45, 456), (c - 45, 470)], BLUE, 1.6, end=True, dash=WR)
    f.line([(c, 456), (c + 60, 456), (c + 60, 538)], BLUE, 1.6, end=True, dash=WR)
    f.text(c + 66, 500, "raw", 9.5, "start", fill=BLUE)
    f.box(c - 120, 472, 150, 50, name + " encoder", [sub if q != 3 else "15 LRU + E[15]"], fill="white", stroke=BLUE, size=10, tsize=11)
    f.line([(c - 45, 522), (c - 45, 538)], BLUE, 1.6, end=True, dash=WR)
    mux(c, 540)
    f.line([(c, 568), (c, 598)], BLUE, SW, end=True, dash=WR)
    f.box(c - 95, 600, 190, 52, "packer", ["LSB first into 128-bit words;", "commits the last partial word"],
          fill=BLUE_L, stroke=BLUE, size=10, tsize=11.5)
    f.line([(c, 652), (c, 688)], BLUE, SW, end=True, dash=WR)
    QB = (c - 115, 690, 230, 112)
    f.rect(*QB, BLUE_L, BLUE, 1.4, rx=6)
    f.text(c, 710, f"output queue {q}: {qname}", 12, "middle", "bold")
    f.text(c, 726, "4 KB, 256 × 128 bits", 10.5, "middle", fill=MUTED)
    f.box(c - 108, 742, 104, 52, "credits 0–2", ["one per 2 KB chunk,", "one for a last part"], fill="white", stroke=EDGE, size=9.5, tsize=10.5)
    f.box(c + 4, 742, 104, 52, f"EVENTS [{ev}]", ["needs service:", "up while a credit"], fill=SAND, stroke=ORANGE, size=9.5, tsize=10.5)

# ---------------- output queue 0 ---------------------------------------------------------------------------------------
Q0 = (1010, 690, 262, 112)
f.rect(*Q0, PURPLE_L, PURPLE, 1.4, rx=6)
f.text(Q0[0] + Q0[2] / 2, 710, "output queue 0: the state record", 12, "middle", "bold")
f.text(Q0[0] + Q0[2] / 2, 726, "68 words, in place, by a read pointer", 10.5, "middle", fill=MUTED)
f.box(Q0[0] + 8, 742, 120, 52, "four transfers", ["words 0, 1,", "2–66 and 67"], fill="white", stroke=EDGE, size=9.5, tsize=10.5)
f.box(Q0[0] + 136, 742, 118, 52, "EVENTS [7]", ["at the rows' end;", "one credit"], fill=SAND, stroke=ORANGE, size=9.5, tsize=10.5)

# ---------------- the DMA and its router input ------------------------------------------------------------------------------
DM = (1010, 846, 262, 62)
f.box(*DM, "DMA (4.17.9)", ["one transfer at a time (PULSE_DMA), sized", "by C&C 3 to the credits and words 37–39"],
      fill="white", stroke=INK, size=10, tsize=12.5)
for c in (CODE, READ, WRITE):
    f.line([(c, 802), (c, 877)], BLUE, SW, end=False, dash=WR)
    dot(c, 877, BLUE)
f.line([(CODE, 877), (DM[0] - 2, 877)], BLUE, SW, end=True, dash=WR)
f.line([(Q0[0] + Q0[2] / 2, 802), (Q0[0] + Q0[2] / 2, DM[1] - 2)], PURPLE, SW, end=True)
RT = (1010, 934, 262, 34)
f.box(*RT, "its router input: C&C 3's free link", [], fill="white", stroke=INK, size=10, tsize=11.5)
f.line([(DM[0] + DM[2] / 2, DM[1] + DM[3]), (DM[0] + DM[2] / 2, RT[1] - 2)], BLUE, SW, end=True, dash=WR)
f.text(1000, 955, "packets into the compute tiles' memory (first implementation, 4.21.6)", 10.5, "end", fill=BLUE)
f.text(1000, 897, "the service flags reach C&C 3 as its EVENTS [13] (4.21.3)", 10.5, "end", fill=ORANGE)

# ---------------- legend -------------------------------------------------------------------------------------------------
LY = 1004
f.line([(40, LY), (76, LY)], BLUE, SW, end=True, dash=WR); f.text(84, LY + 4, "the streams' data", 11.5, "start", fill=MUTED)
f.line([(240, LY), (276, LY)], PURPLE, SW, end=True); f.text(284, LY + 4, "the state record and its totals", 11.5, "start", fill=MUTED)
f.line([(520, LY), (556, LY)], ORANGE, SW, end=True, dash=CT); f.text(564, LY + 4, "control: the sizes, the choice", 11.5, "start", fill=MUTED)
f.rect(800, LY - 10, 30, 16, SAND, ORANGE, 1.2, rx=3); f.text(838, LY + 4, "a live service flag", 11.5, "start", fill=MUTED)

f.caption(["Each word the core logs goes into its FIFO and through set 1, which sizes its stream as the rows run. When the rows end, set 1's sizes choose raw or coded per stream and",
           "give the state record its totals; then one scan of each FIFO drives set 2 and the packers into output queues 1–3, each with its credits and service flag, and the DMA sends them,",
           "with output queue 0, the state record read in place. The drain's order (the state queue's service, the streams, done last) is in spec 4.21.6."],
          y=H - 66, source="Source: spec/machine-spec.md §4.21.6 (to 66f399e), §4.21.3, §4.17.9; planning/DECISIONS-v1.3.md §2–4, §24. Review: WORKER24/NOTES.md.")
f.save("fig9_2_output.svg")
