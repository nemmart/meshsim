# Figure 1.4: the 102-bit link word and the packet modes 0–15.
from common import *

W, H = 1300, 1170
f = Fig(W, H, "Figure 1.4. The link word and the packet modes",
        "v1.0 (spec/machine-spec.md §3.1, §4.8, §4.12) — the receiving tile's traffic director routes every packet by its mode alone")

# ---------------- the link word -------------------------------------------------------
X0, Y0, S = 44, 104, 11.9
f.text(X0, Y0 - 12, "link word: 102 bits, one per cycle per link direction", 13, "start", "bold")
fields = [("src", 7, SAND), ("dst", 7, SAND), ("mode", 4, ORANGE_L), ("addr", 20, BOX), ("payload", 64, BOX)]
bitbar(f, X0, Y0, fields, S, h=46)
notes = [
    ("src (7)", "address of the originator, added by the router; 127 = idle packet (carries nothing)"),
    ("dst (7)", "destination, absolute: 0–63 tiles, 64 C&C, 126 broadcast; 65 unused"),
    ("mode (4)", "what the packet is: the table below (4 bits)"),
    ("addr (20)", "receiver-local address; its meaning depends on the mode (0 where unused)"),
    ("payload (64)", "data; a 32-bit use is the low half (bits 31–0); a key sits in bits 31–0,"),
    ("", "and a mode-4 packet gets the ordinal in bits 63–32 (§4.9)"),
]
for i, (k, v) in enumerate(notes):
    col, row = (0, i) if i < 3 else (1, i - 3)
    x = X0 + col * 610
    yy = Y0 + 74 + row * 17
    f.text(x, yy, k, 11.5, "start", "bold")
    f.text(x + 92, yy, v, 11.5, "start", fill=INK)
f.text(X0 + 102 * S, Y0 - 12, "field order as listed in §3.1; bit positions are not assigned in the draft", 11, "end", fill=MUTED, italic=True)

# ---------------- the mode table ------------------------------------------------------
TY = 252
cols = [("mode", 52), ("packet", 150), ("sent by", 238), ("addr", 150), ("director sends it to", 164),
        ("what happens", 330), ("counted in", 128)]
xs = [X0]
for _, w in cols:
    xs.append(xs[-1] + w)
f.rect(X0, TY, xs[-1] - X0, 30, "#E9E7E0", EDGE, 1, rx=0)
for (nm, w), x in zip(cols, xs):
    f.text(x + 8, TY + 20, nm, 12.5, "start", "bold")

DEST = {"resp": (GREEN, GREEN_L), "gath": (GREEN, GREEN_L), "we": (BLUE, BLUE_L), "pb": (PURPLE, PURPLE_L), "drop": (EDGE, BOX)}
rows = [
    ("0", "invalid", "never sent (invariant 5)",
     "—", "drop", "dropped; sets the bad-mode condition", "", "—", "bad"),
    ("1", "gather request", "the address generator: a mode-1 stream with d = 0 (local only, invariant 10)",
     "unused (0)", "gath", "the GATHER engine's request queue (§4.4)",
     "payload = (key, ordinal); k reads at DMA_RECORD_BASE_ADDRESS + 2k·ordinal, each sent back to the tile itself as a mode-2 gather value",
     "— (its gather values count)", None),
    ("2", "big-SRAM write by source", "address-generator streams (all-to-alls, local copies); the GATHER engine's values; the controller's send port; C&C staging (src 64)",
     "unused (0)", "we", "the write engine (§4.9)",
     "64 bits written at PTR[src], then PTR[src] += 2; count-on-arrival applies if set", "DMA_SRAM_REMAINING", None),
    ("3", "bucket write", "the address generator: mode-3 streams (the local sort's bucket passes, to the tile itself)",
     "unused (0)", "we", "the write engine (§4.9)",
     "64 bits written at PTR[t], t = the payload's bucket digit (§4.15), then PTR[t] += 2", "DMA_SRAM_REMAINING", None),
    ("4", "augmented write", "the address generator: mode-4 streams (step 1's local pass, the keys all-to-all)",
     "unused (0)", "we", "the write engine (§4.9)",
     "as mode 2, at PTR[src], with the high 32 bits replaced by the ordinal, (target chunk − DMA_RECORD_BASE_ADDRESS) / 2; may mix with mode 2 in a phase",
     "DMA_SRAM_REMAINING", None),
    ("5", "controller-memory write", "the address generator: a mode-5 stream with d = 0 (block copy, §6.5); C&C, loading halted tiles at boot; a send port (vectors such as the group maxima, §14)",
     "word address in instruction or data memory", "pb", "port B of instruction or data memory",
     "2 consecutive words (64 bits) written at addr; an addr outside both memories or in the mailbox is a programming error, unchecked in hardware (invariant 11)", "DMA_CM_REMAINING", "addr"),
    ("6", "mailbox write", "C&C: commands (arguments to slots 1–63 first, the command word to slot 0 last); the only mode that writes the mailbox",
     "slot = addr[7:0] (256 slots)", "pb", "port B of data memory: the mailbox",
     "the low 32 bits written to the slot; a write to slot 0 sets \"command landed\" (§6.3)", "— (\"command landed\")", None),
    ("7", "reply", "a tile's send port: one-word exchanges such as the split's probe keys, and per-tile totals (addr by convention: base + own index); the read port's results, to the tile itself (§4.3)",
     "the data-memory word to write, chosen by the requester", "pb", "port B of data memory",
     "the low 32 bits written at addr in data memory; an addr in the mailbox or outside data memory is a programming error (invariants 12, 13)", "DMA_REPLY_REMAINING", None),
    ("8–15", "reserved", "never sent (invariant 5); free for packet types still to come", "—", "drop", "dropped; sets the bad-mode condition",
     "", "—", "bad"),
]
y = TY + 30
for (m, name, by, addr, dk, dest, what, cnt, special) in rows:
    cells = [wrap(name, cols[1][1] - 14, 12), wrap(by, cols[2][1] - 14, 11.5), wrap(addr, cols[3][1] - 14, 11.5),
             wrap(dest, cols[4][1] - 24, 11.5), wrap(what, cols[5][1] - 14, 11.5), wrap(cnt, cols[6][1] - 14, 11.5)]
    nl = max(len(c) for c in cells)
    rh = max(40, 12 + nl * 15)
    if special == "addr":
        rh += 16
    if special == "bad":
        rh += 4
    if special == "new":
        rh += 18
    f.rect(X0, y, xs[-1] - X0, rh, "white", "#D9D7CF", 0.8, rx=0)
    c, cl = DEST[dk]
    # mode number chip
    cw_ = 32 if len(m) < 3 else 42
    f.rect(xs[0] + 26 - cw_ / 2, y + 8, cw_, 24, ORANGE_L, ORANGE, 1, rx=4)
    f.text(xs[0] + 26, y + 25, m, 14 if len(m) < 3 else 12, "middle", "bold")
    f.lines(xs[1] + 8, y + 19, cells[0], 12, weight="bold", lh=15)
    f.lines(xs[2] + 8, y + 19, cells[1], 11.5, lh=15)
    f.lines(xs[3] + 8, y + 19, cells[2], 11.5, lh=15)
    f.rect(xs[4] + 6, y + 7, cols[4][1] - 12, rh - 14, cl, c, 1, rx=4)
    f.lines(xs[4] + 13, y + 19, cells[3], 11.5, lh=15)
    f.lines(xs[5] + 8, y + 19, cells[4], 11.5, lh=15)
    f.lines(xs[6] + 8, y + 19, cells[5], 11.5, lh=15, weight="bold" if cnt[0] != "—" else "normal")
    y += rh
# column rules
for x in xs[1:-1]:
    f.raw(f'<line x1="{x}" y1="{TY}" x2="{x}" y2="{y}" stroke="#D9D7CF" stroke-width="0.8"/>')
f.rect(X0, TY, xs[-1] - X0, y - TY, "none", EDGE, 1, rx=0)

# ---------------- notes ---------------------------------------------------------------
ny = y + 30
f.lines(X0, ny, [
    ("Two principles behind the table (§3.1):", {"weight": "bold"}),
    "• At most one streaming reader of big SRAM per tile at a time, plus the dependent single reads of the GATHER engine and the controller read port.",
    "   There are no remote pulls: gathers are local only (mode 1), and the controller reads only its own big SRAM (§4.3).",
    "• Bulk movement is a push: anything larger than one word is requested with a small descriptor and delivered by the owner's address generator.",
], 12, lh=17)
f.lines(X0, ny + 80, [
    ("Counting (§4.12):", {"weight": "bold"}),
    "each counted packet decrements its 20-bit counter by 1 as the director accepts it, a 64-bit mode-5 packet included. An arrival at a counter already at 0 sets",
    "the unexpected-arrival condition instead of wrapping (§4.13). Packets from C&C (src 64) are never counted, in any of the three counters (§4.12).",
    "Modes 0 and 8–15 are dropped and set the bad-mode condition, TILE_CONDITIONS bit 9 (§4.13).",
], 12, lh=17)

f.caption(["Every packet carries a 4-bit mode, and the receiver's traffic director routes on it alone: mode 1 feeds the GATHER engine, 2–4 the write engine,",
           "5–7 port B of the controller's memories; 0 and 8–15 are dropped with the bad-mode condition. Three down-counters count modes 2–4, 5 and 7."],
          y=H - 38, source="Source: spec/machine-spec.md v1.0, §3.1, §4.2–§4.4, §4.8, §4.9, §4.11, §4.12, §4.13, §9.")
f.save("fig1_4_packets.svg")
