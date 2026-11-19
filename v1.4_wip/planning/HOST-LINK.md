# The link from the host: streams, packets and recovery

Notes from the discussion with the owner on 2026-11-16, as a first draft. This is the layer under the frames
(`HOST-FRAMES.md`): it is what lets a frame assume it was delivered correctly and in order. Ingress and Egress, as
the master tile's core sees them, are `INGRESS-INTERFACE.md`; this is the link they sit on. A dagger (†) marks what
is proposed and not ruled on; (owner) marks what the owner said.

**A packet here is the link's:** a header and its data. It is not a packet of the mesh. How the master tile
takes packets out of Ingress is `INGRESS-INTERFACE.md`.

**Where this document stands (2026-11-18).** WORKER8 built the link from the first form of this document and
searched it exhaustively: **as first written the protocol did not hold.** One damaged packet could stop a link
for ever, in several ways. **The rules that mend it are now in here,** each marked (WORKER8): in "When a packet is
bad", "Recovery", "Starting a link again", "Holding the host back", "What a sender must keep" and "The chain".

- **What they rest on** (WORKER8, on the protocol as it stands here, with the fast sending unit): 36.9 million
  states searched on its machine and 86.5 million on the runner, every search complete, on one link and on
  chains of 1, 2 and 3; 18 planted bugs each caught; a kept case for every rule, which fails without the rule
  and holds with it; 1,140 random runs on chains of 1, 2, 4 and 16, none failing; and two campaigns on the
  runner, none failing and no packet refused for want of room: 6,071 runs and 306 million packets with the slow
  unit (Quest 232), and **7,519 runs, 18 billion cycles and 385 million packets with the fast unit** (Quest 237)
  (`v1.37/link/` and `v1.37/planning/updater/sessions/WORKER8/REPORT.md` and `QUESTIONS.md`, on branch
  `main` since 2026-11-18).
- **The long search is complete and holds** (Quest 231, 2026-11-18): 86,515,438 states, every one tried, in 47
  minutes depth-first. From every state, with either stream never taken again, the other finished.
- **The runner's long search ran before the fast unit went in.** The fast unit changes when a sender's packets
  leave and none of the rules; the campaign of Quest 237 is the first long run on the runner of the protocol
  exactly as it stands here.
- **What is still out,** with the fast unit: the campaign with forged headers and with bits flipped in senders'
  memory, the larger searches, the long bursts of noise, and the long search again (Quests 238 to 242). The
  searches are of small sizes (a window of 2, queues of a few words); the random runs are of the real sizes.
- **The figures here are the fast unit's,** but where one says otherwise.
- **The link end is built to be used** (WORKER8, 2026-11-18): one header, `v1.37/link/hostlink.h`, with an
  `End` (one end of a link, with its queues) and a `Node` (an ASIC on a chain, the pin its argument), each
  clocked once a cycle with the bytes that arrived going in and the bytes to send coming out. What it costs as
  hardware is "The link as hardware", below, from `sessions/WORKER8/HARDWARE.md`.
- **Three times that evening the protocol as it stood here was found unsound** and mended: one run of numbers
  with "stop" holding back a packet sent again; a resend request lost; a packet hidden in the data. Each is told
  where its rule is.
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
- **They interleave packet by packet.** The longest a foreground packet waits for the line is one background
  packet: 534 cycles at most in the random runs (WORKER8), with the sending unit of 128 bits a cycle ("The CRC as
  hardware").
  - **That is the wait for the line only.** A foreground packet can also wait for the window, and behind packets
    being sent again after damage: in 60 random runs one stood ready up to 6,367 cycles in all.
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
| [95:64] | data checksum | over the data as it is held, before the mask |
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

- **A number is given to a packet when it is first sent,** whichever stream its data is on. A stream that is
  stopped has no new packets numbered, and the numbers of the other's stay consecutive.
- **So one acknowledgement, one resend flag, one window and one set of kept packets** for a direction of a link.
- **Both streams' stop bits are in every header.** They are what is a stream's; everything else in the header is
  the link's or the packet's.
- **What "stop" holds back** (owner, 2026-11-17, on WORKER8's finding): **only packets that have no number yet.**
  **A packet that already has a number is sent again whatever its stream's stop bit says.**
  - **Why it must be so.** As first written here, "a stream that is stopped simply has no packets sent", and
    that was unsound. WORKER8's search found it in four events:
    1. the background's queue is half full, and its end says "stop";
    2. background packet 3, already sent, is lost on the wire;
    3. foreground packet 4 arrives ahead of the 3 expected, is thrown away, and a resend is asked for;
    4. the sender goes back to 3, which is the background's, and the background is stopped: it is not sent.
    Packet 4 is behind 3 in the one run of numbers and can never be taken first. The foreground stood still for
    as long as the background stayed stopped (`cases/stopped-blocks.case`).
  - **Why it is right, and not only a mend:** the room for a numbered packet was counted when it was first
    sent. A queue says stop with 8 KB still free so that a window of packets already on their way, 4 KB, can
    still land. A packet sent again is one of those same packets, not a new one.
- **Why a stopped stream then cannot block the other:**
  1. a sender that hears anything from the other side hears both stop bits, and numbers no new packet for a
     stopped stream;
  2. at most a window of numbered packets is outstanding, and the queue kept room for a window when it said
     stop. So a good packet always has room, and the receiver never has to refuse one;
  3. a numbered packet that is damaged is sent again as soon as it is asked for, stopped or not, so nothing
     waits behind it.
  - The design session's first argument had 1 and 2 and missed 3: a damaged packet of the stopped stream.
- **What it costs** (WORKER8's figures, at the real sizes):
  - **a damaged background packet sets the foreground behind it back,** since both are sent again. A full
    foreground packet let out near the damage arrived later than it would have by:

    | the background packet's damage | at most | on average |
    |---|---|---|
    | a bit of its data | 1,056 cycles | 700 |
    | a bit of its header | 2,144 | 1,433 |
    | its first byte zero | 2,672 | 1,778 |
    | the whole packet gone | 1,584 | 1,067 |

    With numbers for each stream the first row would be nothing. The other three cost the link a recovery
    whatever the numbering.
  - **the window is shared,** but it does not fill: up to a wire delay of 512 cycles each way no more than 3
    packets are ever kept with only acknowledgements coming back, 5 with full packets flooding the other way,
    and a foreground packet never waits for the window. At a delay of 2,048 it fills, and a foreground packet
    waited up to 849 cycles, 1,550 with full packets the other way.
  - **8 is still the right window:** the data rate in the random runs was 0.5803 bytes a cycle with a window of
    2, 0.6252 with 4, 0.6389 with 8 and 0.6392 with 16.
  - **The wire's delay matters to it,** and this document gives none: the window of 8 is never full up to 512
    cycles each way, and fills at 2,048.
- **What it saves:** the header stays one 128-bit word, 3.0% of a full packet, and the link end is one machine
  where it was two.
- **Shown** (WORKER8): that a stopped stream cannot block the other holds on 7 searches, 9.7 million states,
  each with one stream never taken again and the other required to finish; the rule as first written fails
  beside them.

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

1. **takes the whole packet in and checks it,** as any receiver does, taking the mask off its data as it
   arrives. A bad packet is the incoming link's business: it is asked for again there, and is not forwarded;
2. **writes the header for the outgoing link:** that link's next packet number, its acknowledge field, its
   flags and its stop bits;
3. **adds one to hops;**
4. **computes the data checksum afresh, from the data as it holds it** (owner, 2026-11-17);
5. **computes the header CRC again** (owner: "always recompute the header CRC");
6. **sends the data under a new mask,** started from the new header's CRC.

The data passes through unchanged, as it is held.

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
  last has nobody to forward to. The field is ignored by the ASIC that takes the packet (owner): no value of it
  is an error there. It happens to be the receiving ASIC's own place in the chain.
  - **An ASIC that must forward a packet does test it, in either direction:** it adds one, and 16 does not fit.
    Hops 15 on a packet to forward is a packet made wrong.
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
  packet" assumes. The pass over its data before it leaves is 32 cycles more for a full packet, with the
  sending unit of 128 bits a cycle; at a byte a cycle it would be the packet's length again.

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
- **Measured** (WORKER8): the fullest a forwarding buffer got was 621 words of 1,024 in 60 random runs, and 759
  in the runner's campaign (Quest 237, the host's window at 64 in some runs); with buffers of 512 words, 361 in
  30 runs, and no packet was ever refused. The rule asks for twice the window, 512 words; 16 KB is twice that.

## The two checks

1. **The data is checksummed first,** and the result goes into the header (owner).
2. **Then the header is checked,** data checksum included (owner). The header CRC is the word's last field.

- **A packet counts only when both pass.** Together they are a 64-bit check; through 300 ms of noise, some 20 million
  words, a false packet is out of the question.
  - **That is true of the header's fields too, on any packet with data** (WORKER8): the mask ties the data to
    the header CRC, so a header that noise has forged takes the wrong mask off and the data checksum fails. A
    packet of length 0 has the header CRC alone.
- **The receiver can test the header as soon as it arrives,** before any data, and then knows which data checksum
  to expect.
- The packet number is checked as well, since the receiver compares it anyway to keep packets in order.

**Both checks are CRC-32C** (Castagnoli; the one in iSCSI, SCTP and ext4; RFC 3720, appendix B.4). The choice is
settled (owner, 2026-11-17: "we can just pick one"); its definition here is exact, and gold's packetizer and its test vectors use it
(`v1.37/pipeline/frames/gold/test/packets/VECTORS.txt`).

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

- **The data checksum** is the CRC-32C of the packet's data as it is held, all 16 × length bytes, in the order
  they go on the link. On the link they are masked; the checksum is of them before the mask. For a packet of
  length 0 it is 0x00000000, and it is checked.
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

### The data is masked on the wire (owner, 2026-11-17, on WORKER8's second finding)

**Each byte of a packet's data is sent XORed with a sequence that starts from that packet's own header CRC.** A
receiver that has read the header takes the mask off. One that has not sees bytes that mean nothing.

- **The hole it closes.** A receiver finds a packet's start by the first byte that is not zero. If the line reads
  as zeros all through a header and the data behind it arrives whole, the receiver sees idle, and then takes the
  start of the data for a header. **If the data holds a whole packet,** a header with the number expected and
  data that passes its checksum, that packet is taken: a word is delivered that is not the next, a true packet is
  lost, and no check fails (WORKER8, `cases/packet-in-data.case`: two events).
  - By chance it needs a header CRC, the number and a data checksum all to pass, far inside the limit already
    accepted. **But the background stream carries whatever the host is given to send,** and data that is itself
    link packets, a capture of this link or a file of test vectors, needs only the header to be lost.
  - Every form of the header had this. It is as old as "the first byte that is not zero starts a header".
- **What the mask gives:** whether the link works no longer depends on what the data holds. To get a packet
  through in the data, whoever made the data would have to know the header it will travel under ("The mask's
  limit", below).
- **What it costs:** a 32-bit shift register for each direction of a link. No new field, no handshake, nothing
  agreed between the ends beforehand.
- **What stays as it was:**
  - the header is not masked, so its first byte is still never zero;
  - the data checksum is over the data as it is held, not as it is sent;
  - a packet's data is at most 512 bytes, so it still cannot hold the 528 zeros a lost receiver waits for;
  - a forwarding ASIC takes the mask off as it checks a packet and puts a new one on with the new header.
- **The sequence** (WORKER8; the design session's proposal with one bit added):

  ```
  reg = header CRC  OR  0x00010000       the header CRC: the 32-bit number in header bytes 12 to 15, byte 12 lowest
  for each data byte d, in order:
      repeat 8 times:  reg = (reg >> 1) XOR 0x82F63B78   if reg's bit 0 is 1
                       reg =  reg >> 1                    otherwise
      the byte on the wire is  d XOR (reg AND 0xFF)
  ```

  - **The eight shifts are the CRC unit's step for a byte of zero,** so the mask is a CRC-32C register that is fed
    nothing. The receiver does the same from the header it has just read.
  - **Why bit 16 is set:** the register has two states that stand still, 0 and 0xFCA42DAF. A header CRC equal to
    either would send its data in the clear, or as good as. With that bit set no header CRC starts it in
    either, and from any other state it returns only after 2³¹ − 1 shifts.
  - **The restart packet's data is masked like any other.**
- **Worked examples** (they agree with a second implementation, and the design session computed the first two
  again):

  | | |
  |---|---|
  | **1.** the first example header: background, length 1, number 1, acknowledge 0 | `02 01 01 00 00 00 00 00 EA 9A 70 42 A0 8C 06 E1` |
  | its header CRC, and the register's start | 0xE1068CA0, 0xE1078CA0 |
  | the data as held | `00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |
  | the data on the wire | `2A 54 C0 74 C0 1C 58 2F A6 5D 27 37 39 C9 47 1F` |
  | **2.** foreground, length 1, number 7, acknowledge 3 | `01 01 07 00 0C 00 00 00 EB 08 C9 D9 9E 94 2A 36` |
  | its header CRC, and the register's start | 0x362A949E, 0x362B949E |
  | the data as held | `00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F` |
  | the data on the wire | `A4 D0 6E 3C 10 A4 2F 89 F5 74 21 9E DE 43 E4 C0` |
  | **3.** the same data forwarded: hops 1, acknowledge 4 | `01 01 07 00 10 00 10 00 EB 08 C9 D9 A3 2B 76 11` |
  | the data on the wire | `79 F1 F2 82 77 D9 B4 AF 91 8F 7C 00 39 96 A1 51` |

  In the first the data is zeros, so the wire shows the sequence itself.
- **It also closes the false packet number,** for any packet with data: a header forged by noise takes the wrong
  mask off, and the data checksum fails (`cases/forged-number.case`). What is left of that limit is a forged
  header on a packet of length 0: a false acknowledgement, false stop bits, a false "resend" or "lost", none of
  which is silent harm.
- **The mask's limit, accepted:** data made on purpose for the header it will travel under.
  - What has to be known is the header's other fields: the stream, the length, the number, the acknowledge
    field, hops, the stop bits, and the four flags of the moment ("resend" is set whenever the sender owes
    one). The CRC follows from them and the data, and since the CRC and the sequence
    are both linear, the data can be solved for, with no search (WORKER8, `cases/data-made-on-purpose.case`).
  - On a quiet link those fields can be guessed well; with foreground messages going between, the number and the
    acknowledge field move in ways the data's author cannot see.
  - It still needs the line to read as zeros through that packet's header and no further.
  - It takes someone making the host's own data against the link, and the host is the end that is trusted. A
    value the author cannot know, in the mask's start, would close it: the value for each life of a link, set
    aside below. A sequence that is not linear would not: the solving becomes a search of 2³², which is minutes.
- The ways not taken: a random value for each life of a link, agreed at the restart, which a capture from the
  same life would still pass; and accepting it, with a rule that the host never sends link packets as data.

### The CRC as hardware (2026-11-17)

- **A CRC unit is small,** and can take any number of bits a cycle. For CRC-32C, counted from the polynomial
  (2-input XOR gates before any sharing; synthesis will share terms and come out lower; not synthesized):

  | bits a cycle | XOR gates | levels of logic | flip-flops |
  |---|---|---|---|
  | 1 | about 17, the feedback shared | 2 | 32 |
  | 8 | about 270 | 4 | 32 |
  | 32 | about 1,040 | 6 | 32 |
  | 128 | about 2,490 | 7 | 32 |

- **A byte a cycle is the fit for receiving:** the link is 8 lines, a byte a transfer, so the unit takes each
  byte as it arrives and is done when the packet is. The definition above is already byte by byte. The sending
  units are 128 bits a cycle, for the reason below.
- **Checking a header** (owner): bytes 0 to 11 go through the unit and the result is compared with bytes 12 to 15.
  - The same thing, simpler to build: all 16 bytes go through, and the result is compared with the constant
    0x48674BC7. The unit then never holds a result while four more bytes arrive.
  - Bytes 8 to 11 are kept as they pass: they are the data checksum.
- **Checking the data** (owner): the unit is set back to its starting value, **all ones, not zero,** the 16 ×
  length data bytes go through, and the result is compared with the four bytes kept. The constant does not serve
  here, since the checksum is in the header and not after the data.
  - A packet of length 0 has no bytes to put through: the result is 0x00000000, and the header must carry that.
- **Two fast units and two slow ones an ASIC** (owner, 2026-11-17), and four mask registers:

  | unit | how many | what it does |
  |---|---|---|
  | fast, 128 bits a cycle | 2, one for each direction it sends in | the pass over a packet's data before it leaves, then its header CRC |
  | slow, a byte a cycle | 2, one for each direction it receives in | checks each packet as it arrives: the header, then the data |
  | a mask register | 4, one beside each | a 32-bit register stepped a byte at a time with nothing fed in |

  - **The receiving units stay a byte wide:** the bytes arrive one at a time and are checked as they come.
  - **The two that receive cannot be one,** since both links can be receiving at once. The two that send could
    share, a full packet's pass being 32 cycles against 528 to send it; the owner took one for each direction.
  - **The fast unit does the header as well.** The header CRC covers 12 bytes, not a whole word: put 32 zero
    bits in front of the 96 and start the unit from the fixed value that those zeros bring to all ones,
    **0x641F6454** (WORKER8, checked on the first example header). The result is the CRC of the 12 bytes, in one
    cycle.
  - † The header is held until the pass over the data is done; the restart packet needs no pass, its checksum
    being a constant; a packet of length 0 may go while a packet with data is still being passed over.
  - † **The header's CRC while a pass is running** (WORKER8, for the RTL to settle): such a packet of length 0
    needs the fast unit for one cycle, in the middle of the pass. Either the pass waits a cycle, or the header's
    CRC has logic of its own and the unit's register is left alone. The CRC is not needed on the wire until 12
    cycles after the header starts, and no rule depends on the cycle: the searches hold with the pass at 128
    bits a cycle, at a byte a cycle and with none.
- **Every packet's data is checksummed before it is sent,** not as it goes: the data checksum is in the header,
  which goes first. So a sending unit passes over a packet's data once before the packet leaves, each time it is
  sent (the owner's ruling that both checks are made afresh).
  - **Why the sending unit is fast** (WORKER8's figures). At a byte a cycle the pass takes as long as the data,
    and it cost:

    | | no pass | a byte a cycle | 128 bits a cycle |
    |---|---|---|---|
    | a full foreground packet on an idle link, let out to delivered | 529 cycles | 1,041 | 561 |
    | the data rate in the random runs, bytes a cycle | 0.5703 | 0.4904, 14% less | 0.5751 |
    | the same with every source saturated | 0.8584 | 0.7138, 17% less | 0.8580 |
    | the longest a foreground packet waited for the line | 528 | 1,021 | 534 |

  - At 128 bits a cycle a full packet's pass is 32 cycles and a one-word message's is one. The unit is about
    2,490 XOR gates before sharing.
  - † The pass starts on the packet that will go next, and starts over if that changes: from the background to
    the foreground, or from an ASIC's own packet to one it forwards.

**The order a receiver tests in:**

1. the header CRC;
2. the header's fields: the stream is 1 or 2, the length is at most 32, the reserved bits are 0, and at an ASIC
   that must forward the packet, hops is not 15. What a failed field leads to is decided only after step 3: "a
   packet made wrong" needs the data checksum to have passed;
3. the data checksum;
4. the packet number, which means something only once the rest has passed.

## When a packet is bad

A receiver expects one number next on a link: the one after the last packet it took.

| what the receiver sees | what it does |
|---|---|
| a good packet with the number it expects, and room in its stream's queue | takes it; its acknowledge field moves on |
| the header good, the data checksum failing, the number the one expected or ahead | throws it away and sets **resend**. The link does not stop (owner) |
| the header good, the data checksum failing, the number behind | throws it away. Nothing is owed: it has that packet already |
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
- **"Sets resend" means in one header, the next to go,** not in every header until the packet comes. One that
  set it in every header would send the sender back over and over.
- **What ends "a resend is owed":** the packet taken, or a packet of length 0 whose number is the one expected,
  which shows the sender has gone back.
- **A receiver that has asked for a resend asks again at any sign that the sender is still ahead** (owner,
  2026-11-17, on WORKER8's finding), but no sooner than 2,048 cycles after it last asked: otherwise every
  packet already on its way sets off another going-back.
  - **The 2,048 cycles hold back only the asking that a packet ahead sets off.** A packet thrown away that
    carries the number expected (its data damaged, no room, or made wrong) is asked for at once, every time.
  - **Why 2,048** (WORKER8, 30 random runs each): packets sent to a receiver that already had them were 232 at
    128 cycles, 104 at 512, and none at 2,048 and above. Longer only lengthens the wait.
  - **The hole this closes:** the request can be lost. As WORKER8 first had it, the receiver asked again only
    when it saw a packet of length 0, and a sender busy with new packets sends none. The sender then stood still
    for the rest of its window. Two random runs of 3,777 failed that way on the runner (Quest 227, seeds 200137
    and 140162; `cases/seeds-227.case`); the host's window of 64 made the stall long enough to trip the check,
    and was not the cause.
- **The header of a packet sent again is written afresh,** with the acknowledge field and the flags of now
  (WORKER8).
- **A sender goes back exactly as far as it must:** in the random runs' 2 million packets no receiver was sent a
  packet it had already taken (WORKER8).

**A packet made wrong** (owner, 2026-11-17):

- **The first time, it is asked for again,** as any bad packet is. It could be noise: a damaged header that
  happened to pass its CRC. (With the mask that can only be so on a packet of length 0: on one with data, a
  forged header takes the wrong mask off and the data checksum fails first.) The owner: "a resend in that case is the right option. It just shouldn't fail the
  same way repeatedly."
- **If the packet sent again fails the same way,** the sender made it so, and asking again cannot help. At the
  second time running, on the same packet number, **the receiver starts the link again** ("Starting a link
  again"), with the error.
  - **How "the same packet again" is known: one flag** in each receiver. It is set by a packet that carries the
    number expected, passes both checks and fails a field test. It is cleared only when a packet is taken, or the link starts
    again. If it
    is already set when the same happens again, that is the second time.
    - Good packets that arrive behind the wrong one do not clear it: they are ahead of the number expected, so
      they are thrown away, not taken. With 5 made wrong and 6 and 7 good, the receiver throws away 5, 6 and 7,
      the sender goes back, and the 5 sent again is the next packet the receiver looks at.
    - The packet's checksums cannot be used to know it again: its header is written afresh when it is sent
      again, so the header CRC differs. The number expected already says which packet it is.
  - **The error is Ingress/Egress's,** `link_error` with `format_error` beside it to say why
    (`INGRESS-INTERFACE.md`, "Errors"), and so its tile's (owner).
  - Noise does not do this twice running: each time needs a damaged header to pass a 32-bit CRC.
  - The error is the point, not the restart. A sender that makes a wrong packet is broken, by a bug or by a chain
    wired longer than 16, and nothing saves the data. What is left to choose is a link that hangs, one that
    drops the packet and says nothing, or one that says so. On a chain the restart also keeps the link usable
    for the ASICs beyond.
- **A length over 32** cannot be passed over with any trust, so the receiver treats it as a lost place the first
  time, and counts it the same way: if its number field is the one expected, and not while the receiver is
  already lost.
- **A wrong packet whose number is not the one expected** is thrown away as ahead or behind, and not counted.
- **A packet of length 0 with a field that cannot be** is counted, if its number field is the one expected.
- **Two is the right count** (WORKER8): one would make every forged header on a packet of length 0 a restart.
  For full packets the second comes 1,056 cycles after the first; the restart then loses 128 words.
- A packet of length 0 that is counted is also asked for again, like one with data, though there is nothing to
  send again.
- `INGRESS-INTERFACE.md`'s `format_error` says that this was why the link started again.
- (WORKER8's rule told the two cases apart by the data: if the data's check passed as well, the sender was taken
  to have made the packet so, at the first time. But noise that damages only the header leaves the data whole,
  and that rule would have called it the sender's fault. The owner's count needs no such judgment.)

**Finding a packet's start:**

- **A header that reads as zeros is taken for idle,** and what follows it is then read as a header. That is safe
  only because the data is masked ("The data is masked on the wire"): unmasked, data that held a packet was
  taken for one.

- **Idle is zero bytes, in any number,** not only whole words: the first byte that is not zero starts a header.
- **"The first byte is never zero" is how a receiver finds a packet's start, not a check it can make.** A first
  byte that noise turns to zero is taken for idle; the receiver then fails a header CRC one byte later, which is
  the "lost my place" case.
- **After a failed header CRC a receiver takes nothing until it has seen a full packet's length of zeros in a
  row,** 528, which no packet can hold. Without this, and without the mask, it read the bad packet's data as
  headers, and one of them can pass: a packet was then lost for good (WORKER8).
  - With the mask the searches hold without this wait. It is kept: it spares a lost receiver a string of failed
    header CRCs, and it is the only guard against data made on purpose.

## Recovery

**The side that has lost its place** (owner):

1. finishes the packet it is sending;
2. sends 1,024 bytes of zeros;
3. sends a packet with **lost** set, its acknowledge field holding the last packet number it received correctly.
   It is a packet of length 0, with the sender's next number in its number field and resend not set;
4. **repeats 2 and 3, over and over, until a good recovery packet arrives** (owner). It sends no data while it
   is lost, only these rounds.

**The other side** (owner):

1. sends 1,024 bytes of zeros;
2. sends its **recovery** packet, and, once that has been heard, carries on from the packet after the
   acknowledged one;
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

**How long it takes** (WORKER8, the runner's campaign with the fast unit, Quest 237): over 257,035 recoveries,
from a link's last damaged byte to its next delivery, 669 cycles on average and 5,187 at most. That is every
kind of damage together. It is the figure that matters about noise (owner, 2026-11-18): not how long a line was
down, but how soon the link carries on once bytes get through again.

## Starting a link again (WORKER8; accepted to carry on with)

Recovery mends a lost place. It cannot mend two ends whose numbers no longer agree, and as first written nothing
could: the link stopped for ever.

**The restart flag** (bit 7 of the header's first byte):

| the flags | says |
|---|---|
| restart with lost | "I have started again" |
| restart with recovery | "so have I" |
| restart with lost and recovery | said by an end that is itself starting again and has heard the other's "I have started again": "so have I, and I too have started again" |

- **An end stops saying "I have started again"** when it hears a restart packet with recovery. **It stops
  answering** at a restart packet without lost, or at any ordinary packet.
- **An end that is starting again takes nothing from ordinary packets** until it has its answer.
- **It is sent in rounds after 1,024 zeros,** as "lost" is.

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
  - Its data goes to no queue and it takes no packet number. Its stream field is 1, its number field 1 (the
    number its sender's first packet will carry) and hops 0. Its acknowledge field and stop bits are written as
    in any header and not used by the receiver.
  - Its data is masked on the wire like any other.
  - A restart packet whose pattern or checksum is wrong is a damaged packet like any other.
- **An end that hears "I have started again"** drops the packets it was keeping, starts its numbers again from 1,
  answers, and **raises an error:** what was on its way is gone, and the frames above the link cannot be trusted.
  It does so for the first it hears only. The packet comes again every round, and an end that is already
  answering, or starting itself, does neither again.
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
    That is the error. With one forged header it is raised only when there is real harm.
  - It is always found when it matters, because the receiver always learns it is missing a packet and always
    asks.
  - An acknowledge field that claims more than was ever sent releases nothing, since there is nothing there to
    release. One that is behind the last one taken releases nothing and changes nothing.
  - **With two forged headers the error can be raised with nothing lost** (WORKER8, `cases/false-alarm.case`): a
    false acknowledgement, and then a false "resend" or "lost" before the packet lands. Two is far inside what
    is accepted, and the failure is loud, not silent.
  - (WORKER8's rule watched for acknowledgements going backwards, two in a row. After a false one the true ones
    are behind it whether or not anything was lost, so that rule would have raised the error with no harm done.
    The owner's needs no watching, and raises one only on a request it cannot meet.)
- **Each of these ends as an error the core sees,** never as a link that hangs. On the master tile a peripheral's
  error is the tile's, which stops everything (owner): the host resets the chip and the proof starts over.

- **"Asks for a packet no longer kept"** is a resend, or a "lost", whose acknowledge field is behind the last
  packet the sender let go.
- **What these cost** (WORKER8): both ends from power-on, 4,657 cycles to a link in step. One end powering on
  under a running link: the other raises its error 1,554 cycles later, and the link is up again after 3,648.

**The false packet number, which was a known limit, is closed by the mask** for any packet with data ("The data
is masked on the wire"). The owner had accepted it ("we can live with that") before the mask was thought of.

## Holding the host back: stop sending and resume sending

- **Decided (owner): the link has "stop sending" and "resume sending".**
- **They are for each stream.** Stopping the background stream, when the Updater stalls, leaves the foreground stream
  running. The owner can't imagine the foreground stream ever needing to stop, but it has the bit all the same.
- **They are one bit a stream, in every header:** each stream's `stop` flag, in every header a side sends, says
  what it wants now, 1 for "stop sending me this stream" and 0 for "send". Resuming is the bit going back to 0.
  A packet of either stream carries both bits.
- **"Stop" means: number no new packet of this stream.** A packet that already has a number is sent again when
  it is asked for, stopped or not ("One run of numbers for a link").
- **When the status is sent** (owner, 2026-11-17):
  1. **On a change, right then,** as a packet of length 0 if there is nothing else to send. A change is a stop
     bit, a resend that is owed, or a packet taken.
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
  - **512 is kept** (WORKER8's figures, 60 random runs each): the data rate does not depend on the timer, since
    the status only goes when the line is idle.

    | the timer | bytes of data a cycle | packets of length 0, of the data's bytes | the longest wait for an acknowledgement |
    |---|---|---|---|
    | 512 | 0.5751 | 7.64% | 2,727 cycles |
    | 2,048 | 0.5756 | 5.95% | 3,380 |
    | 8,192 | 0.5773 | 5.76% | 8,356 |

    The longest wait grows with the timer, which is the reason to keep it short.

- **An acknowledgement goes out as soon as the line is free** (owner's ruling on WORKER8's question), as a packet
  of length 0, not at the next status. It is what short foreground messages need: 400 one-word packets took
  31,023 cycles without it and 17,489 with, from power-on. The data rate does not change, since these packets go
  only when nothing else is going: they were 7.64% of the data's bytes with it and 4.03% without, on lines that
  were idle.
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
    8 as the chip has. WORKER8 measured the same data rate at 8, 16 and 64 (0.6389, 0.6392, 0.6392 bytes a
    cycle), and with 64 the queue filled and a packet was refused. With an acknowledgement for every packet the host seldom has more than two or three outstanding.
  - The fullest a queue of 1,024 words got in the random runs was 629; no packet was ever refused, there or in
    either of the runner's campaigns.
  - **In the runner's campaign with the fast unit the fullest was 953** (Quest 237). Traced (WORKER8): seed
    150397, a chain of 4, the first ASIC's background queue under a host whose window the campaign had drawn at
    64, which the line above rules out. No packet was refused. Not a finding.
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
  sender starts again from the oldest packet not acknowledged. A packet to be sent again goes before any new one
  of either stream.
- **No timer sends a packet again unasked** (WORKER8): the receiver always learns what it missed, from a number
  ahead of the one it expects, and asks. There is no such timer in the code that passes.
- **What is kept is the data;** the header and both checks are made anew each time a packet is sent (owner).
- **An ASIC sends on two links:** towards the host, its own packets and the ones it forwards; away from the host,
  the host's packets it passes down the chain. It keeps a window for each (owner, 2026-11-17).

## The link as hardware (WORKER8, 2026-11-18)

Counted from the model, register by register, in `v1.37/planning/updater/sessions/WORKER8/HARDWARE.md`: every
register with its flip-flops, every memory with its size and the reason, the units and when each runs, and what
the model does in one cycle that hardware must spread over several. Its tables are printed by a program
(`build/linkhw`) and checked against the document; the widths were checked against the model in a run of
3,000,000 cycles with noise. It counts state only: no registers added for timing, no synchronizers.

**One link end:** 630 flip-flops and 4,608 bytes of memory (the packet buffer, 512 bytes, and the 8 packets kept
for sending again, 4,096).

**One ASIC in the middle of a chain:**

| | how many | flip-flops | memory, bytes |
|---|---|---|---|
| a link end's registers | 2 | 1,260 | |
| a forwarding buffer's pointers and count | 4 | 164 | |
| the packet buffer of a receiver | 2 | | 1,024 |
| the kept packets of a sender | 2 | | 8,192 |
| a forwarding buffer's words, 16 KB each | 4 | | 65,536 |
| a forwarding buffer's marks | 4 | | 2,560 |
| **the link, in all** | | **1,424** | **77,312** |
| the chip's own queues behind it, 16 KB a stream (`INGRESS-INTERFACE.md`) | 2 | | 32,768 |
| **with those** | | | **110,080** |

- **85% of the link's memory is the four forwarding buffers.** The last ASIC on a chain uses its upper end only,
  and no forwarding buffer.
- **The size is not a concern** (owner, 2026-11-18): 100 KB is not an issue beside the Tracer's 768 KB.
- **What could be smaller,** if it ever is one:

  | | bytes saved | what it takes |
  |---|---|---|
  | the four forwarding buffers at 512 words, not 1,024 | 34,048 | nothing, but it is exactly at the rule's limit, as 8 KB is for a queue. In 30 random runs at that size the fullest got was 361 words and no packet was refused |
  | no forwarding buffer for the background towards the host | 17,024 | a new rule: a background packet with data that comes from below is a packet made wrong (Open) |
  | a window of 4 packets, not 8 | half the kept packets, and half the forwarding buffers' least size | data rate: 0.6252 bytes a cycle against 0.6389 |

- **What hardware must spread over cycles,** none of which changes what goes on the wire, only when, by 32
  cycles at most: handing a checked packet to the queue and the forwarding buffer a word a cycle; copying a new
  packet into its kept slot during the pass; comparing a restart packet's data with its table as it arrives.

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
- **The rest of the runner's runs with the fast sending unit** (Quests 238 to 242, WORKER8): the campaign with
  forged headers and flipped bits in memory, the larger searches, long noise, the long search again. The first
  campaign passed (Quest 237).
- **† A background packet with data that comes from below** (WORKER8). Towards the host the background carries no
  data, but nothing here says what an ASIC does with such a packet if one arrives. The model forwards it, through
  a 16 KB forwarding buffer that nothing else ever uses. Were it a packet made wrong, that buffer could go.
- **† The header's CRC while a pass is running** ("The CRC as hardware"): the pass waits a cycle, or the
  header's CRC has its own logic. For the RTL.
- **A neighbour that goes silent** (WORKER8). Nothing here notices a link whose other end has stopped: the sender
  waits for ever, which is right if the neighbour is slow and wrong if it is dead. A count of status periods with
  nothing heard would tell. **Not designed for now** (owner, 2026-11-17): it stays an open question. Any such
  count would have to outlast the longest noise, hundreds of milliseconds, so it is a matter of seconds.
  - **The same from the wire's side** (WORKER8): a line that reads as zeros for long looks idle. The end that
    hears nothing stays in step and waits, its window full. In the runner's campaigns a foreground packet stood
    ready 515,932 cycles that way with the slow unit and 384,187 with the fast (a chain of 16, one line read as
    zeros in bursts of up to 446,377 cycles); the link carried on when the wire did. The zeros are the test's
    own damage, and the wait is as long as the burst: nothing was lost or delivered wrong. A live end sends a
    header at least every 512 cycles, so an end that has heard none for some multiple of that could tell its
    owner.
- **The background behind the foreground** (WORKER8). A background packet waits while any foreground packet is
  ready, so a foreground that never pauses holds the background for as long. That is the order as decided, and
  not a flaw of the protocol; it is a thing to know.
- **The sizes of the forwarding buffers,** 16 KB as written: the rule asks for 8 KB, and the random runs do not
  press even that.
- **What the error is called and where it is seen:** a link that started again raises an error to the core:
  † `ERRORS.link_error` in Ingress/Egress, one bit for both of an ASIC's links (`INGRESS-INTERFACE.md`,
  "Errors"). Whether the core should be told which link, and why, is open there.
