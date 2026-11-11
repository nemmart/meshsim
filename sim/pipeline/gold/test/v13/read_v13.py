#!/usr/bin/env python3
"""Reads a v1.3 segment file (DECISIONS-v1.3 section 16) and checks it against independent outputs.

    read_v13.py <v1.3 file> [--v12=<v1.2 file of the same run>] [--jumpmap=<jumpmap out dir of the same run>]
        [--layout=<upstream_rows .json>]

Frames: 7 (u32 version, u32 n, n u64: log2 K, lowest_address, a16z's MemoryLayout), 1 (u32 per segment: 4 KB pages
written), 6 (u32 k, p1, p3, 2^k lines of 128 B), then per segment 2 (68 x 128-bit record), 3, 4 or 5.
Checks: frame order; frame 6 equals jumpmap's frame6.bin; frame 1 equals jumpmap's seg/NNN.json pages_written; each
frame 2 equals the v1.2 file's 131-word state (pc, instr_idx, offset, registers), its advice field frame 3's count,
word 66 frame 7's log2 K [71:66] and the I/O region's size 0x8000_0000 - lowest_address [102:72] (spec 4.21.2, 66f399e); frames 3-5 equal the v1.2 file's; frame 7's layout equals
upstream_rows's dump of a16z's MemoryLayout.
"""
import json, os, re, struct, sys

FIELDS = ["program_size", "max_trusted_advice_size", "trusted_advice_start", "trusted_advice_end",
          "max_untrusted_advice_size", "untrusted_advice_start", "untrusted_advice_end", "max_input_size",
          "max_output_size", "input_start", "input_end", "output_start", "output_end", "stack_size", "stack_end",
          "heap_size", "heap_end", "panic", "termination", "io_end"]

def frames(b):
    out, i = [], 0
    while i < len(b):
        n, t = struct.unpack_from("<II", b, i)
        out.append((t, b[i + 8:i + 4 * n]))
        i += 4 * n
    return out

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--"))
    fr = frames(open(args[0], "rb").read())
    types = [t for t, _ in fr]
    assert types[:3] == [7, 1, 6], f"order {types[:3]}"
    ver, n = struct.unpack_from("<II", fr[0][1])
    f7 = struct.unpack_from(f"<{n}Q", fr[0][1], 8)
    log2k, lowest = f7[0], f7[1]
    nseg = len(fr[1][1]) // 4
    pages = struct.unpack(f"<{nseg}I", fr[1][1])
    k, p1, p3 = struct.unpack_from("<III", fr[2][1])
    segs = fr[3:]
    assert len(segs) == 3 * nseg and all(segs[3 * s][0] == 2 and segs[3 * s + 1][0] == 3 for s in range(nseg))
    print(f"frame 7: version {ver}, {n} fields, log2 K {log2k}, lowest_address {lowest:#x}; frame 1: {nseg} segments; "
          f"frame 6: k {k}, p1 {p1:#x}, p3 {p3}")
    ok = True
    if "jumpmap" in opts:
        jm = open(os.path.join(opts["jumpmap"], "frame6.bin"), "rb").read()
        same = jm[8:] == fr[2][1]
        print(f"  frame 6 vs jumpmap's frame6.bin: {'identical' if same else 'DIFFERS'}")
        ok &= same
        pj = json.load(open(os.path.join(opts["jumpmap"], "proof.json")))
        same = pj["log2_k"] == log2k and int(pj["lowest_address"], 16) == lowest
        print(f"  frame 7's log2 K and lowest_address vs jumpmap's proof.json ({pj['log2_k']}, {pj['lowest_address']}): "
              f"{'equal' if same else 'DIFFER'}")
        ok &= same
        jp = [json.load(open(os.path.join(opts["jumpmap"], "seg", f"{s:03d}.json")))["pages_written"] for s in range(nseg)]
        same = list(pages) == jp
        print(f"  frame 1 vs jumpmap's pages_written per segment: {'equal' if same else 'DIFFER'} {list(pages)[:8]}")
        ok &= same
    if "v12" in opts:
        old = frames(open(opts["v12"], "rb").read())[1:]
        bad = 0
        for s in range(nseg):
            st = struct.unpack("<131Q", old[3 * s][1])
            rec = struct.unpack("<136Q", segs[3 * s][1])
            adv = len(segs[3 * s + 1][1]) // 8
            want = [st[0], (st[1] & 0xffffffff) | (st[2] & 0xffff) << 32, 0, adv << 38] + list(st[3:131]) + \
                   [0, log2k << 2 | (0x80000000 - lowest) << 8, 0, 0]
            if list(rec) != want or segs[3 * s + 1][1] != old[3 * s + 1][1] or segs[3 * s + 2] != old[3 * s + 2]:
                bad += 1
        print(f"  frames 2-5 vs the v1.2 file: {nseg - bad} of {nseg} segments agree")
        ok &= bad == 0
    if "layout" in opts:
        dump = json.load(open(opts["layout"]))["upstream_layout"]
        vals = {m.group(1): int(m.group(2), 0) for m in re.finditer(r"(\w+): (0x[0-9A-Fa-f]+|\d+)", dump)}
        diff = [f for i, f in enumerate(FIELDS) if vals.get(f) != f7[2 + i]]
        print(f"  frame 7's MemoryLayout vs a16z's (upstream_rows): {'equal' if not diff else 'DIFFER: ' + str(diff)}")
        ok &= not diff
    print("OK" if ok else "MISMATCH")
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
