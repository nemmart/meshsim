#!/usr/bin/env python3
"""proof_numbers.py LOG...: the figures of proof_run's runs (tests/proof_run_main.cpp), from its output, as a table.

    python3 tests/proof_numbers.py results/proof-29.log results/proof-matrix.log

A line a run: its name, N and s; the run's cycles from the reset; the link's bytes; the pages the page map marks; the
Updater's work summed over its runs (cycles busy, of them waiting on data, on pages and on memory; pages and half
pages read and written); the cycles the master's core was awake, and their share of the run. Markdown.

Then, for runs that print them, a second table. For WORKER7's forward count: the link's rate; the master's wakes, in
all and to the hand-over; its cycles awake to the hand-over, and their share of the hand-over's cycle (the owner's
target, the Updater at most 10% of the master tile's load, is this share: everything before the hand-over is the
frames); the share after the boot (from the first frame's header word, MR_FRAME_START's first, to the hand-over: the
cycles awake then, less those at the first frame, MR_AWAKE_FIRST_FRAME, over the cycles between);
Ingress's DMA transfers and the count's stores; the words forwarded and dropped; the cycle of the hand-over, of the
link's last byte, and the cycles the link waited for room. For WORKER4's threshold (its logs): the threshold, the
wakes, the cycles awake to the hand-over and a packet, and the link rate the firmware could keep up with. The rate is
"as taken" for an old run's --fast-link (gone at v1.37's stage 4: --link-rate 8 is the fastest). With --frames, a third: each run's wakes and Ingress's transfers by frame.
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
        m = re.match(r"the master: the tracer tile's load at its cycle (\d+)", line)
        if m:
            cur["handed"] = int(m[1])
            continue
        m = re.match(r"the threshold: (\d+); the link at (\d+) bytes a cycle; the master's wakes: (\d+), (\d+) to the hand-over; Ingress read out in (\d+) transfers, (\d+) to the hand-over; "
                     r"(\d+) threshold stores; the master awake (\d+) cycles to the hand-over; the link: its last byte at (\d+), (\d+) cycles waiting for room", line)
        if m:
            cur.update(threshold=int(m[1]), rate=int(m[2]), wakes=int(m[3]), wakes_handed=int(m[4]), reads=int(m[5]), reads_handed=int(m[6]), stores=int(m[7]),
                       awake_handed=int(m[8]), link_last=int(m[9]), room=int(m[10]))
            continue
        m = re.match(r"the forward count: the link at (\d+) bytes a cycle; the master's wakes: (\d+), (\d+) to the hand-over; Ingress's DMA: (\d+) transfers, (\d+) to the hand-over, "
                     r"(\d+) words; (\d+) count stores; (\d+) words forwarded, (\d+) dropped; the master awake (\d+) cycles to the hand-over at (\d+): [0-9.]+%; "
                     r"the link: its last byte at (\d+), (\d+) cycles waiting for room(?:; awake (\d+) cycles at the first frame, at its cycle (\d+))?", line)
        if m:
            cur.update(forward=True, rate=int(m[1]), wakes=int(m[2]), wakes_handed=int(m[3]), reads=int(m[4]), reads_handed=int(m[5]), dma_words=int(m[6]),
                       stores=int(m[7]), forwarded=int(m[8]), dropped=int(m[9]), awake_handed=int(m[10]), handed=int(m[11]), link_last=int(m[12]),
                       room=int(m[13]), awake_boot=int(m[14] or 0), first_frame=int(m[15] or 0))
            continue
        m = re.match(r"each frame, its type: cycles awake:(.*) \(the last to the hand-over\)", line)
        if m:
            cur["frame_awake"] = m[1].strip()
            continue
        m = re.match(r"each frame, its type: wakes/transfers:(.*) \(the last to the hand-over\)", line)
        if m:
            cur["frames"] = m[1].strip()
            continue
        m = re.match(r"(PASS|FAIL) proof_run", line)
        if m:
            cur["ok"] = cur["ok"] and m[1] == "PASS"
            yield cur
            cur = None

def main():
    frames = "--frames" in sys.argv
    rows = [r for p in sys.argv[1:] if p != "--frames" for r in runs(p)]
    print("| run | N | s | | cycles | link, bytes | pages | Updater: runs | busy | on data | on pages | on memory | read | written | master awake | share |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        u = r["upd"]
        print(f"| {r['name']} | {r['n']} | {r['s']} | {'pass' if r['ok'] else '**FAIL**'} | {r['cycles']:,} | {r.get('bytes', 0):,} | {r.get('pages', 0):,} | {r['runs']} | "
              f"{u[0]:,} | {u[1]:,} | {u[2]:,} | {u[3]:,} | {u[4]:,} | {u[5]:,} | {r.get('awake', 0):,} | {100.0 * r.get('awake', 0) / max(1, r['cycles']):.1f}% |")
    f = [r for r in rows if "forward" in r]
    if f:
        print("\n| run | N | s | link, bytes a cycle | wakes | to the hand-over | awake to the hand-over | hand-over at | share | share after the boot | DMA transfers | count stores | forwarded | dropped | link's last byte | link waited |")
        print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
        for r in f:
            after = (r["awake_handed"] - r["awake_boot"]) / max(1, r["handed"] - r["first_frame"]) if r["awake_boot"] else None
            print(f"| {r['name']} | {r['n']} | {r['s']} | {r['rate'] or 'as taken'} | {r['wakes']:,} | {r['wakes_handed']:,} | {r['awake_handed']:,} | {r['handed']:,} | "
                  f"{100.0 * r['awake_handed'] / max(1, r['handed']):.2f}% | {'' if after is None else f'{100.0 * after:.2f}%'} | {r['reads']:,} | {r['stores']} | "
                  f"{r['forwarded']:,} | {r['dropped']:,} | {r['link_last']:,} | {r['room']:,} |")
    t = [r for r in rows if "threshold" in r]
    if t:
        print("\n| run | N | s | threshold | link, bytes a cycle | packets | wakes | to the hand-over | awake to the hand-over | awake a packet | keeps up with, bytes a cycle | hand-over at | link's last byte | link waited |")
        print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
        for r in t:
            p = max(1, r.get("packets", 0))
            print(f"| {r['name']} | {r['n']} | {r['s']} | {r['threshold']} | {r['rate'] or 'as taken'} | {r.get('packets', 0):,} | {r['wakes']:,} | {r['wakes_handed']:,} | {r['awake_handed']:,} | "
                  f"{r.get('awake', 0) / p:.1f} | {r.get('bytes', 0) / max(1, r.get('awake', 0)):.2f} | {r.get('handed', 0):,} | {r['link_last']:,} | {r['room']:,} |")
    if frames and (t or f):
        print("\n| run | N | s | wakes/transfers by frame type, in order | cycles awake by frame type |")
        print("|---|---|---|---|---|")
        for r in t + f: print(f"| {r['name']} | {r['n']} | {r['s']} | {r.get('frames', '')} | {r.get('frame_awake', '')} |")
    print(f"\n{len(rows)} runs, {sum(1 for r in rows if r['ok'])} passed")

if __name__ == "__main__":
    main()
