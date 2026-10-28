# Figure 2.1: control timelines — boot, a one-command phase, a two-command phase, and the local wake (block copy).
from common import *

W, H = 1300, 1300
f = Fig(W, H, "Figure 2.1. Control: done, command landed, READY, GO, WFI and the local wake",
        "v1.0 (spec/machine-spec.md §4.12, §4.13, §6.1–§6.5) — one tile's view; schematic, not to scale")

LX, T0, T1 = 30, 300, 1268          # label column, time axis
RH = 32                              # row pitch
def X(t): return T0 + (T1 - T0) * t / 100.0

class Panel:
    def __init__(self, y, title, note=None):
        self.y = y
        f.text(LX, y, title, 14, "start", "bold")
        if note:
            f.text(LX, y + 18, note, 11.5, "start", fill=MUTED)
        self.r = 0
        self.top = y + (34 if note else 30)

    def row(self, label, sub=None):
        y = self.top + self.r * RH
        f.raw(f'<line x1="{LX}" y1="{y + RH - 3}" x2="{T1}" y2="{y + RH - 3}" stroke="#ECEAE3" stroke-width="1"/>')
        f.text(LX, y + (13 if sub else 18), label, 12, "start", "bold")
        if sub:
            f.text(LX, y + 26, sub, 10.5, "start", fill=MUTED)
        self.r += 1
        return y

    def digital(self, label, steps, c=INK, sub=None):
        """steps: list of (t, level) starting at t=0."""
        y = self.row(label, sub)
        hi, lo = y + 5, y + 22
        pts = []
        for i, (t, lev) in enumerate(steps):
            yy = hi if lev else lo
            if pts:
                pts.append((X(t), pts[-1][1]))
            pts.append((X(t), yy))
        pts.append((X(100), pts[-1][1]))
        f.line(pts, c, 2, end=False)
        return y

    def pulses(self, label, ts, c=ORANGE, sub=None, labels=None):
        y = self.row(label, sub)
        hi, lo = y + 5, y + 22
        pts = [(X(0), lo)]
        for t in ts:
            pts += [(X(t), lo), (X(t), hi), (X(t) + 7, hi), (X(t) + 7, lo)]
        pts.append((X(100), lo))
        f.line(pts, c, 2, end=False)
        if labels:
            for t, s in zip(ts, labels):
                f.text(X(t) + 12, y + 14, s, 10.5, "start", fill=c)
        return y

    def bus(self, label, segs, sub=None):
        """segs: list of (t0, t1, text, fill)"""
        y = self.row(label, sub)
        a, b = y + 3, y + 25
        for t0, t1, s, fill in segs:
            x0, x1 = X(t0), X(t1)
            k = min(5, (x1 - x0) / 3)
            f.raw(f'<polygon points="{x0},{(a + b) / 2} {x0 + k},{a} {x1 - k},{a} {x1},{(a + b) / 2} {x1 - k},{b} {x0 + k},{b}" '
                  f'fill="{fill}" stroke="{EDGE}" stroke-width="1"/>')
            if s:
                f.text((x0 + x1) / 2, y + 18, s, 11, "middle")
        return y

    def mark(self, t, n, text=None, c=MUTED):
        """vertical event line through the panel with a numbered bubble"""
        y0 = self.top - 4
        y1 = self.top + self.r * RH - 4
        f.raw(f'<line x1="{X(t)}" y1="{y0}" x2="{X(t)}" y2="{y1}" stroke="{c}" stroke-width="1" stroke-dasharray="2 3"/>')
        f.rect(X(t) - 9, y0 - 16, 18, 16, "white", c, 1, rx=8)
        f.text(X(t), y0 - 4, str(n), 10.5, "middle", "bold", fill=c)

    def notes(self, items, dy=14):
        y = self.top + self.r * RH + dy
        return f.lines(LX, y, items, 11.5, lh=15) + 26

CC = ORANGE_L
GREYF = "#F4F3EE"

# ---------------------------------------------------------------- (a) boot
p = Panel(92, "(a) Boot (§6.1, §6.2)", "no boot ROM: C&C loads the halted tiles over the mesh, then one marker word makes READY fire through the general rule")
p.bus("C&C sends", [(1, 40, "mode 5, broadcast: the loader (0x1F00–0x1FFF) and image A", CC),
                    (40, 57, "mode 6: marker → slot 0", CC), (58, 70, "waits: 64 READY", GREYF), (71, 99, "", "white")])
p.digital("RUN/HALT", [(0, 0), (76, 1)], ORANGE, "at the tile")
p.bus("core", [(1, 76, "HALT: the core does not fetch; routers, director and write engine run", GREYF),
               (76, 99, "RUN: the loader, PC 0x1F00", PURPLE_L)])
p.digital("done", [(0, 1), (76, 0)], INK, "forced to 1 in HALT")
p.digital("command landed", [(0, 0), (58, 1), (76, 0)])
p.pulses("READY", [59])
p.mark(58, 1); p.mark(71, 2, c=ORANGE); p.mark(76, 3)
y = p.notes(["(1) The marker lands in slot 0 → command landed. done is already 1, so READY fires at once. In-order delivery means everything sent before it has landed",
             "      (a command's packets to a tile are all broadcast or all unicast, §3.2).",
             "(2) C&C has all 64 READY bits and raises RUN.  (3) run_latency later the tile enters RUN: x1–x31 = 0, the core starts at the loader, and done and",
             "      command landed clear."])

# ---------------------------------------------------------------- (b) one-command phase
p = Panel(y, "(b) A one-command phase (a local sort, a gather) (§6.3)")
p.bus("core", [(1, 22, "previous work", PURPLE_L), (22, 74, "WFI", GREYF), (74, 99, "reads slot 0 and runs the phase", PURPLE_L)])
f.text(X(22) + 14, p.top + 2 - 6, "wait_for_command(): pulse done, then WFI", 10.5, "start", fill=PURPLE)
p.bus("C&C sends", [(30, 46, "args → slots 1–63", CC), (46, 54, "cmd → slot 0", CC), (55, 99, "", "white")])
p.digital("done", [(0, 0), (22, 1), (74, 0)])
p.digital("command landed", [(0, 0), (56, 1), (74, 0)])
p.pulses("READY", [57], sub="when both are 1")
p.bus("C&C", [(58, 72, "all 64 READY: clear, GO", CC)])
p.pulses("GO", [73], labels=["wakes WFI; clears done and command landed"])
p.bus("local-wake latch", [(1, 22, "may be set", "white"), (22, 73, "the done gate: held clear, only GO can wake the core", ORANGE_L),
                       (73, 99, "may be set", "white")])
p.mark(22, 1); p.mark(56, 2); p.mark(73, 3, c=ORANGE)
y = p.notes(["(1) The done gate: the done pulse clears the local-wake latch, and until GO an edge cannot set it, so wait_for_command() is",
             "      woken only by GO. A down-counter still non-zero at done sets the done-early condition (TILE_CONDITIONS bit 10, §4.13).",
             "(2) Command landed with done set: READY goes out. The two may become true in either order: a command that lands before the tile is done waits for the done pulse.",
             "(3) GO wakes every core in WFI. GO is not latched; it cannot arrive before the core is in WFI, since it needs the READY round trip (≈ ready_latency + 3 cycles)."])

# ---------------------------------------------------------------- (c) two-command phase
p = Panel(y, "(c) A two-command phase: engines that must start together (an all-to-all) (§6.3, §4.2)")
p.pulses("GO", [3, 70], labels=["configure", "start"])
p.bus("core", [(5, 32, "DMA_CONFIG and the tables", PURPLE_L), (32, 71, "done + WFI", GREYF),
               (71, 79, "same code", PURPLE_L), (79, 99, "phase runs; WFI for the local wake", PURPLE_L)])
f.text(X(79) + 2, p.top - 6 + RH * 0 - 0, "", 10)
p.bus("address generator", [(1, 80, "idle", "white"), (80, 99, "running", "#A8DCC8")])
p.bus("C&C sends", [(40, 48, "start → slot 0", CC)])
p.digital("done", [(0, 1), (3, 0), (32, 1), (70, 0)])
p.digital("command landed", [(0, 1), (3, 0), (49, 1), (70, 0)])
p.pulses("READY", [50])
p.mark(32, 1); p.mark(70, 2, c=ORANGE); p.mark(79, 3, c=GREEN)
y = p.notes(["(1) The configure command ends: done, then WFI.  (2) GO: every core leaves WFI on the same cycle.",
             "(3) Each core runs identical code from WFI to the start store, DMA_PULSE[1], so every address generator starts on the same cycle (start skew ≤ 1 cycle, §3.2);",
             "      the first requests follow 4·L_ts + 3 cycles after the start store (4·L_ts + 2 after the cycle the start takes effect) on every tile. GO never starts an engine itself."])

# ---------------------------------------------------------------- (d) local wake
p = Panel(y, "(d) The local wake: a block copy into controller memory (§6.5, §4.12)", None)
p.bus("core", [(1, 7, "STREAM", PURPLE_L), (7, 19, "DMA_CM_REMAINING = n", PURPLE_L), (19, 30, "DMA_CONFIG", PURPLE_L),
               (30, 39, "start", PURPLE_L), (39, 82, "WFI", GREYF), (82, 99, "continues", PURPLE_L)])
p.bus("address generator", [(39, 72, "mode-5 stream run to itself: 64 bits a cycle", GREEN_L), (72, 99, "", "white")])
p.bus("DMA_CM_REMAINING", [(1, 13, "0", "white"), (13, 41, "n", BOX), (41, 76, "n − 1, …, 1: one per mode-5 packet", BOX), (76, 99, "0", "white")])
p.digital("cm_idle", [(0, 1), (41, 0), (79, 1)], INK, "port-B pipeline empty")
p.digital("local-wake latch", [(0, 0), (79, 1), (82, 0)], ORANGE)
p.mark(13, 1); p.mark(79, 2, c=ORANGE); p.mark(82, 3)
y = p.notes(["(1) The store to DMA_CM_REMAINING clears the latch, so the wait starts clean. DMA_CONFIG then describes a one-stream run, and one DMA_PULSE store starts it.",
             "(2) The rising edge of \"every counter done\" (all three 20-bit counters at 0, with mem_idle and cm_idle) sets the latch: the copied data is already in memory.",
             "(3) WFI wakes and consumes the latch. Had the copy ended before WFI, the latch would hold the edge and WFI would fall straight through.",
             "      A count of 0 never makes an edge. The wake waits for every counter, not one (a program that needs one counter polls it instead). The other wake source is GO.",
             "      One WFI consumes the latch."], dy=12)

f.h = H = int(y + 80)

f.caption(["READY is automatic: a tile pulses it once when its done bit and \"command landed\" are both set. C&C answers 64 READY pulses with one GO, which",
           "only wakes cores in WFI. A tile's own arrivals wake it through the local wake, the latched edge of its three down-counters all reaching 0.",
           "From the done pulse until GO, only GO can wake the core."],
          y=H - 54, source="Source: spec/machine-spec.md v1.0, §4.2, §4.12, §4.13, §5, §6.1–§6.5, §9.")
f.save("fig2_1_control.svg")
