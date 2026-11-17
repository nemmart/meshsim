# Ingress and the master tile: the interface

Draft, 2026-11-16, from the discussion with the owner; rewritten that evening for the owner's forward count, which
takes the firmware out of the path of the bulk data. How the master tile's core (C&C 1 in v1.35's names) takes what
the host sends out of Ingress. It is written so that a worker need not guess: where the owner did not rule, a choice
is made and marked with a dagger (†). (owner) marks what the owner said.

The link itself, its packets and its checks, is `HOST-LINK.md`. The frames inside the packets are `HOST-FRAMES.md`.

**What v1.37 has today is the design before this one** (WORKER4): a threshold for each stream, and the firmware
moving every word to the Updater by DMA under credits. "In v1.37", at the end, says what is built and what is to
come.

## Terms

| term | meaning |
|---|---|
| **a packet** | one packet of the link: a header word, then 0 to 32 words of data. Not a packet of the mesh, which this document calls a `WRITE` |
| **a word** | 128 bits |
| **a stream** | foreground or background (owner's names). The foreground carries what the host sends for the proof being worked on; the background sets up the next proof |
| **a queue** | where a stream's words wait in Ingress: the foreground queue and the background queue |
| **a frame** | one of `HOST-FRAMES.md`'s, sent as one or more packets of a stream |

## The pipeline (owner, 2026-11-16)

| stage | what it does |
|---|---|
| the link | packets of up to 512 bytes of data |
| the packet buffer | holds one packet while its checks are made. A packet that passes is accepted: its header word is dropped and its data words go into its stream's queue. A packet that fails is rejected (`HOST-LINK.md`) |
| the two queues | foreground and background, **8 KB each,** 512 words: pencilled in (owner), the size to be settled (Open). A stream that fills tells the host to stop sending it (`HOST-LINK.md`) |
| the DMA | the standard one (17.4): copies words out of either queue to an address, on this tile or another |
| the forward count | moves words from the background queue straight to the Updater, or discards them |

- **The DMA is how the firmware reads words.** It is used for the little the firmware must see: a frame's header
  word, the headers, its own record and advice log.
- **The forward count is how the bulk data moves.** The firmware wakes once for a frame, not for its words.
- **The Updater's input is flow-controlled** (owner): the two units are side by side on the master tile, so there
  are no credits. This is the only way into the Updater's data input.

## What changes in Ingress/Egress

Ingress/Egress is the master tile's peripheral 1 (`v1.36/spec/machine-spec.md` 17.6).

- **What the RTL has built stays as it is:** the error lines, the barrier's master side, the watchdog, the LED and
  the UART (words 32 to 45, `EVENTS` bits 2 to 5).
- **The host interface is replaced by this design's, below.** v1.36 pencilled in two stream ports read a beat at a
  time by the core (words 64 to 72, `STATUS.stream_last`, `EVENTS.stream_in` and `stream_out`).
  - † For now the new interface sits beside the old one, at words and bits of its own: the RTL's firmware images
    and v1.36's stream tests use the old words, and those run unchanged.
- **Ingress gains a DMA,** the standard one of 17.4. v1.36's Ingress/Egress has none.
- **Ingress gains a word port to the Updater** (below).

## The registers

| where | register or bit | access | meaning |
|---|---|---|---|
| `STATUS` [3] † | `foreground_ready` | R, live | the foreground queue holds words |
| `STATUS` [4] † | `background_ready` | R, live | the background queue holds words |
| `EVENTS` [8] † | `foreground_service` | counted, depth 1 | the foreground queue asks for service (below) |
| `EVENTS` [9] † | `background_service` | counted, depth 1 | the background queue asks for service (below) |
| `EVENTS` [1] | `dma_done` | counted, depth 1 | the DMA's transfer has ended (17.4) |
| `ERRORS` [1] † | `crc_error` | sticky | a packet failed its header CRC or its data checksum |
| `ERRORS` [2] † | `sequence_error` | sticky | a packet's number was not the next one on its stream |
| `ERRORS` [3] † | `format_error` | sticky | a packet passed both checks but a field cannot be: its stream, its length, or a reserved bit |
| word 80 † | `FOREGROUND_WORDS` | R, live | the foreground queue's length in words; 0 when empty |
| word 81 † | `BACKGROUND_WORDS` | R, live | the background queue's length in words |
| word 82 † | `DMA_FORWARD_COUNT` | R/W | the forward count (below). 0 at the reset |
| block 1 | `DMA_HDR`, `DMA_DEST_ADDR`, `DMA_START` | 17.4's | copies words out of a queue (below) |

- **Two service flags, one for each stream** (owner).
- **Two readable registers: the foreground queue's length and the background queue's** (owner).
- **There are no thresholds.** They were how the firmware kept its cost down while it moved every word itself.

## The forward count

`DMA_FORWARD_COUNT` (the owner's name and design):

| bits | name | meaning |
|---|---|---|
| [30:0] | `count` | the words still to move out of the background queue |
| [31] | `discard` | 0: each word goes to the Updater. 1: each word is dropped (owner) |

- **While `count` is not 0, one word a cycle leaves the background queue** and `count` goes down by one, in any
  cycle in which the queue holds a word and, when sending, the Updater can take one (owner).
- **It waits when the queue is empty.** So the firmware writes a frame's whole length at once, long before the
  frame's words have arrived (owner: "you can ask it to send words it hasn't received yet").
- **Discarding does not wait on the Updater,** only on the queue. It runs at the link's rate.
- **Forwarding and a DMA transfer from the foreground queue can move words in the same cycle** (owner).
- **Starting a DMA transfer from the background queue while `count` is not 0 is undefined** (owner).
  - † So is a store of a count while a DMA transfer from the background queue is in flight.
- **† A store replaces the count.** A store of 0 stops the forwarding: for when the Updater halts on an error.
- **† A load gives what remains,** and may be stale by the time it is used.
- **There is no event for the count reaching 0.** The firmware learns that a frame is through from the Updater's
  `done`, or from the next service flag.

## The word port to the Updater

| signal | from | meaning |
|---|---|---|
| `valid` | Ingress | a word is offered: `count` is not 0, `discard` is 0, and the background queue holds a word |
| `data` [127:0] | Ingress | the word at the head of the background queue |
| `ready` | the Updater | it can take a word this cycle |

- **A word moves in a cycle in which `valid` and `ready` are both 1.**
- Words arrive at the Updater in the stream's order, each once.
- What the Updater keeps behind the port is its own (`UPDATER-INTERFACE.md`).

## The rule for a service flag

**A queue asks for service when it holds words and nothing is taking them** (owner: "if `DMA_FORWARD_COUNT` is zero
and there is data in the background queue, or if there is data in the foreground queue, then the ingress requests
service").

| queue | it asks for service while |
|---|---|
| foreground | it holds words, † and no DMA transfer from it is in flight |
| background | it holds words, `count` is 0, † and no DMA transfer from it is in flight |

† When the flag is posted, the flag being a counted event of depth 1:

1. **When a queue starts to ask:** a word arrives in an empty queue, or `count` reaches 0 with words in the queue.
2. **When a DMA transfer from the queue ends** and the queue still holds words.
3. **Not for words that arrive while it is already asking.** After a flag, the next one comes only when the core
   has started something that takes words from the queue, and that has ended.

- **The core wakes, and the flag is cleared** by the load of `EVENTS` (owner).
- **`…_WORDS` is live,** so a core that loads it after the flag sees every word that has arrived by then.
- The two streams are independent: words waiting on one do not hold the other.

## Reading words out: the DMA

The core tells Ingress to copy the next *n* words of a queue to an address (owner).

| register | what the core writes |
|---|---|
| `DMA_HDR` | the count *n*; the queue, in 17.4's `queue` field († 0 foreground, 1 background); the destination tile; the counter and mode the `WRITE`s carry |
| `DMA_DEST_ADDR` | the address the first word goes to; it advances by 16 a word |
| `DMA_START` | starts the transfer |

- **The queue to copy from is a bit of the DMA's header** (owner). 17.4's `DMA_HDR` already has the field.
- **A frame may be read out in pieces** (owner): the core copies the first word into its own memory, works out what
  the frame is, and deals with the rest.
- **One transfer may run across packets,** up to the words in the queue.
- **† A count beyond the words in the queue is undefined,** as other misuse of a DMA is (17.4). The core loads
  `…_WORDS` and copies no more than that.
  - Why the DMA does not wait for words, as the forward count does: there is one DMA for both queues, and a
    transfer waiting on the background's packets would hold a foreground message behind it.
- **Each word is sent as one 128-bit `WRITE`,** as every peripheral's DMA sends.

## Knowing a transfer is complete

Two signals, both already in v1.36:

| signal | what it says |
|---|---|
| `EVENTS.dma_done` | the last word has left Ingress |
| a counter's event | the words have arrived: every `WRITE` carries the counter named in `DMA_HDR`, and the tile they go to has armed that counter with the number it expects |

| destination | how the master tile knows the words are in |
|---|---|
| its own memory | it arms one of its own counters before the transfer, and waits for that counter's event and for `dma_done` |
| another tile | that tile arms its counter. If the master tile must know as well, the tile says so by a request |

- **`dma_done` can be masked off** when only the landing matters, as for a frame's header word.

## Frames

- **A frame ends at the end of a packet** (owner). The next frame starts at the start of a packet, so the first word
  of that packet is the frame's header word. This is the sender's rule; the chip does not depend on it.
- **The core keeps track of frames, not Ingress.** It reads a frame's length from the header word and counts the
  words that follow. Ingress knows only packets and words.

What the core does with a background frame:

1. **It is woken by `background_service`,** and copies one word into its own memory. It is the frame's header word:
   the type, the length and the compression.
2. **It decides** from the type what to do with the frame's data.

| the frame | what the core does with its data |
|---|---|
| update words: the program image, a jump table, the data image, the memory segments | sets up the Updater's run, and writes the frame's length to `DMA_FORWARD_COUNT` |
| a header: frames 1, 3 and 4 | copies it into its own memory with the DMA |
| the register states and the advice logs, frames 7 and 8 | discards up to its own record or log, copies that with the DMA, and discards the rest |
| a frame it has no use for | discards it |

- **One wake a frame for the bulk data.** The next `background_service` is the next frame's header word.
- **A frame may feed several runs of the Updater,** as the memory segments do. The count is written once; the
  Updater takes words only as its runs need them, and the port waits in between.
- **The discard is what makes frames 7 and 8 cheap** (owner). They carry *N* records and *N* logs, and an ASIC
  needs two records and one log. Its own log is short: on gold's runs from one or two words to about 380.

## Holding the host back

- **Nothing is counted.** The Updater takes words when it can; when it cannot, the background queue fills; a full
  queue tells the host to stop sending that stream (`HOST-LINK.md`). The foreground stream is not touched.
- **The firmware has no part in it.**

## Errors

- **A packet that fails a check is fatal** (owner). `ERRORS.crc_error` is set and Ingress accepts no more packets.
- † A packet number out of order is treated the same way, with `ERRORS.sequence_error`.
- † So is a packet that passes both checks with a field that cannot be, with `ERRORS.format_error`: that is the
  sender's fault, not noise.
- **The order of the tests is `HOST-LINK.md`'s:** the header CRC, the fields, the data checksum, then the number.
  So a packet with a bad data checksum and a wrong number is a `crc_error`.
- Sending again and recovery need the way back to the host, and are for later (`HOST-LINK.md`).

## In v1.37

**Decided (owner):** v1.37's Ingress is a black box in the simulator, fed from a file.

| | |
|---|---|
| the file | gold's frame file, packetized: wrapped in the link's packets with the right checks (owner) |
| the streams | background only |
| what the black box does | checks each packet (both checks, and the packet numbers in order), strips the header word, and puts the packet's words in the queue |
| what it leaves out | everything that needs the way back to the host: acknowledgements, "resend", "lost" and recovery, "stop". The file's next packet simply waits while the queue has no room for it |

**What is built, and what is to come:**

| | as merged (WORKER4) | this design (WORKER7) |
|---|---|---|
| the queues | 16 KB a stream | 8 KB a stream, pencilled in |
| words 82 and 83 | a threshold for each stream | `DMA_FORWARD_COUNT` at 82; 83 unmapped |
| the service flag | posted when the words waiting reach the threshold, and none while words are owed | the rule above |
| the Updater's data | by DMA into its segment queue, under credits the firmware keeps | by the word port |
| the firmware | about 100 cycles awake a packet's worth, 0.17 to 0.20 of the link's time | a wake a frame |

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
- **The size of the two queues.** 8 KB each is pencilled in (owner): the size is arbitrary, and 4 KB or 16 KB
  changes little in the design. What decides it is the room needed for the packets still on their way when a
  stream says stop (`HOST-LINK.md`).
- Whether the forward count wants an event of its own for reaching 0.
- The sending side, Egress: nothing goes back to the host in v1.37.
- Whether the foreground stream's messages are frames of the same kind, and what they are.
