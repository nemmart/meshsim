#!/usr/bin/env python3
"""proof_numbers.py LOG...: the figures of proof_run's runs (tests/proof_run_main.cpp), from its output, as a table.

    python3 tests/proof_numbers.py results/proof-29.log results/proof-matrix.log

A line a run: its name, N and s; the run's cycles from the reset; the link's bytes; the pages the page map marks; the
Updater's work summed over its runs (cycles busy, of them waiting on data, on pages and on memory; pages and half
pages read and written); the cycles the master's core was awake, and their share of the run. Markdown.
"""
import re, sys

def runs(path):
    cur = None
    for line in open(path, errors="replace"):
        m = re.match(r"(.*), N (\d+), s (\d+): (passed|FAILED) in (\d+) cycles", line)
        if m:
            cur = {"name": m[1].replace(".frames", ""), "n": int(m[2]), "s": int(m[3]), "ok": m[4] == "passed", "cycles": int(m[5]), "upd": [0] * 6, "runs": 0}
            continue
        if cur is None: continue
        m = re.match(r"(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$", line)
        if m:
            cur["runs"] += 1
            for i in range(6): cur["upd"][i] += int(m[3 + i])
            continue
        m = re.match(r"the link: (\d+) packets, (\d+) words, (\d+) bytes; the page map: (\d+) pages marked; the master's core: (\d+) cycles awake", line)
        if m:
            cur.update(packets=int(m[1]), words=int(m[2]), bytes=int(m[3]), pages=int(m[4]), awake=int(m[5]))
            continue
        m = re.match(r"(PASS|FAIL) proof_run", line)
        if m:
            cur["ok"] = cur["ok"] and m[1] == "PASS"
            yield cur
            cur = None

def main():
    rows = [r for p in sys.argv[1:] for r in runs(p)]
    print("| run | N | s | | cycles | link, bytes | pages | Updater: runs | busy | on data | on pages | on memory | read | written | master awake | share |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        u = r["upd"]
        print(f"| {r['name']} | {r['n']} | {r['s']} | {'pass' if r['ok'] else '**FAIL**'} | {r['cycles']:,} | {r.get('bytes', 0):,} | {r.get('pages', 0):,} | {r['runs']} | "
              f"{u[0]:,} | {u[1]:,} | {u[2]:,} | {u[3]:,} | {u[4]:,} | {u[5]:,} | {r.get('awake', 0):,} | {100.0 * r.get('awake', 0) / max(1, r['cycles']):.1f}% |")
    print(f"\n{len(rows)} runs, {sum(1 for r in rows if r['ok'])} passed")

if __name__ == "__main__":
    main()
