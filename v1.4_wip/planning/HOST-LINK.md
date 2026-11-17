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
| [33:16] | packet number | 18 bits (owner, 2026-11-17). This packet's number on its stream: the first is 1, they run to 0x3FFFE, then 1 again |
| [51:34] | acknowledge | 18 bits. The last packet number received in order on this stream; 0 for none yet |
| [55:52] | hops | the ASICs that have forwarded this packet along the chain (owner, 2026-11-17; below). Whoever makes a packet sends 0 |
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
| 4 | low two bits: packet number bits 16 and 17; high six bits: acknowledge bits 0 to 5 |
| 5 | acknowledge bits 6 to 13 |
| 6 | low four bits: acknowledge bits 14 to 17; high four bits: hops |
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
- **18 bits of packet number is enough** (owner; 20 until 2026-11-17, when four bits went to the hops field).
  Only packets not yet acknowledged are outstanding, and that is a small window. A stream's numbers come round
  again after 262,142 packets, about 138 MB.
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
| 1,024-byte packets | half as many packets for the firmware to wake for | twice the foreground's wait at every hop. The firmware's cost is met instead by taking it out of the data's path: Ingress forwards the bulk data to the Updater itself (`INGRESS-INTERFACE.md`) |
| a second link for the foreground, one LVDS line beside the eight | no waiting behind bulk data at all | a line carries an eighth of the rate, so any message over about 75 bytes is slower than on the shared link at its worst; with no update running the shared link is eight times faster; and it costs pins, a second receiver, and finding byte boundaries on one line |

## The chain, and the hops field (owner, 2026-11-17)

The ASICs sit on a daisy chain from the FPGA: **probably no more than 4 on one chain** (owner). A packet from the
host passes through the ASICs before its own; a packet to the host passes through the ASICs between its sender and
the host.

**Every link of the chain is a link of its own** (owner): the FPGA to the first ASIC, the first to the second, and
so on. Everything in this document but the hops field belongs to one link.

| | belongs to |
|---|---|
| the packet number, the acknowledge field, the flags | the one link the header is travelling on (owner: "packet numbers don't mean anything except on the specific link") |
| sending again, "lost" and recovery, "stop" | the one link: a garbled packet is sent again only over the link that garbled it (owner) |
| a packet of length 0 | the one link: it is never forwarded |
| hops | the packet, end to end |
| the data and the data checksum | the packet, end to end |

**An ASIC that forwards a packet, in either direction:**

1. **takes the whole packet in and checks it,** as any receiver does. A bad packet is the incoming link's
   business: it is asked for again there, and is not forwarded;
2. **writes the header for the outgoing link:** that link's next packet number on the stream, its acknowledge
   field and its flags;
3. **adds one to hops;**
4. **computes the header CRC again** (owner: "always recompute the header CRC").

The data and its checksum pass through unchanged.

- **Whoever makes a packet sends it with hops 0:** the host, or an ASIC.
- **Towards the host, hops says which ASIC made the packet:** its place in the chain. It counts hops, but what it
  is for is this (owner): the FPGA sorts what arrives by it and so rebuilds each ASIC's replies. No ASIC has to
  know its own place.
  - The owner's example: the FPGA sends a query to all the ASICs. Each runs it and writes its reply's packets out,
    and forwards any that come in from further down the chain.
  - An ASIC's packets stay in order all the way, because every link delivers in order. Packets of different ASICs
    interleave.
- **From the host, every ASIC gets the same data.** Each ASIC takes a packet for itself and forwards it on; the
  last has nobody to forward to. The field is ignored (owner): it is not tested, and no value of it is an error.
  It happens to be the receiving ASIC's own place in the chain.
- **On an outgoing link an ASIC's own packets and the ones it forwards share one run of packet numbers** on each
  stream.
- **The chain comes before an ASIC's own data** (owner). Towards the host, what an ASIC sends next is the first of
  these that is waiting:
  1. a foreground packet to forward;
  2. a foreground packet of its own;
  3. a background packet to forward;
  4. a background packet of its own.
  - The stream decides first; within a stream the chain wins. An ASIC fills the gaps with its own data (owner).
  - It keeps the forwarding short, so a chain does not back up behind an ASIC with much to say. The ASIC nearest
    the host waits longest, behind every ASIC further down.
  - Away from the host there is nothing to order: an ASIC makes no packets of its own in that direction.
- **An ASIC keeps what it has sent until its neighbour acknowledges it,** in both directions (owner): towards the
  host its own packets and the ones it forwards, and away from the host the host's packets it passes down.
- **"Stop" is for each link** (owner). An ASIC whose background queue fills stops its neighbour up the chain. That
  neighbour's forwarding backs up, and it in turn stops the one above it. So a chain's background stream moves at
  the pace of its slowest ASIC. The foreground stream has its own bit on every link and still gets through.
- **Why the hops field is under the header CRC:** one flipped bit in it would credit a packet to the wrong ASIC.
  The header CRC is computed again at each hop anyway, since the link's fields change.
- † Four bits: a chain of up to 16.
- **A hop holds a packet for its length,** since it checks the whole of it first. That is what "The size of a
  packet" assumes.

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
- **An example:** stream 2, no flags, length 1, number 1, acknowledge 0, hops 0, the data one zero word. The
  header's bytes are `02 01 01 00 00 00 00 00 EA 9A 70 42 A0 8C 06 E1`.
  - The same packet after one hop, hops 1: `02 01 01 00 00 00 10 00 EA 9A 70 42 9E BC 0A 21`.
  - With number 0x2ABCD, acknowledge 0x15555 and hops 3: `02 01 CD AB 56 55 35 00 EA 9A 70 42 C9 C4 23 30`.
- A cross-check for any implementation: the CRC-32C of all 16 header bytes of a good packet is 0x48674BC7.

**The order a receiver tests in:**

1. the header CRC;
2. the header's fields: the stream is 1 or 2, the length is at most 32, the reserved bits are 0. The hops field
   is not tested;
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
- **They are for each link** (owner, 2026-11-17): "stop" goes to the neighbour, not to the host ("The chain").
- **The queue behind a stream keeps room when it says stop:** the packets already on that link's wire still arrive.
  Only the one link counts, since the neighbour stops within a packet or two of seeing the bit.
- **Ingress's queues are pencilled in at 8 KB on each stream** (owner, 2026-11-16; `INGRESS-INTERFACE.md`), 16
  full packets, behind a buffer that holds the one packet being checked. The size is to be settled (Open).
  - The owner's sizes that day were 16 KB, "to make sure we can stop the remote side", which is what v1.37 has as
    merged; then 4 KB with the forward count; then 8 KB as a placeholder, the size being arbitrary and changing
    little in the design.
- † A stream says stop when its queue is half full, which leaves 4 KB, 8 full packets, for what is on its way.
- This replaces the per-stream window first proposed here, which did the same job with more machinery.

## What a sender must keep

- A sender keeps every packet until it is acknowledged, to send it again.
- **The host has the memory for that.**
- **The chip does not.** It keeps a small window of packets on each link it sends on, and stops when the
  acknowledgements stop.
- **An ASIC sends on two links:** towards the host, its own packets and the ones it forwards; away from the host,
  the host's packets it passes down the chain. It keeps a window for each (owner, 2026-11-17).

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
- The size of Ingress's queues, 8 KB pencilled in. With "stop" for each link, the room to keep when a stream says
  stop is only what one link has in flight, a packet or two, so the size is no longer set by the chain's length.
- The timers: how soon an unacknowledged packet is sent again without a "resend".
- How large the chip's own window of kept packets is.
