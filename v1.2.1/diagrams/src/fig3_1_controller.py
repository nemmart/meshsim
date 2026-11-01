# Figure 3.1: the controller and every memory on its buses — what it reads and writes, and what must sit close to it.
# After design v1's figure 6 (the controller): one box per memory, r / w on each bus tap, the unit on each memory's other side.
from common import *

W, H = 1300, 986
f = Fig(W, H, "Figure 3.1. The controller's memories: what it reads and writes, all in the control island",
        "v1.0 (spec/machine-spec.md §4.3, §4.10, §4.11, §8, §12) — arrows point the way data moves")

# ---------------- zones ------------------------------------------------------------------------
IZ = (30, 152, 1240, 548)         # the control island: everything on the controller's buses
f.rect(*IZ, "white", PURPLE, 1.4, rx=12, dash="7 4")
f.lines(IZ[0] + 14, IZ[1] + IZ[3] - 48, ["the control island (§12):", "≈ 0.09 mm², ≈ 0.3 mm across", "[estimate]"], 12, weight="bold", fill=PURPLE, lh=16)

# ---------------- top row of memories -------------------------------------------------------
MY, MH = 176, 104
def mem(x, w, title, lines, fill, stroke=PURPLE):
    f.box(x, MY, w, MH, title, lines, fill=fill, stroke=stroke, size=10.5, tsize=13)
    return x + w / 2
cD = mem(50, 196, "data memory", ["8 KB, 2 banks", ("0x4000–0x5FFF", {"mono": True}), "mailbox 0x5C00–0x5FFF"], PURPLE_L)
cR = mem(262, 196, "peripheral registers", ["13 registers (§8)", ("0xFFFFF800–", {"mono": True}), "DMA_CONFIG, …"], PURPLE_L)
cC = mem(474, 196, "HIST[256]", ["17 bits each", ("0x2000–0x23FF", {"mono": True}), "the per-digit counts"], PURPLE_L)
cP = mem(760, 160, "PTR[256]", ["20 bits each", ("0x2400–0x27FF", {"mono": True}), "landing pointers"], PURPLE_L)
cS = mem(934, 160, "STREAM[256]", ["8 bytes each", ("0x2800–0x2FFF", {"mono": True}), "stream entries"], PURPLE_L)
cN = mem(1108, 150, "GROUP[64]", ["8 bytes each", ("0x3000–0x31FF", {"mono": True}), "group entries"], PURPLE_L)

# ---------------- the other side of each memory ------------------------------------------
TY = 80
def other(cx, lines, kind):
    f.lines(cx, TY, lines, 10.5, anchor="middle", lh=13)
    y0 = TY + 13 * len(lines) - 4
    if kind == "in":       # written from the other side
        f.line([(cx, y0), (cx, MY - 2)], PURPLE, 2, end=True)
    elif kind == "out":    # read by the other side
        f.line([(cx, MY - 2), (cx, y0)], INK, 1.6, end=True)
    else:
        f.line([(cx, MY - 2), (cx, y0)], INK, 1.6, end=True, start=True)
other(cD, ["traffic director, port B:", "modes 5, 6, 7 (w)"], "in")
other(cR, ["configuration to the engines;", "status from every unit"], "both")
other(cC, ["write engine: count-on-arrival", "read-modify-write"], "both")
other(cP, ["write engine: read-", "modify-write (modes 2–4)"], "both")
other(cS, ["address generator:", "reads (r)"], "out")
other(cN, ["address generator:", "read-ahead (r)"], "out")

# ---------------- data bus ------------------------------------------------------------------------
BY = 372
CT = (150, 436, 470, 92)
DBX = 470
f.raw(f'<line x1="{cD}" y1="{BY}" x2="{cN}" y2="{BY}" stroke="{GREEN}" stroke-width="5" stroke-linecap="round"/>')
f.raw(f'<line x1="{DBX}" y1="{BY}" x2="{DBX}" y2="{CT[1]}" stroke="{GREEN}" stroke-width="5"/>')
f.text(DBX + 12, BY + 26, "data bus (loads and stores), 32-bit", 11.5, "start", "bold", fill=GREEN)
for cx, acc in [(cD, "r/w"), (cR, "r/w"), (cC, "r/w"), (cP, "w"), (cS, "w"), (cN, "w")]:
    if acc == "w":
        f.line([(cx, BY - 4), (cx, MY + MH + 2)], GREEN, 2, end=True)
    else:
        f.line([(cx, BY - 4), (cx, MY + MH + 2)], GREEN, 2, end=True, start=True)
    f.text(cx + 8, BY - 26, acc, 12, "start", "bold", fill=GREEN)
f.text(742, 404, "PTR, STREAM and GROUP: the controller only writes them", 11, "start", fill=MUTED)

# ---------------- controller and instruction memory --------------------------------------------
f.box(*CT, "RV32 Controller", ["RV32I + Zmmul + WFI: in order, one instruction per cycle, no cache;",
                          "loads, stores and fetches complete in one cycle (load-use 0)",
                          "WFI wakes on GO or on the local wake (figure 2.1)"], fill="#FCEFC7", stroke="#B8860B", sw=1.6, size=11, tsize=14)
IM = (260, 576, 200, 92)
IBX = IM[0] + IM[2] / 2
f.box(*IM, "instruction memory", ["8 KB, 2 banks", ("0x0000–0x1FFF", {"mono": True}), "loader 0x1F00–0x1FFF"],
      fill=PURPLE_L, stroke=PURPLE, size=10.5, tsize=13)
# shaft from instruction memory's top edge up to just below the arrowhead, then the arrowhead at the controller
f.raw(f'<line x1="{IBX}" y1="{IM[1]}" x2="{IBX}" y2="{CT[1] + CT[3] + 14}" stroke="{PURPLE}" stroke-width="5"/>')
f.line([(IBX, CT[1] + CT[3] + 16), (IBX, CT[1] + CT[3] + 2)], PURPLE, 2, end=True)
f.text(IBX + 10, CT[1] + CT[3] + 26, "r (fetch only): instruction bus, 32-bit", 12, "start", "bold", fill=PURPLE)
# instruction memory's other side: port B from below
f.line([(IBX, IM[1] + IM[3] + 62), (IBX, IM[1] + IM[3] + 2)], PURPLE, 2, end=True)
f.text(IBX + 10, IM[1] + IM[3] + 58, "traffic director, port B: mode 5 (w)", 10.5, "start")

# ---------------- notes on the right ---------------------------------------------------------
NX = 742
f.lines(NX, 456, [
    ("Why everything is in the island", {"weight": "bold"}),
    "A fetch or load completes in one cycle and the next instruction",
    "uses it, so every memory on the controller's buses sits by it.",
    "The four tables (HIST, PTR, STREAM, GROUP) are separate macros,",
    "each with its own ports (§4.10), and they go by the controller,",
    "not the engines: the engines' table traffic (read-modify-writes",
    "with bypass, the STREAM and GROUP read-ahead) is pipelined and pays",
    "ts_engine_latency each way; the controller's loads cannot wait.",
], 11.5, lh=16)

# ---------------- notes below --------------------------------------------------------------------
f.lines(30, 792, [
    "r = the controller reads (fetch or load); w = it writes; r/w = both. Strict Harvard: it never loads from or stores to instruction memory; code arrives through port B.",
    "The table window (0x2000–0x3FFF) is used only when no engine is using it: the address generator not running, and ts_busy = 0 (§4.10). No hardware clear.",
    "Mode 6 writes the mailbox; mode-7 replies land at the data-memory word their addr names. The controller reads its big SRAM through the read port (§4.3):",
    "its stores queue 32-bit reads, and the results come back as mode-7 packets to the tile itself, through the loopback.",
    "Across the island's edge, the engines' table accesses take ts_engine_latency each way and port B's writes take cm_latency; both are pipelined (§12).",
], 11.5, lh=17)

f.caption(["The controller reaches every memory here in one cycle, so all of them sit with it in the control island. It reads and writes data memory, the registers",
           "and HIST, and only writes PTR, STREAM and GROUP; the engines that use the tables pay ts_engine_latency instead."],
          y=H - 38, source="Source: spec/machine-spec.md v1.0, §4.3, §4.10, §4.11, §4.12, §8, §12.")
f.save("fig3_1_controller.svg")
