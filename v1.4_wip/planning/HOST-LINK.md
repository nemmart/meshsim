# The link from the host: streams, packets and recovery

Notes from the discussion with the owner on 2026-11-16, as a first draft. This is the layer under the frames
(`HOST-FRAMES.md`): it is what lets a frame assume it was delivered correctly and in order. Ingress and Egress are
still to be designed; this is what was said about the link they sit on. A dagger (†) marks what is proposed and not
ruled on; (owner) marks what the owner said.

**A packet here is the link's:** a header word and its data. It is not a packet of the mesh. How the master tile
takes packets out of Ingress is `INGRESS-INTERFACE.md`.

## The link

- **8 LVDS data lines** (owner): one byte per transfer. Byte alignment comes from the wires, so the receiver only
  ever has to find where a packet starts.
- **About 1 GB a second.** A full packet is 16 bytes of header in 528, so the frames get 32/33 of it: gold measures
  3.13% of overhead on both workloads.
- **A 128-bit word is 16 transfers,** least significant byte first, as everywhere in the design.
- **Noise can last hundreds of milliseconds** (owner: a phone call in the server room). The link has to come back
  from that by itself.

## Two streams

| stream | number | what it carries | example |
|---|---|---|---|
| foreground | 1 | what the host sends for the proof the mesh is working on | "run a sumcheck" |
| background | 2 | the frames that set up the next proof, and start its tracing | "run an update" |

- **Decided (owner): Ingress has two streams.**
- **Neither may block the other.** The background stream can stall for a long time, while the Updater waits on LPDDR
  or on free pages, and the foreground stream has to keep getting through. So each has its own queue and its own flow
  control in Ingress.
- **They interleave packet by packet.** The longest a foreground packet waits is one background packet.
- **A stream's number is never 0** (owner), so that the first byte of a packet on the link is never zero (below).

## A packet

A header word of 128 bits, then 0 to 32 words of data.

| bits | field | meaning |
|---|---|---|
| [3:0] | stream | 1 foreground, 2 background; never 0 |
| [7:4] | flags | below |
| [15:8] | length | the data's length in 128-bit words, 0 to 32 |
| [35:16] | packet number | this packet's number on its stream: the first is 1, they run to 0xFFFFE, then 1 again |
| [55:36] | acknowledge | the last packet number received in order on this stream; 0 for none yet |
| [63:56] | reserved | 0 |
| [95:64] | data checksum | over the data |
| [127:96] | header CRC | over bits [95:0] of this word |

| flag | bit | meaning |
|---|---|---|
| resend | [4] | "your last packet was bad: send again from the number after my acknowledge field" |
| lost | [5] | "I have lost my place" (below) |
| recovery | [6] | the answer to "lost": the first packet after the zeros |
| stop | [7] | "stop sending me this stream"; 0 is "send" |

The same word byte by byte, as it goes on the link. Byte *i* holds bits [8*i* + 7 : 8*i*] and is the (*i* + 1)th
byte sent. The data follows, 16 bytes a word.

| byte | holds |
|---|---|
| 0 | the stream in its low four bits, the flags in its high four. A background packet with no flags is 0x02 |
| 1 | the length, 0 to 32 |
| 2, 3 | packet number bits 0 to 15, low byte first |
| 4 | low four bits: packet number bits 16 to 19; high four bits: acknowledge bits 0 to 3 |
| 5, 6 | acknowledge bits 4 to 19, low byte first |
| 7 | reserved, 0 |
| 8 to 11 | the data checksum, low byte first |
| 12 to 15 | the header CRC, low byte first |

- **Everything rides in 128 bits** (owner): the header is one word, and the data is whole words, so the link is
  word-aligned throughout.
- **The length is any number of words up to 32,** 16 to 512 bytes of data (owner). Any frame then cuts exactly into
  packets.
- **A frame ends at the end of a packet** (owner): the next frame never starts in the middle of one.
- **A length of 0 is a header with no data** (owner): a way to acknowledge with nothing else to send.
  - **It takes no packet number** (ruled 2026-11-16, on WORKER2's question): if it did, the other side would have to
    acknowledge it, and an acknowledgement that must itself be acknowledged never settles. Its number field holds
    the number the stream's next data packet will carry, so the field is still never 0. The receiver checks the
    field and does not advance.
- **Packet numbers are per stream** †, since the streams are flow-controlled apart.
- **No packet number is 0 or all ones** (owner), so a header word is never all zeros.
- **About 20 bits of packet number is enough** (owner), which left more bits for the checks. Only packets not yet
  acknowledged are outstanding, and that is a small window.
- **The first byte on the link is never zero** (owner): it holds the stream, which is never 0.

## The size of a packet

- **Decided (owner, 2026-11-16): packets are small.** At most 512 bytes of data, as above; perhaps 256.
- **Why:** a background packet delays a foreground packet that arrives behind it. The link is not one hop: there is
  the daisy chain of ASICs, and the FPGA, and each hop that forwards packets can hold a foreground packet behind
  one background packet (owner: "it's not just local cost").
- **What it costs the foreground:** on one hop, up to 0.53 µs a message with 512-byte packets. The owner's figure
  for a proof is perhaps 800 rounds; "2 ms probably covers it".
- **A sender may always cut smaller:** the format takes any length up to 32 words. At 256 bytes the wait a hop
  halves and the headers cost 6% of the link instead of 3%.

Considered and not taken:

| | what it is | why not |
|---|---|---|
| 1,024-byte packets | half as many packets for the firmware to wake for | twice the foreground's wait at every hop. The firmware's cost is met by the threshold instead (`INGRESS-INTERFACE.md`) |
| a second link for the foreground, one LVDS line beside the eight | no waiting behind bulk data at all | a line carries an eighth of the rate, so any message over about 75 bytes is slower than on the shared link at its worst; with no update running the shared link is eight times faster; and it costs pins, a second receiver, and finding byte boundaries on one line |

**Open:** the chain itself: how many ASICs are on it, and whether a hop forwards whole packets or passes bytes
straight through. That decides the real delay a hop.

## The two checks

1. **The data is checksummed first,** and the result goes into the header (owner).
2. **Then the header is checked,** data checksum included (owner). The header CRC is the word's last field.

- **A packet counts only when both pass.** Together they are a 64-bit check; through 300 ms of noise, some 20 million
  words, a false packet is out of the question.
- **The receiver can test the header as soon as it arrives,** before any data, and then knows which data checksum
  to expect.
- The packet number is checked as well, since the receiver compares it anyway to keep packets in order.

**Both checks are CRC-32C** (Castagnoli; the one in iSCSI, SCTP and ext4; RFC 3720, appendix B.4). † The choice of
CRC-32C is proposed; its definition here is exact, and gold's packetizer and its test vectors use it
(`updater_wip/pipeline/gold/test/packets/VECTORS.txt`).

| | |
|---|---|
| width | 32 bits |
| polynomial | 0x1EDC6F41; reflected, as the steps below use it, 0x82F63B78 |
| initial value | 0xFFFFFFFF |
| input and output | reflected: each byte enters least significant bit first |
| final XOR | 0xFFFFFFFF |
| of the nine ASCII bytes "123456789" | 0xE3069283 |
| of no bytes | 0x00000000 |
| of 16 zero bytes | 0x42709AEA |

```
crc = 0xFFFFFFFF
for each byte b, in order:
    crc = crc XOR b
    repeat 8 times:  crc = (crc >> 1) XOR 0x82F63B78   if crc's bit 0 is 1
                     crc =  crc >> 1                    otherwise
result = crc XOR 0xFFFFFFFF
```

- **The data checksum** is the CRC-32C of the packet's data, all 16 × length bytes, in the order they go on the
  link. For a packet of length 0 it is 0x00000000, and it is checked.
- **The header CRC** is the CRC-32C of header bytes 0 to 11, the data checksum included.
- **An example:** stream 2, no flags, length 1, number 1, acknowledge 0, the data one zero word. The header's bytes
  are `02 01 01 00 00 00 00 00 EA 9A 70 42 A0 8C 06 E1`.
- A cross-check for any implementation: the CRC-32C of all 16 header bytes of a good packet is 0x48674BC7.

**The order a receiver tests in:**

1. the header CRC;
2. the header's fields: the stream is 1 or 2, the length is at most 32, the reserved bits are 0;
3. the data checksum;
4. the packet number, which means something only once the rest has passed.

## When a packet is bad

| what went wrong | still in step? | what the receiver does |
|---|---|---|
| the header is good, the data checksum fails | yes: the length could be trusted | sets **resend** on its next packet back. The link does not stop (owner) |
| the header's CRC fails | no: the packet's length is unknown | the recovery below |
| both checks pass but a field cannot be: a stream that does not exist, a length over 32, a reserved bit set | yes | † this is the sender's fault, not noise, and sending again would not help: an error of its own (`ERRORS.format_error` in `INGRESS-INTERFACE.md`) |

- **Idle is zero bytes, in any number,** not only whole words: the first byte that is not zero starts a header.
- **"The first byte is never zero" is how a receiver finds a packet's start, not a check it can make.** A first
  byte that noise turns to zero is taken for idle; the receiver then fails a header CRC one byte later, which is
  the "lost my place" case.

## Recovery

**The side that has lost its place** (owner):

1. finishes the packet it is sending;
2. sends 1,024 bytes of zeros;
3. sends a packet with **lost** set, its acknowledge field holding the last packet number it received correctly;
4. **repeats 2 and 3, over and over, until a good recovery packet arrives** (owner).

**The other side** (owner):

1. sends 1,024 bytes of zeros;
2. sends its **recovery** packet, and carries on from the packet after the acknowledged one;
3. † repeats these until it sees an ordinary acknowledgement.

Why the zeros work:

- **1,024 bytes is longer than the longest packet,** 528 bytes with its header. Whatever length a confused receiver
  believes it is in the middle of, it has run out before the zeros end.
- **A zero word is never a header,** so between packets it is plainly idle.
- **The first byte that is not zero starts a header,** since a header's first byte is never zero.

Nothing in the exchange is relied on to arrive, because the noise can cover it: that is why both sides repeat.

## Holding the host back: stop sending and resume sending

- **Decided (owner): the link has "stop sending" and "resume sending".**
- **They are for each stream.** Stopping the background stream, when the Updater stalls, leaves the foreground stream
  running. The owner can't imagine the foreground stream ever needing to stop, but it has the bit all the same.
- **† They are one bit, not two messages:** the `stop` flag in every header a side sends on a stream says what it
  wants now, 1 for "stop sending me this stream" and 0 for "send". Resuming is the bit going back to 0.
  - A "resume" sent as a message could be lost in noise, and the stream would hang. A bit that is in every header
    is put right by the next packet that gets through.
- **The queue behind a stream keeps some room when it says stop:** the packets already on the wire still arrive.
- This replaces the per-stream window first proposed here, which did the same job with more machinery.

## What a sender must keep

- A sender keeps every packet until it is acknowledged, to send it again.
- **The host has the memory for that.**
- **The chip does not.** On the way back to the host it keeps a small window of packets, and stops when the
  acknowledgements stop.

## What this is, in other designs

The owner's remark: there must be a mountain of literature on building these. There is. The closest:

| design | what matches |
|---|---|
| PCI Express, the data link layer | nearly all of it: a sequence number and a CRC on every packet, ACK and NAK, a replay buffer at the sender, and credits for flow control kept apart for each virtual channel. The two streams are two virtual channels. Its NAK is the "resend" flag |
| Interlaken | several channels on one chip-to-chip link, with flow control for each in band |
| HDLC and what came from it | the sliding window: go-back-N and selective repeat |
| ATM and GFP | finding where packets start by hunting for a header whose CRC checks |
| Xilinx Aurora | a light link protocol for FPGAs, if the host's side is one |

- **† The replay buffer is taken from PCI Express,** not invented here. Its flow control is by credits; this link's
  is the simpler stop bit.
- One difference: PCI Express never loses its place, because its physical layer marks packet starts with symbols of
  their own. This link marks nothing, which is why it has the zeros and "lost".

## Open

- What the foreground stream carries, and how large its messages are.
- Whether the recovery packet states the packet number it restarts from, or that is the "lost" packet's number plus one.
- How much room a stream's queue keeps when it says stop, and how large the queues are.
- The timers: how soon an unacknowledged packet is sent again without a "resend".
- How large the chip's own window of kept packets is.
