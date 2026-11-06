# Figure 9.3: the 68-word state record as a memory block, in the style of a system-call parameter block: one row per 128-bit
# word with its byte offset and word number, each word broken into its fields by bit, then one description per field.
# Drawn from spec 4.19.2 (v1.26.1) and 4.21.2, the layouts final.
# Uses a local field-row helper rather than common.bitbar(): it labels bit ranges, not widths, and widens narrow fields.
from common import *
from rtl import R

W, H = 1300, 1209
f = Fig(W, H, "Figure 9.3. The state record: 68 words of 128 bits, one format for the host, the Tracer and the replayer",
        "v1.3 (spec §4.19.2 at v1.26.1, §4.21.2) — "
        "byte offsets 0x000–0x43F of the region; narrow fields widened, not to scale")
r = R(f)
PROP = DASH["proposed"]

BX, BW = 172, 1000          # the block: bit 127 at the left, bit 0 at the right
RH = 56                     # one 128-bit word
MINW = 82                   # narrow fields are widened to this

START, TOTAL, LIVE, NONE = PURPLE_L, BLUE_L, SAND, "white"


def row(y, fields, word, offset):
    """fields: msb first, each (name lines, label, bits, fill, dash). label is a bit range, or a width for unplaced fields."""
    small = [b * BW / 128 < MINW for _, _, b, _, _ in fields]
    rest = BW - MINW * sum(small)
    scale = rest / sum(b for (_, _, b, _, _), s in zip(fields, small) if not s)
    x = BX
    for (names, label, bits, fill, dash), s in zip(fields, small):
        w = MINW if s else bits * scale
        stroke = PURPLE if dash == PROP else EDGE
        f.rect(x, y, w, RH, fill, stroke, 1.3 if dash else 1, rx=0, dash=dash)
        n = len(names)
        y0 = y + RH / 2 - (n * 12.5 + 12) / 2 + 10
        for i, nm in enumerate(names):
            f.text(x + w / 2, y0 + i * 12.5, nm, 10.5, "middle", "bold", fill=MUTED if fill == NONE else INK)
        f.text(x + w / 2, y0 + n * 12.5 + 1, label, 10, "middle", fill=MUTED, mono=label.startswith("["))
        x += w
    f.text(60, y + RH / 2 + 4, offset, 11.5, "start", mono=True)
    f.text(150, y + RH / 2 + 4, str(word), 12, "end", "bold")


# ---------------- the block -----------------------------------------------------------------------------------------
TOP = 108
f.text(60, TOP - 10, "offset", 11, "start", fill=MUTED)
f.text(150, TOP - 10, "word", 11, "end", fill=MUTED)
f.text(BX, TOP - 10, "127", 10.5, "start", fill=MUTED, mono=True)
f.text(BX + BW, TOP - 10, "0", 10.5, "end", fill=MUTED, mono=True)
f.text(BX + BW / 2, TOP - 10, "bits", 10.5, "middle", fill=MUTED)

y = TOP
row(y, [(["spare"], "[127:117]", 11, NONE, None),
        (["reserved"], "[116:115]", 2, NONE, None),
        (["reserved"], "[114:112]", 3, NONE, None),
        (["next offset"], "[111:96]", 16, START, None),
        (["instr_idx"], "[95:64]", 32, START, None),
        (["pc"], "[63:0]", 64, START, None)], 0, "0x000")
f.text(BX + BW + 10, y + 24, "the state word:", 10.5, "start", fill=MUTED)
f.text(BX + BW + 10, y + 38, "writing it resets", 10.5, "start", fill=MUTED)
y += RH
row(y, [(["spare"], "[127:125]", 3, NONE, None),
        (["advice: values"], "[124:102]", 23, LIVE, None),
        (["write log", "compressed"], "[101]", 1, TOTAL, None),
        (["read log", "compressed"], "[100]", 1, TOTAL, None),
        (["code", "compressed"], "[99]", 1, TOTAL, None),
        (["write log", "values out"], "[98:82]", 17, TOTAL, None),
        (["read log", "values out"], "[81:65]", 17, TOTAL, None),
        (["code", "values out"], "[64:48]", 17, TOTAL, None),
        (["write log", "words"], "[47:32]", 16, TOTAL, None),
        (["read log", "words"], "[31:16]", 16, TOTAL, None),
        (["code", "words"], "[15:0]", 16, TOTAL, None)], 1, "0x010")
f.text(BX + BW + 10, y + 24, "the stream word:", 10.5, "start", fill=MUTED)
f.text(BX + BW + 10, y + 38, "four streams", 10.5, "start", fill=MUTED)
y += RH
for i in (0, 1):
    row(y, [([f"register {2 * i + 1}"], "[127:64]", 64, START, None),
            ([f"register {2 * i}"], "[63:0]", 64, START, None)], 2 + i, f"0x{0x20 + 0x10 * i:03X}")
    y += RH
# the break: words 4-64
G = 44


def wave(yy):
    d = f"M{BX - 8},{yy}"
    n = 0
    x = BX - 8
    while x < BX + BW + 8:
        d += f" q6,{-5 if n % 2 == 0 else 5} 12,0"
        x += 12
        n += 1
    f.path(d, EDGE, 1.3, end=False)


f.rect(BX, y, BW, G, "white", "white", 0, rx=0)
f.line([(BX, y), (BX, y + G)], EDGE, 1, end=False, dash="3 3")
f.line([(BX + BW, y), (BX + BW, y + G)], EDGE, 1, end=False, dash="3 3")
wave(y + 9)
wave(y + G - 9)
f.text(BX + BW / 2, y + G / 2 + 4, "words 4–64 (0x040–0x40F): registers 4–125, two to a word, not shown", 11, "middle", fill=MUTED,
       italic=True)
y += G
row(y, [(["register 127"], "[127:64]", 64, START, None),
        (["register 126"], "[63:0]", 64, START, None)], 65, "0x410")
y += RH
row(y, [(["spare"], "[127:103]", 25, NONE, None),
        (["I/O region's size, in bytes"], "[102:72]", 31, START, None),
        (["log2 K"], "[71:66]", 6, START, None),
        (["map", "depth"], "[65:64]", 2, START, None),
        (["map file root"], "[63:33]", 31, START, None),
        (["memory", "depth"], "[32:31]", 2, START, None),
        (["memory file root"], "[30:0]", 31, START, None)], 66, "0x420")
f.text(BX + BW + 10, y + 24, "the files word:", 10.5, "start", fill=MUTED)
f.text(BX + BW + 10, y + 38, "constant", 10.5, "start", fill=MUTED)
y += RH
row(y, [(["template bitmap: bit i set if the run used controller-fed inline template i (an output; a write is ignored)"], "[127:0]", 128, TOTAL, None)],
    67, "0x430")
f.text(BX + BW + 10, y + 32, "the end: 0x440", 10.5, "start", fill=MUTED)
y += RH
f.rect(BX, TOP, BW, y - TOP, "none", INK, 1.6, rx=0)

# ---------------- legend ---------------------------------------------------------------------------------------------
LY = y + 30
items = [(START, PURPLE, "at the end of run n: window n+1's start (to tile n+1)"), (TOTAL, BLUE, "run n's totals (to tile n)"),
         (LIVE, ORANGE, "the live advice count (state, carried on)"), (NONE, EDGE, "spare or reserved: 0")]
x = 40
for fill, c, s in items:
    f.rect(x, LY - 12, 30, 18, fill, c, 1.2, rx=3)
    f.text(x + 38, LY + 2, s, 11.5, "start", fill=MUTED)
    x += 48 + 6.3 * len(s)

# ---------------- the fields ---------------------------------------------------------------------------------------------
TY = LY + 48
f.text(40, TY - 10, "The fields", 14, "start", "bold")
same_w, same_r = "host (frame 2), C&C 3; the Tracer", "the Tracer; the replayer"
rows = [
    ["word, field", "holds", "written by", "read by", "sources"],
    ["0  pc [63:0]", "the pc at the window's first row; live while the Tracer runs", same_w, same_r, "4.19.2, 4.21.2"],
    ["0  instr_idx [95:64]", "that row's index in the committed bytecode; live", same_w, same_r, "4.19.2, 4.21.4"],
    ["0  next offset [111:96]", "the rows of the first instruction's expansion already run", same_w, same_r, "4.19.2, 4.21.4"],
    ["0  reserved [114:112]", "v1.25's compression flags: written 0 and ignored, not reused", "—", "—", "4.19.2"],
    ["0  reserved [116:115]", "v1.26's per-pass flags, now RUN_CONFIG [24], [25]: written 0, ignored", "—", "—", "4.19.2, v1.26 item 15"],
    ["0  spare [127:117]", "0", "—", "—", "4.19.2"],
    ["1  words [15:0] [31:16] [47:32]", "code, read log, write log: 128-bit words as sent, padding included", "the Tracer, when the rows end", "C&C 3 (words 37–39); the replayer", "4.19.2, 4.21.6"],
    ["1  values out [64:48] [81:65] [98:82]", "code, read log, write log: the values each stream decodes to", "the Tracer, when the rows end", "the replayer", "4.19.2, 4.21.6"],
    ["1  compressed [99] [100] [101]", "code, read log, write log: LRU-coded (1) or raw (0), set 1's choice", "the Tracer, when the rows end", "the replayer", "4.19.2, 4.21.6"],
    ["1  advice [124:102]", "the segment's advice values (frame 3's count) at load; one less per pop", "C&C 3 at load; the Tracer", "the Tracer; C&C 3 at the end", "4.19.2, 4.21.2"],
    ["1  spare [127:125]", "0", "—", "—", "4.19.2"],
    ["2–65  registers 0–127", "word 2 + i: register 2i in [63:0], 2i + 1 in [127:64]; the live register file", same_w, same_r, "4.19.2, 4.21.2"],
    ["66  memory root, depth", "[30:0], [32:31]: the Tracer's copy of the guest's memory (.pages)", "C&C 3, at load (§22)", "the Tracer", "4.21.2, 4.21.5"],
    ["66  map root, depth", "[63:33], [65:64]: the proof's jump-target map file", "C&C 3, at load (§22)", "the Tracer", "4.21.2, 4.21.4"],
    ["66  log2 K [71:66]", "the proof's RAM size in 8-byte cells: K for the range check", "C&C 3, at load (frame 7)", "the Tracer", "4.21.2, 4.21.5"],
    ["66  I/O region's size [102:72]", "0x8000_0000 − lowest_address, exact in bytes: the .pages offset, the range check, the keys", "C&C 3, at load (frame 7)", "the Tracer", "4.21.2, 4.21.5"],
    ["66  spare [127:103]", "0", "—", "—", "4.21.2"],
    ["67  template bitmap [127:0]", "bit i: the run used controller-fed inline template i (inlines only)", "the Tracer (EXPANSION_TEMPLATE)", "C&C 3; the tile's controller", "4.21.4, 4.19.13"],
]
fills = {i: (TOTAL if 7 <= i <= 9 or i == 18 else (LIVE if i == 10 else (NONE if i in (4, 5, 6, 11, 17) else START))) for i in range(1, len(rows))}
r.table(40, TY, [40, 262, 690, 902, 1112, 1260], rows, rh=21, size=10, fills=fills)

NY = TY + 21 * len(rows) + 22
f.lines(40, NY, [
    "At the end of run n the record is read out in place, as output queue 0, in four transfers (4.21.6): words 0 and 2–66 are window n+1's start, sent to tile n+1;",
    "word 1's three output fields and word 67 are run n's totals, sent to tile n with its streams. Nothing writes a state between runs. Word 1's advice field is state:",
    "the segment's advice still unconsumed, one less per value popped (a pop at 0 is advice underflow, ERRORS [4]), and C&C 3 checks it is 0 after the 64th run.",
    "The replayer ignores words 66 and 67 and word 1's advice field. Frame 2 of the segment file takes the same layout (§23).",
], 11.5, fill=INK, lh=16)

f.caption(["The state record is one format from the host through the Tracer to the replayers: 68 words of 128 bits at byte offsets 0x000–0x43F, loaded state word first,",
           "whose write resets the unit. It is the Tracer's working state, so at the end of each run it is read out in place: purple fields are window n+1's start,",
           "blue fields run n's totals, and the advice field is the live count of the segment's advice."],
          y=H - 66, source="Source: spec/machine-spec.md §4.19.2 (v1.26.1), §4.21.2 (to 66f399e), §4.21.6; DECISIONS-v1.26.md items 2, 15–17; DECISIONS-v1.3.md §23, §24. Review: wip/planning/sessions/WORKER24-NOTES.md.")
f.save("fig9_3_record.svg")
