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
| `STATUS` [3] † | `foreground_ready` | R, live | words are waiting on the foreground stream |
| `STATUS` [4] † | `background_ready` | R, live | words are waiting on the background stream |
| `EVENTS` [8] † | `foreground_service` | counted, depth 1 | the words waiting on the foreground stream reached its threshold |
| `EVENTS` [9] † | `background_service` | counted, depth 1 | the same for the background |
| `EVENTS` [1] | `dma_done` | counted, depth 1 | the DMA's transfer has ended (17.4) |
| `ERRORS` [1] † | `crc_error` | sticky | a packet failed its header CRC or its data checksum |
| `ERRORS` [2] † | `sequence_error` | sticky | a packet's number was not the next one on its stream |
| `ERRORS` [3] † | `format_error` | sticky | a packet passed both checks but a field cannot be: its stream, its length, or a reserved bit |
| word 80 † | `FOREGROUND_WORDS` | R, live | the words waiting on the foreground stream, in every packet that has arrived and passed its checks; 0 when none |
| word 81 † | `BACKGROUND_WORDS` | R, live | the same for the background |
| word 82 † | `FOREGROUND_THRESHOLD` | R/W | the words that must be waiting on the foreground stream before its service flag is posted; 1 at the reset |
| word 83 † | `BACKGROUND_THRESHOLD` | R/W | the same for the background |
| block 1 | `DMA_HDR`, `DMA_DEST_ADDR`, `DMA_START` | 17.4's | reads words out of a stream (below) |

- **Two service flags, one for each stream** (owner).
- **`STATUS` says whether there is something to read on each stream** (owner).
- **A register for each stream gives the number of words waiting** (owner: "the number of 128 bit words in the packet
  that just requested service"; since the threshold, the words of every packet waiting).
- **The master tile tells Ingress how many words to gather before it wakes it** (owner, 2026-11-16).

## The rule for a service flag

As built in v1.37 (WORKER4), from the owner's rule: after a flag, no further flag until the core has read out the
words that were presented.

1. **A flag is posted when the stream owes nothing and the words waiting reach its threshold.**
2. **At the post the stream owes the words then waiting.** The DMA's read-outs count that debt down.
3. **The core wakes, and the flag is cleared** by the load of `EVENTS` (owner).
4. **No further flag while anything is owed** (owner).
5. **The stream is looked at again** when a packet arrives, when the last owed word is read out, and when the
   threshold is stored.

- **`…_WORDS` is live,** so a core that loads it after the flag may see more than was owed. Those words may be read
  out too.
- **For firmware writers:** a flag can be posted while the core is still reading words it has already counted. So
  the core loads the count only after a flag, with no transfer of its own in flight and what it last loaded used
  up. The count is then exact, and may be 0.
- **A threshold of 0 acts as 1.** A threshold above the buffer, 1,024 words, is never reached: the firmware's
  mistake.
- The two streams are independent: words waiting on one do not hold the other.

## The threshold in use

Why it is there: v1.37's first firmware woke for every packet, about 330 cycles awake against a packet's 528 on the
link. Larger packets are not the cure (`HOST-LINK.md`, "The size of a packet").

**The firmware, as the owner put it:**

1. It says: 1 word to start.
2. It is woken, and reads the frame's header word. Now it knows how many words are coming.
3. If the frame is a big one, which it almost always is, it says: wake me when 2 KB, 128 words, is available.
   (Built as 4 KB, 256 words, since the owner then raised the Updater's credit to 256.)
4. At the frame's end it says 1 again.

- **The threshold is never more than the words left in the frame,** so the tail of a frame needs no timer. It is
  lowered as the tail comes, not only set at the header word: "what is left" means what is left when the core next
  waits.
- **256 words is one credit of the Updater's segment queue** (owner: the queue is 8 KB, with a 256-word credit).
- **Ingress's buffers are oversized on purpose: 16 KB on each stream** (owner), 1,024 words of data. A threshold can
  always be reached, and there is room to be sure the far side can be stopped (`HOST-LINK.md`). The link waits when
  a packet's data does not fit.
- **The foreground stream keeps its threshold at 1.**
- **The firmware's cost does not depend on a packet's size,** so the size can be chosen for the link alone.

**What it gave** (WORKER4's report, at a byte a cycle):

| | a packet at a time | with the threshold at 256 |
|---|---|---|
| the master awake a packet's worth | about 330 cycles | about 100 |
| the master awake, of the link's time | about 0.6 | 0.17 to 0.20 |
| the link rate the firmware could keep up with | 1.6 bytes a cycle | 4.3 to 5.4 |

- **A transfer still costs 2.3 to 3 wakes:** the flag, the transfer's landing, and a credit every other one. The
  firmware is not tuned.
- **The owner's target (2026-11-16): the Updater is at most 10% of the master tile's load,** so that the core has
  slack for the command stream. That is 53 cycles awake a packet's worth, or about 420 for each 4 KB.
- **With a faster link the Updater is the next limit,** its memory work first, near 2 bytes a cycle in v1.37's
  black box.

## Reading words out: the DMA

The core tells Ingress to copy the next *n* words of a stream to an address (owner).

| register | what the core writes |
|---|---|
| `DMA_HDR` | the count *n*; the stream, as 17.4's `queue` († 0 foreground, 1 background); the destination tile; the counter and mode the `WRITE`s carry |
| `DMA_DEST_ADDR` | the address the first word goes to; it advances by 16 a word |
| `DMA_START` | starts the transfer |

- **A stream may be read out in pieces** (owner): the core copies the first word into its own memory, works out what
  the frame is, and sends the rest where it belongs, for example to the Updater.
- **One transfer may run across packets,** up to the words waiting.
- **† A count beyond the words waiting is undefined,** as other misuse of a DMA is (17.4).
- **Each word is sent as one 128-bit `WRITE`,** as every peripheral's DMA sends.
- **Suggested by WORKER4, not decided:** a transfer that discards its words. The master tile reads every record of
  frame 7 but its own, and every advice log of frame 8 but its own, into a scrap area.

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
  of that packet is the frame's header word. This is the sender's rule: since the threshold the chip does not depend
  on it, and the firmware's check of it is dropped. A frame header with a wrong length is then caught only by the
  frames' own fields.
- **The core keeps track of frames, not Ingress.** It reads a frame's length from the header word and counts the
  words that follow. Ingress knows only packets.

What the core does with a background frame:

1. **At the start of a frame:** copy one word into its own memory. It is the frame's header word: the type, the
   length and the compression.
2. **Decide** from the type where the frame's data goes.
3. **Send the frame's words there,** in transfers of up to the threshold's worth, until the frame's length is used
   up.

## Credits for the Updater

**The credits come to the master tile to manage** (owner). There is no signal from the Updater to Ingress.

1. The Updater posts `segment_credit` to the master tile each time half its segment queue, 256 words, has drained
   (`UPDATER-INTERFACE.md`).
2. The master tile keeps an allowance: a credit adds 256 words; a transfer into the segment queue takes off what it
   sent. One credit covers eight full packets.
3. It reads out to the Updater as many words as the allowance covers. The rest stay in Ingress.
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
