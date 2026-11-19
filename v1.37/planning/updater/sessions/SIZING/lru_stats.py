#!/usr/bin/env python3
"""lru_stats.py: the LRU code mix of the frames of update words in the given .frames files (29.2, 29.3).

For each LRU-coded frame (compression 1): the codes decoded as the Updater's decoder decodes them (17.10.2), counted
by kind (hit, 34-bit miss, 66-bit miss), the hit indexes, and the bits a code. Raw frames (compression 0) are counted
for their words only. Used by count.py for the firmware's cost a code. Usage: lru_stats.py FILE.frames ...
"""
import struct, sys, json

def frames(buf):
    off = 0
    while off + 16 <= len(buf):
        lo, hi = struct.unpack_from('<QQ', buf, off)
        comp, typ, length = lo & 0xFF, hi & 0xFFFFFFFF, hi >> 32
        data = buf[off + 16: off + 16 + 16 * length]
        yield comp, typ, length, data
        off += 16 + 16 * length

def decode_lru(data):
    bits = int.from_bytes(data, 'little'); nb = 8 * len(data); pos = 0
    table = [0] * 32; out = []
    hits = []; m34 = m66 = 0
    while True:
        if pos + 1 > nb: break
        if bits >> pos & 1:
            if pos + 6 > nb: break
            i = bits >> (pos + 1) & 31; v = table.pop(i); table.insert(0, v); pos += 6; hits.append(i)
        else:
            if pos + 2 > nb: break
            if bits >> (pos + 1) & 1:
                if pos + 66 > nb: break
                v = bits >> (pos + 2) & (2**64 - 1); pos += 66; m66 += 1
            else:
                if pos + 34 > nb: break
                v = bits >> (pos + 2) & (2**32 - 1); pos += 34; m34 += 1
            table.insert(0, v); table.pop()
        out.append(v)
    return out, hits, m34, m66, pos

def words_of(out):
    """The update words, cut at the end the frame's runs give (padding decodes as codes past the last run)."""
    k = 0; n = 0
    while k < len(out):
        hdr = out[k]; cnt = hdr >> 32; k += 1 + cnt
        if k > len(out): break
        n = k
    return n

def main(paths):
    tot = dict(frames_lru=0, frames_raw=0, codes=0, hits=0, m34=0, m66=0, bits=0, raw_words=0)
    hist = [0] * 32
    for p in paths:
        buf = open(p, 'rb').read()
        for comp, typ, length, data in frames(buf):
            if typ not in (2, 5, 6, 9): continue
            if comp == 0:
                tot['frames_raw'] += 1; tot['raw_words'] += 2 * length; continue
            out, hits, m34, m66, pos = decode_lru(data)
            n = words_of(out)
            # count only the codes of the frame's words (the padding's codes, at most 127 bits, are dropped)
            h = m = 0; c34 = c66 = 0; bits_used = 0
            # recount over the first n codes
            bits = int.from_bytes(data, 'little'); q = 0
            for _ in range(n):
                if bits >> q & 1: hist[bits >> (q + 1) & 31] += 1; h += 1; q += 6
                elif bits >> (q + 1) & 1: c66 += 1; q += 66
                else: c34 += 1; q += 34
            tot['frames_lru'] += 1; tot['codes'] += n; tot['hits'] += h; tot['m34'] += c34; tot['m66'] += c66; tot['bits'] += q
    c = max(tot['codes'], 1)
    tot['hit_share'] = tot['hits'] / c
    tot['bits_a_code'] = tot['bits'] / c
    tot['mean_hit_index'] = sum(i * n for i, n in enumerate(hist)) / max(tot['hits'], 1)
    tot['hit_index_hist'] = hist
    return tot

if __name__ == '__main__':
    print(json.dumps(main(sys.argv[1:]), indent=1))
