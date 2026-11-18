# The link end as hardware

For the RTL engineer. This is what the model in `updater_wip/link/` holds, register by register, with the bits each
needs at the real sizes, every memory with its size and the reason for it, the CRC units and when each runs, and a
budget for one ASIC in the middle of a chain.

- **The protocol** is `v1.4_wip/planning/HOST-LINK.md` ("HL"). **The model** is `link_end.h` and `link_end.cpp` (a
  link end) and `node.h` and `node.cpp` (a chain node). The model passes everything in `REPORT.md` beside this
  file. Where this document and the model differ, the model is right.
- **Every table with numbers here is printed by `build/linkhw`,** and `make check` fails if this file does not hold
  them word for word. The list of registers is checked against the model's: one row for each of its 47.
- **A dagger (†) marks what is my choice** and not HL's or the model's: mostly where the model does in one cycle
  what hardware must spread over several.
- **The count is of state.** It has no registers added for timing, no synchronizers on the inputs, and one clock.
  A cycle is one byte on the link.

## 1. What an ASIC's link is made of

| part | how many | what |
|---|---|---|
| a link end | 2 | the upper, towards the host, and the lower, away from it. Each is a receiver, a sender and the link's mode |
| a forwarding buffer | 4 | whole packets waiting to go on: down the chain and up it, a buffer for each stream |
| the pin | 1 | "another ASIC follows this one". Without it the lower end does nothing and its two forwarding buffers down are not used |
| the chip's own queues (Ingress) | 2 | the host's words for this ASIC, a queue for each stream. Not the link's, but the link's stop bits come from them |
| the chip's own packets (Egress) | 1 | this ASIC's words for the host, on the foreground. HL gives it no size |

**A link end's signals:**

| signal | bits | direction | what |
|---|---|---|---|
| the byte in, the byte out | 8 and 8 | in, out | the link: a byte a cycle each way. Idle is zero |
| say stop | 1 a stream | in | for each stream: this side cannot take much more. Up when the queue or the forwarding buffer behind this end is half full |
| room | 11 a stream | in | for each stream, the words a packet arriving now could be given. A packet is taken only if there is room for the whole of it |
| forwards | 1 | in | a packet received here would be forwarded: hops 15 on it cannot be. The upper end: the pin. The lower end: always |
| a packet offered | 10 a stream, and its words | in | for each stream: the next packet's length (6) and hops (4). Taken with one strobe a stream |
| a packet handed over | 12, and its words | out | a strobe (1), with the packet's stream (1), length (6) and hops (4) |
| error | 1 | out | sticky: the link started again under this end |
| in step | 1 | out | none of lost, answer, restart, restart answer |

## 2. The registers of a link end

**A link end, the receiver**

| register | flip-flops | memory, bytes | what it holds |
|---|---|---|---|
| `rx_state` | 3 |  | HUNT, HEADER, DATA, SKIP or FLUSH |
| `rx_count` | 10 |  | the bytes so far of the header (to 16) or of the data (to 512) |
| `rx_bytes` |  |  | † no register: the data's length in bytes is 16 × the length field held in `rx_hdr` |
| `rx_crc` | 32 |  | **the slow unit's register** |
| `rx_mask` | 32 |  | **the mask register** for the data arriving |
| `zero_run` | 10 |  | the zero bytes in a row on the wire, stopping at 528 |
| `rx_hdr` | 96 |  | † the header's bytes 0 to 11, until the packet is judged. Bytes 12 to 15 need no place: they go into the unit and into `rx_mask` as they arrive |
| `rx_valid` | 1 |  | a packet is being handed to the owner |
| `rx_stream` | 1 |  | of the packet handed over: its stream |
| `rx_length` | 6 |  | its length in words, 1 to 32 |
| `rx_hops` | 4 |  | its hops |
| `rx_buf` |  | 512 | **the packet buffer:** the data of the one packet being checked |
| `last` | 18 |  | the last packet number taken in order, 0 for none yet: the acknowledge field this end sends |
| `need` | 1 |  | a resend is owed: a packet was thrown away, and the one expected has not come |
| `asked` | 1 |  | "resend" has gone out for it |
| `ask_timer` | 12 |  | cycles since it went, stopping at 2,048: asked again only then |
| `wrong` | 1 |  | the packet expected has failed a field test once ("A packet made wrong") |
| `ack_owed` | 1 |  | a packet was taken since the last header sent |
| `stop_sent` | 2 |  | the stop bits in the last header sent, to see a change |
| **in all** | **231** | **512** | |

**A link end, the sender**

| register | flip-flops | memory, bytes | what it holds |
|---|---|---|---|
| `tx_state` | 2 |  | IDLE, HEADER, DATA or ZEROS |
| `tx_count` | 11 |  | the bytes sent of the header (to 16), the data (to 512) or the zeros (to 1,024) |
| `tx_bytes` |  |  | † no register: 16 × the length in `tx_hdr`, or 1,024 for the zeros |
| `tx_hdr` | 128 |  | the header going out. It is latched when it starts, since the fields behind it can change while it goes |
| `tx_slot` | 3 |  | which kept packet is going out |
| `tx_data` | 1 |  | the packet going out has data |
| `tx_pattern` | 1 |  | and the data is the restart packet's, from its table |
| `tx_mask` | 32 |  | **the mask register** for the data going out |
| `tx_byte` | 8 |  | the wire |
| `acked` | 18 |  | the last acknowledge field taken: the oldest packet kept is the one after it |
| `next` | 18 |  | the number that goes on the wire next: behind `tail` while packets are being sent again |
| `tail` | 18 |  | the number the next new packet takes |
| `head` | 3 |  | the slot of the oldest packet kept |
| `peer_stop` | 2 |  | the other end's stop bits, from the last good header |
| `meta` | 88 |  | † for each of the 8 kept packets: its stream (1), length (6) and hops (4) |
| `data` |  | 4,096 | **the kept packets:** 8 slots of 32 words |
| `prep_key` | 5 |  | † which packet the fast unit has passed over: none, one to send again or a new one (2); the slot, or the stream and whether it is forwarded or the ASIC's own (3) |
| `prep_left` | 6 |  | the cycles of that pass still to go, 32 at most |
| **in all** | **344** | **4,096** | |

**A link end, the link's mode and timer**

| register | flip-flops | memory, bytes | what it holds |
|---|---|---|---|
| `lost` | 1 |  | this end has lost its place in the bytes |
| `answer` | 1 |  | the other end has said "lost": this end answers with recovery rounds |
| `restart` | 1 |  | this end has started again and waits for the answer |
| `restart_answer` | 1 |  | the other end has started again: this end answers |
| `error` | 1 |  | **the error output.** Sticky: the link started again under this end |
| `round` | 1 |  | in a round: the zeros come next, or the packet |
| `due` | 1 |  | a status is due: the timer ran out, or the link came back into step |
| `status_timer` | 9 |  | cycles since the end of the last packet sent, to 512 |
| `timer_dead` |  |  | none: a planted bug's |
| `answered` |  |  | none: a planted bug's |
| **in all** | **16** | **0** | |

**A link end, what hardware needs and the model does not have**

| register | flip-flops | memory, bytes | what it holds |
|---|---|---|---|
| the fast unit's register | 32 |  | † **the fast unit:** the CRC so far of the pass, then the data checksum for the header |
| the handing-over counter | 6 |  | † which word of the packet buffer is going to the queue and the forwarding buffer |
| the pattern flag | 1 |  | † a restart packet's data has matched the table so far |
| **in all** | **39** | **0** | |

**Checked against the model:** two ends back to back for 3,000,000 cycles, with noise: 11,114 packets delivered, 940 recovery and restart rounds. No register held more than its flip-flops here allow, and `rx_bytes` and `tx_bytes` were always what the tables say they are.

**One link end:** 591 flip-flops for the model's 47 registers and 39 more, 630 in all; 4,608 bytes of memory.

- **At power-on** every register is zero but three: `next` and `tail` are 1, and `restart` is set, so an end that
  powers on says "I have started again" and waits for the answer.
- **Within a cycle the model runs the receiver, then the timers, then the sender,** each seeing what the one
  before it wrote. Where two write one register in a cycle the later wins. A packet taken by the receiver is
  acknowledged by a header the sender starts in that same cycle.
- **Packet numbers** run 1 to 0x3FFFE and then 1 again. `last` and `acked` are 0 for "none yet".

## 3. The states

**The receiver** (`rx_state`):

| state | what it does | leaves |
|---|---|---|
| HUNT | between packets: zeros are idle | on a byte that is not zero, to HEADER |
| HEADER | collects 16 bytes through the slow unit | on the 16th: the header CRC fails, to FLUSH, and the link is "lost"; a length over 32, to FLUSH likewise; a field that cannot be, to SKIP; otherwise to DATA. With length 0 the packet is judged at once, and to HUNT |
| DATA | collects 16 × length bytes, the mask taken off, through the slow unit | on the last byte the packet is judged, and to HUNT |
| SKIP | the same for a packet with a field that cannot be: checked, and not kept | on the last byte the packet is judged, and to HUNT |
| FLUSH | takes nothing | after 528 zeros in a row, to HUNT |

**Judging a packet** whose header CRC passed, in HL's order:

| | what | then |
|---|---|---|
| 1 | a restart packet | acted on only if its length is 32, its checksum passes and its data is the table's; else thrown away as damaged |
| 2 | the data checksum fails | thrown away. A resend is owed if its number is the one expected, at once, or ahead, after the 2,048 cycles |
| 3 | a field that cannot be, both checks passing | the number expected: `wrong` is set, and a resend is owed; `wrong` already set: the error, and the link starts again. Another number: thrown away |
| 4 | the acknowledge field, the stop bits, "resend" and "lost" | taken, in any mode but while this end is starting again. "Resend" or "lost" with an acknowledge field behind the last one taken: the error, and the link starts again |
| 5 | the number, of a packet with data | the one expected and room: taken, `wrong` cleared. No room: thrown away, a resend owed. Ahead: thrown away, a resend owed. Behind: thrown away, acknowledged |
| 6 | the number, of a packet of length 0 | ahead of the one expected: a resend is owed. The one expected: nothing is owed any more |

- **A field that cannot be:** a stream that is not 1 or 2; a reserved bit set; hops 15 on a packet with data that
  this end would forward.

**The sender** (`tx_state`): IDLE, HEADER (16 bytes), DATA (16 × length bytes, the mask put on), ZEROS (1,024
bytes). From IDLE it starts, in this order:

| | when | what |
|---|---|---|
| 1 | the link is not in step | a round: 1,024 zeros, then the round's packet (below) |
| 2 | a packet with data is next and the pass over it is done | that packet: one to be sent again before a new one; of new ones the foreground's before the background's; a new one only if its stream is not stopped and fewer than 8 are kept |
| 3 | a stop bit has changed, a resend is owed and not yet asked, a packet was taken since the last header, or the status timer has run out | a packet of length 0 |

**The link's mode,** four flags:

| flag | set by | cleared by | while set, the round's packet |
|---|---|---|---|
| `lost` | a header CRC that fails; a length over 32 | a good packet with "recovery"; a good restart packet | "lost" |
| `answer` | a good packet with "lost" | a good packet without "lost" | "recovery" (with "lost" too, if this end is lost) |
| `restart` | power-on; a packet made wrong twice; asked for a packet no longer kept | a restart packet with "recovery" | the restart packet with "lost" (and "recovery", if answering) |
| `restart_answer` | a restart packet with "lost". If this end was not starting itself: it drops what it kept, numbers from 1, and raises the error | a restart packet without "lost"; any ordinary good packet | the restart packet with "recovery"; "lost" first, if this end is lost |

- With no flag set the link is in step, and only then does data go.

## 4. The CRC units and the mask registers

An ASIC has two fast units, two slow units and four mask registers: one of each kind at each link end, and a mask
register for each direction of each link. All compute CRC-32C: polynomial 0x1EDC6F41, reflected 0x82F63B78, the
register started at all ones and complemented at the end.

| unit | where | starts | runs | gives |
|---|---|---|---|---|
| **slow, a byte a cycle** (`rx_crc`) | each receiver | set to all ones at a header's first byte, the first byte in HUNT that is not zero | a byte a cycle through the header's 16 bytes | after the 16th the result must be 0x48674BC7: the header CRC is good |
| | | set to all ones again after the header's 16th byte | a byte a cycle through the data, each byte after the mask is off | after the last byte the result must be the header's bytes 8 to 11. Of no data the checksum is 0, and the header must carry that |
| **the receiver's mask** (`rx_mask`) | each receiver | loaded after the header's 16th byte: bytes 12 to 15, byte 12 lowest, with bit 16 set | once for each data byte, before the byte is used: the CRC unit's step for a byte of zero (8 shifts, nothing fed in) | its low 8 bits, XORed onto the byte from the wire |
| **fast, 128 bits a cycle** | each sender | set to all ones when the packet that goes next is not the one it has passed over (`prep_key`) | a word a cycle over that packet's data, 1 to 32 cycles (`prep_left`). It runs while the packet before is going out, or while the line is idle | the data checksum, bytes 8 to 11 of the header |
| | | when a header is written: started from 0x641F6454 | one cycle: 32 zero bits and the header's bytes 0 to 11 | the header CRC, bytes 12 to 15 |
| **the sender's mask** (`tx_mask`) | each sender | loaded when the header is written: its CRC with bit 16 set | once for each data byte as it leaves | its low 8 bits, XORed onto the byte from the kept packet |

- **The pass starts over** when what goes next changes before it is done: a resend is asked for, a stop bit
  arrives, the link loses step, or a forwarded packet comes in front of the ASIC's own.
- **A packet of length 0 has no pass:** its checksum is 0. **The restart packet has none:** its checksum is the
  constant 0xC8981D21 and its data comes from the table.
- **A packet of length 0 may go while a pass is running,** and the data follows it.
- **The header waits for the pass.** A packet with data starts only when the pass over it is done.
- **† One thing for the RTL engineer to settle: the header's CRC during a pass.** The model computes a header's CRC
  in the cycle the header starts and charges no cycle for it. A packet of length 0 that starts while a pass is
  running needs the fast unit for that one cycle. Either the pass waits a cycle, or the header's CRC has its own
  logic and the register is left alone. The header's CRC is not needed on the wire until 12 cycles after the
  header starts, and no rule depends on the cycle: the searches hold with the pass at 128 bits a cycle, at a
  byte a cycle and with none.
- **The mask keeps out of two states,** 0 and 0xFCA42DAF, in which the register stands still; that is what bit 16
  is for.

**The constants:**

| | |
|---|---|
| a good header through the slow unit, all 16 bytes | 0x48674BC7 |
| the fast unit's start for a header | 0x641F6454 |
| the bit set in a mask's start | bit 16: 0x00010000 |
| the restart packet's data | the first 32 primes as bytes, 2 to 131, sixteen times over: 32 words |
| the restart packet's data checksum | 0xC8981D21 |
| the zeros of a round | 1,024 bytes |
| the zeros a lost receiver waits for | 528 bytes: a full packet's length |
| the status timer | 512 cycles |
| the wait before asking again | 2,048 cycles |
| the window | 8 packets |
| a packet's data at most | 32 words, 512 bytes |

## 5. The memories, and why each is the size it is

| memory | at | bytes | why that size |
|---|---|---|---|
| the packet buffer (`rx_buf`) | each receiver | 512 | one packet of the most data. The data checksum is in the header, so a packet can be used only when all its data has been through the unit: until then it waits here |
| the kept packets (`data`) | each sender | 4,096 | the window: 8 packets of 32 words (HL, "What a sender must keep"). A packet stays until it is acknowledged, to be sent again. 8 is the owner's: the data rate in the random runs was 0.5803 bytes a cycle with a window of 2, 0.6252 with 4, 0.6389 with 8 and 0.6392 with 16 |
| a forwarding buffer | four a node | 16,384 | † HL: "16 KB like a queue". The rule is that it be at least twice the window of the end that feeds it, 512 words: "stop" is said at half full, and the sender may still send to the end of its window if the stop bit is lost. The fullest one got in 60 random runs was 621 words of 1,024 |
| its marks | beside each | 640 | † which word ends a packet, and the packet's hops: 5 bits a word. The model keeps a length and hops for each packet |
| the chip's queue (Ingress) | two a chip | 16,384 | HL, the owner: "stop" at half, 8 KB, and the window of 4 KB must fit in what is left with room to spare |

- **What is kept is data only.** The header and both checks are made afresh each time a packet goes out, a packet
  sent again too.
- **The restart packet's data is a table of 32 bytes,** not a memory.

**How each is read and written,** which is what decides its shape:

| memory | written | read |
|---|---|---|
| the packet buffer | a byte a cycle, as the data arrives | † a word a cycle when the packet is handed over, 32 cycles for a full packet |
| the kept packets | † a word a cycle, during a new packet's pass: the pass is also the copy into the slot | a word a cycle during the pass over a packet to be sent again; a byte a cycle as a packet goes out. **Both at once:** the pass over the next packet runs while the one before goes out |
| a forwarding buffer | † a word a cycle, as a packet is handed over | † a word a cycle, during the pass |

- **† One packet buffer is enough.** The model hands a packet over in the cycle of its last byte. At a word a
  cycle hardware takes 32 cycles, and the next packet may be arriving meanwhile. Its header takes 16 cycles and
  its data a byte a cycle, so the buffer is emptied 16 times as fast as it is filled, from 16 cycles ahead.
- **† A pass that starts over** leaves part of a new packet in a slot. The slot is not yet counted as kept, and
  the next pass writes over it.

## 6. The chain node

**A forwarding buffer (a node has four: each stream, each direction)**

| register | flip-flops | memory, bytes | what it holds |
|---|---|---|---|
| its words |  | 16,384 | 1,024 words: whole packets, first in first out |
| its marks |  | 640 | † 5 bits beside each word: the last word of its packet (1), and the packet's hops (4) |
| read pointer | 10 |  | † the next word to offer the sender |
| write pointer | 10 |  | † where the word being handed over goes |
| whole-packet pointer | 10 |  | † the write pointer after the last whole packet: only whole packets are offered |
| words held | 11 |  | for "half full" (the stop bit) and for the room a packet needs |
| **in all** | **41** | **17,024** | |

| what | the rule (HL, "The chain") | in the model |
|---|---|---|
| a packet taken at the upper end | goes to the chip's own queue and, with a neighbour below, into the forwarding buffer down, hops plus one | `node.cpp`: both in the cycle it is handed over |
| a packet taken at the lower end | goes into the forwarding buffer up, hops plus one | the same |
| the upper end's stop bit, each stream | up when the chip's queue or the forwarding buffer down is half full | `say_stop` |
| the lower end's stop bit, each stream | up when the forwarding buffer up is half full | |
| the upper end's room for a packet | the smaller of the chip's queue's and the forwarding buffer's | a packet is taken into both or into neither |
| what the upper end sends | a forwarded packet before one of the ASIC's own; the ASIC's own on the foreground only | the own packet is offered only when the forwarding buffer up has no foreground packet |
| what the lower end sends | the forwarding buffers down, the foreground's before the background's | |
| the pin | with no neighbour below: the lower end is never clocked, sends zeros, and nothing is forwarded down | `has_down` |
| a packet acknowledged | when it is taken here, not when the next link has acknowledged it | |
| hops | every packet is made with hops 0. One that must be forwarded with hops 15 is a packet made wrong | the upper end tests it only with a neighbour below; the lower end always |

- **Towards the host the background carries no data:** only packets of length 0, which never leave their link. In
  the model nothing is ever put in the forwarding buffer up for the background but by a forged header.

## 7. What the model does in one cycle that hardware cannot

| | the model | † hardware |
|---|---|---|
| 1 | hands a packet's data to the queue and the forwarding buffer in the cycle of its last byte | a word a cycle, 32 at most: the handing-over counter. The packet is acknowledged at once; it can be offered for forwarding only when it is whole in the buffer, 32 cycles later at most |
| 2 | copies a new packet's data into its kept slot in the cycle its header starts | during the pass, a word a cycle |
| 3 | computes the data checksum in one step when the header starts, having counted the pass's cycles | the fast unit's register, a word a cycle during the pass |
| 4 | computes the header CRC in the cycle the header starts | the fast unit, one cycle; section 4 has the one case to settle |
| 5 | compares a restart packet's data with the table when its last byte has come | byte by byte as it arrives: the pattern flag |
| 6 | knows a forwarded packet by a CRC of what it holds (`PacketFifo::front`) | by the buffer's read pointer: `prep_key` |

- None of these changes what goes on the wire, only when, by 32 cycles at most. No rule depends on those cycles.

## 8. The budget for one ASIC in the middle of a chain

**The budget for one ASIC in the middle of a chain**

| | how many | flip-flops | memory, bytes |
|---|---|---|---|
| a link end's registers | 2 | 1,260 | |
| a forwarding buffer's pointers and count | 4 | 164 | |
| the packet buffer of a receiver | 2 | | 1,024 |
| the kept packets of a sender | 2 | | 8,192 |
| a forwarding buffer's words | 4 | | 65,536 |
| a forwarding buffer's marks | 4 | | 2,560 |
| **the link, in all** | | **1,424** | **77,312** |
| the chip's own queues behind it (Ingress), 16 KB a stream | 2 | | 32,768 |
| **with those** | | | **110,080** |

**Where the link's memory goes:** the four forwarding buffers' words 85%, their marks 3%, the packets kept for sending again 11%, the two packet buffers 1%.

- The last ASIC on a chain uses its upper end only, and no forwarding buffer.

**What could be smaller**

| | bytes saved | breaks a rule? |
|---|---|---|
| the four forwarding buffers at 512 words, twice the window that feeds them, not 1,024 | 34,048 | no, but it is exactly the limit, as 8 KB is for a queue |
| no forwarding buffer for the background towards the host | 17,024 (8,512 if the buffers are 512 words) | it needs a new one: a background packet with data from below is a packet made wrong |
| the packets a sender keeps left in the forwarding buffer until acknowledged, not copied to slots (the link away from the host) | 4,096 | no, but it is not built and not searched |
| a packet checked in place at the end of its queue, not in a buffer of its own | 1,024 | no, but it is not built and not searched |
| the chip's own queues at 8 KB | 16,384 | no, but it is exactly the limit: the owner chose 16 KB for that reason |

- **The first row is the one that costs nothing to try:** in 30 random runs with forwarding buffers of 512 words
  the fullest got was 361 words, no packet was refused, and the data rate was 0.6368 bytes a cycle against 0.6389
  (`figures.sh d`).
- **The second needs HL to say so first.** HL has the background empty towards the host but does not say what an
  ASIC does with a background packet that has data and comes from below.
- **The window is the other lever:** 4 packets and not 8 halves the kept packets and the forwarding buffers'
  least size. It costs data rate: 0.6252 bytes a cycle against 0.6389 in the random runs.
