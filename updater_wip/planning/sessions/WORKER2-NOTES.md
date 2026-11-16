# WORKER2 notes: the packetizer

*Closed and merged into `main` on 2026-11-16. The files this one calls `NOTES.md`, `REPORT.md` and `QUESTIONS.md` are
`WORKER2-NOTES.md`, `WORKER2-REPORT.md` and `WORKER2-QUESTIONS.md`, beside it; its Quest task is in `WORKER2-tasks/`.*

Branch `updater-w2`, from main `543e1eda`. The brief is `PROMPT.md`. The link is `v1.4_wip/planning/HOST-LINK.md`
(HL); the black box that reads my file is `INGRESS-INTERFACE.md` (II); the frames are `HOST-FRAMES.md` (HF).

**Status, 2026-11-16: built and checked; the results are in `REPORT.md`.** The owner gave the go-ahead on the plan
and ruled on the four questions (section 9). What follows is the plan as built.

## 1. What is built

Three commands in gold (`updater_wip/pipeline/gold/`), beside `frames` and `frames-read`:

```sh
java -cp build gold.Main packets <frame file> <packet file> [--idle=N] [--first=N]
java -cp build gold.Main packets-read <packet file> [<frame file out>] [--compare=<frame file>] [--first=N]
     [--no-idle] [--packets-only] [--list]
java -cp build gold.Main packets-vectors <out dir>
```

- **`packets`**, the packetizer. It walks the frame file by each frame's header word (HF, "Every frame": the length
  is bits [127:96], in 128-bit words) and writes each frame as packets on stream 2: 32 words of data in each but the
  frame's last, which holds what remains. It prints the packets, words, frames, bytes, the overhead and the packet
  file's SHA-256.
  - `--idle=N`: *N* zero bytes between every two packets (none before the first or after the last). For the tests.
  - `--first=N`: the first packet's number, 1 to 0xFFFFE; default 1. For the wrap test.
  - It refuses a frame file that is not whole 128-bit words or whose last frame runs past the end. It does not look
    at a frame's type or contents: a base table on its own, or a made-up file, packetizes like any other.
- **`packets-read`**, the reader. It checks every packet (section 4), and with `<frame file out>` writes the frames
  back. `--compare` checks them byte for byte against a frame file and names the first differing byte. It prints
  what it found (here keccak256's frames, N 64):

  ```
  packet file: 75,040 bytes: 148 packets on stream 2 (numbers 1 to 148), 0 on stream 1; 0 of length 0; 0 idle bytes
  data: 4,542 words in 9 frames; 139 packets of 32 words, 9 shorter (9 a frame's last)
  against the frames: 72,672 bytes; overhead 2,368 bytes (3.258%): 148 header words, 0 idle bytes; the frames' SHA-256 23cf06c3...
  checks: ok
  ```

  On a failure it prints one line, exits 1 and writes nothing:
  `REFUSED data-checksum: packet 3 at byte 528: stored 0xf835d1ac, computed 0x619d9aaf`. The word after `REFUSED` is
  the check's name from section 4, the same names the vectors file uses.
  - `--first=N`: the number the first packet on stream 2 must have; default 1.
  - `--no-idle`: a zero byte where a header is expected is refused (`first-byte`) instead of skipped.
  - `--packets-only`: the packets alone, as Ingress sees them: the frames are not looked at (C13).
  - `--list`: one line for every packet (byte, stream, number, length, both check values, the frame it belongs to).
- **`packets-vectors`** writes the vectors file and the small packet files of section 5, the same bytes every time.
  The build's test regenerates them and compares with the committed ones, so they cannot drift from the code.

The frames' commands, their output and their code are untouched (`Main.java` gained the three commands' dispatch).

## 2. The packet on the link, byte by byte

HL's header word, least significant byte first: byte *i* of the word holds bits [8*i*+7:8*i*], and it is the
(*i*+1)th byte on the link. A packet is the 16 header bytes, then 16 × length bytes of data in the frame file's
order.

| byte | bits | holds |
|---|---|---|
| 0 | [7:0] | stream in the low four bits, flags in the high four. A background packet with no flags is `0x02` |
| 1 | [15:8] | length, 0 to 32 |
| 2, 3 | [31:16] | packet number bits 0 to 15, low byte first |
| 4 | [39:32] | low four bits: packet number bits 16 to 19; high four bits: acknowledge bits 0 to 3 |
| 5, 6 | [55:40] | acknowledge bits 4 to 19, low byte first |
| 7 | [63:56] | reserved, 0 |
| 8 to 11 | [95:64] | data checksum, low byte first |
| 12 to 15 | [127:96] | header CRC, low byte first |

## 3. The two checks, exactly

**Both are CRC-32C** (Castagnoli; the one in iSCSI, SCTP and ext4; RFC 3720 appendix B.4):

| | |
|---|---|
| width | 32 bits |
| polynomial | 0x1EDC6F41; reflected, as the algorithm below uses it, 0x82F63B78 |
| initial value | 0xFFFFFFFF |
| input and output | reflected: each byte enters least significant bit first |
| final XOR | 0xFFFFFFFF |
| check value | CRC-32C("123456789", the nine ASCII bytes) = 0xE3069283 |
| of no bytes | 0x00000000 |
| of 16 zero bytes | 0x42709AEA |
| of 32 zero bytes | 0x8A9136AA |
| of 32 bytes of 0xFF | 0x62A8AB43 |

```
crc = 0xFFFFFFFF
for each byte b, in order:
    crc = crc XOR b
    repeat 8 times:  crc = (crc >> 1) XOR 0x82F63B78   if crc's bit 0 is 1
                     crc =  crc >> 1                    otherwise
result = crc XOR 0xFFFFFFFF
```

- **The data checksum** is the CRC-32C of the packet's data, all 16 × length bytes, in the order they go on the link
  (the first data word's byte 0 first). It is stored in header bytes 8 to 11, least significant byte first.
- **The header CRC** is the CRC-32C of header bytes 0 to 11 (bits [95:0]: the data checksum included), in link
  order. It is stored in bytes 12 to 15, least significant byte first.
- **A packet of length 0** has the data checksum of no bytes, 0x00000000 (C3).
- A cross-check for any implementation: the CRC-32C of all 16 header bytes of a good packet is always 0x48674BC7.

An example, worked by hand before any code was written, and now the first vector of `VECTORS.txt` with the same
values: stream 2, no flags, length 1, number 1, acknowledge 0, the data one zero word. Bytes 0 to 7 are
`02 01 01 00 00 00 00 00`; the data checksum is 0x42709AEA, bytes 8 to 11 `EA 9A 70 42`; the header CRC over those
twelve bytes is 0xE1068CA0, bytes 12 to 15 `A0 8C 06 E1`.

## 4. The reader's checks, in order

Where a header is expected, zero bytes are idle and skipped, in any number, before the first packet and after the
last as well. The first byte that is not zero starts a header. Then, for each packet:

| # | name | refused when |
|---|---|---|
| 1 | `truncated` | the file ends inside a header or inside a packet's data; or, at the end of the file, inside a frame |
| 2 | `header-crc` | the CRC-32C of bytes 0 to 11 is not bytes 12 to 15. Tested first: nothing in a header that fails it can be trusted |
| 3 | `stream` | the stream is not 1 or 2 |
| 4 | `length` | the length is over 32 |
| 5 | `reserved` | bits [63:56] are not 0 |
| 6 | `data-checksum` | the CRC-32C of the data is not bytes 8 to 11 |
| 7 | `number` | a packet with data: the number is not the next one on its stream: `--first` (1) for a stream's first packet, then the last number + 1, and 1 after 0xFFFFE. A packet of length 0: the number is not the one the stream's next packet with data will carry |
| 8 | `frame-end` | (stream 2) a frame ends before the end of the packet its last word is in |
| | `first-byte` | only with `--no-idle`: a zero byte where a header is expected |

- **Frames are tracked on stream 2 only,** as the master tile does (II, "Frames"): the first data word of the first
  packet after a frame's end is a frame's header word, and the frame is 1 + its length words.
- **A packet of length 0 takes no number** (owner, 2026-11-16; Q2): its number field holds the number the next
  packet with data on that stream will carry, the reader checks that it does, and the numbering does not move. So
  several in a row hold the same number, and the data packet after them holds it too. It passes checks 2 to 6 like
  any other, carries nothing, and may come anywhere, inside a frame as well.
- **A stream 1 packet** is checked (2 to 7, with its own numbers from 1) and counted. Its data is not frames and is
  written nowhere (C6).
- **The flags and the acknowledge field are not checked** (the brief's list does not have them); the reader prints
  how many packets had either set (C7).
- **A header's first byte turned to zero** is one more idle byte. The header is then taken to start at the next
  byte, and those 16 bytes fail `header-crc` (C2).

## 5. The test vectors, for WORKER3

In `updater_wip/pipeline/gold/test/packets/`, committed:

| file | bytes | what |
|---|---|---|
| `VECTORS.txt` | 63,443 | the definition of sections 2 to 4 in its heading; 8 CRC-32C values; 43 single packets, good and bad, with every field and both checks spelled out; the five packet files below with their counts and SHA-256; 32 damage lines |
| `small.frames` | 4,240 | a made-up frame file of nine frames, types 1 to 9, with data of 0, 1, 2, 30, 31, 32, 33, 63 and 64 words (so 1, 2, 3, 31, 32, 33, 34, 64 and 65 with the header: every way a frame can meet a packet's end) |
| `small.packets` | 4,464 | `small.frames` as the packetizer writes it: 14 packets |
| `small-idle.packets` | 6,795 | the same with 3 zero bytes before the first packet, runs of 1, 15, 16, 17, 31, 33, 1,024, 1,025, 0, 32, 7, 2 and 16 between the packets and 40 after the last, and three packets of length 0: one between two frames, two together inside a frame |
| `small-split.packets` | 4,656 | the same frames cut into packets of 1, 7, 32, 3, 16, 31, 2, 32 and 5 words in turn: what the link allows and the packetizer never writes (C8) |
| `wrap.packets` | 4,464 | the same numbered from 0xFFFFA (`--first`), so the numbers pass 0xFFFFE to 1 |
| `two-streams.packets` | 5,840 | the same with stream 1's packets (made-up data, one of length 0) among stream 2's, each stream with its own numbers. v1.37's file has no stream 1: for an Ingress that takes both |

The frame headers in `small.frames` are well-formed (type 1 to 9, compression 0) but the file is not a proof: it is
for Ingress alone. Real packet files are made with `packets` from any frame file; `REPORT.md` gives the SHA-256 of
the ones the runner made.

**`VECTORS.txt`** is plain text, a key and its value to a line, `#` for comments, hex bytes always in link order.
Its heading says all of this again, so that it can be read without these notes. The arrows below are not in the
file:

```
crc32c 313233343536373839 0xe3069283    # the ASCII string 123456789

packet one-zero-word
  # the first packet of a file: one word of data, all zeros
  next-number        0x00001            <- the number the receiver expects next on this stream, before the packet
  stream             2
  flags              0
  length             1
  number             0x00001
  acknowledge        0x00000
  reserved           0x00
  data               00000000000000000000000000000000        <- one line for each word
  data-checksum      0x42709aea         <- the header's field
  data-crc32c        0x42709aea         <- the CRC-32C of the data; equal in a good packet
  header-0-11        0201010000000000ea9a7042                <- what the header CRC is over
  header-crc         0xe1068ca0         <- the header's field
  header-crc32c      0xe1068ca0         <- the CRC-32C of bytes 0 to 11
  header-word        0xe1068ca042709aea0000000000010102      <- the 128-bit word, most significant digit first
  bytes              0201010000000000ea9a7042a08c06e1        <- the whole packet, 16 bytes to a line
  bytes              00000000000000000000000000000000
  expect             ok                 <- or the check that refuses it
  next-number-after  0x00002            <- for ok: the number expected next on the stream after it
end

file small.packets ... end              <- bytes, sha256, first-number, the counts, frames-file, expect

damage small.packets byte 16 xor 0x01 expect data-checksum packet 1 at 0    # the first data byte
```

- **Single packets are judged alone, as packets** (C13): Ingress knows only packets, so their data need not be
  frames.
- **Good packets (19):** lengths 1, 2 and 32; all ones; numbers 1, 0x12345, 0xFFFFE, and 1 after 0xFFFFE; stream
  1; flags and acknowledge set; the packet the bad ones are made from; **length 0 four ways** (number 7 when 7 is
  next; first in the file; when 0xFFFFE is next; on stream 1), each with `next-number-after` equal to
  `next-number`.
- **Bad packets (24), `expect` naming the check:**
  - one bit changed in each header field, in each check value and in the data, with the checks left as they were:
    `header-crc`, or `data-checksum` for the data;
  - each field wrong **with both checks made right again,** which is the only way to reach the checks behind the
    header CRC: stream 0 with a flag set, stream 3 and 15; length 33 and 64; a reserved bit, low and high; a wrong
    data checksum under a good header CRC; number 0, 0xFFFFF, the same number again, a number skipped, 0xFFFFF and 2
    where 1 is next after the wrap, a wrong number on stream 1;
  - **length 0:** holding the last packet's number; holding the one after the next; number 0; a data checksum that
    is not 0x00000000.
- **The damage table (32 lines):** one byte of `small.packets` or `small-idle.packets` changed, and what a reader
  must then say: the check, the packet counted from 1 and the byte it is taken to start at. So no damaged binary
  files are committed.
- **Checked three ways.** The vectors are written by the packetizer's side (`java.util.zip.CRC32C`), read back by
  the reader's side (its own CRC-32C; `PacketsTest`), and re-derived by `check_vectors.py`, a reader in Python
  written from `VECTORS.txt`'s heading with the bit-by-bit algorithm and nothing from the Java. It rebuilds every
  header from its fields, recomputes both checks, gets every `expect`, reads every file and applies every damage
  line. Three planted errors (a header CRC in the text, a damage line's check, a byte of `wrap.packets`) were each
  reported by it.

## 6. The code

| file | what |
|---|---|
| `src/gold/Packets.java` | the writing side: the header word (`Packet`, every field free, for the vectors), the cut of a frame file into packets, a packet file built a packet at a time (`Writer`), `java.util.zip.CRC32C`; the `packets` command |
| `src/gold/PacketFile.java` | the reading side, written from HL and sections 2 to 4, sharing nothing with `Packets`: its own CRC-32C, the checks, the frames |
| `src/gold/PacketReader.java` | the `packets-read` command: the printout, `--compare`, `--list` |
| `src/gold/PacketVectors.java` | `packets-vectors`: the vectors, the small files, the damage table, `VECTORS.txt` |
| `src/gold/Main.java` | the three commands added to the dispatch and the usage; nothing else |
| `test/gold/PacketsTest.java` | section 7's tests |
| `test/packets/packets.sh` | run by `build.sh` (`PACKETS_TESTS=0` skips it): the vectors regenerated, `check_vectors.py`, the commands, `PacketsTest` |
| `test/packets/check_vectors.py` | the third implementation |
| `test/packets/frames_unchanged.sh` | the `frames` command against another gold's |
| `test/frames/guests.sh`, `test/frames/workloads.sh` | a hook in each (the owner's choice, C10): every frame file they write is also packetized, read back and compared, before they delete it |
| `README.md` | the section "The link's packets" |
| `updater_wip/planning/sessions/WORKER2/tasks/make_task.sh` | the Quest task's generator |

Whole files are held in memory, as gold's frame commands do; the largest frame file is 26 MB.

## 7. The tests

In the build (`./build.sh`, 2 minutes here; 92 s before):

| the brief's check | how |
|---|---|
| round trip | every frame file `guests.sh` makes (80 runs), by the hook: as the packetizer writes it for N 128, with `--idle=17 --first=0xFFFFE` for N 64, with `--idle=1024` for N 256. `PacketsTest`: 132 made-up frame files (an empty one; one frame of 1 to 71 words; 60 of many frames), with the packetizer's rule checked on each (a frame of *W* words is ⌈*W*/32⌉ packets, every packet 32 words or a frame's last, numbers from 1) |
| damage is caught | `PacketsTest`, on the packets of a real guest's frames, six packets: 21 single bits of the header and 10 of the data, the checks left (`header-crc`, `data-checksum`); 13 fields wrong with the checks made right (`stream`, `length`, `reserved`, `data-checksum`, `number`); the first byte made zero, with and without `--no-idle`: 275 changes, each refused by the expected check at the expected packet and byte. A packet dropped, sent twice, swapped with the next (41 files). A frame ending inside a packet. `small.packets` cut short at each of its 4,464 bytes: `truncated`, except at the 9 frame ends. **Every single bit of the five small packet files changed in turn: 209,752 files, every one refused** |
| the wrap | files numbered from 0xFFFFA, 0xFFFFE, 0xFFFFD, 0xFFFF0 and 2, read back with the same `--first` and refused with another; and a made-up frame file of 1,048,600 one-word frames, numbered from 1, which passes 0xFFFFE to 1 with no option and ends at 26; 0xFFFFF, 0 and 2 after 0xFFFFE refused |
| idle zeros | `--idle` of 1, 2, 15, 16, 17, 31, 32, 33, 1,023, 1,024, 1,025 and 5,000; 200 files with a different count before every packet, before the first and after the last; `--no-idle` refuses them |
| length 0 (Q2) | taken anywhere, several together, around the wrap, taking no number; refused holding the last number or the one after the next; a data packet numbered as if the empty one had taken a number is refused |
| the two streams, other cuts | each stream numbered by itself; 300 files cut into packets of any legal lengths |
| the vectors | regenerated and equal to the committed files; every `expect` got from the reader and from `check_vectors.py` |
| CRC-32C | both Java implementations on the published values and 700 random strings, with the 0x48674BC7 cross-check; the Python on `VECTORS.txt`'s eight |
| nothing else changed | gold's existing tests in the same build; `git diff main` touches no frame code; `frames_unchanged.sh`: every guest frame file, its side file and the command's output byte-identical to main's gold (72 runs) |

On the runner, one Quest task (`tasks/make_task.sh`): the build; the small inputs and both workloads at 2²⁹, each
frame file of `workloads.sh`'s three configurations packetized, read back and compared; the 12 frame files'
SHA-256s equal to Quest 200's; `frames_unchanged.sh` on the guests and the small inputs.

## 8. Choices

Where the documents are open. Each: the choice, the reason, the alternative.

- **C1. The checks are CRC-32C, as section 3** (the brief; † in HL). Stored least significant byte first, like every
  other field.
- **C2. Zeros where a header is expected are idle, one byte at a time** (the brief; †; Q3, as taken). The reader
  skips them anywhere between packets, before the first and after the last. *Consequence:* the brief's check "the
  first byte is not zero" cannot fail by itself: a first byte turned to zero is skipped, and the packet is then
  refused one byte later by `header-crc` (always that check: it is tested before the stream is looked at; the plan
  said "`stream` or `header-crc`", which was wrong). `--no-idle` gives the other reading, where it is `first-byte`.
  Alternative: idle only in whole 16-byte words; HL's recovery says "the first byte that is not zero", so bytes.
- **C3. A data checksum over no data is 0x00000000,** the CRC-32C of no bytes, and the reader checks it. The same
  rule as every other length, with no special case. Alternative: the field is ignored when the length is 0 (HL lists
  this as open).
- **C4. A packet of length 0 takes no number** (ruled by the owner, Q2): its number field holds the number the next
  data packet on its stream will carry; the reader checks that and does not advance. My plan had it take the next
  number like any packet; that is gone.
- **C5. A stream's first packet is number 1;** `--first` on both commands, for the wrap. Alternative: the reader
  takes whatever number comes first. II says the numbers run from 1, and a reader that takes anything would miss a
  file whose start was lost. `--first` is stream 2's; stream 1 starts at 1.
- **C6. Stream 1 is checked and counted, and its data is dropped.** The foreground's contents are not defined, and
  v1.37's file has none. Alternative: refuse stream 1 (but the brief has the reader accept it).
- **C7. The flags and the acknowledge field are not checked, only counted.** They are legal on the link and the
  brief's list leaves them out. Alternative: refuse them, since the file has no way back and the packetizer writes 0.
- **C8. The reader takes any legal cut of a frame into packets,** not only the packetizer's (32 words but the last):
  the rule it checks is HL's, that a frame ends at a packet's end. It prints how many packets were short without
  being a frame's last, 0 for the packetizer's files. Alternative: refuse them; then `small-split.packets` goes.
- **C9. The order of the checks is section 4's** (Q4, as taken): the header CRC first, the data checksum before the
  number (HL: "a packet counts only when both pass", so a packet with bad data has not used its number). It decides
  which error is named when two are wrong. No vector is wrong in two ways.
- **C10. The round trip of "every frame file gold's tests make" is a hook in WORKER1's two scripts** (the owner's
  choice), a few lines each, counted in their own pass and fail.
- **C11. The packetizer does not check the frames** beyond the lengths it needs to cut them; `frames-read --check`
  does that.
- **C12. A file cut off at a frame's end is a good file with fewer frames.** Nothing in the link or the frames says
  how many frames to expect (HF: no end frame). `--compare` catches it.
- **C13. `--packets-only`, and single-packet vectors judged without frames** (added while building). A vector's
  data should be free (all ones, random words), and such data is not a frame; Ingress knows only packets. So the
  reader can leave the frames alone, and `VECTORS.txt`'s `frame-end` and `truncated` apply to whole files only.
  Alternative: make every vector's data a whole frame, which rules out most data.
- **C14. `two-streams.packets`** (added while building): the reader's "numbers in order on each stream" needs a
  file with both streams to be tested at all, so there is one, marked as outside v1.37.
- **C15. A data bit changed with both checks made right again is a good packet file of other frames.** No check of
  the link's can tell; only `--compare` does. It is in the tests so that this is on record.

## 9. The questions, as ruled (owner, 2026-11-16)

In full in `QUESTIONS.md`.

- **Q1.** A packet holds 0 to 32 words, as HL says.
- **Q2.** A packet of length 0 does not take a number and does not advance the numbering; its number field holds
  the number the next data packet on that stream will carry, and the reader checks that. The vectors cover it.
- **Q3.** As taken: zeros are idle, and a first byte turned to zero is caught one byte later.
- **Q4.** As taken: the data checksum is tested before the number.

## 10. The order of the work, as done

1. `Packets`, `PacketFile`, `PacketReader`, the commands; `PacketsTest`; `packets-vectors`, `VECTORS.txt`, the
   small files, `check_vectors.py`, `packets.sh` (`0587981a`).
2. The hooks, `build.sh`, `frames_unchanged.sh`, the README (`0294affc`).
3. The Quest task, `REPORT.md`, main merged in.
