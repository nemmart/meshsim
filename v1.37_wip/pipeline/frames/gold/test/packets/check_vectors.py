#!/usr/bin/env python3
"""The packet test vectors checked by a third implementation (WORKER2).

    check_vectors.py <directory holding VECTORS.txt and the small packet files>

Nothing here comes from gold's Java: the CRC-32C is the bit-by-bit algorithm written in VECTORS.txt's own heading,
and the reader below is written from HOST-LINK.md and that heading. It checks:
  - every "crc32c" line;
  - every "packet": the fields against the bytes (the header rebuilt from the fields, and decoded again), both stored
    checks and both computed ones, the 128-bit header word, the data lines; then what a reader says of the packet
    alone ("expect"), and the number expected after it;
  - every "file": its size and SHA-256, its counts, that it reads without a refusal, and that its frames are
    small.frames byte for byte;
  - every "damage" line: the file with that byte changed is refused by that check, at that packet and byte.
"""
import hashlib
import sys
from pathlib import Path


def crc32c(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x82F63B78 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


class Refused(Exception):
    def __init__(self, check, packet, at):
        super().__init__(f"{check} at packet {packet}, byte {at}")
        self.check, self.packet, self.at = check, packet, at


def unmask(data, header_crc):
    """Takes chapter 28.6's mask off a link packet's data: a CRC-32C register from header_crc | 0x10000, fed zeros."""
    reg, out = header_crc | 0x10000, bytearray()
    for d in data:
        for _ in range(8):
            reg = (reg >> 1) ^ 0x82F63B78 if reg & 1 else reg >> 1
        out.append(d ^ (reg & 0xFF))
    return bytes(out)


RESTART_WORDS = bytes([2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53,
                       59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131])   # 28.9, copied


def read(f, first=1, frames=True):
    """Reads a packet file of chapter 28's link packets. Returns its counts and the frames' bytes; raises Refused."""
    expect = first                                 # one run of numbers for the link, both streams (28.4)
    c = dict(packets=0, s2=0, s1=0, empty=0, idle=0, words2=0, words1=0, restarts=0, frames=0, after=None)
    out = bytearray()
    left = 0
    pos = 0
    while True:
        while pos < len(f) and f[pos] == 0:        # idle
            pos += 1
            c["idle"] += 1
        if pos == len(f):
            break
        k = c["packets"] + 1
        if len(f) - pos < 16:
            raise Refused("truncated", k, pos)
        word = int.from_bytes(f[pos:pos + 16], "little")
        if word >> 96 != crc32c(f[pos:pos + 12]):
            raise Refused("header-crc", k, pos)
        stream = word & 15
        length = (word >> 8) & 0xFF
        number = (word >> 16) & 0x3FFFF            # [33:16]; acknowledge [51:34] and hops [55:52] are not tested
        if stream not in (1, 2):
            raise Refused("stream", k, pos)
        if length > 32:
            raise Refused("length", k, pos)
        if (word >> 58) & 0x3F:                    # [63:58]; [57:56] are the stop bits, not tested
            raise Refused("reserved", k, pos)
        data = f[pos + 16:pos + 16 + 16 * length]
        if len(data) < 16 * length:
            raise Refused("truncated", k, pos)
        data = unmask(data, word >> 96)
        if (word >> 64) & 0xFFFFFFFF != crc32c(data):
            raise Refused("data-checksum", k, pos)
        if word & 0x80:                            # restart (28.9): the restart link packet, or refused
            if (stream, length, number, (word >> 52) & 15) != (1, 32, 1, 0) or data != RESTART_WORDS * 16 or left:
                raise Refused("restart", k, pos)
            c["packets"] += 1
            c["s1"] += 1
            c["restarts"] += 1
            expect = 1                             # it takes no number; the numbers start again
            c["after"] = expect
            pos += 16 + 16 * length
            continue
        if number != expect:                       # 0 and 0x3FFFF are never expected
            raise Refused("number", k, pos)
        c["packets"] += 1
        c["s2" if stream == 2 else "s1"] += 1
        if length == 0:
            c["empty"] += 1                        # it takes no number
        else:
            expect = 1 if number == 0x3FFFE else number + 1
            if stream == 1:
                c["words1"] += length
            else:
                c["words2"] += length
                if frames:
                    if left == 0:
                        left = 1 + int.from_bytes(data[12:16], "little")
                        c["frames"] += 1
                    if length > left:
                        raise Refused("frame-end", k, pos)
                    left -= length
                    out += data
        c["after"] = expect
        pos += 16 + 16 * length
    if left:
        raise Refused("truncated", c["packets"], len(f))
    return c, bytes(out)


def said(f, **kw):
    try:
        return "ok", read(f, **kw)
    except Refused as r:
        return r.check, r


def main(directory):
    d = Path(directory)
    failures = []

    def expect(ok, what):
        if not ok:
            failures.append(what)

    blocks, crcs, damage = [], [], []
    block = None
    for raw in (d / "VECTORS.txt").read_text().splitlines():
        line = raw.split("#")[0].split()
        if not line:
            continue
        if line[0] == "crc32c":
            crcs.append((b"" if line[1] == "-" else bytes.fromhex(line[1]), int(line[2], 16)))
        elif line[0] == "damage":
            damage.append((line[1], int(line[3]), int(line[5], 16), line[7], int(line[9]), int(line[11])))
        elif line[0] in ("packet", "file"):
            block = {"kind": line[0], "name": line[1], "data": [], "bytes": []}
        elif line[0] == "end":
            blocks.append(block)
            block = None
        elif line[0] in ("data", "bytes") and block["kind"] == "packet":
            block[line[0]].append(bytes.fromhex(line[1]))
        else:
            block[line[0]] = line[1]

    for data, want in crcs:
        expect(crc32c(data) == want, f"crc32c of {data.hex() or 'no bytes'}")

    packets = [b for b in blocks if b["kind"] == "packet"]
    for v in packets:
        name = "packet " + v["name"]
        num = lambda key: int(v[key], 0)
        data, all_bytes = b"".join(v["data"]), b"".join(v["bytes"])
        header = all_bytes[:16]
        # the header rebuilt from the fields
        low = (num("stream") | num("flags") << 4 | num("length") << 8 | num("number") << 16 | num("acknowledge") << 34
               | num("hops") << 52 | num("stops") << 56 | num("reserved") << 58 | num("data-checksum") << 64)
        word = low | num("header-crc") << 96
        expect(word.to_bytes(16, "little") == header, name + ": the fields make the header bytes")
        expect(low.to_bytes(12, "little") == bytes.fromhex(v["header-0-11"]), name + ": header-0-11")
        expect(word == num("header-word"), name + ": header-word")
        expect(unmask(all_bytes[16:], num("header-crc")) == data, name + ": the data lines are the bytes after the header, unmasked")
        expect(crc32c(data) == num("data-crc32c"), name + ": data-crc32c")
        expect(crc32c(header[:12]) == num("header-crc32c"), name + ": header-crc32c")
        good = v["expect"] == "ok"
        if good:
            expect(num("data-checksum") == num("data-crc32c") and num("header-crc") == num("header-crc32c"),
                   name + ": a good packet's stored checks are the computed ones")
            expect(crc32c(header) == 0x48674BC7, name + ": the CRC-32C of a good header's 16 bytes")
            expect(len(data) == 16 * num("length"), name + ": the data is length words")
        # what a reader says of it alone
        nxt = num("next-number")
        got, result = said(all_bytes, first=nxt, frames=False)
        expect(got == v["expect"], f"{name}: expect {v['expect']}, this reader says {got}")
        if good and got == "ok":
            expect(result[0]["after"] == num("next-number-after"), name + ": next-number-after")
            expect(num("next-number-after") == (1 if num("flags") & 8 else nxt if num("length") == 0 else 1 if nxt == 0x3FFFE
                                                 else nxt + 1),
                   name + ": a packet with data takes a number, one of length 0 does not")

    # machine-spec 28.5's four example headers and 28.6's masked data lines, copied from v1.37_wip/spec/host-link.md
    host_link = {"host-link-example": ("02 01 01 00 00 00 00 00 EA 9A 70 42 A0 8C 06 E1",
                                       "2A 54 C0 74 C0 1C 58 2F A6 5D 27 37 39 C9 47 1F"),
                 "host-link-example-one-hop": ("02 01 01 00 00 00 10 00 EA 9A 70 42 9E BC 0A 21", None),
                 "host-link-example-hops-3": ("11 01 CD AB 56 55 35 02 EA 9A 70 42 D2 BF BA 0A", None),
                 "host-link-example-length-0": ("01 00 05 00 10 00 00 01 00 00 00 00 57 09 4D 4E", ""),
                 "host-link-mask-example": ("01 01 07 00 0C 00 00 00 EB 08 C9 D9 9E 94 2A 36",
                                            "A4 D0 6E 3C 10 A4 2F 89 F5 74 21 9E DE 43 E4 C0"),
                 "host-link-mask-example-forwarded": ("01 01 07 00 10 00 10 00 EB 08 C9 D9 A3 2B 76 11",
                                                      "79 F1 F2 82 77 D9 B4 AF 91 8F 7C 00 39 96 A1 51")}
    restart = next((b for b in packets if b["name"] == "restart-link-packet"), None)
    expect(restart is not None and int(restart["data-checksum"], 0) == 0xC8981D21 and crc32c(RESTART_WORDS * 16) == 0xC8981D21,
           "28.9's restart link packet: its data checksum 0xC8981D21, copied")
    for name, (head, line) in host_link.items():
        v = next((b for b in packets if b["name"] == name), None)
        b = b"".join(v["bytes"]) if v is not None else b""
        expect(b[:16] == bytes.fromhex(head), f"28.5's example {name}: the header's bytes")
        if line is not None:
            expect(b[16:] == bytes.fromhex(line), f"28.6's example {name}: the data on the line")

    frames = (d / "small.frames").read_bytes()
    files = {}
    for v in [b for b in blocks if b["kind"] == "file"]:
        name = v["name"]
        f = (d / name).read_bytes()
        files[name] = (f, int(v["first-number"], 0))
        expect(len(f) == int(v["bytes"]) and hashlib.sha256(f).hexdigest() == v["sha256"], name + ": its size and SHA-256")
        got, result = said(f, first=int(v["first-number"], 0))
        expect(got == v["expect"], f"{name}: expect {v['expect']}, this reader says {got}")
        if got == "ok":
            c, out = result
            expect(out == frames, name + ": its frames are small.frames")
            for key, field in (("packets", "packets"), ("packets-stream-2", "s2"), ("packets-stream-1", "s1"),
                               ("packets-length-0", "empty"), ("idle-bytes", "idle"), ("words-stream-2", "words2"),
                               ("words-stream-1", "words1"), ("restart-packets", "restarts"), ("frames", "frames")):
                expect(c[field] == int(v[key]), f"{name}: {key} {v[key]}, this reader counts {c[field]}")

    for name, at, mask, check, packet, where in damage:
        f, first = files[name]
        g = bytearray(f)
        g[at] ^= mask
        got, r = said(bytes(g), first=first)
        expect(got == check and (r.packet, r.at) == (packet, where),
               f"damage {name} byte {at} xor {mask:#04x}: expect {check} at packet {packet}, byte {where}; this reader says {r}")

    for what in failures[:40]:
        print("FAIL", what)
    print(f"vectors by the Python reader: {len(crcs)} CRC-32C values, {len(packets)} single packets, {len(files)} whole files, "
          f"{len(damage)} damaged files: " + ("ok" if not failures else f"{len(failures)} FAILED"))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
