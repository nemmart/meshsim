#!/usr/bin/env python3
"""Recomputes every worked example of the specification's chapter 28 from the definitions alone (SPEC session)."""
POLY = 0x82F63B78
def step(reg, byte):
    reg ^= byte
    for _ in range(8):
        reg = (reg >> 1) ^ POLY if reg & 1 else reg >> 1
    return reg
def crc(bs, start=0xFFFFFFFF):
    r = start
    for b in bs: r = step(r, b)
    return r ^ 0xFFFFFFFF
def header(stream, flags, length, number, ack, hops, fgstop, bgstop, dsum):
    low = (stream & 15) | (flags & 15) << 4 | length << 8 | (number & 0x3FFFF) << 16 | (ack & 0x3FFFF) << 34 \
        | (hops & 15) << 52 | fgstop << 56 | bgstop << 57
    b = bytearray(low.to_bytes(8, 'little')) + dsum.to_bytes(4, 'little')
    return bytes(b + crc(b).to_bytes(4, 'little'))
def mask(hdr, data):
    reg = int.from_bytes(hdr[12:16], 'little') | 0x10000
    out = bytearray()
    for d in data:
        reg = step(reg, 0)
        out.append(d ^ (reg & 0xFF))
    return bytes(out)
hx = lambda b: ' '.join('%02X' % x for x in b)
RESEND, LOST, RECOVERY, RESTART = 1, 2, 4, 8
print('CRC-32C("123456789") = 0x%08X' % crc(b'123456789'))
print('CRC-32C(no bytes)    = 0x%08X' % crc(b''))
z = bytes(16)
print('CRC-32C(16 zeros)    = 0x%08X' % crc(z))
e1 = header(2, 0, 1, 1, 0, 0, 0, 0, crc(z)); print('1:', hx(e1))
e2 = header(2, 0, 1, 1, 0, 1, 0, 0, crc(z)); print('2:', hx(e2))
e3 = header(1, RESEND, 1, 0x2ABCD, 0x15555, 3, 0, 1, crc(z)); print('3:', hx(e3))
e4 = header(1, 0, 0, 5, 4, 0, 1, 0, 0); print('4:', hx(e4))
for e in (e1, e2, e3, e4):
    r = 0xFFFFFFFF
    for b in e: r = step(r, b)
    print('  residue over 16 bytes 0x%08X' % (r ^ 0xFFFFFFFF))
# the fast unit's start: the value that 4 zero bytes take to all ones
for s in range(1):
    pass
# solve: find S with step^4(S, 0) == 0xFFFFFFFF; the step is invertible
def unstep0(reg):
    for _ in range(8):
        # reverse one shift: reg = (r>>1) ^ (POLY if r&1)
        if reg & 0x80000000: r = ((reg ^ POLY) << 1) | 1
        else: r = reg << 1
        reg = r & 0xFFFFFFFF
    return reg
s = 0xFFFFFFFF
for _ in range(4): s = unstep0(s)
print('fast unit start 0x%08X' % s)
r = s
for b in bytes(4) + e1[:12]: r = step(r, b)
print('  check on example 1: 0x%08X vs header CRC 0x%08X' % (r ^ 0xFFFFFFFF, int.from_bytes(e1[12:], 'little')))
print('mask 1:', hx(mask(e1, z)), ' start 0x%08X' % (int.from_bytes(e1[12:16], 'little') | 0x10000))
d = bytes(range(16))
m2 = header(1, 0, 1, 7, 3, 0, 0, 0, crc(d)); print('mask ex 2 header:', hx(m2)); print('  wire:', hx(mask(m2, d)))
m3 = header(1, 0, 1, 7, 4, 1, 0, 0, crc(d)); print('mask ex 3 header:', hx(m3)); print('  wire:', hx(mask(m3, d)))
primes = [2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97,101,103,107,109,113,127,131]
print('restart data checksum 0x%08X' % crc(bytes(primes * 16)))
# the register's still states
print('still: step(0)=0x%08X step(0xFCA42DAF)=0x%08X' % (step(0, 0), step(0xFCA42DAF, 0)))
