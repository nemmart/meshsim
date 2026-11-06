#!/usr/bin/env python3
"""pair.py DIR_4K DIR_8K: Stage B's log waits window by window, 4 KB against 8 KB (WORKER20; V3-12, spec item 143).
Tests the start-up explanation: if the log waits are the pass's start (the first refill run sends the code stream, then the
read log, then the write log, one packet a cycle), a window's 8 KB write-log wait exceeds its 4 KB one by at most the extra
128 read-log entries sent ahead of the write log, and no wait exceeds the first run's reach (code + read log + write log)."""
import csv, re, sys, collections
def load(d):
    out = {}
    for r in csv.reader(open(d + "/results.csv")):
        if len(r) < 9 or r[4] != "OK" or not r[3].startswith("B"): continue
        m = re.search(r"waits image/code/newexp/exp/rlog/wlog/outq ([0-9/]+) logq (\d+)", r[8])
        if m: out[int(r[1])] = ([int(v) for v in m.group(1).split("/")], int(r[6]))
    return out
a, b = load(sys.argv[1]), load(sys.argv[2])
common = sorted(set(a) & set(b))
dr = collections.Counter(); dw = collections.Counter(); dc = collections.Counter()
for w in common:
    dr[b[w][0][4] - a[w][0][4]] += 1; dw[b[w][0][5] - a[w][0][5]] += 1; dc[b[w][1] - a[w][1]] += 1
n = len(common)
print(f"{n} windows in both; 4 KB: max read wait {max(a[w][0][4] for w in common)}, max write wait {max(a[w][0][5] for w in common)}; "
      f"8 KB: max read {max(b[w][0][4] for w in common)}, max write {max(b[w][0][5] for w in common)}")
print("read-log wait, 8 KB minus 4 KB: most common", dr.most_common(5))
print("write-log wait, 8 KB minus 4 KB: most common", dw.most_common(6), f"; in [0, 128]: {sum(c for k, c in dw.items() if 0 <= k <= 128)} of {n}")
print("pass cycles, 8 KB minus 4 KB: most common", dc.most_common(6), f"; 4 KB faster or equal in {sum(c for k, c in dc.items() if k >= 0)} of {n}")
