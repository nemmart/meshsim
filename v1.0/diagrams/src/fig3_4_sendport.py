# Figure 3.4: the controller send port — ROUTER_SET_HDR, ROUTER_SET_HI, the capture by ROUTER_SEND_LO into the holding
# register, auto-increment, the stall rule, send_busy, and the handshake with the injection point. Item 10.
from rtl import *

W = 1300
f = Fig(W, 2000, "Figure 3.4. The controller send port: ROUTER_SET_HDR, ROUTER_SET_HI, ROUTER_SEND_LO and the holding register",
        "v1.0 (spec/machine-spec.md §4.11, §8.4) — one packet a cycle when no read return is waiting")
r = R(f)

AY = 76
r.frame(16, AY, W - 32, 400, "A. Datapath (§4.11, §4.11)")
HD = (40, AY + 70, 250, 64)
r.reg(*HD, "ROUTER_SET_HDR", ["dst [6:0], mode [10:7], addr [30:11]", "a store loads it; addr steps at capture"])
HI = (40, AY + 170, 250, 58)
r.reg(*HI, "ROUTER_SET_HI", ["the payload's high 32 bits", "a store loads it; cleared at capture"])
r.lbl(40, AY + 258, "ROUTER_SEND_LO store: wdata = the payload's low 32 bits", "start", c=ORANGE, weight="bold")
# auto-increment
AI = (340, AY + 52, 230, 54)
f.box(*AI, "addr + inc", ["inc = mode 5: 2; modes 6, 7: 1; else 0", "mod 2^20, written back at capture"], fill=BOX, stroke=INK, sw=1.3, size=10, tsize=11.5)
f.line([(HD[0] + HD[2], HD[1] + 20), (AI[0] - 1, HD[1] + 20)], INK, 1.4, end=True)
f.line([(AI[0] + 115, AI[1] - 1), (AI[0] + 115, AY + 40), (165, AY + 40), (165, HD[1] - 1)], INK, 1.4, end=True)
# holding register
HR = (640, AY + 120, 280, 120)
r.reg(*HR, "holding register (one packet)", ["valid; dst, mode, addr; payload 64", "", "capture = st_sendlo & ¬stall:", "{SET_HDR, SET_HI, wdata} → holding"],
      fill=PURPLE_L, stroke=PURPLE)
f.line([(HD[0] + HD[2], HD[1] + 44), (600, HD[1] + 44), (600, HR[1] + 30), (HR[0] - 1, HR[1] + 30)], INK, 1.6, end=True)
f.line([(HI[0] + HI[2], HI[1] + 29), (600, HI[1] + 29), (600, HR[1] + 60), (HR[0] - 1, HR[1] + 60)], INK, 1.6, end=True)
f.line([(400, AY + 254), (620, AY + 254), (620, HR[1] + 90), (HR[0] - 1, HR[1] + 90)], ORANGE, 1.6, end=True)
f.text(590, AY + 248, "wdata", 10, "end", fill=ORANGE)
f.line([(HR[0] + HR[2], HR[1] + 60), (1250, HR[1] + 60)], PURPLE, 2, end=True)
f.text(1250, HR[1] + 52, "to the injection point (figure 7.3), input 2", 10.5, "end", "bold", fill=PURPLE)
f.line([(1180, HR[1] + 100), (HR[0] + HR[2] + 1, HR[1] + 100)], ORANGE, 1.4, end=True, dash=CTL)
f.text(1180, HR[1] + 116, "send_taken: injected this cycle", 10, "end", fill=ORANGE)
# stall and busy
SY = AY + 320
ig = r.gate_and(700, SY, 3, "stall")
for y, s, c in zip(ig, ["st_sendlo", "valid", "send_taken"], [ORANGE, PURPLE, ORANGE]):
    f.line([(630, y), (692 if s == "send_taken" else 699, y)], c, 1.3, end=False, dash=CTL if c == ORANGE else None)
    r.lbl(626, y, s, c=c)
r.bubble(700, ig[2], ORANGE)
f.line([(736, SY), (800, SY)], ORANGE, 1.6, end=True, dash=CTL)
r.lbl(806, SY, "stall the core: hold the store this cycle", "start", c=ORANGE, weight="bold")
r.lbl(806, SY + 22, "send_busy = valid → TILE_STATUS [3]", "start", c=PURPLE, weight="bold")
f.lines(30, AY + 300, ["Valid clears on send_taken, and is set by", "a capture; both in one cycle: set. The store",
                       "stalls only if the register is full and not", "injected this cycle (§4.11), so back-to-back",
                       "sends go out one a cycle."], 10.5, lh=15, fill=MUTED)

# B. timing
BY = AY + 420
r.frame(16, BY, W - 32, 290, "B. Cycle by cycle: three sends, the third behind a two-packet return")
yb = r.wave(30, BY + 34, 190, 105, 10, [
    ("core", "bus", ["SEND_LO a", "SEND_LO b", "SEND_LO c", "SEND_LO c", "SEND_LO c", "next", "", "", "", ""], PURPLE_L),
    ("stall", "lvl", [0, 0, 1, 1, 0, 0, 0, 0, 0, 0], ORANGE),
    ("holding", "bus", ["", "a", "b", "b", "b", "c", "", "", "", ""], PURPLE_L),
    ("return waiting", "lvl", [0, 0, 1, 1, 0, 0, 0, 0, 0, 0], GREEN),
    ("injected", "bus", ["", "a", "ret lo", "ret hi", "b", "c", "", "", "", ""], GREEN_L),
    ("send_taken", "lvl", [0, 1, 0, 0, 1, 1, 0, 0, 0, 0], ORANGE),
])
f.lines(30, yb + 18, ["t: a is captured; t+1: a goes out while b is captured (no stall: the register is being emptied). t+2, t+3: a 128-bit return holds the",
                      "injection point for two packets, so b waits and the store of c stalls. t+4: b goes out and c is captured; t+5: c goes out."], 10.5, lh=15, fill=MUTED)

# C. interface
CY = BY + 310
f.text(30, CY, "C. Interface", 14, "start", "bold")
rows = [["signal", "width", "from → to", "meaning"],
        ["st_hdr, st_hi, st_sendlo, wdata", "1, 1, 1, 32", "register decode (fig 3.2) → here", "stores to ROUTER_SET_HDR, ROUTER_SET_HI, ROUTER_SEND_LO"],
        ["stall", "1", "here → controller", "hold the ROUTER_SEND_LO store this cycle"],
        ["holding valid, packet", "1, 95", "here → injection point (fig 7.3)", "dst 7, mode 4, addr 20, payload 64"],
        ["send_taken", "1", "injection point → here", "the packet was injected this cycle"],
        ["send_busy", "1", "here → TILE_STATUS [3] (fig 3.2)", "the holding register is full"]]
y = r.table(30, CY + 12, [30, 280, 380, 660, 1270], rows)
y = r.notes(y + 26, ["No checks at the sender (§4.11): an invalid mode, mode 1 to another tile, or dst 65 (unused) go out as written; the receiver drops a bad mode "
                     "(condition 9 there), and the simulator checks the rest. ROUTER_SET_HDR and ROUTER_SET_HI are undefined at chip reset (§8)."])
finish(f, r, y, ["A ROUTER_SEND_LO store captures the header, the high word and its own data into the one-packet holding register; the header's addr then",
                 "steps by the words the packet writes, and ROUTER_SET_HI clears. The store stalls only while the register is full and not leaving this cycle,",
                 "so sends run one a cycle unless read returns, which go first, hold the injection point."],
       "Source: spec/machine-spec.md v1.0.",
       legend=[("line", PURPLE, "the controller"), ("line", GREEN, "returns"), ("ctrl", ORANGE, "control"), ("dot", INK, "junction; wedge: clocked register")])
f.save("fig3_4_sendport.svg")
