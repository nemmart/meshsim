# The host link: machine specification, v1.37

*Chapter 28 of the machine specification (`spec/machine-spec.md`), which keeps a stub there. Section numbers are the
machine specification's. This chapter is the link that carries the host's frames (chapter 29) to the master tile's
Ingress/Egress (17.6), and the chip's replies back, along a chain of ASICs: the link packet, its checks, numbering,
sending again, recovery, starting a link again, "stop", the chain, and the link end as hardware. **The link end of
`updater_wip/link/` (`link_end.*`, `node.*`) is this chapter's authority** on what a link end does: this text states
what it does, and where the model does in one cycle what hardware spreads over several, what hardware does (28.13.6).
Designed here; not built in the RTL.*

### 28.1 The link

- **A link joins two ends:** the host and the first ASIC of a chain, or two neighbouring ASICs. It is **8 LVDS data
  lines each way,** one byte a transfer, about 1 GB/s each way. Byte alignment comes from the wires, so a receiver
  only ever has to find where a link packet starts.
- **A link cycle is one byte on the line** each way. Every count of cycles in this chapter (the timers, the waits,
  the figures) counts link cycles.
- **Idle is a zero byte.** A line with nothing to send sends zeros.
- **A 128-bit word is 16 bytes on the line,** its least significant byte first, as everywhere in the design.
- **Noise can last hundreds of milliseconds** (owner). The link comes back from it by itself (28.8), and nothing it
  does relies on any one link packet getting through.
- **Every link is a link of its own** (owner): the packet numbers, the acknowledgements, the flags, sending again,
  recovery, starting again and "stop" belong to one link. Only a link packet's stream, length, hops and data go
  from end to end along a chain (28.12).

### 28.2 Two streams

| Stream | Number | What it carries |
| --- | --- | --- |
| foreground | 1 | what the host sends for the proof the mesh is working on, and every reply from the chip |
| background | 2 | the frames that set up the next proof and start its tracing |

- **Neither may block the other** (owner). The background can stall for long, while the Updater waits on LPDDR or
  on free pages, and the foreground has to keep getting through. Each stream has its own queue at a receiver and
  its own stop bit (28.10); a stopped stream holds back only its own new link packets (28.4).
- **They interleave link packet by link packet.** A sender sends a foreground link packet before a background one
  (28.11), so a foreground link packet waits for the line for at most one background link packet.
- **A stream's number is never 0,** so the first byte of a link packet is never zero (28.3).
- **Towards the host, data is on the foreground alone** (owner). The background appears there only as link packets
  of length 0, which carry its stop bit and never leave their link.

### 28.3 The link packet

**A header of 16 bytes, then 0 to 32 128-bit words of data.** The header is one 128-bit word:

| Bits | Field | Meaning |
| --- | --- | --- |
| [3:0] | `stream` | whose data the link packet carries: 1 foreground, 2 background; never 0 |
| [4] | `resend` | "your last link packet was bad: send again from the number after my acknowledge field" (28.7) |
| [5] | `lost` | "I have lost my place" (28.8); with `restart`, "I have started again" (28.9) |
| [6] | `recovery` | the answer to "lost": the first link packet after the zeros (28.8); with `restart`, "so have I" |
| [7] | `restart` | the link is starting again (28.9) |
| [15:8] | `length` | the data's length in 128-bit words, 0 to 32 |
| [33:16] | `number` | the link packet's number on the link (28.4). In a link packet of length 0, the number the next link packet with data will carry |
| [51:34] | `ack` | the last number received in order on the link; 0 for none yet |
| [55:52] | `hops` | the ASICs that have forwarded the link packet along the chain (28.12). Whoever makes a link packet sends 0 |
| [56] | `fg_stop` | "stop sending me the foreground"; 0 is "send" |
| [57] | `bg_stop` | "stop sending me the background"; 0 is "send" |
| [63:58] | reserved | 0 |
| [95:64] | `data_checksum` | the CRC-32C of the data as it is held, before the mask (28.5) |
| [127:96] | `header_crc` | the CRC-32C of bytes 0 to 11 (28.5) |

**The same header byte by byte,** as it goes on the line: byte *i* holds bits [8*i* + 7 : 8*i*] and is the
(*i* + 1)th byte sent. The data follows, 16 bytes a word.

| Byte | Holds |
| --- | --- |
| 0 | `stream` in its low four bits; `resend`, `lost`, `recovery` and `restart` in bits 4 to 7. A background link packet with no flag is 0x02 |
| 1 | `length` |
| 2, 3 | `number` bits 0 to 15, low byte first |
| 4 | low two bits: `number` bits 16 and 17; high six bits: `ack` bits 0 to 5 |
| 5 | `ack` bits 6 to 13 |
| 6 | low four bits: `ack` bits 14 to 17; high four bits: `hops` |
| 7 | bit 0 `fg_stop`, bit 1 `bg_stop`; the rest 0 |
| 8–11 | `data_checksum`, low byte first |
| 12–15 | `header_crc`, low byte first |

- **The data is at most 32 words, 512 bytes:** a full link packet is 528 bytes, of which the header is 3.0 %.
  **A sender may always cut smaller** (owner); a receiver takes any length from 0 to 32.
- **A field that cannot be** (28.7): a `stream` other than 1 or 2; a reserved bit set; or, at an end that would
  forward the link packet (28.12), `hops` 15 on a link packet with data. A `length` over 32 is a lost place
  (28.7).

### 28.4 One run of numbers for a link

**A direction of a link has one run of numbers, shared by both streams** (owner): one acknowledgement, one
`resend`, one window and one set of kept link packets (28.11) for the direction.

- **Numbers run from 1 to 0x3FFFE, then 1 again.** None is 0 or 0x3FFFF. An `ack` of 0 is "none yet".
- **A number is given to a link packet when it is first sent,** whichever stream its data is on. A stream that is
  stopped has no new link packets numbered, and the other's stay consecutive.
- **Ahead and behind** are judged within half the numbers: *b* is ahead of *a* when it is 1 to 0x1FFFF steps after
  it, counting round; otherwise it is behind. A window of 8 is far inside that.
- **"Stop" holds back only link packets that have no number yet** (owner). **A link packet that already has a
  number is sent again whatever its stream's stop bit says.** Its room was counted when it was first sent: a queue
  says stop with room for a window of link packets still to land (28.10). So a damaged link packet of a stopped
  stream never holds the other stream's behind it.
- **Both stop bits are in every header,** whatever its stream: they are what is a stream's; the rest of the header
  is the link's or the link packet's.

### 28.5 The two checks

1. **The data is checksummed first,** and the result goes into the header.
2. **Then the header is checked,** the data checksum included: `header_crc` is the header's last field.

A link packet counts only when both pass: together a 64-bit check. On a link packet with data the mask (28.6) ties
the data to the header, so a header that noise has forged takes the wrong mask off and the data checksum fails; a
link packet of length 0 has the header CRC alone.

**Both are CRC-32C** (Castagnoli; RFC 3720, appendix B.4; owner):

| | |
| --- | --- |
| width | 32 bits |
| polynomial | 0x1EDC6F41; reflected, as the steps below use it, 0x82F63B78 |
| initial value | 0xFFFF_FFFF |
| input and output | reflected: each byte enters its least significant bit first |
| final XOR | 0xFFFF_FFFF |
| of the nine ASCII bytes "123456789" | 0xE306_9283 |
| of no bytes | 0x0000_0000 |
| of 16 zero bytes | 0x4270_9AEA |

```
crc = 0xFFFFFFFF
for each byte b, in order:
    crc = crc XOR b
    repeat 8 times:  crc = (crc >> 1) XOR 0x82F63B78   if crc's bit 0 is 1
                     crc =  crc >> 1                    otherwise
result = crc XOR 0xFFFFFFFF
```

- **`data_checksum`** is the CRC-32C of the link packet's data as it is held, all 16 × `length` bytes in the order
  they go on the line, before the mask. Of a link packet of length 0 it is 0x0000_0000, and it is checked.
- **`header_crc`** is the CRC-32C of header bytes 0 to 11.
- **A good header's residue:** the CRC-32C of all 16 bytes of a header whose `header_crc` is right is 0x4867_4BC7,
  whatever the header. A receiver may check a header so (28.13.4).
- **Both checks are made afresh whenever a link packet is made ready to send** (owner): a link packet of the
  sender's own, one it forwards, and one it sends again, from the data as it holds it. So the checks cover the
  line and not the memory: a bit that flips in a queue, a kept link packet or a forwarding buffer goes out with a
  checksum that matches it.

**Worked examples,** each header's 16 bytes, the data one zero word where it has data:

| | Header |
| --- | --- |
| 1. background, `length` 1, `number` 1, `ack` 0, no flags, `hops` 0 | `02 01 01 00 00 00 00 00 EA 9A 70 42 A0 8C 06 E1` |
| 2. the same after one hop, `hops` 1 | `02 01 01 00 00 00 10 00 EA 9A 70 42 9E BC 0A 21` |
| 3. foreground, `length` 1, `number` 0x2ABCD, `ack` 0x15555, `hops` 3, `resend` and `bg_stop` set | `11 01 CD AB 56 55 35 02 EA 9A 70 42 D2 BF BA 0A` |
| 4. length 0: stream 1, the next number 5, `ack` 4, `fg_stop` set | `01 00 05 00 10 00 00 01 00 00 00 00 57 09 4D 4E` |

### 28.6 The data is masked on the line

**Each byte of a link packet's data is sent XORed with a sequence that starts from that link packet's own
`header_crc`** (owner). A receiver that has read the header takes the mask off; one that has not sees bytes that mean
nothing. So whether the link works does not depend on what the data holds: data that is itself link packets (a
capture of a link, a file of test vectors) can never be taken for one.

```
reg = header_crc OR 0x00010000        header_crc: the 32-bit number in header bytes 12 to 15, byte 12 lowest
for each data byte d, in order:
    repeat 8 times:  reg = (reg >> 1) XOR 0x82F63B78   if reg's bit 0 is 1
                     reg =  reg >> 1                    otherwise
    the byte on the line is  d XOR (reg AND 0xFF)
```

- **The eight shifts are the CRC unit's step for a byte of zero:** the mask is a CRC-32C register fed nothing. The
  receiver runs the same from the header it has just read.
- **Bit 16 is set** because the register has two states that stand still, 0 and 0xFCA4_2DAF; with it set no
  `header_crc` starts in either, and from any other state the register returns only after 2³¹ − 1 shifts.
- The header is not masked, so its first byte is still never zero. `data_checksum` is over the data as held. The
  restart link packet's data is masked like any other. A forwarding ASIC takes the mask off as it checks a link
  packet and puts a new one on under its new header (28.12).
- **The mask's limit, accepted** (owner): data made on purpose for the header it will travel under. The CRC and the
  sequence are linear, so such data can be solved for, but it needs the header's other fields of that moment and
  the line reading as zeros through exactly that header. It takes the host's own data made against the link.

**Worked examples:**

| | |
| --- | --- |
| **1.** example header 1 of 28.5 | `02 01 01 00 00 00 00 00 EA 9A 70 42 A0 8C 06 E1` |
| its `header_crc`, and the register's start | 0xE106_8CA0, 0xE107_8CA0 |
| the data as held | `00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |
| the data on the line: the sequence itself | `2A 54 C0 74 C0 1C 58 2F A6 5D 27 37 39 C9 47 1F` |
| **2.** foreground, `length` 1, `number` 7, `ack` 3 | `01 01 07 00 0C 00 00 00 EB 08 C9 D9 9E 94 2A 36` |
| its `header_crc`, and the register's start | 0x362A_949E, 0x362B_949E |
| the data as held | `00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F` |
| the data on the line | `A4 D0 6E 3C 10 A4 2F 89 F5 74 21 9E DE 43 E4 C0` |
| **3.** the same data forwarded: `hops` 1, `ack` 4 | `01 01 07 00 10 00 10 00 EB 08 C9 D9 A3 2B 76 11` |
| the data on the line | `79 F1 F2 82 77 D9 B4 AF 91 8F 7C 00 39 96 A1 51` |

### 28.7 The receiver

**Finding a link packet's start.** Between link packets, zero bytes are idle, in any number. **The first byte that
is not zero starts a header.** That a header's first byte is never zero is how a receiver finds a start, not a
check: a first byte that noise turns to zero is taken for idle, and the header CRC then fails one byte later.

**The order of the tests,** as the bytes arrive:

1. **`header_crc`,** when the header's 16th byte is in. A failure: the receiver has **lost its place** (28.8).
2. **The fields.** A `length` over 32 under a good `header_crc`: the receiver cannot know how far to pass over, so
   it is a lost place, counted as a link packet made wrong (below) if its `number` is the one expected. A field that
   cannot be (28.3): the data is still passed over and checked, and not kept; what it leads to is decided after
   step 3.
3. **`data_checksum`,** when the data's last byte is in, the mask taken off as it arrived.
4. **The `number`,** which means something only once the rest has passed.

**A receiver expects one number next on a link:** the one after the last link packet it took. What it does:

| What it sees | What it does |
| --- | --- |
| a good link packet with the number expected, and room for all its words in its stream's queue | **takes it;** its `ack` moves on |
| a good link packet with the number expected and no room | throws it away and does not acknowledge it; a resend is owed. With the sizes right this never happens |
| a good link packet whose number is **behind** | throws it away, and acknowledges as usual: it has it already |
| a good link packet whose number is **ahead** | throws it away; a resend is owed: one was missed |
| a link packet of length 0 whose number is ahead | a resend is owed: the last link packet of a burst was lost. So a sender needs no timer to send again |
| a link packet of length 0 whose number is the one expected | nothing is owed any more: the sender has gone back |
| the data checksum failing, the number the one expected or ahead | throws it away; a resend is owed. The link does not stop (owner) |
| the data checksum failing, the number behind | throws it away; nothing is owed |
| `header_crc` failing | a lost place (28.8) |
| both checks passing, and a field that cannot be | a link packet made wrong (below) |

- **The `ack` field and the stop bits of every good link packet are taken,** whether or not its data is (28.11).
- **"A resend is owed"** sets `resend` in **one** header, the next to go, not in every header until the link packet
  comes, which would send the sender back over and over. The debt ends when the link packet is taken, or a link
  packet of length 0 with the number expected comes.
- **Asking again.** A receiver that has asked asks again at any sign that the sender is still ahead (a link packet
  of either length whose number is ahead), but no sooner than **2,048 cycles** after it last asked: otherwise every
  link packet already on its way would set off another going back. The wait holds back only what a link packet
  ahead sets off. **A link packet thrown away that carries the number expected** (its data damaged, no room, made
  wrong) is asked for at once, every time.
- **The data of a good link packet taken** goes to its stream's queue, or into a forwarding buffer (28.12), a 128-bit
  word at a time (28.13.6); it is acknowledged at once.

**A link packet made wrong** (owner): both checks pass and a field cannot be.

- **The first time, it is asked for again,** as any bad link packet is: it may be noise, a damaged header that
  happened to pass its CRC, which on a link packet with data the mask makes possible only for length 0.
- **The second time running, on the same number, the receiver starts the link again** (28.9), with the error: the
  sender made it so, and asking again cannot help.
- **One flag in the receiver tells,** `wrong`: set by a link packet that carries the number expected, passes both
  checks and fails a field test; cleared only when a link packet is taken, or the link starts again. Set already
  when it happens again: the second time. Good link packets behind the wrong one are ahead of the number expected,
  so they are thrown away and do not clear it.
- **A wrong link packet whose number is not the one expected** is thrown away as ahead or behind, and not counted.
  **One of length 0** with the number expected is counted, and asked for again.

**After a failed header CRC** a receiver takes nothing until it has seen **528 zeros in a row,** a full link
packet's length, which no link packet holds: its data is at most 512 bytes. It spares a lost receiver a string of
failed header CRCs in the bad link packet's data.

### 28.8 Recovery

**The end that has lost its place** (owner):

1. finishes the link packet it is sending;
2. sends 1,024 zero bytes;
3. sends a link packet of length 0 with `lost` set, its `ack` the last number it received in order, its `number` the
   next it will send, `resend` clear;
4. repeats 2 and 3 until a good link packet with `recovery` arrives. It sends no data while it is lost.

**The other end:**

1. sends 1,024 zero bytes;
2. sends a link packet of length 0 with `recovery` set, its `number` the next it will send, and, once that has been
   heard, carries on from the link packet after the one the "lost" acknowledged;
3. repeats 1 and 2 until it hears a good link packet without `lost`. It sends no data until its answer has been
   heard.

- **Both ends lost at once:** an end that is itself lost and hears "lost" answers it, with `lost` and `recovery`
  both set in one link packet. It stops answering at the first good link packet without `lost`.
- **While lost,** a good link packet's data is not taken; its `ack` and its stop bits are.
- **An `ack` counts in any state:** every good link packet's releases what it covers. A "lost" link packet's `ack`
  is also where the sender goes back to.
- **The recovery link packet states its number,** which the receiver checks against the one it expects; it never
  takes its place from it.
- **Why the zeros work:** 1,024 bytes is longer than the longest link packet, 528 bytes, so whatever length a
  confused receiver believes it is inside, it has run out before the zeros end; a zero byte is never a header's
  first; and the first byte that is not zero starts a header.
- **Nothing in the exchange is relied on to arrive:** both ends repeat.
- **How long it takes** `[meas: the model]`: from a link's last damaged byte to its next delivery, 669 cycles on
  average and 5,187 at most, over 257,035 recoveries of every kind of damage on chains of 1 to 16.

### 28.9 Starting a link again

Recovery mends a lost place. **Starting again** mends two ends whose numbers no longer agree.

| The flags of a restart link packet | Says |
| --- | --- |
| `restart` and `lost` | "I have started again" |
| `restart` and `recovery` | "so have I" |
| `restart`, `lost` and `recovery` | said by an end that is starting again itself and has heard the other's: "so have I, and I too have started again" |

**The restart link packet** is a full link packet (owner): `length` 32, `stream` 1, `number` 1, `hops` 0, its data the
first 32 primes as bytes, 2 to 131, sixteen times over, two 128-bit words repeated:

```
02 03 05 07 0B 0D 11 13 17 1D 1F 25 29 2B 2F 35
3B 3D 43 47 49 4F 53 59 61 65 67 6B 6D 71 7F 83
```

Its `data_checksum` is 0xC898_1D21. Its `ack` and stop bits are written as in any header and not used by the
receiver; its data goes to no queue and it takes no number; it is masked like any other.

- **An end acts on one only if the header, the data checksum and the pattern all agree.** Anything less is a damaged
  link packet. Noise cannot make one; "lost" and its answer stay 16 bytes, since they must get through a noisy line.
- **It is sent in rounds after 1,024 zeros,** as "lost" is.
- **An end that hears "I have started again"** drops the link packets it was keeping, numbers again from 1, answers,
  and **raises its error** (28.14): what was on its way is gone, and the frames above the link cannot be trusted. It
  does so at the first it hears; an end already answering, or starting itself, does neither again.
- **An end stops saying "I have started again"** when it hears a restart link packet with `recovery`, and **stops
  answering** at a restart link packet without `lost`, or at any ordinary good link packet. An end that is starting
  again takes nothing from ordinary link packets until it has its answer. An end that answers a restart and has
  lost its place says "lost" first.
- **A link carries nothing until both ends are up.** Two ends that power on together bring the link up with no
  error.

**What starts a link again,** each ending in an error the core sees (17.6), never in a link that hangs:

| The cause | Noticed by |
| --- | --- |
| one end is reset or powers on, or the FPGA is loaded again, while the other runs | the newcomer: it starts with `restart` set (28.14) |
| a link packet made wrong, twice running | the receiver (28.7) |
| the other end asks for a link packet this end no longer keeps: a `resend`, or a "lost", whose `ack` is behind the last this end let go | the sender, which cannot send it |

- **A false acknowledgement** (a damaged header that passed its CRC and released link packets still on their way)
  is not watched for (owner). Usually they arrive whole, and no harm is done. If one is damaged, the receiver asks
  for it, the sender no longer has it, and that is the error: it is always found when it matters. With two forged
  headers the error can be raised with nothing lost; far inside what is accepted, and loud.
- **An `ack` that claims more than was ever sent,** or is behind the last one taken, releases nothing.
- **What it costs** `[meas: the model]`: both ends from power-on, 4,657 cycles to a link in step; one end powering on
  under a running link, the other raises its error 1,554 cycles later, and the link is up again after 3,648.

### 28.10 Holding the sender back: "stop"

- **Each stream has a stop bit in every header,** `fg_stop` and `bg_stop` (owner): 1 "stop sending me this
  stream", 0 "send". Resuming is the bit going back to 0.
- **"Stop" means: number no new link packet of this stream.** A link packet with a number is sent again when asked,
  stopped or not (28.4).
- **"Stop" is for each link** (owner): it goes to the neighbour, not to the host. An ASIC whose background queue
  fills stops its neighbour up the chain, whose forwarding then backs up and stops the one above it: a chain's
  background moves at its slowest ASIC's pace, and the foreground still gets through.
- **When a stream says stop:** at **half its queue** (owner): Ingress/Egress's queue of 16 KiB says stop with 8 KiB
  still free (17.6); a chain node's stop bit for a link is up when its own queue or the forwarding buffer behind
  that link is half full (28.12).
- **The window is no more than half a queue** (28.11): with the stop bit and the acknowledgements both lost, a sender
  goes on to the end of its window, and the half kept free takes it. The chip's window of 8 full link packets is
  4 KiB, a quarter of a queue.

**When the status is sent** (owner):

1. **On a change, right then,** as a link packet of length 0 if there is nothing else to send: a stop bit changed, a
   resend owed and not yet asked, or a link packet taken (**an acknowledgement goes as soon as the line is free**).
2. **A status timer** runs in each direction from the end of the last link packet sent; any link packet starts it
   again.
3. **At 512 cycles** the status is sent again, one link packet of length 0, which starts the timer again.

So an end is never more than 512 cycles of silence from telling the other where it stands, and no "send" can be
lost for good. One timer a direction is enough: every link packet carries the link's `ack` and both stop bits.

### 28.11 The sender

- **The window: 8 link packets** for each direction an end sends in, of both streams together, about 4 KiB. A sender
  keeps every link packet's data until it is acknowledged, and numbers no new one while 8 are kept.
- **Sending again is go-back-N** (owner): at a `resend`, or a "lost", the sender goes back to the oldest link packet
  not acknowledged and sends from there. A link packet to be sent again goes before any new one.
- **No timer sends a link packet again unasked:** the receiver always learns what it missed, from a number ahead of
  the one it expects, and asks.
- **What is kept is the data alone:** the header and both checks are made afresh each time a link packet is sent,
  with the `ack`, the stop bits and `resend` of that cycle.
- **What it sends,** each time the line is free:

  | | When | What |
  | --- | --- | --- |
  | 1 | the link is not in step (28.8, 28.9) | a round: 1,024 zeros, then the round's link packet |
  | 2 | a link packet with data is next and the pass over its data is done | that link packet: one to be sent again; else a new one, the foreground's before the background's, a new one only if its stream is not stopped and fewer than 8 are kept |
  | 3 | the status is due (28.10) | a link packet of length 0 |

- **The pass.** The data checksum is in the header, which goes first, so the sender passes over a link packet's data
  once before the link packet leaves, each time it is sent. Its fast unit takes 128 bits a cycle (28.13.4), so a full
  link packet's pass is 32 cycles, and it runs while the link packet before it is going out. **The pass starts over**
  when what goes next changes before it is done: a resend asked for, a stop bit, the link losing step, or a
  forwarded link packet coming in front of the ASIC's own. **The header waits for the pass.** A link packet of length
  0 needs no pass and may go while one runs; the restart link packet needs none, its checksum a constant.

### 28.12 The chain

The ASICs sit on a daisy chain from the FPGA, probably no more than 4 (owner), at most 16. A link packet from the
host passes through the ASICs before its own; one to the host passes through the ASICs between its maker and the
host. **Every link of the chain is a link of its own** (28.1).

| | Belongs to |
| --- | --- |
| `number`, `ack`, the flags, the stop bits | the one link the header is on |
| sending again, recovery, starting again, "stop" | the one link: a damaged link packet is sent again only over the link that damaged it |
| a link packet of length 0 | the one link: it is never forwarded |
| `data_checksum`, `header_crc`, the mask | the one link: made afresh at every hop |
| `stream`, `length`, `hops`, the data | the link packet, end to end |

**A chain node,** an ASIC on the chain, has two link ends: the **upper,** towards the host, and the **lower,** away
from it.

- **The pin `chain_next`** (18) says another ASIC follows on the chain (owner): a fact of the board, known before any
  firmware runs. With it 0, the lower end is never clocked and sends zeros, and nothing is forwarded down. A node
  must be told: a lower link that comes up late, through noise, would look like no link, and the node would take
  the host's link packets without passing them on.
- **A link packet taken at the upper end** goes, in the same cycle, to the ASIC's own queue for its stream (17.6)
  and, with `chain_next`, into the **forwarding buffer down** for its stream, `hops` plus one. A link packet is
  taken into both or into neither: the room the upper end has for it is the smaller of the queue's and the
  buffer's.
- **A link packet taken at the lower end** goes into the **forwarding buffer up** for its stream, `hops` plus one.
- **A node acknowledges a link packet when it has taken it in,** into its queue and its forwarding buffer, not when
  it has passed it on (owner): from then on it is the node's to send.
- **Forwarding**, in either direction, is what any sender does with a link packet it holds (28.11): the outgoing
  link's next number, its `ack`, flags and stop bits; `hops` as the buffer holds it; the data checksum and the
  header CRC made afresh; the data under a new mask.
- **`hops`:** whoever makes a link packet sends 0, and every ASIC that forwards it adds one. **Towards the host it
  says which ASIC made the link packet,** its place on the chain: the FPGA sorts what arrives by it and rebuilds
  each ASIC's replies; no ASIC needs to know its own place. **From the host** every ASIC gets the same data, and the
  ASIC that takes a link packet for itself ignores `hops`. An end that would forward a link packet with data tests
  it: 16 does not fit, so `hops` 15 there is a field that cannot be (28.3). `hops` is under the header CRC, so a
  flipped bit cannot credit a link packet to the wrong ASIC.
- **What a node sends:**
  - **towards the host,** a link packet it is forwarding before one of its own, filling the gaps with its own
    (owner); its own on the foreground only. So the ASIC nearest the host waits longest, behind every ASIC further
    down;
  - **away from the host,** what the host sent, from the forwarding buffers down, the foreground's before the
    background's; it makes no link packets of its own that way.
- **An ASIC's link packets stay in order** end to end, since every link delivers in order; different ASICs'
  interleave.
- **A background link packet with data that comes from below** is forwarded up, through the fourth forwarding
  buffer, though nothing sends one *(item 185)*.
- **The stop bits of a node:** the upper end's, for each stream, are up when the ASIC's own queue or the forwarding
  buffer down is half full; the lower end's when the forwarding buffer up is half full.
- **The last ASIC** uses its upper end alone, and no forwarding buffer.

### 28.13 The link end as hardware

The state below is the model's, register by register, at the real sizes. It counts state only: no registers added
for timing, no synchronizers, one clock, the link cycle (28.1).

#### 28.13.1 What an ASIC's link is made of

| Part | How many | What |
| --- | --- | --- |
| a link end | 2 | the upper and the lower: each a receiver, a sender and the link's mode |
| a forwarding buffer | 4 | whole link packets waiting to go on: down the chain and up it, one for each stream, 16 KiB each |
| `chain_next` | 1 | the pin (28.12) |
| the ASIC's own queues | 2 | Ingress/Egress's, one for each stream (17.6): not the link's, but its stop bits come from them |
| the ASIC's own link packets | 1 | Egress's outgoing queue (17.6), on the foreground |

**A link end's signals:**

| Signal | Bits | Direction | What |
| --- | --- | --- | --- |
| the byte in, the byte out | 8 and 8 | in, out | the line, a byte a cycle each way; idle is zero |
| say stop | 1 a stream | in | the stream's stop bit: this side cannot take much more |
| room | 11 a stream | in | the words a link packet arriving now could be given. A link packet is taken only if there is room for the whole of it |
| forwards | 1 | in | a link packet received here would be forwarded, so `hops` 15 cannot be. The upper end: `chain_next`; the lower end: 1 |
| a link packet offered | 10 a stream, and its words | in | for each stream, the next link packet's `length` (6) and `hops` (4), taken with one strobe a stream |
| a link packet handed over | 12, and its words | out | a strobe (1), with the link packet's stream (1), `length` (6) and `hops` (4) |
| error | 1 | out | sticky: the link started again under this end (28.9) |
| in step | 1 | out | none of `lost`, `answer`, `restart`, `restart_answer` |

#### 28.13.2 The registers of a link end

**The receiver:**

| Register | Bits | Memory, bytes | What it holds |
| --- | --- | --- | --- |
| `rx_state` | 3 | | HUNT, HEADER, DATA, SKIP or FLUSH |
| `rx_count` | 10 | | the bytes so far of the header (to 16) or of the data (to 512) |
| `rx_crc` | 32 | | the slow unit's register |
| `rx_mask` | 32 | | the mask register for the data arriving |
| `zero_run` | 10 | | the zero bytes in a row on the line, stopping at 528 |
| `rx_hdr` | 96 | | the header's bytes 0 to 11 until the link packet is judged. Bytes 12 to 15 need no place: they go into the unit and into `rx_mask` as they arrive. The data's length in bytes is 16 × its `length` |
| `rx_valid` | 1 | | a link packet is being handed to the owner |
| `rx_stream`, `rx_length`, `rx_hops` | 1, 6, 4 | | of the link packet handed over |
| `rx_buf` | | 512 | **the packet buffer:** the data of the one link packet being checked |
| `last` | 18 | | the last number taken in order, 0 for none yet: the `ack` this end sends |
| `need` | 1 | | a resend is owed |
| `asked` | 1 | | `resend` has gone out for it |
| `ask_timer` | 12 | | cycles since it went, stopping at 2,048 |
| `wrong` | 1 | | the link packet expected has failed a field test once |
| `ack_owed` | 1 | | a link packet was taken since the last header sent |
| `stop_sent` | 2 | | the stop bits in the last header sent, to see a change |
| **in all** | **231** | **512** | |

**The sender:**

| Register | Bits | Memory, bytes | What it holds |
| --- | --- | --- | --- |
| `tx_state` | 2 | | IDLE, HEADER, DATA or ZEROS |
| `tx_count` | 11 | | the bytes sent of the header (to 16), the data (to 512) or the zeros (to 1,024) |
| `tx_hdr` | 128 | | the header going out, latched when it starts, since the fields behind it can change while it goes |
| `tx_slot` | 3 | | which kept link packet is going out |
| `tx_data`, `tx_pattern` | 1, 1 | | it has data; the data is the restart link packet's, from its table |
| `tx_mask` | 32 | | the mask register for the data going out |
| `tx_byte` | 8 | | the line |
| `acked` | 18 | | the last `ack` taken: the oldest kept link packet is the one after it |
| `next` | 18 | | the number that goes on the line next: behind `tail` while link packets are sent again |
| `tail` | 18 | | the number the next new link packet takes |
| `head` | 3 | | the slot of the oldest kept link packet |
| `peer_stop` | 2 | | the other end's stop bits, from the last good header |
| `meta` | 88 | | for each of the 8 kept link packets: its stream (1), `length` (6) and `hops` (4) |
| `data` | | 4,096 | **the kept link packets:** 8 slots of 32 128-bit words |
| `prep_key` | 5 | | which link packet the fast unit has passed over: none, one to send again, or a new one (2); its slot, or its stream and whether forwarded or the ASIC's own (3) |
| `prep_left` | 6 | | the cycles of that pass still to go, 32 at most |
| **in all** | **344** | **4,096** | |

**The link's mode and timer:**

| Register | Bits | What it holds |
| --- | --- | --- |
| `lost` | 1 | this end has lost its place |
| `answer` | 1 | the other end has said "lost": this end answers with recovery rounds |
| `restart` | 1 | this end has started again and waits for the answer |
| `restart_answer` | 1 | the other end has started again: this end answers |
| `error` | 1 | **the error output,** sticky until the reset |
| `round` | 1 | in a round: the zeros next, or the link packet |
| `due` | 1 | a status is due: the timer ran out, or the link came back into step |
| `status_timer` | 9 | cycles since the end of the last link packet sent, to 512 |
| **in all** | **16** | |

**What hardware has beside the model's registers:**

| Register | Bits | What it holds |
| --- | --- | --- |
| the fast unit's register | 32 | the CRC so far of the pass, then the data checksum for the header |
| the handing-over counter | 6 | which word of the packet buffer is going to the queue and the forwarding buffer |
| the pattern flag | 1 | a restart link packet's data has matched the table so far |
| **in all** | **39** | |

**One link end: 630 flip-flops and 4,608 bytes of memory.**

- **At the reset** every register is zero but three: `next` and `tail` are 1, and `restart` is set, so an end that
  is reset says "I have started again" and waits for the answer (28.14).
- **Within a cycle the receiver acts first, then the timers, then the sender,** each seeing what the one before it
  wrote; where two write one register, the later wins. So a link packet taken in a cycle is acknowledged by a header
  the sender starts in that cycle.

#### 28.13.3 The states

**The receiver** (`rx_state`):

| State | What it does | Leaves |
| --- | --- | --- |
| HUNT | between link packets: zeros are idle | on a byte that is not zero, to HEADER |
| HEADER | collects 16 bytes through the slow unit | on the 16th: `header_crc` fails, to FLUSH, and the end is lost; `length` over 32, to FLUSH likewise; a field that cannot be, to SKIP; otherwise to DATA. With `length` 0 the link packet is judged at once, and to HUNT |
| DATA | collects 16 × `length` bytes, the mask taken off, through the slow unit | on the last byte the link packet is judged, and to HUNT |
| SKIP | the same for a link packet with a field that cannot be: checked, and not kept | on the last byte it is judged, and to HUNT |
| FLUSH | takes nothing | after 528 zeros in a row, to HUNT |

**Judging a link packet** whose `header_crc` passed, in this order:

| | What | Then |
| --- | --- | --- |
| 1 | a restart link packet | acted on only if its `length` is 32, its checksum passes and its data is the table's; else thrown away as damaged |
| 2 | the data checksum fails | thrown away; a resend owed if its number is the one expected (at once) or ahead (after the 2,048 cycles) |
| 3 | a field that cannot be, both checks passing | the number expected: `wrong` set and a resend owed; `wrong` already set: the error, and the link starts again. Another number: thrown away |
| 4 | `ack`, the stop bits, `resend` and `lost` | taken, in any mode but while this end is starting again. A `resend` or a "lost" whose `ack` is behind the last one taken: the error, and the link starts again |
| 5 | the number, of a link packet with data | the one expected and room: taken, `wrong` cleared. No room: thrown away, a resend owed. Ahead: thrown away, a resend owed. Behind: thrown away, acknowledged |
| 6 | the number, of a link packet of length 0 | ahead of the one expected: a resend owed. The one expected: nothing owed any more |

**The sender** (`tx_state`): IDLE; HEADER, 16 bytes; DATA, 16 × `length` bytes, the mask put on; ZEROS, 1,024 bytes.
From IDLE it starts what 28.11's table says, in that order.

**The link's mode,** four flags:

| Flag | Set by | Cleared by | While set, the round's link packet |
| --- | --- | --- | --- |
| `lost` | a `header_crc` that fails; a `length` over 32 | a good link packet with `recovery`; a good restart link packet | "lost" |
| `answer` | a good link packet with `lost` | a good link packet without `lost` | "recovery" (with `lost` too, if this end is lost) |
| `restart` | the reset; a link packet made wrong twice; asked for a link packet no longer kept | a restart link packet with `recovery` | the restart link packet with `lost` (and `recovery`, if answering) |
| `restart_answer` | a restart link packet with `lost`; if this end was not starting itself, it drops what it kept, numbers from 1 and raises the error | a restart link packet without `lost`; any ordinary good link packet | the restart link packet with `recovery`; "lost" first, if this end is lost |

With no flag set the link is **in step,** and only then does data go. Coming back into step, a status is sent at once.

#### 28.13.4 The CRC units and the mask registers

An ASIC has **two fast units, two slow units and four mask registers** (owner): at each link end one of each unit,
and a mask register for each direction. All compute CRC-32C (28.5).

| Unit | Where | Starts | Runs | Gives |
| --- | --- | --- | --- | --- |
| **slow, a byte a cycle** (`rx_crc`) | each receiver | at all ones at a header's first byte | a byte a cycle through the header's 16 bytes | after the 16th, the result must be 0x4867_4BC7 |
| | | at all ones again after the header's 16th byte | a byte a cycle through the data, after the mask is off | after the last byte, the result must be `data_checksum`; of no data, 0 |
| **the receiver's mask** (`rx_mask`) | each receiver | after the header's 16th byte: bytes 12 to 15 with bit 16 set | once for each data byte, before the byte is used | its low 8 bits, XORed onto the byte from the line |
| **fast, 128 bits a cycle** | each sender | at all ones when the link packet that goes next is not the one it has passed over | a word a cycle over that link packet's data, 1 to 32 cycles, while the link packet before goes out or the line is idle | `data_checksum` |
| | | when a header is written: at **0x641F_6454** | one cycle: 32 zero bits, then the header's bytes 0 to 11 | `header_crc`: the 32 zeros take 0x641F_6454 to all ones, so the result is the CRC of the 12 bytes |
| **the sender's mask** (`tx_mask`) | each sender | when the header is written: its `header_crc` with bit 16 set | once for each data byte as it leaves | its low 8 bits, XORed onto the byte from the kept link packet |

- **The receiving units are a byte wide:** the bytes arrive one at a time and are checked as they come. The two
  cannot be one, since both links can be receiving at once.
- **The sending units are 128 bits wide** (owner), so that the pass costs a full link packet 32 cycles and a one-word
  message one; at a byte a cycle it would double a full link packet's time on an idle line. One is about 2,490 XOR
  gates before sharing, against about 270 for a byte.
- **`header_crc` during a pass.** A link packet of length 0 may start while the fast unit is passing over the next
  link packet's data, and needs its `header_crc`. **The requirement:** `header_crc` is on the line by the header's
  byte 12. Whether the pass waits a cycle for the unit, or the header's CRC has logic of its own, is the RTL's: no
  rule depends on the cycle.

**The constants:**

| | |
| --- | --- |
| a good header through the slow unit, all 16 bytes | 0x4867_4BC7 |
| the fast unit's start for a header | 0x641F_6454 |
| the bit set in a mask's start | 0x0001_0000 |
| the restart link packet's data | the first 32 primes as bytes, sixteen times over: 32 words |
| the restart link packet's `data_checksum` | 0xC898_1D21 |
| the zeros of a round | 1,024 bytes |
| the zeros a lost receiver waits for | 528 bytes |
| the status timer | 512 cycles |
| the wait before asking again | 2,048 cycles |
| the window | 8 link packets |
| a link packet's data, at most | 32 128-bit words, 512 bytes |
| hops, at most | 15 |

#### 28.13.5 The memories

| Memory | At | Bytes | Why that size |
| --- | --- | --- | --- |
| the packet buffer | each receiver | 512 | one link packet of the most data: it can be used only once all its data has been through the unit |
| the kept link packets | each sender | 4,096 | the window: 8 link packets of 32 words, each kept until acknowledged |
| a forwarding buffer | four a node | 16,384 | twice the least the rule asks: at least twice the window of the end that feeds it, 512 words, since "stop" is said at half full and a sender may still send to the end of its window |
| its marks | beside each | 640 | 5 bits a word: the last word of its link packet (1), and the link packet's `hops` (4) |
| the ASIC's own queues | Ingress/Egress, two | 16,384 each | "stop" at half; the window of 4 KiB fits in what is left with room to spare (17.6) |

| Memory | Written | Read |
| --- | --- | --- |
| the packet buffer | a byte a cycle, as the data arrives | a word a cycle as the link packet is handed over: 32 cycles for a full one |
| the kept link packets | a word a cycle during a new link packet's pass: the pass is also the copy into its slot | a word a cycle during the pass over a link packet to be sent again, and a byte a cycle as a link packet goes out: both at once, since the next pass runs while the link packet before goes out |
| a forwarding buffer | a word a cycle, as a link packet is handed over | a word a cycle, during the pass |

- **One packet buffer is enough:** handed over a word a cycle, it empties 16 times as fast as the next link packet
  fills it, and from 16 cycles ahead.
- **A pass that starts over** leaves part of a new link packet in a slot; the slot is not yet counted as kept, and
  the next pass writes over it.
- **The restart link packet's data is a table of 32 bytes,** not a memory.

**A forwarding buffer's registers:** a read pointer (10), a write pointer (10), the whole-packet pointer, the write
pointer after the last whole link packet, since only whole link packets are offered (10), and the words held (11), for
"half full" and the room a link packet needs: 41 flip-flops.

#### 28.13.6 What hardware spreads over cycles

| | The model | Hardware |
| --- | --- | --- |
| 1 | hands a link packet's data to the queue and the forwarding buffer in the cycle of its last byte | a word a cycle, 32 at most: the handing-over counter. The link packet is acknowledged at once; it can be offered for forwarding only when it is whole in the buffer, 32 cycles later at most |
| 2 | copies a new link packet's data into its kept slot in the cycle its header starts | during the pass, a word a cycle |
| 3 | computes the data checksum in one step when the header starts, having counted the pass's cycles | the fast unit's register, a word a cycle during the pass |
| 4 | computes `header_crc` in the cycle the header starts | the fast unit, one cycle (28.13.4) |
| 5 | compares a restart link packet's data with the table when its last byte has come | byte by byte as it arrives: the pattern flag |
| 6 | knows a forwarded link packet by a CRC of what it holds | by the buffer's read pointer: `prep_key` |

None of these changes what goes on the line, only when, by 32 cycles at most; no rule depends on those cycles.

#### 28.13.7 The budget for one ASIC in the middle of a chain

| | How many | Flip-flops | Memory, bytes |
| --- | --- | --- | --- |
| a link end's registers | 2 | 1,260 | |
| a forwarding buffer's pointers and count | 4 | 164 | |
| the packet buffer of a receiver | 2 | | 1,024 |
| the kept link packets of a sender | 2 | | 8,192 |
| a forwarding buffer's words | 4 | | 65,536 |
| a forwarding buffer's marks | 4 | | 2,560 |
| **the link, in all** | | **1,424** | **77,312** |
| the ASIC's own queues, 16 KiB a stream (17.6) | 2 | | 32,768 |
| **with those** | | | **110,080** |

The four forwarding buffers are 85 % of the link's memory; the last ASIC on a chain has none. The size is not a
concern beside the Tracer's (owner). What could be smaller, if it ever is one: the forwarding buffers at 512 words,
exactly the rule's limit (34,048 bytes saved); none for the background towards the host, with a rule that such a
link packet is made wrong (17,024, item 185); a window of 4 (half the kept link packets, at a data rate of 0.6252
bytes a cycle against 0.6389 `[meas: the model]`).

### 28.14 Errors and the reset

- **Nothing a damaged link packet does is an error.** A link packet that fails a check or arrives out of order is
  thrown away and sent again, and a receiver that loses its place recovers; none of it reaches a register.
- **A link end's error** is raised when the link starts again under it (28.9), and is sticky until the reset. On the
  chip it is Ingress/Egress's `ERRORS.link_error`, one bit for both of the ASIC's link ends, with `format_error`
  beside it when the cause was a link packet made wrong twice (17.6). **A peripheral's error is its tile's,** so it
  stops the chip (owner). What follows is not designed in v1.37 *(item 181)*.
- **A neighbour that goes silent raises nothing:** a line that reads as zeros looks idle, and the end waits, its
  window full, for as long as the silence lasts *(item 186)*.
- **The reset** is the chip's one reset (18; owner): it reaches the link ends, the forwarding buffers and the ASIC's
  own queues as it reaches everything else. A link end comes out of it as at power-on (28.13.2): it says "I have
  started again", so **its neighbours each raise their error,** and the link comes back up with the next exchange.

### 28.15 What the host keeps

The host's end of the first link keeps a link end's rules:

- **its window is no more than half the chip's queue:** at most 16 link packets outstanding; 8, as the chip's, is
  enough, and the data rate is the same at 8, 16 and 64 `[meas: the model]`;
- every link packet it makes has `hops` 0; each frame starts a new link packet (29.2);
- it obeys the chip's stop bits and its window, and sends again when asked, as 28.10 and 28.11 say.

### 28.16 What has been shown, and the simulator's checks

**The protocol as this chapter states it has been shown on the model** `[meas: the model]`:

- **exhaustive searches** of a shrunken model (a window of 2, queues of a few words, numbers of 3 bits), on one link
  and on chains of 1, 2 and 3: every reachable state, 86,515,438 in the longest, and from every state, with either
  stream never taken again, the other finishes;
- **a kept case for every rule,** which fails without the rule and holds with it, and 18 planted flaws, each caught;
- **random runs at the real sizes,** noise on every line and consumers that stall up to 1 ms, on chains of 1, 2, 4
  and 16: 7,519 runs, 18 billion cycles and 385 million link packets with none failed and none refused for want of
  room.

**The figures a builder needs** `[meas: the model]`:

| | |
| --- | --- |
| the longest a foreground link packet waits for the line | 534 cycles |
| the data rate in the random runs, bytes a cycle | 0.5751; 0.8580 with every source saturated |
| a full foreground link packet on an idle line, let out to delivered | 561 cycles |
| link packets of length 0, of the data's bytes | 7.64 % on lines otherwise idle |
| the longest wait for an acknowledgement | 2,727 cycles |
| the fullest an ASIC's own queue got, of 1,024 words | 953 |
| the fullest a forwarding buffer got, of 1,024 words | 759 |

**The simulator's link ends** are this chapter's model, `End` and `Node` of `updater_wip/link/hostlink.h`, clocked a
byte a cycle. They count every kind of damage, recovery and restart, and report a link packet refused for want of
room, which with the sizes right never happens and so says a size is wrong.

### 28.17 Open items (chapter 26's numbering)

| item | what is to be settled | section | status |
|---|---|---|---|
| 181 | everything after an error stops the chip: the host's reset, draining the queues, a "ready" message | 28.14 | open |
| 185 | a background link packet with data that comes from below: forwarded, through a buffer nothing else uses; or made wrong, saving 17 KiB | 28.12 | open |
| 186 | a neighbour that goes silent: nothing notices it. A count of status periods with nothing heard would tell, but it must outlast the longest noise, seconds | 28.14 | open |
| 187 | the foreground stream's frames, towards the chip and back, but the reply frame (29) | 28.2 | open |
| 188 | the clock of the link ends against the chip's: a link cycle is a byte on the line (28.1) | 28.1, 18 | open |
