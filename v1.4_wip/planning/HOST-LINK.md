# The link from the host: streams, packets and recovery

Notes from the discussion with the owner on 2026-11-16, as a first draft. This is the layer under the frames
(`HOST-FRAMES.md`): it is what lets a frame assume it was delivered correctly and in order. Ingress and Egress are
still to be designed; this is what was said about the link they sit on. A dagger (†) marks what is proposed and not
ruled on; (owner) marks what the owner said.

**A packet here is the link's:** a header and its data. It is not a packet of the mesh. How the master tile
takes packets out of Ingress is `INGRESS-INTERFACE.md`.

**Where this document stands (2026-11-17).** WORKER8 built the link from the first form of this document and
searched it exhaustively: **as first written the protocol did not hold.** One damaged packet could stop a link
for ever, in several ways. **The rules that mend it are now in here,** each marked (WORKER8): in "When a packet is
bad", "Recovery", "Starting a link again", "Holding the host back", "What a sender must keep" and "The chain".

- **What they rest on:** about 36 million states searched, every search complete, on one link and on chains of 1, 2
  and 3; 14 planted bugs each caught; 300 random runs on chains of up to 16, 202 million cycles, with none failing.
  Each flaw has a kept case that fails without its rule (`updater_wip/link/` and
  `updater_wip/planning/sessions/WORKER8/QUESTIONS.md`, on branch `updater-w8` until it is merged).
- **What is not yet shown:** those runs were on packet numbers for each stream. **One run of numbers for a link**
  (below) was decided after them, and WORKER8 is to search it. The rules here do not depend on which, as far as the
  design session can tell; the claim that a stopped stream cannot block the other does, and is marked.
- A dagger (†) marks a number or a choice that is proposed and not ruled on.

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

**A header word of 128 bits, then 0 to 32 words of data.**

| bits | field | meaning |
|---|---|---|
| [3:0] | stream | whose data this packet carries: 1 foreground, 2 background; never 0 |
| [4] | resend | "your last packet was bad: send again from the number after my acknowledge field" |
| [5] | lost | "I have lost my place" (below) |
| [6] | recovery | the answer to "lost": the first packet after the zeros |
| [7] | restart | proposed by WORKER8, accepted to carry on with: with lost, "I have started again"; with recovery, "so have I" |
| [15:8] | length | the data's length in 128-bit words, 0 to 32 |
| [33:16] | packet number | 18 bits. This packet's number **on the link:** the first is 1, they run to 0x3FFFE, then 1 again. In a packet of length 0: the number the next packet with data will carry |
| [51:34] | acknowledge | 18 bits. The last packet number received in order on the link; 0 for none yet |
| [55:52] | hops | the ASICs that have forwarded this packet along the chain (below). Whoever makes a packet sends 0 |
| [56] | foreground stop | "stop sending me the foreground stream"; 0 is "send" |
| [57] | background stop | "stop sending me the background stream"; 0 is "send" |
| [63:58] | reserved | 0 |
| [95:64] | data checksum | over the data |
| [127:96] | header CRC | over bits [95:0] |

The same header byte by byte, as it goes on the link. Byte *i* holds bits [8*i* + 7 : 8*i*] and is the (*i* + 1)th
byte sent. The data follows, 16 bytes a word.

| byte | holds |
|---|---|
| 0 | the stream in its low four bits; resend, lost, recovery and restart in bits 4 to 7. A background packet with none of them is 0x02 |
| 1 | the length, 0 to 32 |
| 2, 3 | packet number bits 0 to 15, low byte first |
| 4 | low two bits: packet number bits 16 and 17; high six bits: acknowledge bits 0 to 5 |
| 5 | acknowledge bits 6 to 13 |
| 6 | low four bits: acknowledge bits 14 to 17; high four bits: hops |
| 7 | bit 0: foreground stop; bit 1: background stop; the rest 0 |
| 8 to 11 | the data checksum, low byte first |
| 12 to 15 | the header CRC, low byte first |

### One run of numbers for a link (owner, 2026-11-17)

**A direction of a link has one run of packet numbers, shared by both streams.** The owner: "why have separate
packet numbers for the streams? The packet number is about the link, not the stream."

- **A number is given to a packet when it is sent,** whichever stream its data is on. A stream that is stopped
  simply has no packets sent, and the numbers of the other's stay consecutive.
- **So one acknowledgement, one resend flag, one window and one set of kept packets** for a direction of a link.
- **Both streams' stop bits are in every header.** They are what is a stream's; everything else in the header is
  the link's or the packet's.
- **Why a stopped stream cannot block the other,** which was the reason for numbers of their own:
  1. a sender that hears anything from the other side hears both stop bits;
  2. a queue says stop at half full, 8 KB still free, and at most a window, 8 packets or 4 KB, can be on its
     way. So a good packet always has room, the receiver never has to refuse one, and nothing waits behind a
     refused packet.
- **What it costs:**
  - a damaged background packet has the foreground packets behind it sent again too: a few packets, and only
    when there is damage;
  - the window is shared: with 8 background packets not yet acknowledged, a foreground packet waits for the next
    acknowledgement, about a packet's time.
- **What it saves:** the header stays one 128-bit word, 3.0% of a full packet, and the link end is one machine
  where it was two.
- **It is still to be shown by WORKER8's search** that the foreground keeps moving while the background is
  stopped, with one run of numbers.

**How it came to this,** in a morning:

- With numbers for each stream and a header that spoke for its own stream only, one stream filling a direction
  starved the other of acknowledgements and its stop bit: WORKER8 measured the background standing still for
  102,447 cycles on a wire with no damage.
- Sending the other stream's status as packets of its own would cost a busy direction about 3% on top of the
  headers' 3%.
- The owner then made the header 20 bytes, with both streams' numbers, acknowledgements and flags in each. It
  was written up here, and WORKER8 built it.
- Then the owner asked why the streams had numbers of their own at all. They need not, and the header is 16 bytes
  again.

### The rest of the rules

- **Everything rides in 128 bits** (owner): the header is one word, and the data is whole words, so the link is
  word-aligned throughout.
- **The length is any number of words up to 32,** 16 to 512 bytes of data (owner). Any frame then cuts exactly into
  packets.
- **A frame ends at the end of a packet** (owner): the next frame never starts in the middle of one.
- **A length of 0 is a header with no data** (owner): a way to say where the link stands with nothing else to send.
  - **It takes no packet number** (ruled 2026-11-16, on WORKER2's question): if it did, the other side would have to
    acknowledge it, and an acknowledgement that must itself be acknowledged never settles. Its number field holds
    the number the next packet with data will carry, so the field is still never 0. Its stream field is 1 or 2,
    so that the first byte is not zero; it means nothing.
  - **That number is how a receiver learns that the last packet of a burst was lost,** with no timer (WORKER8):
    it sees a number ahead of the one it expects.
- **A packet that is forwarded gets a new number** on the next link (owner).
- **No packet number is 0 or all ones** (owner), so a header word is never all zeros.
- **18 bits of packet number is enough** (owner; 20 until 2026-11-17, when four bits went to the hops field).
  Only packets not yet acknowledged are outstanding, and that is a small window. A link's numbers come round
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
| the data | the packet, end to end |
| the data checksum | the one link: computed afresh each time a packet is made ready to send (owner, 2026-11-17) |

**An ASIC that forwards a packet, in either direction:**

1. **takes the whole packet in and checks it,** as any receiver does. A bad packet is the incoming link's
   business: it is asked for again there, and is not forwarded;
2. **writes the header for the outgoing link:** that link's next packet number, its acknowledge
   field and its flags;
3. **adds one to hops;**
4. **computes the data checksum afresh, from the data as it holds it** (owner, 2026-11-17);
5. **computes the header CRC again** (owner: "always recompute the header CRC").

The data passes through unchanged.

**What is left of a forwarded packet's header** (owner: "the header gets mostly stripped"):

| field | on the outgoing link |
|---|---|
| packet number, acknowledge, flags | dropped, and written afresh: they were the incoming link's |
| hops | kept, plus one |
| stream, length | kept: they describe the packet |
| data checksum | computed afresh |
| header CRC | computed again |

- **Both checks are made anew whenever a packet is made ready to send** (owner, 2026-11-17: "use the fresh value
  as the packet checksum, recompute the header checksum"): a packet of the ASIC's own, a forwarded one, and one
  sent again.
  - **What it gives:** one rule for every packet that goes out, and no state in which a sender holds a checksum
    that disagrees with its data. As first written, a kept packet that went bad in memory was sent again for
    ever; WORKER8's mend was to compare with the checksum held and start the link again.
  - **What it gives up, knowingly:** the checks then cover the wire and not the memory. A bit that flips in a
    queue, in the packets kept for sending again, or in a forwarding buffer goes out with a checksum that
    matches it, and nobody is told. The design session had kept the checksum end to end for that reason and put
    the choice to the owner. If those memories are to be guarded, it is by a check of their own.

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
- **On an outgoing link an ASIC's own packets and the ones it forwards share the link's one run of packet
  numbers.**
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

**What the chain needed besides** (WORKER8; searched on chains of 1, 2 and 3, run at random on up to 16):

- **A node acknowledges a packet when it has taken it in:** into its own queue and into its forwarding buffer,
  not when it has passed it on. The owner: "it's the problem of ASIC0 to send it. The driver's job is done as soon
  as ASIC0 has received it successfully."

- **A node must be told whether it has a neighbour down the chain.** The last has none. **By a pin** (owner,
  2026-11-17): it is a fact about the board, and it must be known before any firmware runs.
  - The restart handshake cannot tell. A lower link that comes up late, because of noise, would look like no
    link, and the node would take the host's packets without passing them on. Told that it has a neighbour, a
    node holds what it must forward until that link is up.
- **A forwarding buffer for each stream and each direction,** † 16 KB like a queue, since the same rule binds it:
  the window of the end that feeds it must be no more than half of it.
- **A link's stop bit is up when either is half full:** the node's own queue, or the forwarding buffer behind
  that link.
- **Measured:** the fullest a forwarding buffer got was 677 words of 1,024. A foreground packet never waited more
  than one packet's time for the line.

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
- **The header CRC** is the CRC-32C of header bytes 0 to 11, the data checksum included.
- **Examples,** each header's 16 bytes:
  1. A background packet, length 1, number 1, acknowledge 0, no flags, hops 0, the data one zero word:
     `02 01 01 00 00 00 00 00 EA 9A 70 42 A0 8C 06 E1`.
  2. The same after one hop, hops 1: `02 01 01 00 00 00 10 00 EA 9A 70 42 9E BC 0A 21`.
  3. A foreground packet, length 1, number 0x2ABCD, acknowledge 0x15555, hops 3, resend and background stop
     set, the data one zero word: `11 01 CD AB 56 55 35 02 EA 9A 70 42 D2 BF BA 0A`.
  4. A packet of length 0: stream 1, the next number 5, acknowledge 4, foreground stop set:
     `01 00 05 00 10 00 00 01 00 00 00 00 57 09 4D 4E`.
  - The first two are the same bytes as with the header before, which WORKER7 built: only the stop bits and the
    restart flag have moved.
- A cross-check for any implementation: the CRC-32C of all 16 header bytes of a good packet is 0x48674BC7.

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
- **Checking a header** (owner): bytes 0 to 11 go through the unit and the result is compared with bytes 12 to 15.
  - The same thing, simpler to build: all 16 bytes go through, and the result is compared with the constant
    0x48674BC7. The unit then never holds a result while four more bytes arrive.
  - Bytes 8 to 11 are kept as they pass: they are the data checksum.
- **Checking the data** (owner): the unit is set back to its starting value, **all ones, not zero,** the 16 ×
  length data bytes go through, and the result is compared with the four bytes kept. The constant does not serve
  here, since the checksum is in the header and not after the data.
  - A packet of length 0 has no bytes to put through: the result is 0x00000000, and the header must carry that.
- **Four units an ASIC** (owner), one for each direction of each of its two links:

  | unit | what it does |
  |---|---|
  | in, from the host's side | checks each packet |
  | out, down the chain | the data checksum and the header CRC of each of the host's packets it passes down |
  | in, from down the chain | checks each packet being passed up |
  | out, towards the host | the data checksum and the header CRC of every packet, its own and forwarded |

  - The two that receive cannot be one: both links can be receiving at once.
  - The owner took four: one for each direction of each link.
- **Every packet's data is checksummed before it is sent,** not as it goes: the data checksum is in the header,
  which goes first. So a sending unit passes over a packet's data once before the packet leaves, each time it is
  sent.
  - At a byte a cycle that pass takes as long as the packet's data: 16 cycles for a one-word message, 512 for a
    full packet. On a busy link it is done while the packet before is going out; on an idle one it is a wait
    before the header. A unit of 128 bits a cycle (about 2,490 gates) would do a full packet in 32.

**The order a receiver tests in:**

1. the header CRC;
2. the header's fields: the stream is 1 or 2, the length is at most 32, the reserved bits are 0. The hops field
   is not tested;
3. the data checksum;
4. the packet number, which means something only once the rest has passed.

## When a packet is bad

A receiver expects one number next on a link: the one after the last packet it took.

| what the receiver sees | what it does |
|---|---|
| a good packet with the number it expects, and room in its stream's queue | takes it; its acknowledge field moves on |
| the header good, the data checksum failing | throws it away and sets **resend**. The link does not stop (owner) |
| a good packet with a number **behind** the one expected | throws it away and acknowledges as usual: it has it already (WORKER8) |
| a good packet with a number **ahead** of the one expected | throws it away and sets **resend:** one was missed (WORKER8) |
| a packet of length 0 whose number is ahead of the one expected | sets **resend:** the last packet of a burst was lost. This is why no timer is needed at the sender (WORKER8) |
| a good packet in order, with no room in its stream's queue | throws it away and does not acknowledge it; the sender sends it again. With the sizes right this never happens; it is what catches a wrong size (WORKER8) |
| the header's CRC failing | it has lost its place: "Recovery" |
| both checks passing, but a field that cannot be: a stream that does not exist, a reserved bit set, hops 15 on a packet to forward | "A packet made wrong", below |
| a length over 32 under a good header CRC | it cannot stay in step, not knowing how far to pass over. "A packet made wrong", below |

- **Ahead and behind are judged within half the numbers,** which a window of 8 is far inside.
- **Without the rows for a wrong number the link stopped for ever after one lost packet:** every packet after
  the lost one was good, out of order, and had no rule (WORKER8).
- **† Having asked for a resend, a receiver asks again only after 2,048 cycles** if the sender still shows itself
  ahead. Otherwise every packet already on its way sets off another going-back. The value only affects speed
  (WORKER8).
- **The header of a packet sent again is written afresh,** with the acknowledge field and the flags of now
  (WORKER8).
- **A sender goes back exactly as far as it must:** in 5 million packets of random runs no receiver was sent a
  packet it had already taken (WORKER8).

**A packet made wrong** (owner, 2026-11-17):

- **The first time, it is asked for again,** as any bad packet is. It could be noise: a damaged header that
  happened to pass its CRC. The owner: "a resend in that case is the right option. It just shouldn't fail the
  same way repeatedly."
- **If the packet sent again fails the same way,** the sender made it so, and asking again cannot help. † At the
  second time running, on the same packet number, **the receiver starts the link again** ("Starting a link
  again"), with the error.
  - **How "the same packet again" is known: one flag** in each receiver. It is set by a packet that carries the
    number expected, passes both checks and fails a field test. It is cleared only when a packet is taken. If it
    is already set when the same happens again, that is the second time.
    - Good packets that arrive behind the wrong one do not clear it: they are ahead of the number expected, so
      they are thrown away, not taken. With 5 made wrong and 6 and 7 good, the receiver throws away 5, 6 and 7,
      the sender goes back, and the 5 sent again is the next packet the receiver looks at.
    - The packet's checksums cannot be used to know it again: its header is written afresh when it is sent
      again, so the header CRC differs. The number expected already says which packet it is.
  - **The error is Ingress/Egress's,** `format_error`, and so its tile's (owner).
  - Noise does not do this twice running: each time needs a damaged header to pass a 32-bit CRC.
  - The error is the point, not the restart. A sender that makes a wrong packet is broken, by a bug or by a chain
    wired longer than 16, and nothing saves the data. What is left to choose is a link that hangs, one that
    drops the packet and says nothing, or one that says so. On a chain the restart also keeps the link usable
    for the ASICs beyond.
- **A length over 32** cannot be passed over with any trust, so the receiver treats it as a lost place the first
  time, and counts it the same way.
- `INGRESS-INTERFACE.md`'s `format_error` says which test failed.
- (WORKER8's rule told the two cases apart by the data: if the data's check passed as well, the sender was taken
  to have made the packet so, at the first time. But noise that damages only the header leaves the data whole,
  and that rule would have called it the sender's fault. The owner's count needs no such judgment.)

**Finding a packet's start:**

- **Idle is zero bytes, in any number,** not only whole words: the first byte that is not zero starts a header.
- **"The first byte is never zero" is how a receiver finds a packet's start, not a check it can make.** A first
  byte that noise turns to zero is taken for idle; the receiver then fails a header CRC one byte later, which is
  the "lost my place" case.
- **After a failed header CRC a receiver takes nothing until it has seen a full packet's length of zeros in a
  row,** 528 †, which no packet can hold. Without this it read the bad packet's data as headers, and one of them
  can pass: a packet was then lost for good (WORKER8).

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

**What had to be added** (WORKER8):

- **Both ends lost at once.** As first written each waited for a "recovery" the other could not send, for ever.
  **A lost end that hears "lost" answers it,** with lost and recovery both set in one packet. It stops answering
  at the first good packet that does not have lost set.
- **An acknowledgement counts in any state:** every good packet's acknowledge field releases what it covers. A
  "lost" packet's is also where the sender goes back to.
- **A good packet while lost:** its data is not taken; its acknowledge field and its stop bits are.
- **The answering end sends no data until its answer has been heard:** only rounds of zeros and "recovery".
- **The recovery packet states its number already:** it has length 0, so its number field is the number the
  sender carries on from. The receiver checks it against the one it expects, and never takes its place from it.

Why the zeros work:

- **1,024 bytes is longer than the longest packet,** 528 bytes with its header. Whatever length a confused receiver
  believes it is in the middle of, it has run out before the zeros end.
- **A zero word is never a header,** so between packets it is plainly idle.
- **The first byte that is not zero starts a header,** since a header's first byte is never zero.

Nothing in the exchange is relied on to arrive, because the noise can cover it: that is why both sides repeat.

## Starting a link again (WORKER8; accepted to carry on with)

Recovery mends a lost place. It cannot mend two ends whose numbers no longer agree, and as first written nothing
could: the link stopped for ever.

**The restart flag** (bit 7 of the header's first byte):

| the flags | says |
|---|---|
| restart with lost | "I have started again" |
| restart with recovery | "so have I" |

- **The restart packet is a full packet with a fixed pattern as its data** (owner, 2026-11-17: "a magic 512 byte
  packet"): length 32, the data being **the first 32 primes as bytes, 2 to 131, repeated 16 times.** That is two
  128-bit words, sixteen times over:

  ```
  02 03 05 07 0B 0D 11 13 17 1D 1F 25 29 2B 2F 35
  3B 3D 43 47 49 4F 53 59 61 65 67 6B 6D 71 7F 83
  ```

  Its CRC-32C, the packet's data checksum, is 0xC8981D21.
  - **An end acts on it only if the header, the data checksum and the pattern all agree.** Anything less is a
    damaged packet.
  - **Why:** starting again is drastic. As a flag in a 16-byte packet it was guarded by the header's CRC alone,
    and a badly damaged header could be read as "I have started again". Noise cannot make this.
  - **"Lost" and its answer stay 16 bytes:** they must get through a noisy line, where a short packet is the one
    that arrives, and a false one only sends the sender back a few packets.
  - † Its data goes to no queue and it takes no packet number; its number field is 1, the number its sender's
    first packet will carry.
- **An end that hears "I have started again"** drops the packets it was keeping, starts its numbers again from 1,
  answers, and **raises an error:** what was on its way is gone, and the frames above the link cannot be trusted.
- **A link carries nothing until both ends are up.** Two ends that power on together bring the link up with no
  error.

**What starts a link again:**

| the cause | how it is noticed |
|---|---|
| one end powers on, or the FPGA is loaded again, while the other runs | the newcomer says so |
| a kept packet has gone bad in the sender's memory | **no longer a cause** (owner, 2026-11-17). The checksum is computed afresh each time a packet is sent, so it always matches what goes out, and the fault is not seen ("The chain"). WORKER8's rule had the sender compare with the checksum it held and start the link again |
| a packet made wrong, twice running | the receiver, above |
| a false acknowledgement: a damaged header that passed its CRC and released packets that never arrived | **nothing is done about the false one itself** (owner, 2026-11-17: "just ignore it"). It shows when it matters: **the other end asks for a packet the sender no longer keeps.** The sender cannot send it, raises the error, and the link starts again |

- **A false acknowledgement, in full:** the sender has sent up to 20 and keeps 13 to 20. A damaged header says
  "acknowledged 18", and the sender lets 13 to 18 go, though they are still on their way.
  - **Usually they arrive whole.** The receiver takes them, nothing is asked for again, and nothing is lost. The
    owner: "if a resend is never requested, then no harm, no foul."
  - **Only if one of them is damaged on the way** does the receiver ask for it, and then the sender has no copy.
    That is the error, and it is raised only when there is real harm.
  - It is always found when it matters, because the receiver always learns it is missing a packet and always
    asks.
  - An acknowledge field that claims more than was ever sent releases nothing, since there is nothing there to
    release.
  - (WORKER8's rule watched for acknowledgements going backwards, two in a row. After a false one the true ones
    are behind it whether or not anything was lost, so that rule would have raised the error with no harm done.
    The owner's needs no watching and raises none.)
- **Each of these ends as an error the core sees,** never as a link that hangs. On the master tile a peripheral's
  error is the tile's, which stops everything (owner): the host resets the chip and the proof starts over.

**A known limit, accepted (owner asked; the design session's recommendation):** a damaged header that passes its
CRC with a **false packet number.** One less than the true number, after a loss, has a packet taken twice and the
next lost, silently. Nothing in the header can tell.

- The header's CRC catches every pattern of up to 7 flipped bits (WORKER8).
- **Damaged data that passes its 32-bit checksum is at least 600 times as likely,** and about 10¹¹ times at a bit
  error rate of 10⁻³ (WORKER8). That was accepted when the data checksum was made 32 bits, so a second check on
  the header would harden the rarer fault.
- A wider data check is the lever if a stronger link is wanted. A wrong trace fails the proof's verification.

## Holding the host back: stop sending and resume sending

- **Decided (owner): the link has "stop sending" and "resume sending".**
- **They are for each stream.** Stopping the background stream, when the Updater stalls, leaves the foreground stream
  running. The owner can't imagine the foreground stream ever needing to stop, but it has the bit all the same.
- **They are one bit a stream, in every header:** each stream's `stop` flag, in every header a side sends, says
  what it wants now, 1 for "stop sending me this stream" and 0 for "send". Resuming is the bit going back to 0.
  A packet of either stream carries both bits.
- **When the status is sent** (owner, 2026-11-17):
  1. **On a change, right then,** as a packet of length 0 if there is nothing else to send.
  2. **A timer runs from the end of the last packet sent** in that direction. Any packet starts it again.
  3. **When it reaches 512 cycles, the status is sent again,** one packet of length 0, which starts the timer
     again.
  - So a side is never more than 512 cycles of silence from telling the other where it stands, stopped or not,
    and no "resume" can be lost for good. The same packets keep the acknowledgements fresh.
  - **One timer a direction is enough,** as the owner first put it, because every packet carries the link's
    acknowledgement and both stop bits. With numbers for each stream it was not: one stream's data started the
    timer again and the other stream's status never went out (WORKER8).
  - There are no zeros in the rule: an idle line is zeros anyway.
  - A status packet that is damaged is a damaged packet like any other (owner): the receiver loses its place,
    says "lost", the link repairs, and the status goes out again.
  - **512 is kept** (WORKER8's figures, 100 random runs each): the data rate does not depend on the timer, since
    the status only goes when the line is idle; the longest a sender waited for an acknowledgement does.

    | the timer | the longest wait |
    |---|---|
    | 512 | 2,422 cycles |
    | 2,048 | 5,240 |
    | 8,192 | 8,958 |

- **An acknowledgement goes out as soon as the line is free** (owner's ruling on WORKER8's question), as a packet
  of length 0, not at the next status. It is what short foreground messages need: 400 one-word packets took
  29,703 cycles without it and 17,553 with. The data rate does not change, since these packets go only when
  nothing else is going; they are 16 bytes for a packet taken.
- **They are for each link** (owner, 2026-11-17): "stop" goes to the neighbour, not to the host ("The chain").
- **Ingress's queues are 16 KB on each stream** (owner, 2026-11-17), 32 full packets, behind a buffer that holds
  the one packet being checked.
- **A stream says stop when its queue is half full** (settled 2026-11-17), which leaves 8 KB, 16 full packets.
- **The window must be no more than half a queue** (WORKER8). With the stop bit and the acknowledgements both
  lost, a sender goes on to the end of its window, and whatever is beyond the room overruns the queue, or is
  refused.
  - **The chip's window is 8 packets, 4 KB:** a quarter of a queue. A window of 8 against a queue of 8 KB sat
    exactly at the limit; the owner: "if we're right on the edge of some bad condition, let's go to 16 KB queues."
  - **The host must keep to it too,** though it has the memory for more: no more than 16 packets outstanding,
    † 8 as the chip has. WORKER8 measured no loss of data rate at 8, and with 64 the queue filled and a packet
    was refused. With an acknowledgement for every packet the host seldom has more than two or three outstanding.
  - The fullest a queue of 1,024 words got in the random runs was 626; no packet was ever refused.
- The sizes before: 16 KB on 2026-11-16, "to make sure we can stop the remote side", which v1.37 had as first
  merged; then 4 KB with the forward count; then 8 KB as a placeholder, which WORKER7 built.
- This replaces the per-stream window first proposed here, which did the same job with more machinery.

## What a sender must keep

- A sender keeps every packet until it is acknowledged, to send it again.
- **The host has the memory for that,** and keeps to the window all the same (above).
- **The chip does not.** It keeps **8 packets** for each direction it sends in (owner, 2026-11-17), about 4 KB,
  of both streams together, and waits when 8 are unacknowledged.
  - In ordinary running two or three are outstanding: an acknowledgement comes back on every packet the other
    side sends, as soon as its line is free.
- **Sending again is go-back-N** (the owner's rule: "send again from the number after my acknowledge field"): the
  sender starts again from the oldest packet not acknowledged.
- **No timer sends a packet again unasked** (WORKER8): the receiver always learns what it missed, from a number
  ahead of the one it expects, and asks. The searches hold with a timer, without one, or with only one.
- **What is kept is the data;** the header and both checks are made anew each time a packet is sent (owner).
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

- **What the foreground stream carries,** and how large its messages are.
- **One run of numbers for a link, searched.** In particular that the foreground keeps moving while the background
  is stopped, what a damaged background packet costs the foreground, and how long a foreground packet waits when
  the window is full of background packets (WORKER8, in hand).
- **Two random runs that failed on the runner** (Quest 227, seeds 200137 and 140162, of 3,777): a sender stood
  still, with no acknowledgement for 20,801 cycles on a link in step. Both had the host's window at 64, past the
  limit above. Whether that is the whole cause is not yet known.
- **A neighbour that goes silent** (WORKER8). Nothing here notices a link whose other end has stopped: the sender
  waits for ever, which is right if the neighbour is slow and wrong if it is dead. A count of status periods with
  nothing heard would tell.
- **The background behind the foreground** (WORKER8). A background packet waits while any foreground packet is
  ready, so a foreground that never pauses holds the background for as long. That is the order as decided, and
  not a flaw of the protocol; it is a thing to know.
- **The numbers marked †:** 2,048 cycles before asking again; the second wrong packet running before the error;
  528 zeros before hunting for a header; the host's window of 8; the forwarding buffers' 16 KB.
- **What the error is called and where it is seen:** a link that started again raises an error to the core. Its
  bit in Ingress/Egress's `ERRORS` is not yet named (`INGRESS-INTERFACE.md`).
