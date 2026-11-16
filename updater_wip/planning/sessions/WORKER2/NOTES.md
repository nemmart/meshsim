# WORKER2 notes: the packetizer

Branch `updater-w2`, from main `543e1eda`. The brief is `PROMPT.md`. The link is `v1.4_wip/planning/HOST-LINK.md`
(HL); the black box that reads my file is `INGRESS-INTERFACE.md` (II); the frames are `HOST-FRAMES.md` (HF).

**Status, 2026-11-16: the plan, waiting for the owner's go-ahead. No code is written.** Gold's build and tests as
they stand on main pass here (92 s: 80 guest runs, `FramesTest`, 168 `segments` runs byte-identical to v1.36 gold's).

## 1. What is built

Three commands in gold (`updater_wip/pipeline/gold/`), beside `frames` and `frames-read`:

```sh
java -cp build gold.Main packets <frame file> <packet file> [--idle=N] [--first=N]
java -cp build gold.Main packets-read <packet file> [<frame file out>] [--compare=<frame file>] [--first=N]
     [--no-idle] [--list]
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
  what it found:

  ```
  packet file: 16,895,712 bytes: 32,004 packets on stream 2 (numbers 1 to 32,004), 0 on stream 1; 0 of length 0; 0 idle bytes
  data: 1,023,978 words in 11 frames; 31,993 packets of 32 words, 11 shorter (11 a frame's last)
  against the frames: 16,383,648 bytes; overhead 512,064 bytes (3.125%): 32,004 header words, 0 idle bytes
  checks: ok
  ```

  (the numbers above are made up). On a failure it prints one line and exits 1:
  `REFUSED header-crc: packet 37 at byte 19,008: stored 0x1234abcd, computed 0x89ef0123`. The word after `REFUSED`
  is the check's name from section 4, the same names the vectors file uses.
  - `--first=N`: the number the first packet on stream 2 must have; default 1.
  - `--no-idle`: a zero byte where a header is expected is refused (`first-byte`) instead of skipped.
  - `--list`: one line for every packet (byte, stream, number, length, both check values, the frame it belongs to).
- **`packets-vectors`** writes the vectors file and the small packet files of section 5, the same bytes every time.
  The build's test regenerates them and compares with the committed ones, so they cannot drift from the code.

The frames' commands, their output and their code are untouched.

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
- **A packet of length 0** has the data checksum of no bytes, 0x00000000 (choice C3).
- A cross-check for any implementation: the CRC-32C of all 16 header bytes of a good packet is always 0x48674BC7.

An example, worked by hand from the above (the vectors file will hold it again, made by the code): stream 2, no
flags, length 1, number 1, acknowledge 0, the data one zero word. Bytes 0 to 7 are `02 01 01 00 00 00 00 00`; the
data checksum is 0x42709AEA, bytes 8 to 11 `EA 9A 70 42`; the header CRC over those twelve bytes is 0xE1068CA0,
bytes 12 to 15 `A0 8C 06 E1`.

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
| 7 | `number` | the number is 0 or 0xFFFFF, or is not the next one on its stream: `--first` (1) for a stream's first packet, then the last number + 1, and 1 after 0xFFFFE |
| 8 | `frame-end` | (stream 2) a frame ends before the end of the packet its last word is in |
| | `first-byte` | only with `--no-idle`: a zero byte where a header is expected |

- **Frames are tracked on stream 2 only,** as the master tile does (II, "Frames"): the first data word of the first
  packet after a frame's end is a frame's header word, and the frame is 1 + its length words.
- **A packet of length 0** passes checks 2 to 7 like any other, takes the next number on its stream (C4), and
  carries nothing: it may come anywhere, inside a frame as well.
- **A stream 1 packet** is checked (2 to 7, with its own numbers from 1) and counted. Its data is not frames and is
  written nowhere (C6).
- **The flags and the acknowledge field are not checked** (the brief's list does not have them); the reader prints
  how many packets had either set (C7).

## 5. The test vectors, for WORKER3

In `updater_wip/pipeline/gold/test/packets/`, committed:

| file | what |
|---|---|
| `VECTORS.txt` | the definition of section 3 again; CRC-32C values of a few byte strings; single packets, good and bad, with every field and both checks spelled out; the list of the packet files below; a damage table |
| `small.frames`, `small.packets` | a made-up frame file of nine frames with data of 0, 1, 2, 30, 31, 32, 33, 63 and 64 words (so 1, 2, 3, 31, 32, 33, 34, 64 and 65 with the header: every way a frame can meet a packet's end), and its packet file; about 4.5 KB |
| `small-idle.packets` | the same frames with zeros between the packets in runs of 1, 15, 16, 17, 31, 33, 1,024 and 1,025 bytes, zeros before the first and after the last, and two packets of length 0, one of them inside a frame |
| `small-split.packets` | the same frames cut into packets of other legal lengths (1, 7, 32, ...): what the link allows and the packetizer never writes |
| `wrap.packets` | the same frames numbered from 0xFFFFA (`--first`), so the numbers pass 0xFFFFE to 1 |

The frame headers in `small.frames` are well-formed (type 1 to 9, compression 0) but the file is not a proof: it is
for Ingress alone. Real packet files are made with `packets` from any frame file; the report will give the SHA-256
of the ones the tests make.

**`VECTORS.txt`** is plain text, one item to a line, `#` for comments, hex bytes always in link order:

```
crc32c  313233343536373839                 e3069283      # "123456789"

packet  one-zero-word
  next-number    0x00001            # the number the receiver expects next on this stream
  stream         2
  flags          0
  length         1
  number         0x00001
  acknowledge    0x00000
  reserved       0x00
  data           00000000000000000000000000000000        # one line for each word
  data-checksum  0x42709aea         # CRC-32C of the data
  header-0-11    0201010000000000ea9a7042                # what the header CRC is over
  header-crc     0xe1068ca0
  header-word    0xe1068ca042709aea0000000000010102      # the 128-bit word, most significant digit first
  bytes          0201010000000000ea9a7042a08c06e1 00000000000000000000000000000000
  expect         ok
end
```

- **Good packets:** lengths 0, 1, 2 and 32; numbers 1, 0xFFFFE, and 1 after 0xFFFFE; stream 1; flags and
  acknowledge set.
- **Bad packets, `expect` naming the check:** one bit changed in each header field, in each check value and in the
  data, with the checks left as they were (`header-crc`, or `data-checksum` for the data); and each field wrong
  **with both checks made right again,** which is the only way to reach the checks behind the header CRC: stream 0
  with a flag set, stream 3 and 15; length 33 and 255; a reserved bit; a wrong data checksum under a good header
  CRC; number 0, 0xFFFFF, the same number again, a number skipped.
- **The damage table:** lines `damage small.packets byte 0x01a4 xor 0x08 expect data-checksum packet 4`, a change to
  a whole file and what a reader must then say, so that no damaged binary files need committing.
- **Checked three ways.** The vectors are written by the packetizer's side (`java.util.zip.CRC32C`), read back by
  the reader's side (its own CRC from section 3's algorithm), and re-derived field by field by a short Python script
  in the tests with the bit-by-bit algorithm above and nothing from the Java.

## 6. The code

| file | what |
|---|---|
| `src/gold/Packets.java` | the writing side: the header word, the cut of a frame file into packets, `java.util.zip.CRC32C`; the options |
| `src/gold/PacketFile.java` | the reading side, written from HL and section 3, sharing nothing with `Packets`: its own CRC-32C, the checks of section 4, the frames |
| `src/gold/PacketReader.java` | the `packets-read` command: the printout, `--compare`, `--list` |
| `src/gold/PacketVectors.java` | `packets-vectors` |
| `src/gold/Main.java` | the three commands added to the dispatch and the usage; nothing else |
| `test/gold/PacketsTest.java`, `test/packets/packets.sh`, `test/packets/check_vectors.py` | the tests below |
| `build.sh` | runs `test/packets/packets.sh` after the frames' tests (`PACKETS_TESTS=0` skips it) |
| `test/frames/guests.sh`, `test/frames/workloads.sh` | a hook of a few lines in each: every frame file they write is also packetized, read back and compared, before they delete it (C10) |
| `README.md` | a section "The link's packets" |

Whole files are held in memory, as gold's frame commands do; the largest frame file is 26 MB.

## 7. The tests

In the build (`./build.sh`), all local:

| the brief's check | how |
|---|---|
| round trip | every frame file `guests.sh` makes (80 runs), by the hook; `PacketsTest`: made-up frame files of every length around the cuts (frames of 1 to 70 words, one and many to a file, an empty file) |
| damage is caught | `PacketsTest`: each change of section 5's bad packets on a real guest's packet file, the reader naming the expected check; **every single bit of `small.packets` and `small-idle.packets` changed in turn** (some 90,000 changed files), each refused; a packet dropped, repeated, two swapped; the file cut short at every byte, refused unless the cut is at a frame's end |
| the wrap | `--first=0xFFFFA` both ways; and a made-up frame file of 1,048,600 one-word frames (17 MB, in memory), numbered from 1, which passes 0xFFFFE with no option |
| idle zeros | `--idle` of 1, 2, 15, 16, 17, 31, 32, 33, 1,023, 1,024, 1,025; a different count before every packet; before the first and after the last; `--no-idle` refuses them |
| the vectors | regenerated and equal to the committed files; every `expect` got from the reader; `check_vectors.py` |
| CRC-32C | both Java implementations and the Python on section 3's values and on random strings |
| nothing else changed | gold's existing tests in the same build; `git diff main` shows no frame code touched; `test/packets/frames_unchanged.sh <main's gold>`: every guest frame file byte-identical to the one main's gold writes (main's gold built in scratch, as `old_unchanged.sh` does for v1.36's) |

On the runner, one Quest task (WORKER1's generator as the pattern; the number claimed when the code is ready):

- the build with all of the above;
- the small inputs (EVM Zone trace-23, Cyclone input/22) and both workloads at 2²⁹, each frame file of
  `workloads.sh`'s three configurations packetized, read back and compared, with its sizes and SHA-256 kept;
- the 2²⁹ frame files' SHA-256 equal to Quest 200's.

The overhead should be one header word for every 32 words, 3.125%, plus at most 16 bytes a frame: about 0.5 MB on
Cyclone at 2²⁹ with the static base (16.4 MB), in about 32,000 packets. No real file comes near 0xFFFFE packets.

## 8. Choices

Where the documents are open. Each: the choice, the reason, the alternative.

- **C1. The checks are CRC-32C, as section 3** (the brief; † in HL). Stored least significant byte first, like every
  other field.
- **C2. Zeros where a header is expected are idle, one byte at a time** (the brief; † ). The reader skips them
  anywhere between packets, before the first and after the last. *Consequence:* the brief's check "the first byte
  is not zero" cannot fail by itself: a first byte turned to zero is skipped, and the packet is then refused one
  byte later, by `stream` or `header-crc`. `--no-idle` gives the other reading, where it is `first-byte`.
  Alternative: idle only in whole 16-byte words; HL's recovery says "the first byte that is not zero", so bytes.
- **C3. A data checksum over no data is 0x00000000,** the CRC-32C of no bytes, and the reader checks it. The same
  rule as every other length, with no special case. Alternative: the field is ignored when the length is 0 (HL lists
  this as open).
- **C4. A packet of length 0 takes the next number on its stream,** like any packet: HL says no packet number is 0,
  so it has one, and one rule for every packet is the simplest. Alternative: it is outside the numbering (as PCI
  Express's acknowledgements are). The packetizer writes none, so this touches only what the reader accepts and two
  vectors. **Q2.**
- **C5. A stream's first packet is number 1;** `--first` on both commands, for the wrap. Alternative: the reader
  takes whatever number comes first. II says the numbers run from 1, and a reader that takes anything would miss a
  file whose start was lost.
- **C6. Stream 1 is checked and counted, and its data is dropped.** The foreground's contents are not defined, and
  v1.37's file has none. Alternative: refuse stream 1 (but the brief has the reader accept it).
- **C7. The flags and the acknowledge field are not checked, only counted.** They are legal on the link and the
  brief's list leaves them out. Alternative: refuse them, since the file has no way back and the packetizer writes 0.
- **C8. The reader takes any legal cut of a frame into packets,** not only the packetizer's (32 words but the last):
  the rule it checks is HL's, that a frame ends at a packet's end. It prints how many packets were short without
  being a frame's last, 0 for the packetizer's files. Alternative: refuse them; then `small-split.packets` goes.
- **C9. The order of the checks is section 4's:** the header CRC first, the data checksum before the number (HL: "a
  packet counts only when both pass", so a packet with bad data has not used its number). It decides which error is
  named when two are wrong. **Q4.**
- **C10. The round trip of "every frame file gold's tests make" is a hook in WORKER1's two scripts,** a few lines
  each, counted in their own pass and fail. Alternative: scripts of my own that write every frame file a second
  time, which doubles the build's 90 s and leaves WORKER1's scripts untouched.
- **C11. The packetizer does not check the frames** beyond the lengths it needs to cut them; `frames-read --check`
  does that.
- **C12. A file cut off at a frame's end is a good file with fewer frames.** Nothing in the link or the frames says
  how many frames to expect (HF: no end frame). `--compare` catches it.

## 9. Questions for the owner

None stops the work; each is taken as the choice above unless ruled otherwise. They matter because WORKER3 writes
Ingress from the documents alone and should land on the same answers. In full in `QUESTIONS.md`.

- **Q1.** II's "Terms" says a packet has 1 to 32 words of data; HL says 0 to 32.
- **Q2.** Does a packet of length 0 take a packet number in the sequence? (C4: yes.)
- **Q3.** With idle zeros, "the first byte is not zero" is no longer a check that can fail (C2). Is that intended?
- **Q4.** A packet with a bad data checksum and a wrong number: `crc_error` or `sequence_error`? (C9: the first.)

## 10. The order of the work

1. `Packets` and the `packets` command; `PacketFile`, `PacketReader` and `packets-read`; the round trip on made-up
   files. Commit.
2. `PacketsTest`: damage, wrap, idle. Commit.
3. `packets-vectors`, `VECTORS.txt`, the small files, `check_vectors.py`. Commit, and tell WORKER3's integrator
   where they are.
4. The hooks, `build.sh`, `frames_unchanged.sh`, the README. Commit.
5. The Quest task: the small inputs and both workloads at 2²⁹.
6. `REPORT.md`, with the list for `HOST-LINK.md`.
