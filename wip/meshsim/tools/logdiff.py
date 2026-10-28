#!/usr/bin/env python3
"""logdiff.py OLD NEW: OLD (an M10 log) with the rename's mapping applied, against NEW (M11's). Prints the remaining
differences; exit 0 if none. The mapping is rename.py's rules and protections, plus the hand edits of the review."""
import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
import rename
RULES = rename.SRAM + rename.HIST + rename.GROUP + rename.STREAM + rename.ABBREV + rename.CLEAN
HAND = [  # wording fixed by hand after the mechanical pass (logged in DEVIATIONS)
    ("area under two streams of one request", "area under two queues of one request"),
    ("cycles after the streams start", "cycles after the queues start"),
    ("ptr cnt rdPort", "ptr hist rdPort"), ("peak_cntReq", "peak_histReq"),
    ("e5f21e47b21d76d8297efb81c9560c95ce812eddb4febeb950d6fc7a8ab43f08", "a89fcfc68c5a717a5d457f3f89774d8a2e6c1b82aa2d4b4542c0f4c135255af5"),  # sort.sym: SLOT_OF -> STREAM_OF
    ("PROPOSAL-stream-iteration-counter", "PROPOSAL-slot-iteration-counter"),
]
def mapped(line):
    for a, b in HAND[:5]: line = line.replace(a, b)
    line = rename.apply(line, RULES, rename.PROTECT + [r"counter stores? (?=clear|start)", r"survives counter stores"], {})[0]
    for a, b in HAND[5:]: line = line.replace(a, b)
    return line
a = [mapped(l) for l in open(sys.argv[1], encoding="utf-8", errors="replace").read().split("\n")]
b = open(sys.argv[2], encoding="utf-8", errors="replace").read().split("\n")
import difflib
ws = lambda x: re.sub(r"\s+", " ", x).strip()
pad = sum(1 for x, y in zip(a, b) if x != y and ws(x) == ws(y)) if len(a) == len(b) else 0
if pad: print("(%d lines differ in padding only: a renamed label inside a fixed-width column)" % pad)
d = [l for l in difflib.unified_diff([ws(x) for x in a], [ws(x) for x in b], "M10 (mapped)", "M11", n=0, lineterm="") if not l.startswith("@@")]
for l in d[:int(os.environ.get("MAXL", "40"))]: print(l)
sys.exit(1 if d else 0)
