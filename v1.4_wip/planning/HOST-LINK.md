# The link from the host: streams, packets and recovery

Notes from the discussion with the owner on 2026-11-16, as a first draft. This is the layer under the frames
(`HOST-FRAMES.md`): it is what lets a frame assume it was delivered correctly and in order. Ingress and Egress are
still to be designed; this is what was said about the link they sit on. A dagger (†) marks what is proposed and not
ruled on; (owner) marks what the owner said.

**A packet here is the link's:** a header and its data. It is not a packet of the mesh. How the master tile
takes packets out of Ingress is `INGRESS-INTERFACE.md`.

**Where this document stands (2026-11-17).** WORKER8 built one link from it and searched it exhaustively: **as
first written the protocol does not hold.** A single damaged packet can stop a link for ever, in several ways
(`DESIGN-NOTES-2026-11-16.md`, section 24; `updater_wip/planning/sessions/WORKER8/QUESTIONS.md` on branch
`updater-w8`). Its rules are accepted to carry on with and go in here once its random campaign and the chain have
confirmed them. Two things have changed here already: the queues are 16 KB, and **the header is 20 bytes and
speaks for both streams** ("A packet"). Until the rest is folded in, do not build from the sections on a bad
packet, recovery and the status alone.

## The link

- **8 LVDS data lines** (owner): one byte per transfer. Byte alignment comes from the wires, so the receiver only
  ever has to find where a packet starts.
- **About 1 GB a second.** A full packet is 20 bytes of header in 532, so the frames get 96.2% of it. (With the
  16-byte header of before, gold measured 3.13% of overhead on both workloads; now about 3.9%.)
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

**A header of 20 bytes, then 0 to 32 words of data** (owner, 2026-11-17). It was one 128-bit word; it grew by four
bytes so that **every header says where both streams stand.**

- **Why:** a header used to speak for its own stream only. With one stream filling a direction, the other
  stream's acknowledgements and stop bit had no packet to ride on, and that stream stood still on a wire with no
  damage at all (WORKER8: 102,447 cycles). Sending the other stream's status as packets of its own would have cost
  a busy link about 6%.
- **What it costs:** 20 bytes in 532, 3.8%, flat. The owner: "it's header info that gets stripped anyway."
- **The header is no longer one word.** The data after it is whole 128-bit words, as before, and the link is a
  byte at a time, so nothing physical minds. This sets aside the earlier rule that everything rides in 128 bits.

| bits | field | meaning |
|---|---|---|
| [3:0] | stream | whose data this packet carries: 1 foreground, 2 background; never 0 |
| [4] | lost | "I have lost my place" (below) |
| [5] | recovery | the answer to "lost": the first packet after the zeros |
| [6] | restart | proposed by WORKER8, accepted to carry on with: with lost, "I have started again"; with recovery, "so have I" |
| [7] | reserved | 0 |
| [15:8] | length | the data's length in 128-bit words, 0 to 32 |
| [33:16] | foreground number | 18 bits. In a foreground packet with data: this packet's number. In any other packet: the number the foreground's next packet with data will carry |
| [51:34] | foreground acknowledge | 18 bits. The last foreground packet number received in order; 0 for none yet |
| [69:52] | background number | as the foreground's |
| [87:70] | background acknowledge | as the foreground's |
| [91:88] | hops | the ASICs that have forwarded this packet along the chain (below). Whoever makes a packet sends 0 |
| [92] | foreground stop | "stop sending me the foreground stream"; 0 is "send" |
| [93] | foreground resend | "your last foreground packet was bad: send again from the number after my acknowledge field" |
| [94] | background stop | as the foreground's |
| [95] | background resend | as the foreground's |
| [127:96] | data checksum | over the data |
| [159:128] | header CRC | over bits [127:0] |

- **A stream's fields are in the same place in every header,** whichever stream the packet's data belongs to.
- **Lost, recovery and restart are the link's,** not a stream's: one packet says them for both.

The same header byte by byte, as it goes on the link. Byte *i* holds bits [8*i* + 7 : 8*i*] and is the (*i* + 1)th
byte sent. The data follows, 16 bytes a word.

| byte | holds |
|---|---|
| 0 | the stream in its low four bits; lost, recovery and restart in bits 4, 5 and 6; bit 7 is 0. A background packet with none of them is 0x02 |
| 1 | the length, 0 to 32 |
| 2, 3 | foreground number bits 0 to 15, low byte first |
| 4 | low two bits: foreground number bits 16 and 17; high six bits: foreground acknowledge bits 0 to 5 |
| 5 | foreground acknowledge bits 6 to 13 |
| 6 | low four bits: foreground acknowledge bits 14 to 17; high four bits: background number bits 0 to 3 |
| 7 | background number bits 4 to 11 |
| 8 | low six bits: background number bits 12 to 17; high two bits: background acknowledge bits 0 and 1 |
| 9, 10 | background acknowledge bits 2 to 17, low byte first |
| 11 | low four bits: hops; then foreground stop, foreground resend, background stop, background resend in bits 4 to 7 |
| 12 to 15 | the data checksum, low byte first |
| 16 to 19 | the header CRC, low byte first |

- **The length is any number of words up to 32,** 16 to 512 bytes of data (owner). Any frame then cuts exactly into
  packets.
- **A frame ends at the end of a packet** (owner): the next frame never starts in the middle of one.
- **A length of 0 is a header with no data** (owner): a way to say where the link stands with nothing else to send.
  - **It takes no packet number** (ruled 2026-11-16, on WORKER2's question): if it did, the other side would have to
    acknowledge it, and an acknowledgement that must itself be acknowledged never settles. Both number fields
    hold the numbers the streams' next packets with data will carry, so they are still never 0. Its stream field
    is 1 or 2, so that the first byte is not zero; it means nothing.
- **Packet numbers are for each link, and on a link for each stream** (owner, 2026-11-17), since the streams are
  flow-controlled apart. A packet that is forwarded gets a new number on the next link. The first is 1, they run
  to 0x3FFFE, then 1 again.
- **No packet number is 0 or all ones** (owner).
- **18 bits of packet number is enough** (owner; 20 until 2026-11-17). Only packets not yet acknowledged are
  outstanding, and that is a small window. A stream's numbers come round again after 262,142 packets, about 138 MB.
- **The first byte on the link is never zero** (owner): it holds the stream, which is never 0.
- **A stream's next number in every header** is how a receiver learns that the last packet of a burst was lost,
  with no timer (WORKER8): it sees a number ahead of the one it expects.

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

**What is left of a forwarded packet's header** (owner: "the header gets mostly stripped"):

| field | on the outgoing link |
|---|---|
| packet number, acknowledge, flags | dropped, and written afresh: they were the incoming link's |
| hops | kept, plus one |
| stream, length | kept: they describe the packet |
| data checksum | kept, unchanged |
| header CRC | computed again |

- **Why the data checksum is kept:** the data is then covered from the ASIC that made the packet to the host, the
  time it waits in each forwarding buffer included. Were each hop to compute it again, a bit that flipped in a
  buffer would be sealed in as good. And the sending unit has only 16 bytes to do for a forwarded packet.

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
- **Towards the host, data is on the foreground stream only** (owner). The background stream appears there only
  as packets of length 0, carrying its acknowledgements and its stop bit, and those never leave their link.
- **The chain comes before an ASIC's own data** (owner). Towards the host, an ASIC sends a packet it is forwarding
  before one of its own, and fills the gaps with its own (owner).
  - (Were the background stream ever to carry data towards the host: the stream decides first, and within a
    stream the chain wins.)
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
- **Four bits: a chain of at most 16** (owner: the absolute maximum, which is why the field is that wide; 4 are
  expected). A packet that arrives with hops 15 at an ASIC that would forward it is a bad packet (below).
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

**Both checks are CRC-32C** (Castagnoli; the one in iSCSI, SCTP and ext4; RFC 3720, appendix B.4). The choice is
settled (owner, 2026-11-17: "we can just pick one"); its definition here is exact, and gold's packetizer and its test vectors use it
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
- **The header CRC** is the CRC-32C of header bytes 0 to 15, the data checksum included.
- **Examples,** each header's 20 bytes:
  1. A background packet, length 1, its number 1, the foreground's next number 1, both acknowledgements 0, no
     flags, hops 0, the data one zero word:
     `02 01 01 00 00 00 10 00 00 00 00 00 EA 9A 70 42 45 94 1A E9`.
  2. The same after one hop, hops 1: `02 01 01 00 00 00 10 00 00 00 00 01 EA 9A 70 42 E9 FB 0B D1`.
  3. A foreground packet, length 1, its number 0x2ABCD, foreground acknowledge 0x15555, the background's next
     number 0x00123, background acknowledge 0x3FFFE, hops 3, foreground stop and background resend set, the data
     one zero word: `01 01 CD AB 56 55 35 12 80 FF FF 93 EA 9A 70 42 57 AE 78 15`.
  4. A packet of length 0: stream 1, the foreground's next number 5 and acknowledge 4, the background's 9 and 8:
     `01 00 05 00 10 00 90 00 00 02 00 00 00 00 00 00 00 DD 0F 8F`.
- A cross-check for any implementation: the CRC-32C of all 20 header bytes of a good packet is 0x48674BC7.

### The CRC as hardware (2026-11-17)

- **A CRC unit is small,** and can take any number of bits a cycle. For CRC-32C, counted from the polynomial
  (2-input XOR gates before any sharing; synthesis will share terms and come out lower; not synthesized):

  | bits a cycle | XOR gates | levels of logic | flip-flops |
  |---|---|---|---|
  | 1 | about 17, the feedback shared | 2 | 32 |
  | 8 | about 270 | 4 | 32 |
  | 32 | about 1,040 | 6 | 32 |
  | 128 | about 2,490 | 7 | 32 |

- **A byte a cycle is the fit:** the link is 8 lines, a byte a transfer, so the unit takes each byte as it
  arrives and is done when the packet is. The definition above is already byte by byte.
- **Checking a header** (owner): bytes 0 to 15 go through the unit and the result is compared with bytes 16 to 19.
  - The same thing, simpler to build: all 20 bytes go through, and the result is compared with the constant
    0x48674BC7. The unit then never holds a result while four more bytes arrive.
  - Bytes 12 to 15 are kept as they pass: they are the data checksum.
- **Checking the data** (owner): the unit is set back to its starting value, **all ones, not zero,** the 16 ×
  length data bytes go through, and the result is compared with the four bytes kept. The constant does not serve
  here, since the checksum is in the header and not after the data.
  - A packet of length 0 has no bytes to put through: the result is 0x00000000, and the header must carry that.
- **Four units an ASIC** (owner), one for each direction of each of its two links:

  | unit | what it does |
  |---|---|
  | in, from the host's side | checks each packet |
  | out, down the chain | seals the header again of each of the host's packets it passes down |
  | in, from down the chain | checks each packet being passed up |
  | out, towards the host | the data checksum of the ASIC's own packets; the header of every packet, its own and forwarded |

  - The two that receive cannot be one: both links can be receiving at once.
  - The two that send could share a unit, a header being 16 bytes of work against a packet's 532 on the wire. The
    owner took four: a second unit is cheaper than the sharing.
- **An ASIC's own packet is checksummed before it is sent,** not as it goes: the data checksum is in the header,
  which goes first. The words are waiting in the outgoing queue.

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
| both checks pass but a field cannot be: a stream that does not exist, a length over 32, a reserved bit set, hops 15 on a packet to forward | yes | **the packet is thrown away, just as one that failed a check** (owner, 2026-11-17): the receiver sets **resend**. There is no path of its own for it. A status bit may say which test failed (`format_error` in `INGRESS-INTERFACE.md`), for diagnosis only |

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
3. repeats these until it sees an ordinary acknowledgement (settled 2026-11-17; the owner: "this should be
   robust"). Neither side ever depends on one packet getting through.

Why the zeros work:

- **1,024 bytes is longer than the longest packet,** 532 bytes with its header. Whatever length a confused receiver
  believes it is in the middle of, it has run out before the zeros end.
- **A zero word is never a header,** so between packets it is plainly idle.
- **The first byte that is not zero starts a header,** since a header's first byte is never zero.

Nothing in the exchange is relied on to arrive, because the noise can cover it: that is why both sides repeat.

## Holding the host back: stop sending and resume sending

- **Decided (owner): the link has "stop sending" and "resume sending".**
- **They are for each stream.** Stopping the background stream, when the Updater stalls, leaves the foreground stream
  running. The owner can't imagine the foreground stream ever needing to stop, but it has the bit all the same.
- **They are one bit a stream, in every header:** each stream's `stop` flag, in every header a side sends, says
  what it wants now, 1 for "stop sending me this stream" and 0 for "send". Resuming is the bit going back to 0.
  Since the header of 20 bytes, a packet of either stream carries both.
- **When the status is sent** (owner, 2026-11-17):
  1. **On a change, right then,** as a packet of length 0 if there is nothing else to send.
  2. **A timer runs from the end of the last packet sent** in that direction. Any packet starts it again.
  3. **When it reaches 512 cycles, the status is sent again,** one packet of length 0, which speaks for both
     streams and starts the timer again.
  - So a side is never more than 512 cycles of silence from telling the other where it stands, stopped or not,
    and no "resume" can be lost for good. The same packets keep the acknowledgements fresh.
  - **One timer a direction is enough,** as the owner first put it, because every packet now carries both
    streams' status. With the 16-byte header it was not: one stream's data started the timer again and the other
    stream's status never went out (WORKER8), and a timer for each stream was the fix proposed.
  - There are no zeros in the rule: an idle line is zeros anyway.
  - A status packet that is damaged is a damaged packet like any other (owner): the receiver loses its place,
    says "lost", the link repairs, and the status goes out again.
- **They are for each link** (owner, 2026-11-17): "stop" goes to the neighbour, not to the host ("The chain").
- **The queue behind a stream keeps room when it says stop:** the packets already on that link's wire still arrive.
  Only the one link counts, since the neighbour stops within a packet or two of seeing the bit.
- **Ingress's queues are 16 KB on each stream** (owner, 2026-11-17), 32 full packets, behind a buffer that holds
  the one packet being checked.
  - **Why the size is not arbitrary** (WORKER8's search of the protocol): if the stop bit is lost, the sender
    goes on until its window is used up, so **the window must be no more than half a queue,** or the queue
    overruns. A window of 8 packets, 4 KB, against a queue of 8 KB sat exactly at that limit. The owner: "if
    we're right on the edge of some bad condition, let's go to 16 KB queues."
  - The sizes before: 16 KB on 2026-11-16, "to make sure we can stop the remote side", which v1.37 had as first
    merged; then 4 KB with the forward count; then 8 KB as a placeholder, which WORKER7 built.
- **A stream says stop when its queue is half full** (settled 2026-11-17), which leaves 8 KB, 16 full packets:
  twice the window.
  What can still arrive after the bit is sent is two or three: the bit may wait behind a packet being sent, and
  the neighbour finishes the packet it is in.
- This replaces the per-stream window first proposed here, which did the same job with more machinery.

## What a sender must keep

- A sender keeps every packet until it is acknowledged, to send it again.
- **The host has the memory for that.**
- **The chip does not.** It keeps **8 packets a stream** on each link it sends on (owner, 2026-11-17), about 4 KB,
  and waits when 8 are unacknowledged.
  - In ordinary running two or three are outstanding: an acknowledgement comes back on every packet the other
    side sends, and at least every 512 idle cycles with the status.
- **Sending again is go-back-N** (the owner's rule: "send again from the number after my acknowledge field"): the
  sender starts again from the oldest packet not acknowledged.
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

- **The replay buffer is PCI Express's,** not invented here. Its flow control is by credits; this link's
  is the simpler stop bit.
- One difference: PCI Express never loses its place, because its physical layer marks packet starts with symbols of
  their own. This link marks nothing, which is why it has the zeros and "lost".

## Open

- What the foreground stream carries, and how large its messages are.
- Whether the recovery packet states the packet number it restarts from, or that is the "lost" packet's number plus one.
- Whether the host, which keeps every packet, must also hold itself to the window of 8 a stream: with more
  outstanding, a lost stop bit can overrun a queue (WORKER8).
- The sizes of a forwarding ASIC's buffers, which the same rule binds.
- The timers: how soon an unacknowledged packet is sent again without a "resend".
