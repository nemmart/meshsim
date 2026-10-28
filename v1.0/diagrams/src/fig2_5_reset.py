# Figure 2.5: reset and boot — every state element's value at chip reset, in HALT, and at the RUN/HALT edges; the boot
# sequence. Item 15.
from rtl import *

W = 1300
f = Fig(W, 2000, "Figure 2.5. Reset and boot: every state element at chip reset, in HALT and at the RUN/HALT edges",
        "v1.0 (spec/machine-spec.md §6.1, §6.2, §8) — one tile")
r = R(f)
AY = 76
f.text(30, AY + 10, "A. State elements (§6.1, §8's reset values, §6.1)", 14, "start", "bold")
U = "undefined"
rows = [["element", "chip reset", "in HALT", "HALT → RUN", "RUN → HALT", "figure"],
        ["controller core: PC, x1–x31", U, "not fetching", "PC = 0x1F00, x1–x31 = 0", "stops fetching", "8"],
        ["controller halted (bus error, illegal, ecall, ebreak)", "—", "—", "restarted", "—", "18"],
        ["run_q, go_q, both_q", "0 (RUN low)", "run_q 0", "run_q 1", "run_q 0", "26"],
        ["done", "reads 1 (forced in HALT)", "reads 1", "cleared", "reads 1", "26"],
        ["command landed", "0", "set by a slot-0 mode-6 write", "cleared", "kept", "26"],
        ["DMA_CONFIG, DMA_RECORD_BASE_ADDRESS", U, "kept", "kept", "kept", "18"],
        ["ROUTER_SET_HDR, ROUTER_SET_HI, SRAM_RD_DEST_ADDR", U, "kept", "kept", "kept", "18"],
        ["the three down-counters", "0", "kept (C&C's packets are never counted)", "kept", "kept", "16"],
        ["local-wake latch, qualifier, done gate, in-WFI", "0", "held 0", "0", "0", "16"],
        ["TILE_CONDITIONS", "0", "sets kept (loading errors survive into RUN)", "kept", "cleared", "17"],
        ["address generator: state, rd_done", "IDLE, 0", "held IDLE; rd_done kept", "IDLE", "→ IDLE", "19"],
        ["GATHER queue, active register", "empty, 0", "held empty", "empty", "cleared", "20"],
        ["read port's queue, in-flight count", "empty, 0", "held empty", "empty", "cleared", "23"],
        ["token FIFO", "empty", "held empty", "empty", "cleared", "22"],
        ["send port's holding register (send_busy)", "empty", "empty (the core is halted)", "empty", "—", "24"],
        ["router FIFOs, round-robin pointers", "empty, 0", "running", "running", "running", "25"],
        ["write engine FIFO, pointer stage, area queues", "empty", "running", "running", "running", "15, 21"],
        ["halt_q (the conditions' clear)", "1 (halted)", "1", "0", "1", "17"],
        ["PTR, HIST, STREAM, GROUP", U, "kept; written by packets", "kept", "kept", "12"],
        ["instruction and data memory, big SRAM", U, "written by modes 5–7 and 2", "kept", "kept", "5, 7"]]
y = r.table(30, AY + 22, [30, 370, 520, 790, 960, 1110, 1270], rows, rh=21, size=10)
f.text(30, y + 18, "In HALT the routers, the traffic director and the write engine run, so packets land exactly as in RUN; no packet may be in flight when RUN/HALT changes (a simulator check).", 10.5, "start", fill=MUTED)

BY = y + 50
f.text(30, BY, "B. Boot (§6.2)", 14, "start", "bold")
steps = ["1. Chip reset: every tile in HALT, done reads 1; the controller memories and big SRAM are undefined.",
         "2. C&C broadcasts the resident loader (0x1F00–0x1FFF) and the first image into instruction and data memory by mode 5.",
         "3. C&C sends one marker word to mailbox slot 0 (mode 6): command landed with done = 1 raises READY at once (figure 2.2).",
         "4. With all 64 READY bits set, C&C raises RUN: HALT → RUN resets the core to PC 0x1F00 and clears done and command landed (figure 2.1 (a)).",
         "5. The loader runs: it sets PTR[64] to its staging area in A7, so later images can be staged into big SRAM by mode 2 (never counted: src 64).",
         "6. The software writes every configuration register before use: they are undefined at chip reset and kept over HALT → RUN (§8)."]
y = BY + 24
for s_ in steps:
    f.text(30, y, s_, 11.5, "start"); y += 19
finish(f, r, y + 4, ["Chip reset puts every tile in HALT with its queues empty and its configuration registers undefined. HALT holds the address generator,",
                     "the GATHER engine, the read port and the token FIFO in reset, while the routers, the director and the write engine run. HALT → RUN",
                     "resets only the core; RUN → HALT clears the conditions. Boot loads by mode 5, then one marker and RUN."],
       "Source: spec/machine-spec.md v1.0.",
       legend=[("dot", INK, "figure numbers: where each element is drawn")])
f.save("fig2_5_reset.svg")
