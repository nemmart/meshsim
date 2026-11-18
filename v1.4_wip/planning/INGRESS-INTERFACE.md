# Ingress/Egress and the master tile: the interface

Draft, 2026-11-16, from the discussion with the owner; rewritten that evening for the owner's forward count, which
takes the firmware out of the path of the bulk data; and extended on 2026-11-17 to the whole peripheral: Egress,
the UART, and what a reset does. Brought up to date on 2026-11-18: the register and bit numbers packed (owner),
and the errors as the link's final protocol has them. How the master tile's core (C&C 1 in v1.35's names) takes
what the host sends out of Ingress, and sends its replies through Egress. It is written so that a worker need
not guess: where the owner did not rule, a choice is made and marked with a dagger (†). (owner) marks what the
owner said.

(The file keeps its name, `INGRESS-INTERFACE.md`, because sessions at work refer to it.)

The link itself, its packets and its checks, is `HOST-LINK.md`. The frames inside the packets are `HOST-FRAMES.md`.

**v1.37 is behind this document.** "In v1.37", at the end, says what is built and what is to come.

## Terms

| term | meaning |
|---|---|
| **a packet** | one packet of the link: a header word, then 0 to 32 words of data. Not a packet of the mesh, which this document calls a `WRITE` |
| **a word** | 128 bits |
| **a stream** | foreground or background (owner's names). The foreground carries what the host sends for the proof being worked on; the background sets up the next proof |
| **a queue** | where a stream's words wait in Ingress: the foreground queue and the background queue. Egress has one, the outgoing queue |
| **a frame** | one of `HOST-FRAMES.md`'s, sent as one or more packets of a stream |

## The peripheral in short

**The master tile has two peripherals** (owner's summary, 2026-11-17): Ingress/Egress, with the UART, is
peripheral 1; the Updater is peripheral 2 (`UPDATER-INTERFACE.md`).

| in Ingress/Egress | what it is |
|---|---|
| two incoming queues | foreground and background, the host's words after the packets' checks |
| the DMA | the standard one, copying words out of either incoming queue |
| the forward count | a register: words from the background queue straight to the Updater, or dropped |
| one outgoing queue | the core's replies to the host, foreground only, with a frame count and a credit |
| the UART | a byte a store out, a byte a load in, behind FIFOs of 16 bytes |
| v1.36's control lines | the other tiles' error lines, the barrier's master side, the watchdog, the LED |
| the link | the packets' checks, numbering, sending again and forwarding along the chain (`HOST-LINK.md`). The core sees one thing of it: an error when a link has started again |
| the pin | "another ASIC follows this one on the chain" (owner): a fact about the board, known before any firmware runs (`HOST-LINK.md`, "The chain") |

- **How the core is woken:** the tile has one bit for each peripheral. `peripheral_1` is up while any event of
  Ingress/Egress that its `EVENTS_MASK` selects is pending; the core then loads the peripheral's `EVENTS` to see
  which. The Updater's events come by `peripheral_2`.
- **The two incoming queues have a service flag each** (owner), two bits of that `EVENTS` word with a mask bit
  each, so the firmware can listen to one and not the other. The UART's two events, the outgoing queue's credit,
  `dma_done`, the watchdog and the barrier's `all_arrived` are in the same word.
- **Each incoming queue has a word count** the core can read.

## The pipeline (owner, 2026-11-16)

| stage | what it does |
|---|---|
| the link | packets of up to 512 bytes of data |
| the packet buffer | holds one packet while its checks are made. A packet that passes is accepted: its header word is dropped and its data words go into its stream's queue. A packet that fails is thrown away, and the link has it sent again (`HOST-LINK.md`) |
| the two queues | foreground and background, **16 KB each,** 1,024 words (owner, 2026-11-17: four times the link's window of 8 packets, where the rule asks for twice, `HOST-LINK.md`). A stream whose queue is half full tells its neighbour up the chain to stop sending it |
| the DMA | the standard one (17.4): copies words out of either queue to an address, on this tile or another |
| the forward count | moves words from the background queue straight to the Updater, or discards them |

- **The DMA is how the firmware reads words.** It is used for the little the firmware must see: a frame's header
  word, the headers, its own record and advice log.
- **The forward count is how the bulk data moves.** The firmware wakes once for a frame, not for its words.
- **The Updater's input is flow-controlled** (owner): the two units are side by side on the master tile, so there
  are no credits. This is the only way into the Updater's data input.

## What changes in Ingress/Egress

Ingress/Egress is the master tile's peripheral 1 (`v1.36/spec/machine-spec.md` 17.6).

| part | in v1.36 | here |
|---|---|---|
| the error lines, the barrier's master side, the watchdog, the LED | built | **as they are** (words 32 to 41, `EVENTS` bits 2 and 3) |
| the host interface | pencilled in: two stream ports read and written a beat at a time by the core (`STREAM_OUT_W0`–`W2`, `STREAM_SEND`, `STREAM_SEND_LAST`, `STREAM_IN_W0`–`W2`, `STREAM_RECEIVE`, words 64 to 72; `STATUS.stream_last`; `EVENTS.stream_in` and `stream_out`) | **removed** (owner, agreed with Tony, 2026-11-17), and replaced by this document's: Ingress and Egress, below. **The new words and bits take the places the old ones leave** (owner: packed, like the other peripherals') |
| the UART | built: four words at 42 to 45 | **redesigned** (owner, 2026-11-17), below. A request to Tony (`RTL-CHANGE-REQUESTS.md`) |
| a DMA | none | **the standard one** of 17.4, reading out of Ingress's queues |
| a region | takes no `WRITE`s | **takes the outgoing queue's words** |
| a port to the Updater | | **the word port,** below |

## The registers

**The control block and the DMA block, words 0 to 31:**

| where | register or bit | access | meaning |
|---|---|---|---|
| `STATUS` [1] | `dma_busy` | R, live | the DMA's (17.4) |
| `STATUS` [2] | `foreground_ready` | R, live | the foreground queue holds words (owner: packed) |
| `STATUS` [3] | `background_ready` | R, live | the background queue holds words |
| `STATUS` [12:8] † | `uart_tx_bytes` | R, live | the bytes in the UART's TX FIFO (owner: the UART's flags are in `STATUS`) |
| `STATUS` [20:16] † | `uart_rx_bytes` | R, live | the bytes in its RX FIFO |
| `STATUS` [24] † | `rx_overrun` | R | a byte arrived with the RX FIFO full and was dropped; stays until the UART's reset |
| `STATUS` [25] † | `framing_error` | R | a frame's stop bit was 0 and the frame was dropped; stays until the UART's reset |
| `ERRORS` [0] | `tile_error` | live | v1.36's: another tile's error line is up |
| `ERRORS` [1] † | `link_error` | sticky | a link of this ASIC has started again, towards the host or away from it: what was on its way is gone ("Errors", below) |
| `ERRORS` [2] † | `format_error` | sticky | set with `link_error` when the cause was a packet made wrong twice running (`HOST-LINK.md`); for diagnosis |
| `ERRORS` [3] † | `egress_error` | sticky | a word pushed into a full outgoing queue, or with `EGRESS_COUNT` at 0; the word is dropped |
| `EVENTS` [1] | `dma_done` | counted, depth 1 | the DMA's transfer has ended (17.4) |
| `EVENTS` [2], [3] | `all_arrived`, `watchdog` | | v1.36's |
| `EVENTS` [4], [5] | `uart_rx`, `uart_tx` | live | v1.36's: a byte in the RX FIFO; room in the TX FIFO |
| `EVENTS` [6] | `foreground_service` | counted, depth 1 | the foreground queue asks for service (below) (owner: packed) |
| `EVENTS` [7] | `background_service` | counted, depth 1 | the background queue asks for service (below) |
| `EVENTS` [8] | `egress_credit` | counted, depth 2 | the outgoing queue's credit (below) |
| words 16 to 18 | `DMA_HDR`, `DMA_DEST_ADDR`, `DMA_START` | 17.4's | copies words out of one of Ingress's queues (below) |
| word 30 | `UART_TX` | W | [7:0] a byte to send. A store pushes it (owner: word 30) |
| word 31 | `UART_RX` | R | [7:0] the oldest byte received; [8] `valid`. A load pops it (owner: word 31) |

**Its own words, 32 and up:**

| word | register | access | meaning |
|---|---|---|---|
| 32 to 41 | `ERROR_LINES_0`–`2`, `ARRIVALS_0`, `_1`, `ARRIVE_EXPECT_0`, `_1`, `RELEASE`, `WATCHDOG`, `LED` | v1.36's | unchanged |
| 42 † | `UART_CONTROL` | R/W | the UART's divider and enables; 0 is its reset (below). 0 at the reset (owner) |
| 64 | `FOREGROUND_WORDS` | R, live | the foreground queue's length in words; 0 when empty (owner: words 64 to 67 for the four host words) |
| 65 | `BACKGROUND_WORDS` | R, live | the background queue's length in words |
| 66 | `DMA_FORWARD_COUNT` | R/W | the forward count (below). 0 at power-on |
| 67 | `EGRESS_COUNT` | R/W | the words of the frame being queued for the host that are still to come (below). 0 at power-on |

- **Two service flags, one for each stream** (owner).
- **Two readable registers: the foreground queue's length and the background queue's** (owner).
- **There are no thresholds.** They were how the firmware kept its cost down while it moved every word itself.
- **The numbers are packed, like the other peripherals'** (owner, 2026-11-17): the two queues' flags at
  `STATUS` [2] and [3], the three new events at `EVENTS` [6], [7] and [8], the four host words at 64 to 67. They
  are the places the old host interface leaves: its `STATUS.stream_last` was [2], its two events [6] and [7], its
  words 64 to 72.
- **Gone:** words 43 to 45 (`UART_RX`, `UART_DIVISOR`, `UART_STATUS`) and 68 to 72 (the rest of the old host
  interface), which are unmapped.
- **Still †:** the UART's fields in `STATUS`, `UART_CONTROL` at word 42, and the three new bits of `ERRORS`.
- **v1.37 as built has the numbers from before the packing** ("In v1.37"): it changes over when the old stream
  words come out of its black box (owner).
- **`START` stays undefined,** as in v1.36: the peripheral has no run.
- **No load of a word from 32 up has a side effect.** With `STREAM_RECEIVE` gone and `UART_RX` moved, that is now
  true of every peripheral. The loads that act are all below 32: `EVENTS`, which consumes, and `UART_RX`, which
  pops.

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
- **† A load gives what remains,** and may be stale by the time it is used. A load of 0 is exact: only the core
  raises the count.
- **There is no event for the count reaching 0,** and none is needed (WORKER7). The firmware learns that a frame
  is through from the Updater's `done`, or from the next service flag.
- **`count` reaches 0 when the frame's last word has entered the Updater,** not when it has been decoded. So the
  next frame's flag can come before the last run's `done`.

**As built in v1.37 (WORKER7), each †:**

- **A cycle works from the registers as they stood at its start.** A word that arrives at the end of a cycle is
  the queue's from the next, for both ways out.
- **`discard` stays as written when `count` reaches 0,** and a load reads both fields; the firmware masks [30:0].
- **A store takes effect at the end of its cycle,** as a store to any window word does.

## The word port to the Updater

| signal | from | meaning |
|---|---|---|
| `valid` | Ingress | a word is offered: `count` is not 0, `discard` is 0, and the background queue holds a word |
| `data` [127:0] | Ingress | the word at the head of the background queue |
| `ready` | the Updater | it can take a word this cycle |

- **A word moves in a cycle in which `valid` and `ready` are both 1.**
- Words arrive at the Updater in the stream's order, each once.
- **`valid` may fall without a word having moved:** a store of 0 to the count, or of a count with `discard` set,
  takes the offer back. The Updater does not depend on it staying (WORKER6: its tests drop `valid` at random).
  While the count stands and the queue holds words, Ingress holds `valid`.
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

- **The same rule as one signal** (WORKER7, as built): "asks" is a level, and the flag is posted in the cycle the
  level rises. That gives the three lines above exactly, with no special case.
- **"In flight"** (owner, 2026-11-17): a DMA transfer is in flight from a queue while the DMA is busy with a
  transfer whose `DMA_HDR.queue` names that queue, from `DMA_START` to `dma_done`. It is `STATUS.dma_busy`, a
  signal the hardware has.
- † A transfer of 0 words counts as a transfer: at its end, with words waiting, a flag is posted.
- **The core wakes, and the flag is cleared** by the load of `EVENTS` (owner).
- **`…_WORDS` is live,** so a core that loads it after the flag sees every word that has arrived by then.
- The two streams are independent: words waiting on one do not hold the other.

**For firmware writers** (WORKER7's firmware, which keeps to these):

- **A flag is a reason to look, never proof.** A transfer's end can post a flag that is still pending when the
  next count is stored. On each wake the core loads `DMA_FORWARD_COUNT` and `BACKGROUND_WORDS`, and acts only on
  a count of 0 with words waiting.
- **A count is stored, or a background transfer started, only after a load of the count reads 0** and with no
  background transfer in flight.
- **Drops that follow one another are one count:** a store replaces a running count, so two drop counts stored
  back to back lose the first's remainder.
- **A frame for the Updater waits for the last run's `done`** before it sets the next run up; a frame that is not
  the Updater's goes ahead, and the run overlaps it.

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

- **Nothing is counted.** The Updater takes words when it can; when it cannot, the background queue fills. **At
  half full** its stop bit goes up in every header this ASIC sends to its neighbour up the chain (`HOST-LINK.md`:
  "stop" is for each link). The foreground stream has a bit of its own and is not touched.
- **The other half of the queue is for what is already on its way:** "stop" holds only packets that have no
  number yet, and a window of numbered packets, 4 KB, may still arrive.
- **The firmware has no part in it.**

## Egress: sending to the host

**The core builds a frame in its own memory, tells Egress how long it is, and sends the words into Egress's
queue. Egress cuts them into packets** (owner, 2026-11-17).

| piece | what it is |
|---|---|
| the outgoing queue | 512 words, 8 KB †. Filled by 128-bit `WRITE`s into the peripheral's region, 0x04 on the master tile, part 1 † (offset bits [22:20], as on the other peripherals; the other parts unmapped) |
| `EGRESS_COUNT` | the frame's length in words (owner: "we set the number of words we want to write in the frame") |
| `egress_credit` | the wake that says there is room (owner: "we get the wakeup saying we can send up to 4KB") |

- **Replies are on the foreground stream only** (owner). Every packet Egress makes is on stream 1. The background
  stream appears on the way back only as packets of length 0, carrying its acknowledgements and its stop bit.
- **The count:** the core stores a frame's length, then pushes that many words.
  - † It counts down as the words land in the queue, not as they leave: the word that takes it to 0 is the
    frame's last, and is marked so in the queue. So the core, which pushes the words, knows when the count is 0,
    and can store the next frame's length without waiting for the link.
  - † A store while the count is not 0 is undefined. A load gives what remains.
- **Packets:** Egress makes a packet when the queue holds 32 words of a frame, or the frame's last word. A frame's
  last packet is the short one, and the next frame starts a new packet: a frame ends at the end of a packet, as on
  the way in.
- **Each packet goes out with hops 0,** behind any packet the ASIC is forwarding along the chain (`HOST-LINK.md`).
- **The credit is the replayer's and the free page queue's** (17.8.8): a counted event of depth 2; one credit is
  leave to push 256 words, 4 KB; a credit comes back each time 256 words have left the queue. There are 2 at
  power-on, so a reply of up to 8 KB needs no wake.
- **In the firmware** (owner): it registers a transmission; that turns the credit's bit on in `EVENTS_MASK`; on
  each wake it sends what the credit allows. With nothing registered the bit is off, and the credits wait.
- **What the frames are** is not settled: the replies' formats come with the foreground stream's (Open). The
  first is the reply frame of the v1.37 milestone, proposed in `updater_wip/planning/MILESTONE-v1.37.md`.
- **The outgoing queue's size is this document's,** not the link's: `HOST-LINK.md` gives Egress none. The link
  keeps its own copies of what it has sent, 8 packets, until they are acknowledged.

## The UART

Redesigned by the owner on 2026-11-17. v1.36's is built, so this is a request to Tony
(`RTL-CHANGE-REQUESTS.md`). The line, the frames (8 data bits, no parity, 1 stop bit), the two FIFOs of 16 bytes
and the two events are v1.36's.

| word | register | access | |
|---|---|---|---|
| 30 | `UART_TX` | W | [7:0] a byte to send. A store pushes it into the TX FIFO, and stalls while the FIFO is full, as today |
| 31 | `UART_RX` | R | [7:0] the oldest byte received; [8] `valid`. A load pops it. With the RX FIFO empty it reads 0 and pops nothing |
| 42 † | `UART_CONTROL` | R/W | [15:0] the divider, the cycles a bit; [16] enable read; [17] enable write |
| `STATUS` | | R | the two FIFOs' bytes, `rx_overrun`, `framing_error` (above) |

- **`UART_CONTROL` is a plain register** (owner: "just a read write register like any other"). No bit of it acts
  when written.
- **The value 0 is the UART's reset** (owner): while it is 0, both FIFOs are empty, `rx_overrun` and
  `framing_error` are 0, the line out is idle and the line in is ignored. A program resets the UART by storing 0,
  and starts it by storing its setting in a later cycle.
- **It is 0 at the reset** (owner), so the UART is off until the master tile's core turns it on. A UART that runs
  with nobody reading it is pointless, and the store is one instruction.
- **The enables only gate the line** (owner: "ignore any crap noise on the uart"):
  - enable read 0: the line in is ignored. No frame starts and no error is raised. What is in the RX FIFO stays;
  - enable write 0: nothing is sent. † A store to `UART_TX` is then dropped, so that it cannot stall for ever.
- **The line errors are status, not errors,** as in v1.36: noise on the line never raises the toast pin. They are
  cleared only by the UART's reset.
- † A value that is not 0 with a divider of 0 is undefined, as a divisor of 0 is today. A new divider takes effect
  at each direction's next idle point.
- † While the UART is in reset, `EVENTS.uart_rx` and `uart_tx` read 0, a store to `UART_TX` is dropped, and
  `UART_RX` reads 0.

## Reset

**The host link cannot be reset from inside** (owner). An ASIC's Ingress/Egress carries the other ASICs' packets
along the chain and keeps packet numbers that it shares with both neighbours.

| part | cleared by |
|---|---|
| the link: the packet buffer and the checks, the packet numbers and acknowledgements, the packets kept until acknowledged, the forwarding | power-on only. If it loses its place, "lost" and recovery are how it comes back. If its two ends no longer agree, the link starts again by itself, and the core is told by `ERRORS.link_error` (`HOST-LINK.md`) |
| what the core sees of the host: the two incoming queues, `DMA_FORWARD_COUNT`, the outgoing queue, `EGRESS_COUNT` and the credits | power-on only † |
| the UART | `UART_CONTROL` at 0, which the reset makes it (owner: the reset "just clears the UART queues / UART status") |
| the barrier's side, the watchdog, the LED | the reset, as v1.36 says |

- **v1.36 has one reset, the whole chip's,** and marks hard and soft open (item 124). This is what a soft reset
  must leave alone. Its proposal already keeps "the host interface's link".
- † Nothing empties the queues the core sees short of power-on. If a way is wanted, for a core that starts again
  with the link up, it is a design of its own: the words dropped were already acknowledged to the neighbour.

## Errors

**Nothing a damaged packet does is an error.** A packet that fails a check or arrives out of order is thrown
away and sent again, and a receiver that loses its place recovers (`HOST-LINK.md`). None of it reaches a
register. (As first written here each was fatal, and v1.37 as built still has it so, having no way back to the
host.)

**The core sees one error of the link, `ERRORS.link_error` †:** a link of this ASIC has started again. What was
on its way on that link is gone, and the frames above it cannot be trusted.

| what starts a link again (`HOST-LINK.md`, "Starting a link again") | |
|---|---|
| the neighbour powers on, or the FPGA is loaded again, under a running link | |
| a packet made wrong, twice running on the same packet number | `format_error` is set as well (owner: "we raise the ingress error") |
| the other end asks for a packet this end no longer keeps | (owner: "we raise the error flag and go for a global reset") |

- **Two ends that power on together bring the link up with no error.**
- † One bit serves both of an ASIC's links, the one towards the host and the one away from it. Whether the core
  should be told which link, and which cause, is open.
- **A peripheral's error is its tile's** (owner, 2026-11-17): `link_error`, `egress_error` and the Updater's
  errors alike raise the master tile's error, which stops everything.
- **It is not recovered from** (owner: "I don't know how you recover from an updater error"). The host resets the
  chip; the boot firmware drops what is left in the background queue with the discard count, says it is ready,
  and the proof starts over.
  - † What is left in the foreground queue it reads out with the DMA and drops: the discard is the background's.
- **A neighbour that goes silent raises nothing.** A line that reads as zeros looks idle, and the link waits. It
  is left open by the owner's ruling (`HOST-LINK.md`, Open).
- **The UART's line errors are status, not errors** ("The UART").

## In v1.37

**Decided (owner):** v1.37's Ingress/Egress is a black box in the simulator, behind its real registers. As built
it is fed from a file. The v1.37 milestone puts a driver and a chain of two ASICs in the file's place
(`updater_wip/planning/MILESTONE-v1.37.md`).

**As built on `main` (2026-11-18), against this document:**

| | as built | this document |
|---|---|---|
| the forward count, the word port, the service rule, the firmware waking once a frame | **as here** (WORKER7; merged 2026-11-17 with WORKER9's first piece) | |
| the queues | 8 KB a stream, one constant (`QUEUE_WORDS` 512) | 16 KB (owner, 2026-11-17) |
| the register and bit numbers | from before the packing: `STATUS` [3], [4]; `EVENTS` [8], [9]; words 80, 81, 82; 83 unmapped | packed: `STATUS` [2], [3]; `EVENTS` [6], [7], [8]; words 64 to 67 |
| the old stream words, and v1.36's UART at words 42 to 45 | still there | removed; the UART redesigned |
| the packet's header | 18-bit numbers for each stream, and the hops field (WORKER7) | one run of numbers for a link, both stop bits in every header, the restart flag, the data masked (`HOST-LINK.md`). A file of background packets alone is numbered the same either way; the mask changes its bytes |
| a bad packet | fatal: `ERRORS` [1] `crc_error`, [2] `sequence_error`, [3] `format_error`, and no more packets are taken | thrown away and sent again; `link_error` only when a link starts again |
| the way back to the host | none. The file's next packet waits while the queue has no room for it | acknowledgements, "resend", "lost" and recovery, "stop", the restart: WORKER8's link end (`updater_wip/link/`) |
| Egress | none: nothing goes back to the host | the outgoing queue, `EGRESS_COUNT`, the credit |
| the chain | none: one ASIC | two link ends and the forwarding buffers |

- **The streams in the file are background only.**
- **What the black box does with a packet today:** checks it (both checks, and the packet numbers in order),
  strips the header word, and puts the packet's words in the queue.

**Measured with the forward count** (WORKER7, Quest 213: the 37 proof runs pass, every image equal to gold's), at
a byte a cycle:

| | with the thresholds (WORKER4) | with the forward count |
|---|---|---|
| the master's wakes a proof, on the small guests | 79 to 97 | 46 to 54 |
| a frame of update words | a transfer of 256 words every 4 KB, 2.3 to 3 wakes each | 2 to 4 wakes and 450 to 1,200 cycles awake, whatever its length |
| the master awake, of the link's time to the hand-over | 17 to 20% | **2.4 to 7.0%** at 2²⁹ (EVM Zone 4.5 to 7.0%, Cyclone 2.4 to 4.1%) |
| the same, the boot left out | | 2.0 to 4.2% |

- **The owner's target, at most 10%, is met on every run.** The figure is all of the master's time awake, of
  which feeding the Updater is a part.
- **What is left is a cost a proof, not a cost a word:** the boot (about 86,000 cycles), parsing the proof header
  (about 14,000 at *N* 64) and frame 8's counts. So the share mostly measures how long the link takes, and rises
  on a faster link only because the same work is divided by less time. A proof is 50 to 100 ms (owner), of which
  this is 0.2 to 0.6%.
- **The hand-over comes about 3% later on the small guests** than with 16 KB queues: the smaller queue holds the
  link sooner during the boot.

**To come** (the order and the workers are `MILESTONE-v1.37.md`'s):

- gold's packetizer, reader and vectors, and v1.37's header, brought to the final protocol: one run of numbers,
  the stop bits and the restart flag where they now are, the mask, the restart packet;
- the link's part of the black box replaced by WORKER8's link end, with the chain and Egress;
- the queues back to 16 KB;
- the old host interface's words taken out, with v1.36's stream tests and any of the RTL's firmware images that
  use them, and the numbers packed at the same time;
- the UART as redesigned, with the store to `UART_CONTROL` added to every image that prints.

**The packetizer** makes the file from gold's frames:

- every packet is on stream 2, the background;
- each frame starts a new packet; a frame's packets are 32 words long but its last, which holds what remains;
- packet numbers run from 1; the acknowledge field and the flags are 0;
- both checks are CRC-32C, as `HOST-LINK.md` defines it;
- as built it does not mask the data, which the final protocol does.

**What the black box as built does with the rest:**

- † zero bytes between packets are idle, and it skips them;
- † a packet of length 0 takes no number and asks for no service: it is checked and passed over;
- † the flags and the acknowledge field are ignored, since there is no way back to the host; the hops field is
  ignored, as it is on every packet from the host (`HOST-LINK.md`), and the packetizer writes 0;
- † at the end of the file no more packets come, which is not an error.

Gold's packetizer and its vectors are `updater_wip/pipeline/gold/` (`packets`, `packets-read`,
`test/packets/VECTORS.txt`).

## Open

- The word numbers and bit numbers still marked †: the UART's fields in `STATUS`, `UART_CONTROL` at 42, and
  `ERRORS` [1] to [3].
- Whether `link_error` should say which of the ASIC's two links started again, and why.
- The foreground stream: what the host's commands and the chip's replies are, as frames.
- Whether another tile may push words into the outgoing queue itself, by its DMA, or only the master tile's core.
- The outgoing queue's size, 8 KB †, and its region, 0x04 part 1 †.
- Whether anything short of power-on empties the queues the core sees ("Reset").
- **The † of this document have not all been put to the owner.** The review of 2026-11-17 reached the service
  flags; Egress's count and credit, the UART's small rules and the reset's are still as proposed.
