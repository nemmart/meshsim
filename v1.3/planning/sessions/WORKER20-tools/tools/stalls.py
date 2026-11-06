#!/usr/bin/env python3
"""stalls.py RESULTS_DIR...: per-window stall averages from run_shard.sh's results.csv (Stage B rows, and F), for WORKER20's
4 KB-versus-8 KB comparison (V3-12, spec item 143). Prints one line per directory and stage."""
import csv, re, sys
N = ["image", "code", "newexp", "exp", "rlog", "wlog", "outq"]
for d in sys.argv[1:]:
    per = {}
    for r in csv.reader(open(d + "/results.csv")):
        if len(r) < 9 or r[4] != "OK": continue
        st = r[3].split(":")[0]
        m = re.search(r"waits image/code/newexp/exp/rlog/wlog/outq ([0-9/]+) logq (\d+)", r[8])
        if not m: continue
        w = [int(v) for v in m.group(1).split("/")]
        per.setdefault((st, m.group(2)), []).append((w, int(r[6]), int(r[5])))
    for (st, q), rows in sorted(per.items()):
        k = len(rows); cyc = sum(c for _, c, _ in rows); n = sum(x for _, _, x in rows)
        avg = [sum(w[i] for w, _, _ in rows) / k for i in range(7)]; mx = [max(w[i] for w, _, _ in rows) for i in range(7)]
        logs = sum(w[4] + w[5] for w, _, _ in rows)
        print(f"{d} stage {st} logq {q} ({int(q) * 16 // 1024} KB): {k} windows, {cyc / n:.4f} cycles/row; per window: "
              + ", ".join(f"{N[i]} {avg[i]:.1f} (max {mx[i]})" for i in range(7))
              + f"; log waits {logs / cyc * 100:.3f}% of pass cycles; windows with read/write waits {sum(1 for w,_,_ in rows if w[4])}/{sum(1 for w,_,_ in rows if w[5])}")
