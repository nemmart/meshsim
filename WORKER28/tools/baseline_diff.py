#!/usr/bin/env python3
"""baseline_diff.py OLD NEW [T2_OLD T2_NEW] -- WORKER28: every number the latency commit moved, as Markdown.

OLD, NEW: results directories of WORKER22's regression (110v at latency 1, 116-w28 at 8): suites/, p4/, smoke-*/.
T2_OLD, T2_NEW: results of make_skip_task.sh (115 at latency 1, 116b at 8): t2-*-true.csv, sb-*-true.csv.
Wall times are masked; a suite line is reported when it differs after masking."""
import csv, glob, os, re, statistics, sys

def mask(line):
    line = re.sub(r"\d+(\.\d+)? ?(s|ms)\b", "#s", line)
    return re.sub(r"\d+ rows per JVM-second", "# rows per JVM-second", line)

def suites(old, new):
    out = ["## Suites (run_quick.sh, run_full.sh)", ""]
    same, changed = [], []
    for f in sorted(glob.glob(os.path.join(old, "suites", "*.log"))):
        n = os.path.basename(f); g = os.path.join(new, "suites", n)
        if not os.path.exists(g): changed.append((n, ["(no log at 8)"])); continue
        a = [mask(l.rstrip("\n")) for l in open(f, errors="replace")]
        b = [mask(l.rstrip("\n")) for l in open(g, errors="replace")]
        if a == b: same.append(n[:-4]); continue
        import difflib
        d = [l for l in difflib.unified_diff(a, b, lineterm="", n=0) if l[:1] in "+-" and l[:3] not in ("+++", "---")]
        changed.append((n[:-4], d))
    out.append(f"**Unchanged ({len(same)}):** " + ", ".join(same) + ".")
    out.append("")
    out.append(f"**Changed ({len(changed)}):** each line at latency 1 (−) and at 8 (+), wall times masked.")
    out.append("")
    for n, d in changed:
        out.append(f"### {n}")
        out.append("")
        out.append("```")
        out.extend(x[:220] for x in d[:40])
        if len(d) > 40: out.append(f"... {len(d) - 40} more lines")
        out.append("```")
        out.append("")
    return out

def p4(old, new):
    def total(d):
        m = re.findall(r"TOTAL ([\d,]+) cycles", open(os.path.join(d, "run.log"), errors="replace").read())
        return int(m[-1].replace(",", "")) if m else None
    a, b = total(old), total(new)
    s = f"{a:,} → {b:,} cycles ({b - a:+,}, {100.0 * (b - a) / a:+.2f}%)" if a and b else f"{a} → {b}"
    return ["## The full-size sort, p4/uniform-k128 (L1 GO to L2 all READY)", "", s, ""]

def windows(path, stage_col, cyc_col, keyf):
    r = {}
    for row in csv.reader(open(path)):
        if len(row) <= cyc_col or not row[cyc_col].isdigit(): continue
        r[keyf(row)] = int(row[cyc_col])
    return r

def smoke(old, new):
    out = ["## The 64-window Stage B smoke (stages A and B, F every 16th): cycles per window", "",
           "| program | stage | windows | mean at 1 | mean at 8 | change, mean | min / max change |", "|---|---|---|---|---|---|---|"]
    for prog in ("evm-zone-29", "cyclone-29"):
        a = windows(os.path.join(old, f"smoke-{prog}", "results.csv"), 3, 6, lambda r: (r[1], r[3]))
        b = windows(os.path.join(new, f"smoke-{prog}", "results.csv"), 3, 6, lambda r: (r[1], r[3]))
        for st in ("A", "B", "F"):
            ks = sorted(k for k in a if k in b and k[1].startswith(st + ":"))
            if not ks: continue
            d = [b[k] - a[k] for k in ks]
            out.append(f"| {prog} | {st} | {len(ks)} | {statistics.mean(a[k] for k in ks):,.0f} | {statistics.mean(b[k] for k in ks):,.0f} | {statistics.mean(d):+,.1f} | {min(d):+,} / {max(d):+,} |")
    return out + [""]

def t2(old, new):
    out = ["## T2, segment 0, all 64 windows (stage 1 on)", "",
           "| program | column | at 1 | at 8 | change |", "|---|---|---|---|---|"]
    for prog in ("evm-zone-29", "cyclone-29"):
        A = list(csv.DictReader(open(os.path.join(old, f"t2-{prog}-true.csv"))))
        B = list(csv.DictReader(open(os.path.join(new, f"t2-{prog}-true.csv"))))
        if not A or not B: continue
        for c in A[0]:
            if c in ("segment", "windows", "wall_s", "ok"): continue
            try: x, y = int(A[0][c]), int(B[0][c])
            except ValueError: continue
            out.append(f"| {prog} | {c} | {x:,} | {y:,} | {y - x:+,} |")
    out += ["", "## Stage B/F, windows 0-63 on every window (stage 1 on): cycles per window", "",
            "| program | stage | windows | mean at 1 | mean at 8 | change, mean | min / max change |", "|---|---|---|---|---|---|---|"]
    for prog in ("evm-zone-29", "cyclone-29"):
        a = windows(os.path.join(old, f"sb-{prog}-true.csv"), 3, 6, lambda r: (r[1], r[3]))
        b = windows(os.path.join(new, f"sb-{prog}-true.csv"), 3, 6, lambda r: (r[1], r[3]))
        for st in ("B", "F"):
            ks = sorted(k for k in a if k in b and k[1].startswith(st + ":"))
            if not ks: continue
            d = [b[k] - a[k] for k in ks]
            out.append(f"| {prog} | {st} | {len(ks)} | {statistics.mean(a[k] for k in ks):,.0f} | {statistics.mean(b[k] for k in ks):,.0f} | {statistics.mean(d):+,.1f} | {min(d):+,} / {max(d):+,} |")
    return out + [""]

if __name__ == "__main__":
    o, n = sys.argv[1], sys.argv[2]
    lines = p4(o, n) + smoke(o, n)
    if len(sys.argv) > 4: lines += t2(sys.argv[3], sys.argv[4])
    lines += suites(o, n)
    print("\n".join(lines))
