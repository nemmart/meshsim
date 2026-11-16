# WORKER2 questions

`HL` is `v1.4_wip/planning/HOST-LINK.md`, `II` is `INGRESS-INTERFACE.md`, both as on main `543e1eda`. They are here
because WORKER3 writes Ingress from the same documents without my code, and where the documents are silent the two
of us can differ.

**All four were ruled by the owner on 2026-11-16;** each ruling is under its question, and the code and the vectors
follow them.

## Q1. How many words a packet holds: II and HL differ

- HL, "A packet": "A header word of 128 bits, then **0 to 32** words of data"; "A length of 0 is a header with no
  data (owner)".
- II, "Terms": a packet is "a header word, then **1 to 32** words of data". II's "Open" then asks what Ingress does
  with a packet of length 0 († it asks for no service).

Taken: HL's. The reader accepts a length of 0 and skips it, as the brief says. Suggested: II's "Terms" says 0 to 32.

**Ruled:** 0 to 32, as HL says.

## Q2. Does a packet of length 0 take a number in its stream's sequence?

- HL: "No packet number is 0 or all ones (owner), so a header word is never all zeros." So a packet of length 0
  carries a number from 1 to 0xFFFFE.
- HL does not say whether that number is the next in the sequence, or whether the receiver's "last packet number
  received in order" moves past it.

Taken (C4): it is a packet like any other; it takes the next number, and the next packet with data has the number
after it. The other reading: it is outside the numbering, as PCI Express's acknowledgements are, and its number field
means something else or repeats the last. The packetizer writes no such packet, so only the reader and two test
vectors depend on it. But an Ingress written the other way would refuse those two vectors with `sequence_error`.

**Ruled: no.** A packet of length 0 does not take a number and does not advance the numbering. Its packet number
field holds the number the next data packet on that stream will carry; the reader checks that it equals that, and
does not advance. Built so: `Packets.Writer.empty`, `PacketFile`'s `number` check, and eight vectors (four good:
when 7 is next, first in a file, when 0xFFFFE is next, on stream 1; four bad: holding the last packet's number,
holding the one after the next, number 0, a data checksum that is not 0), with three such packets in
`small-idle.packets` (two of them together) and one in `two-streams.packets`.

## Q3. "The first byte is not zero" and idle zeros

- The brief's list of the reader's checks starts with "the first byte is not zero".
- The brief also takes zeros between packets as idle (†): "a zero byte where a header's first byte is expected is
  skipped, in any number".

With the second, the first cannot fail: a header whose first byte has been turned to zero is not a bad header but
one more idle byte. The packet is still refused, one byte later: the receiver takes the length byte as a header's
first byte, and either its stream is not 1 or 2, or the 16 bytes from there fail the header CRC (all but one time in
2³²).

Taken (C2): zeros are skipped, and the damage test for the first byte expects `stream` or `header-crc` at the next
byte. `packets-read --no-idle` is the other reading, where a zero is refused as `first-byte`. Is the first what is
wanted of Ingress as well?

**Ruled: as taken.** One correction to what I wrote above, found by the tests: the refusal one byte later is always
`header-crc`, never `stream`, because the header CRC is tested before the stream is looked at. Ten damage lines of
`VECTORS.txt` are this case.

## Q4. A packet with a bad data checksum and a wrong number: which error?

II has two sticky bits, `crc_error` (a header CRC or a data checksum failed) and `sequence_error` (the number was not
the next). A packet can be wrong in both ways, and the documents do not give the order of the checks.

Taken (C9): the header CRC, then the stream, the length and the reserved bits, then the data checksum, then the
number. HL: "A packet counts only when both pass", so a packet whose data is bad has not used its number, and the
error is `crc_error`. The vectors file has no packet wrong in two ways, so that WORKER3's tests do not depend on
this before it is ruled.

**Ruled: as taken.**
