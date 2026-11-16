# Ingress and the master tile: the interface

Draft, 2026-11-16, from the discussion with the owner. How the master tile's core (C&C 1 in v1.35's names) takes what
the host sends out of Ingress. It is written so that a worker need not guess: where the owner did not rule, a choice
is made and marked with a dagger (†). (owner) marks what the owner said.

The link itself, its packets and its checks, is `HOST-LINK.md`. The frames inside the packets are `HOST-FRAMES.md`.

## Terms

| term | meaning |
|---|---|
| **a packet** | one packet of the link: a header word, then 0 to 32 words of data. Not a packet of the mesh, which this document calls a `WRITE` |
| **a word** | 128 bits |
| **a stream** | foreground or background (owner's names). The foreground carries what the host sends for the proof being worked on; the background sets up the next proof |
| **a frame** | one of `HOST-FRAMES.md`'s, sent as one or more packets of a stream |

## What changes in Ingress/Egress

Ingress/Egress is the master tile's peripheral 1 (`v1.36/spec/machine-spec.md` 17.6).

- **What the RTL has built stays as it is:** the error lines, the barrier's master side, the watchdog, the LED and
  the UART (words 32 to 45, `EVENTS` bits 2 to 5).
- **The host interface is replaced by this design's, below.** v1.36 pencilled in two stream ports read a beat at a
  time by the core (words 64 to 72, `STATUS.stream_last`, `EVENTS.stream_in` and `stream_out`).
  - † For now the new interface sits beside the old one, at words and bits of its own: the RTL's firmware images
    and v1.36's stream tests use the old words, and those run unchanged.
- **Ingress gains a DMA,** the standard one of 17.4. v1.36's Ingress/Egress has none.

## The registers

| where | register or bit | access | meaning |
|---|---|---|---|
| `STATUS` [3] † | `foreground_ready` | R, live | a packet is waiting on the foreground stream |
| `STATUS` [4] † | `background_ready` | R, live | a packet is waiting on the background stream |
| `EVENTS` [8] † | `foreground_service` | counted, depth 1 | a foreground packet asks to be read out |
| `EVENTS` [9] † | `background_service` | counted, depth 1 | a background packet asks to be read out |
| `EVENTS` [1] | `dma_done` | counted, depth 1 | the DMA's transfer has ended (17.4) |
| `ERRORS` [1] † | `crc_error` | sticky | a packet failed its header CRC or its data checksum |
| `ERRORS` [2] † | `sequence_error` | sticky | a packet's number was not the next one on its stream |
| `ERRORS` [3] † | `format_error` | sticky | a packet passed both checks but a field cannot be: its stream, its length, or a reserved bit |
| word 80 † | `FOREGROUND_WORDS` | R | the words in the foreground packet that asked for service; 0 when none is waiting |
| word 81 † | `BACKGROUND_WORDS` | R | the same for the background |
| block 1 | `DMA_HDR`, `DMA_DEST_ADDR`, `DMA_START` | 17.4's | reads words out of a packet (below) |

- **Two service flags, one for each stream** (owner).
- **`STATUS` says whether there are packets to read on each stream** (owner).
- **A register for each stream gives the number of words in the packet that just asked for service** (owner).

## The rule for a service flag

1. A packet reaches the front of its stream, having passed both of its checks. Its stream's `…_WORDS` shows its
   length and its `…_ready` bit is 1.
2. **Its service flag is posted.**
3. **The core wakes, and the flag is cleared** by the load of `EVENTS` (owner).
4. The core reads the packet's words out with the DMA, in one transfer or several.
5. **The stream posts no further service flag until the core has read out as many words as the register showed**
   (owner). Then the next packet, if there is one, is at step 1.

- † `…_WORDS` holds the packet's length until the packet has been read out in full.
- The two streams are independent: a packet waiting on one does not hold the other.

## Reading words out: the DMA

The core tells Ingress to copy the next *n* words of a stream's packet to an address (owner).

| register | what the core writes |
|---|---|
| `DMA_HDR` | the count *n*; the stream, as 17.4's `queue` († 0 foreground, 1 background); the destination tile; the counter and mode the `WRITE`s carry |
| `DMA_DEST_ADDR` | the address the first word goes to; it advances by 16 a word |
| `DMA_START` | starts the transfer |

- **A packet may be read out in pieces** (owner): the core copies the first word into its own memory, works out what
  the packet is, and sends the rest where it belongs, for example to the Updater.
- **† A count beyond what is left of the packet is undefined,** as other misuse of a DMA is (17.4).
- **Each word is sent as one 128-bit `WRITE`,** as every peripheral's DMA sends.

## Knowing a transfer is complete

Two signals, both already in v1.36:

| signal | what it says |
|---|---|
| `EVENTS.dma_done` | the last word has left Ingress |
| a counter's event | the words have arrived: every `WRITE` carries the counter named in `DMA_HDR`, and the tile they go to has armed that counter with the number it expects |

| destination | how the master tile knows the words are in |
|---|---|
| its own memory, or the Updater's segment queue | it arms one of its own counters before the transfer, and waits for that counter's event and for `dma_done` |
| another tile | that tile arms its counter. If the master tile must know as well, the tile says so by a request |

This is how v1.36's replay firmware refills a replayer's queues. The next service flag has nothing to do with it.

## Frames

- **A frame ends at the end of a packet** (owner). The next frame starts at the start of a packet, so the first word
  of that packet is the frame's header word.
- **The core keeps track of frames, not Ingress.** It reads a frame's length from the header word and counts the
  words that follow. Ingress knows only packets.

What the core does with a background packet:

1. **At the start of a frame:** copy one word into its own memory. It is the frame's header word: the type, the
   length and the compression.
2. **Decide** from the type where the frame's data goes.
3. **Send the rest of this packet there,** and each following packet of the frame, until the frame's length is used
   up.

## Credits for the Updater

**The credits come to the master tile to manage** (owner). There is no signal from the Updater to Ingress.

1. The Updater posts `segment_credit` to the master tile each time half its segment queue, 128 words, has drained
   (`UPDATER-INTERFACE.md`).
2. The master tile keeps an allowance: a credit adds 128 words; a transfer into the segment queue takes off what it
   sent. One credit covers four full packets.
3. It reads a background packet out to the Updater only when the allowance covers it. Otherwise the packet stays in
   Ingress.
4. Ingress knows nothing of the Updater: its background stream simply backs up.
5. With the real link, a backed-up background stream is what makes Ingress tell the host to stop sending it
   (`HOST-LINK.md`). The foreground stream is not touched.

## Errors

- **A packet that fails a check is fatal** (owner). `ERRORS.crc_error` is set and Ingress presents no more packets.
- † A packet number out of order is treated the same way, with `ERRORS.sequence_error`.
- † So is a packet that passes both checks with a field that cannot be, with `ERRORS.format_error`: that is the
  sender's fault, not noise.
- **The order of the tests is `HOST-LINK.md`'s:** the header CRC, the fields, the data checksum, then the number.
  So a packet with a bad data checksum and a wrong number is a `crc_error`.
- Sending again and recovery need the way back to the host, and are for later (`HOST-LINK.md`).

## In v1.37: a black box and a file

**Decided (owner):** v1.37's Ingress is a black box in the simulator, fed from a file.

| | |
|---|---|
| the file | gold's frame file, packetized: wrapped in the link's packets with the right checks (owner) |
| the streams | background only |
| what the black box does | checks each packet (both checks, and the packet numbers in order), strips the header word, and presents the packet to the master tile through the registers above |
| what it leaves out | everything that needs the way back to the host: acknowledgements, "resend", "lost" and recovery, "stop" |

**The packetizer** makes the file from gold's frames:

- every packet is on stream 2, the background;
- each frame starts a new packet; a frame's packets are 32 words long but its last, which holds what remains;
- packet numbers run from 1; the acknowledge field and the flags are 0;
- both checks are CRC-32C, as `HOST-LINK.md` defines it.

**What the black box does with the rest:**

- † zero bytes between packets are idle, and it skips them;
- † a packet of length 0 takes no number and asks for no service: it is checked and passed over;
- † the flags and the acknowledge field are ignored, since there is no way back to the host;
- † at the end of the file no more packets come, which is not an error.

Gold's packetizer and its vectors are `updater_wip/pipeline/gold/` (`packets`, `packets-read`,
`test/packets/VECTORS.txt`).

## Open

- The word numbers and bit numbers marked †.
- The sending side, Egress: nothing goes back to the host in v1.37.
- Whether the foreground stream's messages are frames of the same kind, and what they are.
