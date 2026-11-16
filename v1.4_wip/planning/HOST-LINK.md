# The link from the host: streams, chunks and recovery

Notes from the discussion with the owner on 2026-11-16, as a first draft. This is the layer under the frames
(`HOST-FRAMES.md`): it is what lets a frame assume it was delivered correctly and in order. Ingress and Egress are
still to be designed; this is what was said about the link they sit on. A dagger (†) marks what is proposed and not
ruled on; (owner) marks what the owner said.

## The link

- **8 LVDS data lines** (owner): one byte per transfer. Byte alignment comes from the wires, so the receiver only
  ever has to find where a chunk starts.
- **About 1 GB a second.**
- **A 128-bit word is 16 transfers,** least significant byte first, as everywhere in the design.
- **Noise can last hundreds of milliseconds** (owner: a phone call in the server room). The link has to come back
  from that by itself.

## Two streams

| stream | number | what it carries | example |
|---|---|---|---|
| active | 1 | what the host sends for the proof the mesh is working on | "run a sumcheck" |
| background | 2 | the frames that set up the next proof, and start its tracing | "run an update" |

- **Decided (owner): Ingress has two streams.**
- **Neither may block the other.** The background stream can stall for a long time, while the Updater waits on LPDDR
  or on free pages, and the active stream has to keep getting through. So each has its own queue and its own flow
  control in Ingress.
- **They interleave chunk by chunk.** The longest an active chunk waits is one background chunk.
- **A stream's number is never 0** (owner), so that the first byte of a chunk on the link is never zero (below).

## A chunk

A header word of 128 bits, then 0 to 32 words of data.

| bits | field | meaning |
|---|---|---|
| [3:0] | stream | 1 active, 2 background; never 0 |
| [7:4] | flags | below |
| [15:8] | length | the data's length in 128-bit words, 0 to 32 |
| [35:16] | packet number | this chunk's number on its stream: 1 to 0xFFFFE, then 1 again |
| [55:36] | acknowledge | the last packet number received in order on this stream; 0 for none yet |
| [63:56] | window † | how many more chunks of this stream the other side may send |
| [95:64] | data checksum | over the data |
| [127:96] | header CRC | over bits [95:0] of this word |

| flag | bit | meaning |
|---|---|---|
| resend | [4] | "your last chunk was bad: send again from the number after my acknowledge field" |
| lost | [5] | "I have lost my place" (below) |
| recovery | [6] | the answer to "lost": the first chunk after the zeros |
| spare | [7] | 0 |

- **Everything rides in 128 bits** (owner): the header is one word, and the data is whole words, so the link is
  word-aligned throughout.
- **The length is any number of words up to 32,** 16 to 512 bytes of data (owner). Any frame then cuts exactly into
  chunks.
- **A length of 0 is a header with no data** (owner): a way to acknowledge with nothing else to send.
- **Packet numbers are per stream** †, since the streams are flow-controlled apart.
- **No packet number is 0 or all ones** (owner), so a header word is never all zeros.
- **About 20 bits of packet number is enough** (owner), which left more bits for the checks. Only chunks not yet
  acknowledged are outstanding, and that is a small window.
- **The first byte on the link is never zero** (owner): it holds the stream, which is never 0.

## The two checks

1. **The data is checksummed first,** and the result goes into the header (owner).
2. **Then the header is checked,** data checksum included (owner). The header CRC is the word's last field.

- **A chunk counts only when both pass.** Together they are a 64-bit check; through 300 ms of noise, some 20 million
  words, a false chunk is out of the question.
- **The receiver can test the header as soon as it arrives,** before any data, and then knows which data checksum
  to expect.
- The packet number is checked as well, since the receiver compares it anyway to keep chunks in order.

## When a chunk is bad

| what went wrong | still in step? | what the receiver does |
|---|---|---|
| the header is good, the data checksum fails | yes: the length could be trusted | sets **resend** on its next chunk back. The link does not stop (owner) |
| the header's CRC fails | no: the chunk's length is unknown | the recovery below |

## Recovery

**The side that has lost its place** (owner):

1. finishes the chunk it is sending;
2. sends 1,024 bytes of zeros;
3. sends a chunk with **lost** set, its acknowledge field holding the last packet number it received correctly;
4. **repeats 2 and 3, over and over, until a good recovery chunk arrives** (owner).

**The other side** (owner):

1. sends 1,024 bytes of zeros;
2. sends its **recovery** chunk, and carries on from the packet after the acknowledged one;
3. † repeats these until it sees an ordinary acknowledgement.

Why the zeros work:

- **1,024 bytes is longer than the longest chunk,** 528 bytes with its header. Whatever length a confused receiver
  believes it is in the middle of, it has run out before the zeros end.
- **A zero word is never a header,** so between chunks it is plainly idle.
- **The first byte that is not zero starts a header,** since a header's first byte is never zero.

Nothing in the exchange is relied on to arrive, because the noise can cover it: that is why both sides repeat.

## Holding the host back

† Each header carries a **window** for its stream: how many more chunks of that stream the other side may send. When
the Updater stalls, the background stream's window closes and the active stream's stays open.

## What a sender must keep

- A sender keeps every chunk until it is acknowledged, to send it again.
- **The host has the memory for that.**
- **The chip does not.** On the way back to the host it keeps a small window of chunks, and stops when the
  acknowledgements stop.

## What this is, in other designs

The owner's remark: there must be a mountain of literature on building these. There is. The closest:

| design | what matches |
|---|---|
| PCI Express, the data link layer | nearly all of it: a sequence number and a CRC on every packet, ACK and NAK, a replay buffer at the sender, and credits for flow control kept apart for each virtual channel. The two streams are two virtual channels. Its NAK is the "resend" flag |
| Interlaken | several channels on one chip-to-chip link, with flow control for each in band |
| HDLC and what came from it | the sliding window: go-back-N and selective repeat |
| ATM and GFP | finding where packets start by hunting for a header whose CRC checks |
| Xilinx Aurora | a light link protocol for FPGAs, if the host's side is one |

- **† The flow control and the replay buffer are taken from PCI Express,** not invented here.
- One difference: PCI Express never loses its place, because its physical layer marks packet starts with symbols of
  their own. This link marks nothing, which is why it has the zeros and "lost".

## Open

- What the active stream carries, and how large its messages are.
- Whether the recovery chunk states the packet number it restarts from, or that is the "lost" chunk's number plus one.
- The window: its unit, and how large the queues behind it are.
- The checksums' polynomials, and what a data checksum over no data is.
- The timers: how soon an unacknowledged chunk is sent again without a "resend".
- Whether a chunk may hold the end of one frame and the start of the next.
- How the chip acknowledges a stream it has nothing to send on: a chunk of length 0 on that stream.
- How large the chip's own window of kept chunks is.
