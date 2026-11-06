# Figure 3.2: register decode — the controller's data-bus address decode, the 13 peripheral registers
# (offsets, access, store side effects, load results, reset values), DMA_PULSE's pulse circuit,
# and the bit layouts of the multi-field registers.
from common import *

W, H = 1300, 2400
f = Fig(W, H, "Figure 3.2. Register decode: the data-bus address map, the 13 registers and their bit layouts",
        "v1.0 (spec/machine-spec.md §4.10, §4.11, §8) — the address map, the 13 registers and their bit layouts; fields as in sw/mesh.h")

CTL = DASH["ctrl"]
RED, RED_L = "#C0392B", "#FBE9E7"
DIM = "#EDEBE4"


def dot(x, y, c=INK):
    f.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{c}"/>')


def bubble(x, y, c=INK):
    f.raw(f'<circle cx="{x - 4:.1f}" cy="{y:.1f}" r="4" fill="white" stroke="{c}" stroke-width="1.3"/>')


def gate_and(x, yc, n, label=None, c=INK, w=36):
    h = max(28, 14 * n + 6)
    y0, y1 = yc - h / 2, yc + h / 2
    f.raw(f'<path d="M{x},{y0} L{x + w - h / 2},{y0} A{h / 2},{h / 2} 0 0 1 {x + w - h / 2},{y1} L{x},{y1} Z" '
          f'fill="white" stroke="{c}" stroke-width="1.4"/>')
    if label:
        f.text(x + w / 2 - 4, y0 - 4, label, 10, "middle", fill=MUTED)
    return [yc + (i - (n - 1) / 2) * 14 for i in range(n)]


def gate_or(x, yc, n, label=None, c=INK, w=38, h=None):
    h = h or max(28, 14 * n + 6)
    y0, y1 = yc - h / 2, yc + h / 2
    f.raw(f'<path d="M{x},{y0} Q{x + w * 0.55},{y0} {x + w},{yc} Q{x + w * 0.55},{y1} {x},{y1} Q{x + 9},{yc} {x},{y0} Z" '
          f'fill="white" stroke="{c}" stroke-width="1.4"/>')
    if label:
        f.text(x + w / 2, y0 - 4, label, 10, "middle", fill=MUTED)
    return [yc + (i - (n - 1) / 2) * 14 for i in range(n)]


def frame(x, y, w, h):
    f.rect(x, y, w, h, "white", GREY, 1, rx=10)


def lbl(x, y, s, anchor="end", c=INK, size=10.5, weight="normal", mono=False):
    f.text(x, y + 4, s, size, anchor, weight, fill=c, mono=mono)


def table(x0, y0, cxs, rows, rh=21, size=10.5, head_fill=BOX, fills=None, bold_col=0):
    for i, row in enumerate(rows):
        y = y0 + i * rh
        for c in range(len(cxs) - 1):
            fill = head_fill if i == 0 else ((fills or {}).get((i, c), "white"))
            f.rect(cxs[c], y, cxs[c + 1] - cxs[c], rh, fill, EDGE, 0.8, rx=0)
            cell = row[c]
            col = INK
            if isinstance(cell, tuple):
                cell, col = cell
            f.text(cxs[c] + 6, y + rh / 2 + 4, cell, size, "start", "bold" if (i == 0 or c == bold_col) else "normal", fill=col)
    return y0 + len(rows) * rh


# =============================================================================================
# A. the data-bus address decode
# =============================================================================================
AY = 76
frame(16, AY, W - 32, 404)
f.text(30, AY + 24, "A. The controller's data-bus address decode (§4.11, §4.10, §8): every load and store, one cycle", 14, "start", "bold")
# inputs
IX = 30
lbl(IX, AY + 64, "addr[31:0], load/store, size (byte, half, word)", "start", weight="bold")
BUS_X = 40
f.line([(BUS_X, AY + 74), (BUS_X, AY + 330)], INK, 3, end=False)
COMPS = [
    ("addr[31:13] = 0", "instruction memory 0x0000–0x1FFF", "sel_imem"),
    ("addr[31:13] = 1", "table window 0x2000–0x3FFF", "sel_tw"),
    ("addr[31:13] = 2", "data memory 0x4000–0x5FFF", "sel_dmem"),
    ("addr[31:11] = all 1s", "registers 0xFFFFF800–0xFFFFFFFF", "sel_reg"),
    ("word: addr[1:0] ≠ 0; half: addr[0]", "misaligned", "misaligned"),
]
CY = AY + 92
for i, (cmp_, what, sig) in enumerate(COMPS):
    y = CY + i * 46
    f.line([(BUS_X, y + 15), (68, y + 15)], INK, 1.4, end=True)
    dot(BUS_X, y + 15)
    f.box(70, y, 270, 30, None, [(cmp_, {"mono": True, "size": 10.5})], fill=BOX, stroke=INK, sw=1.2)
    f.line([(340, y + 15), (380, y + 15)], INK, 1.4, end=False)
    f.text(386, y + 12, sig, 10.5, "start", "bold")
    f.text(386, y + 25, what, 10, "start", fill=MUTED)

# table-window sub-decode (box)
TW = (586, AY + 52, 352, 196)
f.rect(*TW, "white", INK, 1.2, rx=6)
f.text(TW[0] + 10, TW[1] + 18, "table-window sub-decode (sel_tw; §4.10, §8.9)", 11.5, "start", "bold")
tw_rows = [
    ["addr", "table", "entry; word"],
    ["0x2000–0x23FF", "HIST, 32-bit", "i = addr[9:2]"],
    ["0x2400–0x27FF", "PTR, 32-bit", "i = addr[9:2]"],
    ["0x2800–0x2FFF", "STREAM, 64-bit", "i = addr[10:3]; addr[2]"],
    ["0x3000–0x31FF", "GROUP, 64-bit", "g = addr[8:3]; addr[2]"],
    ["0x3200–0x3FFF", ("unmapped: bus error", RED), "—"],
]
table(TW[0] + 8, TW[1] + 28, [TW[0] + 8, TW[0] + 100, TW[0] + 206, TW[0] + 344], tw_rows, rh=20, size=10, bold_col=-1)
f.text(TW[0] + 10, TW[1] + 176, "addr[2]: 0 the low word, 1 the high word. Word accesses only;", 10, "start", fill=MUTED)
f.text(TW[0] + 10, TW[1] + 189, "loads from HIST only (PTR, STREAM, GROUP are write-only).", 10, "start", fill=MUTED)

# register offset decode (box)
RG = (950, AY + 52, 318, 196)
f.rect(*RG, "white", INK, 1.2, rx=6)
f.text(RG[0] + 10, RG[1] + 18, "register offset decode (sel_reg; §8)", 11.5, "start", "bold")
f.lines(RG[0] + 10, RG[1] + 40, [
    "i = addr[10:2]: register i at 0xFFFFF800 + 4·i",
    "i = 0–12: one of the 13 registers (panel B);",
    "one-hot select reg_sel[12:0]",
    ("i = 13–511 (0x834–0xFFC): unmapped,", {"fill": RED}),
    ("a bus error", {"fill": RED}),
    "",
    "per register, from its access attribute:",
    "st_X = store & reg_sel[X] & writable(X)",
    "load data = readable(X) ? value(X) : 0",
], 10.5, lh=15)

# bus-error OR
EX = 760
ie = gate_or(EX, AY + 330, 6, "bus error")
terms = [
    "sel_imem (a data access to instruction memory)",
    "none of sel_imem, sel_tw, sel_dmem, sel_reg (unmapped)",
    "misaligned",
    "sel_reg & (size ≠ word | i ≥ 13 | store & read-only(i))",
    "sel_tw & (size ≠ word | 0x3200–0x3FFF)",
    "sel_tw & load & (PTR | STREAM | GROUP)",
]
for y, s in zip(ie, terms):
    f.line([(EX - 24, y), (EX + 4, y)], RED, 1.2, end=False)
    lbl(EX - 28, y, s, c=RED, size=10)
f.line([(EX + 38, AY + 330), (EX + 80, AY + 330)], RED, 1.8, end=True)
f.box(EX + 82, AY + 300, 340, 60, "bus error", ["sets condition 6 (figure 2.4) and halts the", "tile's controller; engines and router run on (§4.11)"],
      fill=RED_L, stroke=RED, sw=1.4, size=10.5, tsize=11.5)
f.text(30, AY + 384 + 4, "Sub-word accesses to data memory are allowed;", 10.5, "start", fill=MUTED)
f.text(30, AY + 398, "the controller never loads or stores instruction memory.", 10.5, "start", fill=MUTED)

# =============================================================================================
# B. the 13 registers
# =============================================================================================
BY = AY + 424
f.text(30, BY, "B. The 13 registers: offset, access, what a store does, what a load returns, reset (§8)", 14, "start", "bold")
rows = [
    ["offset", "register", "access", "a store", "a load returns", "after chip reset"],
    ["0x800", "TILE_STATUS", "R", "bus error", "live bits, tile index, size (panel D)", "live; index, size fixed"],
    ["0x804", "TILE_CONDITIONS", "R", "bus error (condition 6)", "the conditions, [10:0] (fig 2.4)", "0"],
    ["0x808", "ROUTER_SET_HDR", "W", "loads dst, mode, addr (panel D); addr += 2 or 1 per send", "0", ("undefined; kept over HALT → RUN", MUTED)],
    ["0x80C", "ROUTER_SET_HI", "W", "loads the next payload's high 32 bits; each send clears it", "0", ("undefined; kept over HALT → RUN", MUTED)],
    ["0x810", "ROUTER_SEND_LO", "W", "sends {ROUTER_SET_HI, data} with ROUTER_SET_HDR; stalls while the holding register is full", "0", "— (no state)"],
    ["0x814", "DMA_CONFIG", "W", "loads the run, digits and k (panel D)", "0", ("undefined; kept over HALT → RUN", MUTED)],
    ["0x818", "DMA_PULSE", "W", "fires start, done for one cycle (panel C)", "0", "— (no state)"],
    ["0x81C", "DMA_SRAM_REMAINING", "R/W", "loads [19:0]; clears the local-wake latch (fig 2.3)", "the count, [19:0]", "0"],
    ["0x820", "DMA_CM_REMAINING", "R/W", "loads [19:0]; clears the local-wake latch (fig 2.3)", "the count, [19:0]", "0"],
    ["0x824", "DMA_REPLY_REMAINING", "R/W", "loads [19:0]; clears the local-wake latch (fig 2.3)", "the count, [19:0]", "0"],
    ["0x828", "DMA_RECORD_BASE_ADDRESS", "W", "loads [19:0], a chunk address: augment's origin or gather's values; one base per run", "0", ("undefined; kept over HALT → RUN", MUTED)],
    ["0x82C", "SRAM_RD_ADDR_QUEUE", "R/W", "the read port's queue: pushes a read of chunk [19:0]; full: dropped, condition 3", "free entries (≥ 64 when empty)", "empty"],
    ["0x830", "SRAM_RD_DEST_ADDR", "W", "loads [11:0], a packed word; +1 on every queue store, a dropped read too", "0", ("undefined; kept over HALT → RUN", MUTED)],
]
CXb = [30, 86, 282, 330, 800, 1036, 1270]
yb = table(30, BY + 12, CXb, rows, rh=22, bold_col=1)
f.lines(30, yb + 18, [
    "W: write-only, a load returns 0. R: read-only, a store is a bus error. A store to an unmapped offset (0x834–0xFFC) or a sub-word access to any register is a bus error.",
    "Bits outside a register's fields are ignored on a store and read 0. A store takes effect at the end of its cycle; a load returns the value at the end of the previous cycle.",
    "HALT → RUN keeps every register's value (§6.1). The configuration registers are undefined at chip reset: software writes each before use (§8), as for the tables.",
], 10.5, lh=15, fill=MUTED)

# =============================================================================================
# C. store side effects
# =============================================================================================
CY0 = yb + 72
frame(16, CY0, W - 32, 236)
f.text(30, CY0 + 24, "C. DMA_PULSE and the other store side effects (§8.6, §4.2, §6.3)", 14, "start", "bold")
PX = 140
pulses = [("wdata[1]", "start_p", "start: load the run from DMA_CONFIG and begin the read-ahead (fig 6.2)"),
          ("wdata[0]", "done_p", "done: after start in the same store (fig 2.1, fig 2.3, fig 2.4 bit 10)")]
lbl(PX - 60, CY0 + 52, "st_pulse", c=ORANGE, weight="bold")
f.line([(PX - 56, CY0 + 52), (PX + 6, CY0 + 52), (PX + 6, CY0 + 180)], ORANGE, 1.4, end=False, dash=CTL)
for k, (bit, sig, what) in enumerate(pulses):
    yc = CY0 + 72 + k * 46
    ia = gate_and(PX + 40, yc, 2)
    f.line([(PX + 6, ia[0]), (PX + 39, ia[0])], ORANGE, 1.3, end=False, dash=CTL)
    dot(PX + 6, ia[0], ORANGE)
    lbl(PX - 10, ia[1] + 6, bit, c=INK)
    f.line([(PX - 6, ia[1]), (PX + 39, ia[1])], INK, 1.3, end=False)
    f.line([(PX + 76, yc), (PX + 130, yc)], ORANGE, 1.6, end=True, dash=CTL)
    lbl(PX + 136, yc, sig, "start", c=ORANGE, weight="bold")
    f.text(PX + 196, yc + 4, what, 10.5, "start", fill=MUTED)
f.text(30, CY0 + 214, "One cycle each. wdata[3:2] (reserved) and [31:4] are ignored. Start acts before done within the store's cycle.", 10.5, "start", fill=MUTED)

SX = 700
f.text(SX, CY0 + 52, "the other side effects, one store strobe each", 11.5, "start", "bold")
f.lines(SX, CY0 + 74, [
    "st_S, st_C, st_R: load the counter, clear the local-wake latch",
    "      and the qualifier (fig 2.3 A, B)",
    "st_rdq: push {chunk, SRAM_RD_DEST_ADDR} to the read port's",
    "      queue; SRAM_RD_DEST_ADDR += 1 on every such store; full: drop, set 3",
    "st_sendlo: the send port (item 10): with the holding register",
    "      full the store stalls the core; once it is taken, ROUTER_SET_HI ← 0",
    "      and ROUTER_SET_HDR.addr += 2 (mode 5) or 1 (modes 6, 7)",
], 10.5, lh=15)

# =============================================================================================
# D. bit layouts
# =============================================================================================
DY = CY0 + 256
f.text(30, DY, "D. Bit layouts of the multi-field registers (bit 31 on the left; fields as in mesh.h)", 14, "start", "bold")
S = 33
BX = 196


def layout(y, name, acc, fields, notes):
    f.text(30, y + 26, name, 11.5, "start", "bold")
    f.text(30, y + 42, acc, 10.5, "start", fill=MUTED)
    bitbar(f, BX, y + 8, fields, S, h=40, label_size=10.5)
    # bit numbers at field edges
    pos = 32
    for nm, bits, *_ in fields:
        hi, lo = pos - 1, pos - bits
        x0 = BX + (32 - pos) * S
        f.text(x0 + 4, y + 4, str(hi), 9, "start", fill=MUTED)
        if bits > 1:
            f.text(x0 + bits * S - 4, y + 4, str(lo), 9, "end", fill=MUTED)
        pos -= bits
    return f.lines(BX, y + 66, notes, 10.5, lh=14, fill=MUTED) + 10


y = DY + 18
y = layout(y, "DMA_CONFIG", "0x814, W", [("spare", 5, DIM), ("k − 1", 3, GREEN_L), ("hist digit", 3, BLUE_L), ("bucket digit", 3, BLUE_L),
                                            ("run count", 9, BOX), ("run start", 8, SAND), ("", 1, ORANGE_L)],
           ["[0] kind: stream run or group run. run start: first stream or GROUP entry. run count: 0–256 streams or 0–64 groups (0: no entries, rd_done at once); numbers past the table's end wrap.",
            "bucket digit: 0 none, 1–4 the key byte (mode 3); hist digit: 0 off, 1–4; 5–7 reserved, decoded as none. k − 1: the gather value size, k = 1–8."])
y = layout(y, "DMA_PULSE", "0x818, W", [("ignored", 28, DIM), ("", 1, DIM), ("", 1, DIM), ("", 1, ORANGE_L), ("", 1, ORANGE_L)],
           ["[0] done, [1] start, [3:2] reserved (ignored, read 0). In one store start acts before done."])
y = layout(y, "TILE_STATUS", "0x800, R", [("size − 1", 3, SAND), ("spare", 2, DIM), ("tile index", 6, SAND), ("spare", 11, DIM), ("live", 10, GREEN_L)],
           ["live [9:0]: 0 rd_running, 1 rd_done, 2 spare, 3 send_busy, 4 ts_busy, 5 mem_idle, 6 cm_idle, 7 done, 8 command landed, 9 GATHER busy.",
            "size − 1: big SRAM in 512 KB areas (0–7). Index and size are fixed per tile and chip."])
y = layout(y, "TILE_CONDITIONS", "0x804, R", [("reserved", 21, DIM), ("conditions", 11, RED_L)],
           ["bit n = condition n (figure 2.4): 0 drift … 10 done early. Read-only; cleared only on entering HALT."])
y = layout(y, "ROUTER_SET_HDR", "0x808, W", [("", 1, DIM), ("addr", 20, SAND), ("mode", 4, BLUE_L), ("dst", 7, GREEN_L)],
           ["dst: tile 0–63, 64 C&C, 126 broadcast. mode: 4 bits. addr: auto-increments after each send (+2 mode 5, +1 modes 6 and 7; §8.4)."])
f.lines(30, y + 4, ["One-field registers: DMA_RECORD_BASE_ADDRESS, the three counters and SRAM_RD_ADDR_QUEUE's store use [19:0]; ROUTER_SET_HI and ROUTER_SEND_LO use",
                    "all 32 bits; SRAM_RD_DEST_ADDR uses [11:0], a packed data-memory word. Unused bits are ignored on a store and read 0."], 10.5, lh=15, fill=MUTED)
y += 52

LY = y + 12
f.line([(30, LY), (70, LY)], INK, 2, end=True)
f.text(78, LY + 4, "address and data", 11.5, "start", fill=MUTED)
f.line([(220, LY), (260, LY)], ORANGE, 1.6, end=True, dash=CTL)
f.text(268, LY + 4, "strobes and pulses", 11.5, "start", fill=MUTED)
f.line([(420, LY), (460, LY)], RED, 1.6, end=True)
f.text(468, LY + 4, "bus-error terms", 11.5, "start", fill=MUTED)
f.raw(f'<circle cx="600" cy="{LY}" r="3.2" fill="{INK}"/>')
f.text(610, LY + 4, "junction (crossings without a dot don't connect)", 11.5, "start", fill=MUTED)

f.h = H = int(LY + 96)
f.caption(["Every load and store is decoded in one cycle into instruction memory (never a data access), the table window, data memory or one of 13 registers at",
           "0xFFFFF800 + 4·i. Anything else, a misaligned or sub-word access to a register or the table window, a load from a table the controller only writes,",
           "or a store to a read-only register is a bus error. DMA_PULSE fires one-cycle pulses, start before done; write-only registers read 0."],
          y=H - 54, source="Source: spec/machine-spec.md v1.0, §4.10, §4.11, §8; sw/mesh.h for bit positions.")
f.save("fig3_2_regdecode.svg")
