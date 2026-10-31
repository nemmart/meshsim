# The worked-example template (REGISTERS.md section 7): figure 1.3's tile, plus a top band with the controller, its memories
# and the router, drawn whole in every example so the five examples share one map. An example names the units and paths
# it uses (highlighted; the rest is drawn faint), numbers its register stores (purple circles, matching the program in
# the controller box) and letters the data-flow steps (circles on the paths, explained under the figure).
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "diagrams", "src"))   # common.py: the figure set's conventions
from common import *


def save(f, name):
    """Write the figure into ../svg/ (common's Fig.save writes into the diagrams folder, which isn't ours)."""
    os.makedirs(os.path.join(HERE, "..", "svg"), exist_ok=True)
    with open(os.path.join(HERE, "..", "svg", name), "w") as out:
        out.write(f.svg())
    print("wrote", name)


def program(name):
    """An example's program, from its block in ../REGISTERS.md (section 7): [(store number or None, code)].
    The block is '<!-- program: NAME -->' then a fenced block; a line is 'N  code' (a register store) or '   code'."""
    doc = open(os.path.join(HERE, "..", "..", "REGISTERS.md"), encoding="utf-8").read()
    m = re.search(r"<!-- program: " + re.escape(name) + r" -->\n```[a-z]*\n(.*?)\n```", doc, re.S)
    if not m:
        raise SystemExit(f"REGISTERS.md has no program block '{name}'")
    out = []
    for line in m.group(1).split("\n"):
        num, code = line[:3].strip(), line[3:].rstrip()      # columns 0-2: the store number; the code keeps its indentation
        out.append((int(num) if num else None, code))
    return out

W = 1300
FAINT_FILL, FAINT_EDGE, FAINT_TEXT = "#F8F7F3", "#D9D7CE", "#BDBBB2"
SW = 2.2

# ---- geometry (figure 1.3's, moved down by DY to make room for the top band) ------------------------------------
DY = 240
CTL = (40, 76, 560, 200)            # the controller and its program
CMEM = (624, 76, 270, 70)           # instruction and data memory (port B's target)
RTR = (930, 76, 250, 130)            # the router; its mesh links leave on the right
EJ_Y = CTL[1] + CTL[3] + 34           # the eject line, under the top band
DIR = (40, 100 + DY, 260, 98)
INJ = (1000, 100 + DY, 260, 98)
EY = 330 + DY
WE = (40, EY, 400, 104)
RP = (480, EY, 180, 104)
AG = (690, EY, 316, 104)
GA = (1036, EY, 196, 104)
IY = 256 + DY
MX, MT, MB = AG[0] + AG[2] / 2, 482 + DY, 530 + DY
SR = (40, 600 + DY, 1220, 96)
RX = 1246

# inputs above the engines: id -> (x, w, name, sub, kind 't' table / 'r' register, both directions, engine top, name size)
INPUTS = {
    "PTR": (118, 82, "PTR[256]", "read-modify-write", "t", True, 10.5),
    "HIST": (210, 82, "HIST[256]", "read-modify-write", "t", True, 10.5),
    "CFG_WE": (302, 132, "DMA_CONFIG", "bucket, hist digits", "r", False, 11),
    "RDQ": (484, 172, "SRAM_RD_ADDR_QUEUE", "SRAM_RD_DEST_ADDR", "r", False, 10),
    "STREAM": (694, 80, "STREAM[256]", "stream entries", "t", False, 10.5),
    "GROUP": (778, 70, "GROUP[64]", "group entries", "t", False, 10.5),
    "CFG_AG": (852, 80, "DMA_CONFIG", "the run", "r", False, 9.5),
    "PULSE": (936, 70, "DMA_PULSE", "start, done", "r", False, 9.5),
    "RECBASE": (1040, 188, "DMA_RECORD_BASE_ADDRESS", "k is in DMA_CONFIG", "r", False, 9.5),
}


class TileMap:
    """Draws the tile. active: ids of units and paths used. stores: {id: [program line numbers]}.
    steps: {path id: letter}. buffers: [(label, sub, x, w, 'read'|'write')] inside the big SRAM."""

    def __init__(self, f, active, stores=None, steps=None, buffers=(), bodies=None, colours=None):
        self.f, self.a = f, set(active)
        self.colours = colours or {}         # {path id: colour}: an example's own colour for a path (and its step marker)
        self.bodies = bodies or {}           # {unit id: [lines]}: an example's own text for a unit
        self.stores, self.steps = stores or {}, steps or {}
        self.buffers = buffers

    # ---- styled primitives: highlighted when used, faint otherwise -----------------------------------------
    def on(self, k):
        return k in self.a

    def unit(self, k, rect, title, body, fill, stroke, tsize=13, size=11):
        x, y, w, h = rect
        body = self.bodies.get(k, body)
        if self.on(k):
            self.f.box(x, y, w, h, title, body, fill=fill, stroke=stroke, sw=1.6, size=size, tsize=tsize)
        else:
            self.f.rect(x, y, w, h, FAINT_FILL, FAINT_EDGE, 1)
            lh = size * 1.32
            total = tsize * 1.35 + len(body) * lh
            cy = y + (h - total) / 2 + tsize * 0.95
            self.f.text(x + w / 2, cy, title, tsize, "middle", "bold", fill=FAINT_TEXT)
            for i, s in enumerate(body):
                self.f.text(x + w / 2, cy + tsize * 1.35 + i * lh, s, size, "middle", fill=FAINT_TEXT)

    def wire(self, k, pts, c, w=SW, dash=None, start=False, end=True):
        c = self.colours.get(k, c)
        if self.on(k):
            self.f.line(pts, c, w, end=end, start=start, dash=dash)
        else:
            self.f.line(pts, FAINT_EDGE, 1.3, end=end, start=start, dash=dash)

    def label(self, k, x, y, s, c, anchor="start", size=11):
        c = self.colours.get(k, c)
        self.f.text(x, y, s, size, anchor, fill=c if self.on(k) else FAINT_TEXT)

    def mark(self, x, y, s, c, filled=False, r=10):
        """A step marker: a circle with a number (a register store) or a letter (a data-flow step)."""
        self.f.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" fill="{c if filled else "white"}" stroke="{c}" stroke-width="1.6"/>')
        self.f.text(x, y + 4 * r / 10, s, 11.5 * r / 10, "middle", "bold", fill="white" if filled else c)

    def step(self, k, x, y, c):
        c = self.colours.get(k, c)
        if k in self.steps:
            self.mark(x, y, self.steps[k], c)

    def store(self, k, x, y):
        for i, n in enumerate(self.stores.get(k, [])):
            self.mark(x + 23 * i, y, str(n), PURPLE, filled=True)

    # ---- the drawing ---------------------------------------------------------------------------------------
    def draw(self, program):
        f = self
        # top band: controller, its memories, the router
        x, y, w, h = CTL
        self.f.rect(x, y, w, h, PURPLE_L, PURPLE, 1.6)
        self.f.text(x + 12, y + 20, "controller (RV32 core): the program", 13, "start", "bold")
        lh = min(17, (h - 52) / max(1, len(program)))       # a long program gets tighter lines, not a bigger box
        size = 11.5 if lh >= 16 else 10.5
        for i, line in enumerate(program):
            n, code = line
            if n is not None:
                self.mark(x + 22, y + 44 + lh * i - 4, str(n), PURPLE, filled=True, r=min(10, lh / 2 - 0.5))
            self.f.text(x + 40, y + 44 + lh * i, code.replace(" ", "\u00A0"), size, "start", mono=True)   # keep the alignment
        self.unit("cmem", CMEM, "instruction and data memory", ["port B writes modes 5, 6, 7"], PURPLE_L, PURPLE, tsize=12.5)
        on = self.on("router")
        self.f.rect(*RTR, BOX if on else FAINT_FILL, INK if on else FAINT_EDGE, 1.6 if on else 1)
        self.f.text(RTR[0] + RTR[2] / 2, RTR[1] + 19, "router", 13, "middle", "bold", fill=INK if on else FAINT_TEXT)
        self.f.text(RTR[0] + RTR[2] / 2, RTR[1] + 34, "routes horizontal first, then vertical", 10.5, "middle", fill=MUTED if on else FAINT_TEXT)

        # router <-> director / injection, and its routes inside: out to the mesh, in from the mesh, the loopback
        ej_x, ij_x = RTR[0] + 60, RTR[0] + RTR[2] - 40
        yo, yi, yl = RTR[1] + 58, RTR[1] + 82, RTR[1] + 112
        rx2 = RTR[0] + RTR[2]
        self.wire("rt_out", [(ij_x, RTR[1] + RTR[3]), (ij_x, yo), (rx2, yo)], GREEN, w=1.8, end=False)
        self.wire("mesh_out", [(rx2, yo), (W - 14, yo)], GREEN, w=SW)
        self.label("mesh_out", rx2 + 6, yo - 6, "to the mesh", GREEN, size=10.5)
        self.wire("mesh_in", [(W - 14, yi), (rx2, yi)], INK, w=SW, end=False)
        self.wire("rt_in", [(rx2, yi), (ej_x, yi), (ej_x, RTR[1] + RTR[3])], INK, w=1.8, end=False)
        self.label("mesh_in", rx2 + 6, yi + 15, "from the mesh", INK, size=10.5)
        self.step("mesh_out", rx2 + 40, yo - 26, GREEN)
        self.step("mesh_in", rx2 + 40, yi + 30, INK)
        self.wire("eject", [(ej_x, RTR[1] + RTR[3]), (ej_x, EJ_Y), (DIR[0] + 130, EJ_Y), (DIR[0] + 130, DIR[1] - 2)], INK)
        self.label("eject", ej_x - 10, EJ_Y - 8, "eject: every packet for this tile", INK, "end")
        self.step("eject", ej_x + 18, (RTR[1] + RTR[3] + EJ_Y) / 2, INK)
        self.wire("inject", [(ij_x, INJ[1]), (ij_x, RTR[1] + RTR[3] + 2)], GREEN, w=SW + 0.4)
        self.label("inject", ij_x + 8, (RTR[1] + RTR[3] + INJ[1]) / 2 + 4, "injection", GREEN)
        if self.on("loopback"):
            lc = self.colours.get("loopback", GREEN)
            self.f.line([(ij_x, yl), (ej_x + 2, yl)], lc, 1.8, end=True)
            self.f.text((ij_x + ej_x) / 2, yl - 4, "loopback: dst = me", 10, "middle", fill=lc)
        self.step("loopback", RTR[0] + RTR[2] / 2, yl + 14, GREEN)

        # director, injection point and their links to the controller
        self.unit("dir", DIR, "traffic director", ["routes each packet by its mode;", "counts as it accepts:",
                  "DMA_SRAM_, DMA_CM_,", "DMA_REPLY_REMAINING"], BOX, INK, size=11)
        self.store("dir", DIR[0] + DIR[2] - 12, DIR[1] + 12)
        self.unit("inj", INJ, "injection point", ["one packet per cycle:", "1. read returns", "2. the send port"], GREEN_L, GREEN)
        self.wire("wake", [(DIR[0] + 30, DIR[1]), (DIR[0] + 30, CTL[1] + CTL[3] + 2)], ORANGE, dash=DASH["ctrl"])
        self.label("wake", DIR[0] + 52, CTL[1] + CTL[3] + 22, "local wake: every counter done → WFI returns", ORANGE)
        self.step("wake", DIR[0] + 30, DIR[1] - 16, ORANGE)
        self.wire("portB", [(DIR[0] + DIR[2], DIR[1] + 30), (CMEM[0] + 135, DIR[1] + 30), (CMEM[0] + 135, CMEM[1] + CMEM[3] + 2)], PURPLE)
        self.label("portB", DIR[0] + DIR[2] + 8, DIR[1] + 22, "port B: modes 5, 6, 7", PURPLE)
        self.step("portB", CMEM[0] + 135, DIR[1] + 10, PURPLE)
        self.wire("send", [(CTL[0] + 470, CTL[1] + CTL[3]), (CTL[0] + 470, DIR[1] + 80), (INJ[0] - 2, DIR[1] + 80)], PURPLE)
        self.label("send", INJ[0] - 10, DIR[1] + 72, "send port: ROUTER_SET_HDR, ROUTER_SET_HI, ROUTER_SEND_LO", PURPLE, "end")
        self.step("send", CTL[0] + 470, DIR[1] + 50, PURPLE)
        self.store("send", CTL[0] + 498, DIR[1] + 20)

        # engines
        self.unit("we", WE, "write engine", ["modes 2, 4: at PTR[src];  mode 3: at PTR[bucket digit];  then += 2",
                  "mode 4: high 32 bits ← ordinal from DMA_RECORD_BASE_ADDRESS",
                  "count-on-arrival: HIST[hist digit] += 1"], BLUE_L, BLUE)
        self.unit("rp", RP, "read port", ["≥ 64-entry request queue;", "32-bit reads queued by", "the controller's stores"], GREEN_L, GREEN, tsize=12.5)
        self.unit("ag", AG, "address generator", ["walks STREAM (stream runs) or", "GROUP (group runs);", "one request per visit"], GREEN_L, GREEN)
        self.unit("ga", GA, "GATHER engine", ["mode 1: k reads at", "base + 2k·ordinal"], GREEN_L, GREEN)

        # inputs: tables (purple) and registers (orange)
        for k, (x, w, name, sub, kind, both, ns) in INPUTS.items():
            c, fill = (PURPLE, "white") if kind == "t" else (ORANGE, SAND)
            if not self.on(k):
                c, fill = FAINT_EDGE, FAINT_FILL
            self.f.rect(x, IY, w, 42, fill, c, 1.3, rx=4)
            self.f.text(x + w / 2, IY + 18, name, ns, "middle", "bold", fill=INK if self.on(k) else FAINT_TEXT, mono=(kind == "r"))
            self.f.text(x + w / 2, IY + 33, sub, 10, "middle", fill=MUTED if self.on(k) else FAINT_TEXT)
            lc = (PURPLE if kind == "t" else ORANGE) if self.on(k) else FAINT_EDGE
            self.f.line([(x + w / 2, IY + 44), (x + w / 2, EY - 2)], lc, 1.6, end=True, start=both,
                        dash=DASH["open"] if kind == "t" else DASH["ctrl"])
            self.store(k, x + 4, IY - 2)
            self.step(k, x + w / 2 + 16, IY + 60, PURPLE)

        # director -> engines
        self.wire("m24", [(DIR[0] + 60, DIR[1] + DIR[3]), (DIR[0] + 60, WE[1] - 2)], BLUE, dash=DASH["write"])
        self.label("m24", DIR[0] + 52, WE[1] - 14, "modes 2–4", BLUE, "end")
        self.step("m24", DIR[0] + 60, IY - 20, BLUE)
        self.wire("m1", [(DIR[0] + 230, DIR[1] + DIR[3]), (DIR[0] + 230, IY - 30), (GA[0] + 20, IY - 30), (GA[0] + 20, GA[1] - 2)], INK, w=1.8)
        self.label("m1", GA[0] + 12, IY - 38, "mode 1", INK, "end")

        # the SRAM read queue (priority mux)
        on = self.on("mux")
        self.f.raw(f'<polygon points="{MX - 90},{MT} {MX + 90},{MT} {MX + 50},{MB} {MX - 50},{MB}" '
                   f'fill="{GREEN_L if on else FAINT_FILL}" stroke="{GREEN if on else FAINT_EDGE}" stroke-width="1.4"/>')
        self.f.text(MX, MT + 38, "priority mux", 11.5, "middle", "bold", fill=INK if on else FAINT_TEXT)
        for k, box_, px, jog, xo in [("rq_rp", RP, MX - 60, MT - 20, 40), ("rq_ag", AG, MX, MT - 12, 0), ("rq_ga", GA, MX + 60, MT - 20, 0)]:
            x = box_[0] + box_[2] / 2 + xo
            self.wire(k, [(x, box_[1] + box_[3]), (x, jog), (px, jog), (px, MT - 2)], GREEN)
        self.step("rq_ag", MX - 16, MT - 32, GREEN)
        self.step("rq_rp", RP[0] + RP[2] / 2 + 56, RP[1] + RP[3] + 14, GREEN)
        self.step("rq_ga", GA[0] + GA[2] / 2 + 16, GA[1] + GA[3] + 14, GREEN)
        for pr, px in [("3", MX - 60), ("2", MX), ("1", MX + 60)]:
            self.f.text(px, MT + 17, pr, 11.5, "middle", "bold", fill=GREEN if on else FAINT_TEXT)
        self.f.lines(MX - 100, MT + 14, ["SRAM read queue: one request a cycle,", "fixed priority 1 > 2 > 3"], 11, anchor="end",
                     fill=GREEN if on else FAINT_TEXT, lh=14)

        # big SRAM, its queues and the returns
        self.f.rect(*SR, BOX, EDGE, 1.3, rx=8)
        self.f.text(SR[0] + 14, SR[1] + 22, "big SRAM", 13.5, "start", "bold")
        for label, sub, bx, bw, kind in self.buffers:
            c, fill = (GREEN, GREEN_L) if kind == "read" else (BLUE, BLUE_L)
            self.f.rect(bx, SR[1] + 34, bw, 50, fill, c, 1.4, rx=4, dash=None if kind == "read" else DASH["write"])
            self.f.text(bx + bw / 2, SR[1] + 55, label, 12, "middle", "bold")
            self.f.text(bx + bw / 2, SR[1] + 72, sub, 10.5, "middle", fill=MUTED, mono=True)
        self.wire("wq", [(WE[0] + 200, WE[1] + WE[3]), (WE[0] + 200, SR[1] + 32)], BLUE, dash=DASH["write"])
        self.label("wq", WE[0] + 208, SR[1] - 24, "SRAM write queue", BLUE)
        self.step("wq", WE[0] + 200, SR[1] - 40, BLUE)
        self.wire("rq", [(MX, MB), (MX, SR[1] + 32)], GREEN)
        self.label("rq", MX + 8, SR[1] - 20, "read requests", GREEN)
        self.step("rq", MX - 16, SR[1] - 20, GREEN)
        self.wire("ret", [(RX, SR[1] - 2), (RX, INJ[1] + INJ[3] + 2)], GREEN, w=SW + 0.4)
        self.f.lines(RX - 8, MT - 30, ["read returns", "become packets,", "in request order"], 11, anchor="end",
                     fill=GREEN if self.on("ret") else FAINT_TEXT, lh=14)
        self.step("ret", RX, MT + 40, GREEN)

    # ---- under the figure: legend and the lettered steps -------------------------------------------------------
    def legend(self, y):
        f = self.f
        f.rect(40, y - 12, 30, 18, BOX, INK, 1.6, rx=4)
        f.text(78, y + 2, "used by this example (in colour)", 11.5, "start", fill=MUTED)
        f.rect(290, y - 12, 30, 18, FAINT_FILL, FAINT_EDGE, 1, rx=4)
        f.text(328, y + 2, "not used", 11.5, "start", fill=MUTED)
        self.mark(400, y - 3, "1", PURPLE, filled=True)
        f.text(416, y + 2, "a register store: the program's line", 11.5, "start", fill=MUTED)
        self.mark(690, y - 3, "a", INK)
        f.text(706, y + 2, "a step of the data flow (below)", 11.5, "start", fill=MUTED)
        for i, (c, dash, s) in enumerate([(GREEN, None, "read"), (BLUE, DASH["write"], "write"), (PURPLE, None, "controller"),
                                          (ORANGE, DASH["ctrl"], "control")]):
            x = 930 + 86 * i
            f.line([(x, y - 3), (x + 30, y - 3)], c, SW, end=True, dash=dash)
            f.text(x + 34, y + 2, s, 11.5, "start", fill=MUTED)

    def step_list(self, y, items, cols=2, colw=610):
        """items: [(letter, colour, text)]; text wraps within the column."""
        per = (len(items) + cols - 1) // cols
        for c in range(cols):
            yy = y
            for letter, col, text in items[c * per:(c + 1) * per]:
                self.mark(52 + c * colw, yy - 4, letter, col)
                for j, s in enumerate(wrap(text, colw - 60, 12)):
                    self.f.text(70 + c * colw, yy + j * 16, s, 12, "start")
                yy += 16 * len(wrap(text, colw - 60, 12)) + 8
