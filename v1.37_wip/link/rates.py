#!/usr/bin/env python3
"""rates.py: how often damage passes a CRC-32C unseen (QUESTIONS.md, F1; owner, 2026-11-17: report the rate of a
false packet number beside the rate of damaged data passing its 32-bit checksum).

A pattern of flipped bits passes a CRC unseen when the XOR of its bits' syndromes is 0. This counts, exactly, the
patterns of each small weight that pass:
  the header   160 bits (20 bytes, its CRC among them): every pattern; those that leave the stream, the flags, the
               length and the data checksum alone (so that the packet's data still passes); and of those, the ones
               that change the foreground number, or an acknowledge field
  the data     of a full packet, 4,096 bits, the checksum in the header untouched: weights 2 to 4 exactly
Above the weights counted, one pattern in 2^32 passes, which the header's exact counts bear out.
Then, for a bit error rate p inside a burst, the chance for one packet.      usage: rates.py [most weight, default 8]
"""
import itertools, math, sys
import numpy as np

POLY = 0x82F63B78

def syndromes(nbits):
    """syn[i]: what flipping bit i (link order: byte by byte, least significant bit first) does to the register after
    all nbits have gone through. The CRC is linear, so a pattern's effect is the XOR of its bits'."""
    syn = np.zeros(nbits, dtype=np.uint32)
    v = POLY                                   # the last bit: it enters, and the register is clocked once
    for k in range(nbits):                     # k more bits follow: k more clocks
        syn[nbits - 1 - k] = v
        v = (v >> 1) ^ (POLY if v & 1 else 0)
    return syn

def check():
    """The syndromes against the CRC itself: two bits flipped in 20 bytes."""
    def crc(bs):
        reg = 0xFFFFFFFF
        for b in bs:
            reg ^= b
            for _ in range(8): reg = (reg >> 1) ^ (POLY if reg & 1 else 0)
        return reg
    syn = syndromes(160)
    base = bytes(range(1, 21))
    for i, j in ((0, 159), (17, 93), (40, 41), (128, 5)):
        hit = bytearray(base); hit[i // 8] ^= 1 << (i % 8); hit[j // 8] ^= 1 << (j % 8)
        assert crc(hit) ^ crc(base) == int(syn[i]) ^ int(syn[j]), (i, j)

def count_zero(syn, positions, maxw, musts=()):
    """For each size up to maxw: the subsets of `positions` whose syndromes XOR to 0; and, for each set in `musts`,
    those of them that hold a position of it. Meet in the middle: a subset is two halves with the same XOR."""
    pos = np.array(sorted(positions)); s = syn[pos]; n = len(pos)
    marks = [np.isin(pos, np.array(sorted(m))) for m in musts]
    tables = {}
    def subsets(k):
        if k not in tables:
            if k <= 3: idx = np.array(list(itertools.combinations(range(n), k)), dtype=np.int16).reshape(-1, k)
            else:                              # size 4: two indices in Python, the other two in numpy
                a, b = np.triu_indices(n, 1); parts = []
                for i in range(n):
                    for j in range(i + 1, n):
                        m = a > j
                        parts.append(np.column_stack([np.full(int(m.sum()), i, np.int16), np.full(int(m.sum()), j, np.int16), a[m].astype(np.int16), b[m].astype(np.int16)]))
                idx = np.concatenate(parts)
            x = np.zeros(len(idx), np.uint32)
            for c in range(k): x ^= s[idx[:, c]]
            order = np.argsort(x, kind="stable")
            tables[k] = (x, idx, order, x[order])
        return tables[k]
    out = {}
    for w in range(2, maxw + 1):
        k1, k2 = w // 2, w - w // 2
        x1, i1, _, _ = subsets(k1); _, i2, order, x2s = subsets(k2)
        lo = np.searchsorted(x2s, x1, "left"); hi = np.searchsorted(x2s, x1, "right")
        counts = [0] * (1 + len(musts))
        for r in np.nonzero(hi > lo)[0]:
            a = set(i1[r].tolist())
            for q in order[lo[r]:hi[r]]:
                b = set(i2[q].tolist())
                if a & b: continue
                counts[0] += 1
                for m, mark in enumerate(marks):
                    if any(mark[t] for t in a | b): counts[1 + m] += 1
        out[w] = [c // math.comb(w, k1) for c in counts]   # each subset is found once for every way of halving it
    return out

def tail(n, p, k):                             # the chance of k or more errors in n bits (summed upward: no cancelling)
    term = lambda w: math.exp(math.lgamma(n + 1) - math.lgamma(w + 1) - math.lgamma(n - w + 1) + w * math.log(p) + (n - w) * math.log1p(-p))
    return sum(term(w) for w in range(k, n + 1))

def main():
    check()
    maxw = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    hdr = syndromes(160)
    keep = set(range(16, 96)) | set(range(128, 160))      # numbers, acknowledge fields, hops, stop and resend; the CRC
    fg_number = set(range(16, 34)); acks = set(range(34, 52)) | set(range(70, 88))
    every = count_zero(hdr, range(160), maxw)
    kept = count_zero(hdr, keep, maxw, (fg_number, acks))
    print("the header, 160 bits: patterns of flipped bits that pass its CRC unseen")
    print("  weight   any pattern   patterns / 2^32   the data's check untouched   ...the foreground number changed   ...an acknowledge field changed")
    for w in range(2, maxw + 1):
        print(f"  {w:6d} {every[w][0]:13,d} {math.comb(160, w) / 2**32:17.2f} {kept[w][0]:28,d} {kept[w][1]:34,d} {kept[w][2]:33,d}")
    passing = [w for w in every if every[w][0]]
    print(f"  the fewest flipped bits that pass: {min(passing) if passing else 'more than ' + str(maxw)}")
    data = syndromes(4096)
    small = count_zero(data, range(4096), 3)
    a, b = np.triu_indices(4096, 1)
    x = data[a] ^ data[b]; order = np.argsort(x); xs = x[order]
    w4 = sum(1 for i in np.nonzero(xs[1:] == xs[:-1])[0] if len({a[order[i]], b[order[i]], a[order[i + 1]], b[order[i + 1]]}) == 4) // 3
    print("\nthe data of a full packet, 4,096 bits, its checksum untouched: patterns that pass unseen")
    print(f"  weight 2: {small[2][0]}; weight 3: {small[3][0]}; weight 4: {w4}  (exact)")
    print("  CRC-32C is known to catch every pattern of up to 5 bits in up to 5,243 bits; from 6 bits, one pattern in 2^32 passes:")
    print("  " + "; ".join(f"weight {w}: {math.comb(4096, w) / 2**32:.3g}" for w in (6, 7, 8)))
    print("\nthe chance for one full packet, at a bit error rate p inside a burst:")
    print("        p   damaged data passes   a false foreground number passes   a false acknowledgement passes   data / number")
    for p in (1e-5, 1e-4, 1e-3, 1e-2, 1e-1):
        pd = tail(4096, p, 6) / 2**32
        heavy = (1 - p)**48 * tail(112, p, maxw + 1) / 2**32      # more bits than counted, all in the fields that may change
        pn = sum(kept[w][1] * p**w * (1 - p)**(160 - w) for w in kept) + heavy * 18 / 112
        pa = sum(kept[w][2] * p**w * (1 - p)**(160 - w) for w in kept) + heavy * 36 / 112
        print(f"  {p:7.0e} {pd:21.3g} {pn:34.3g} {pa:32.3g} {pd / pn if pn else float('inf'):15.3g}")
    print("  (a false number does harm only if it is the very number the receiver expects, and a false acknowledgement")
    print("   only if it covers a packet that was lost: both are rarer still than these columns)")

main()
