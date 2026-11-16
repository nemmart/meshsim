# WORKER2 report: the packetizer

2026-11-16. Branch `updater-w2`, from main `543e1eda`. The brief is `PROMPT.md`; the plan as built, the exact
definition of the checks and the choices are `NOTES.md`; the questions and the owner's rulings are `QUESTIONS.md`.
The link is `v1.4_wip/planning/HOST-LINK.md` (HL); Ingress is `INGRESS-INTERFACE.md` (II).

## Status

| | |
|---|---|
| the packetizer, the reader, the vectors | built and checked; gold's build passes with them (2 minutes here) |
| on the runner | **Quest 201 passed** (updater-w2 `4efa4089`, kit `c59e13da…`): every step, 342 s. The packet files it made have the same SHA-256 as a rehearsal of the same task here |
| the frames | unchanged: the 12 workload frame files have Quest 200's SHA-256s, and every guest's and both small inputs' are byte-identical to main's gold |
| main | merged: at the time of writing main was still `543e1eda`, the commit this branch started from, so there was nothing to merge. **The documents' changes the owner announced were not on main yet;** main is to be merged in again when they are |

## What was built

In `updater_wip/pipeline/gold/`, beside the frames' commands, which are untouched:

| command | what |
|---|---|
| `packets <frame file> <packet file>` | the packetizer: each frame as packets on stream 2, 32 words in each but the frame's last; numbers from 1, and 1 after 0xFFFFE; acknowledge, flags and reserved 0; both checks CRC-32C. `--idle=N` and `--first=N` for the tests |
| `packets-read <packet file> [<frames out>]` | the reader: every check of the brief, in a stated order; writes the frames back; prints packets, words, frames and the overhead; refuses a bad file with one line naming the check, the packet and the byte, exit 1, nothing written. `--compare`, `--first`, `--no-idle`, `--packets-only`, `--list` |
| `packets-vectors <dir>` | writes the test vectors again, the same bytes every time |

**The writing side (`Packets`, with `java.util.zip.CRC32C`) and the reading side (`PacketFile`, with its own
CRC-32C from the definition) share no code,** as WORKER1's two sides do not.

**For WORKER3: the vectors are in `updater_wip/pipeline/gold/test/packets/`.**

| file | what |
|---|---|
| `VECTORS.txt` | readable alone: its heading has the packet byte by byte, CRC-32C as an algorithm, the checks and their order, and its own format. Then 8 CRC-32C values; 43 single packets (19 good, 24 bad) with every field, both checks as stored and as computed, the bytes, and what a reader must say; the five files below; 32 lines each changing one byte of a file and naming the check, the packet and the byte of the refusal |
| `small.frames`, `small.packets` | nine made-up frames of 1, 2, 3, 31, 32, 33, 34, 64 and 65 words, and their 14 packets |
| `small-idle.packets` | the same with idle zeros in runs of 1 to 1,025 bytes, before the first packet and after the last, and three packets of length 0 |
| `small-split.packets`, `wrap.packets`, `two-streams.packets` | other legal cuts; numbered from 0xFFFFA across the wrap; stream 1 among stream 2 (outside v1.37) |

Real packet files are made with `packets` from any frame file of gold's; the SHA-256s of the twelve the runner made
are below.

## The owner's rulings, as built

| # | ruling | in gold |
|---|---|---|
| Q1 | a packet holds 0 to 32 words | the reader takes length 0 and refuses 33 and over |
| Q2 | a packet of length 0 takes no number; its field holds the number the next data packet on its stream will carry, and the reader checks that without advancing | `Packets.Writer.empty`; `PacketFile`'s `number` check; eight vectors (four good, four bad), three such packets in `small-idle.packets`, two of them together, and one on stream 1 in `two-streams.packets`; `PacketsTest`'s "length 0" group |
| Q3 | zeros between packets are idle; a first byte turned to zero is caught one byte later | as taken; found while building: the refusal is always `header-crc`, never `stream` (the CRC is tested first) |
| Q4 | the data checksum is tested before the number | as taken |
| | the hooks in `guests.sh` and `workloads.sh` | added: every frame file they write is packetized, read back and compared |

## The checks, and their results

| the brief's check | result |
|---|---|
| **round trip:** packetize, read back, byte-identical | **ok on all of them:** the 80 guest frame files (plain; `--idle=17 --first=0xFFFFE`; `--idle=1024`); 132 made-up files of every length around the cuts; the six small-input files and the six at 2²⁹ (Quest 201) |
| **damage is caught,** the reader saying which check | **ok:** 275 changes to the fields of six packets of a real file, each refused by the check it should be, at the right packet and byte: single bits with the checks left (`header-crc`; `data-checksum` for the data), and each field wrong with both checks made right again (`stream`, `length`, `reserved`, `data-checksum`, `number`). 41 files with a packet dropped, repeated or swapped (`number`; `truncated` for the last). A frame ending inside a packet (`frame-end`). `small.packets` cut short at each of its 4,464 bytes (`truncated`, but at the 9 frame ends: C12). **Every single bit of the five small packet files changed in turn, 209,752 files: every one refused** (179,584 by `data-checksum`, 30,048 by `header-crc`, 120 by `truncated`) |
| **the wrap** | **ok, both ways:** by `--first` (0xFFFFA, 0xFFFFE, 0xFFFFD, 0xFFFF0, 2), read back with the same first number and refused with another; and a made-up file of 1,048,600 packets numbered from 1, which passes 0xFFFFE to 1 and ends at 26; 0xFFFFF, 0 and 2 after 0xFFFFE refused |
| **idle zeros** | **ok:** 1, 2, 15, 16, 17, 31, 32, 33, 1,023, 1,024, 1,025 and 5,000 between packets; 200 files with a different count before every packet, before the first and after the last; `--no-idle` refuses them (`first-byte`) |
| **nothing else changed** | **ok:** gold's own tests pass in the same build (80 guest runs, `FramesTest`, 168 `segments` runs byte-identical to v1.36 gold's, the guests as expected); the frame files, their side files and the `frames` command's output are byte-identical to main's gold on 72 guest runs and the 6 small-input runs; the 12 workload frame files have Quest 200's SHA-256s. In `src/`, only `Main.java` is changed among the files that were there (the three commands' dispatch) |
| the vectors | **ok three ways:** written by the packetizer's side; every `expect`, count and damage line got from the reader's side (`PacketsTest`) and from `check_vectors.py`, a reader in Python written from `VECTORS.txt`'s heading. The committed files are what `packets-vectors` writes. Three planted errors were each reported |
| CRC-32C | the JDK's, the reader's and the Python agree on 0xE3069283 for "123456789" and on RFC 3720's 32-byte values (zeros 0x8A9136AA, ones 0x62A8AB43, ascending 0x46DD794E, descending 0x113FDB5C); the two Java ones on 700 random strings |

**Quest 201** (the runner, 16 threads, OpenJDK 21.0.13): build 71 s; small 44 s; 2²⁹ 196 s; the frame files against
Quest 200's 0 s; the frames against main's gold 31 s. All PASS, `DONE`.

- **One line of its `VERDICTS` has no name,** `: PASS (0 s)`. It is the step `frames-as-200`: the step's function
  and the comparison's loop used the same shell variable. The verdict itself is right, and its twelve lines are in
  `frames-as-200.txt`. Fixed in `tasks/make_task.sh`; not run again.
- Quest 201 ran `4efa4089`. Nothing in `updater_wip/pipeline/gold/` has changed since.

## Sizes: the packet files and the overhead

Every frame file of `workloads.sh`'s three configurations (as WORKER1's report: a full table, uncompressed, *N* 128;
the static base, the smaller form, *N* 128, **the real configuration**; the static base, all LRU, *N* 256).

| run | configuration | frames | words | packets | frame file, bytes | packet file, bytes | overhead, bytes | |
|---|---|---|---|---|---|---|---|---|
| **EVM Zone 2²⁹** | full table, none, N 128 | 9 | 364,218 | 11,388 | 5,827,488 | 6,009,696 | 182,208 | 3.127% |
| | **static base, smaller, N 128** | 11 | 264,648 | 8,277 | 4,234,368 | **4,366,800** | 132,432 | 3.128% |
| | static base, LRU, N 256 | 11 | 273,544 | 8,555 | 4,376,704 | 4,513,584 | 136,880 | 3.127% |
| **Cyclone 2²⁹** | full table, none, N 128 | 9 | 1,604,230 | 50,138 | 25,667,680 | 26,469,888 | 802,208 | 3.125% |
| | **static base, smaller, N 128** | 11 | 1,023,978 | 32,006 | 16,383,648 | **16,895,744** | 512,096 | 3.126% |
| | static base, LRU, N 256 | 11 | 1,032,874 | 32,284 | 16,525,984 | 17,042,528 | 516,544 | 3.126% |
| EVM Zone trace-23 | full table, none, N 128 | 9 | 239,107 | 7,479 | 3,825,712 | 3,945,376 | 119,664 | 3.128% |
| | static base, smaller, N 128 | 11 | 178,107 | 5,573 | 2,849,712 | 2,938,880 | 89,168 | 3.129% |
| | static base, LRU, N 256 | 11 | 187,003 | 5,851 | 2,992,048 | 3,085,664 | 93,616 | 3.129% |
| Cyclone input/22 | full table, none, N 128 | 9 | 336,879 | 10,534 | 5,390,064 | 5,558,608 | 168,544 | 3.127% |
| | static base, smaller, N 128 | 11 | 249,833 | 7,814 | 3,997,328 | 4,122,352 | 125,024 | 3.128% |
| | static base, LRU, N 256 | 11 | 258,729 | 8,092 | 4,139,664 | 4,269,136 | 129,472 | 3.128% |

- **The overhead is one header word a packet:** 16 bytes for every 512 of data, 3.125%, and one more header for each
  frame whose length is not a multiple of 32 words, at most 16 bytes × 11 frames. No idle bytes, no packets of
  length 0.
- **No file comes near the wrap:** the largest is 50,138 packets, against 1,048,574 numbers.
- A frame of *W* words (its header word included) is ⌈*W*/32⌉ packets.

The packet files' SHA-256s (Quest 201, `results/201-w2-packets/{small,29}/<name>.packets.txt`, which also hold what
both commands printed):

```
evm-zone-29-full     7a978cdd2f39c01f76f97a9e691f4f00aa1f16a6c8b9c9dbc742702f68f46eb5
evm-zone-29-base     0094358f219c6720154b8f614e83f232a2dc4248c0cada0a782278d97f425459
evm-zone-29-base256  0e4523928f10a4142b251cf60b22a28644ce00e0e81ec6d14921da5666bc0f34
cyclone-29-full      caeef4fcf4c5086aa94d44312d541c10978ad333366b95b640fd2afaa202a255
cyclone-29-base      7936449865725a9b04d9cc0df978604e8e101621423bc21effe5b1265b4d7fb4
cyclone-29-base256   9696d7e7bd717bb3fbe893649f7d753a4693ea3ac9ea7e1b0dca07bfd31acfdf
evm-zone-23-full     737af575ef54d8ff6c9071dd3312bac636f5fcd4b162a89262990ba3200ab3d9
evm-zone-23-base     b7e3d2fed9ecd4b53bec35fa4b6d60826e95a1f7c93d92b7776df8f74af78fa2
evm-zone-23-base256  e795a3c95033eeb19a6fcf4dda0d9323e22c47dc408420c34706100a4beb53b1
cyclone-22-full      8d3d9fbbd88cad6c1d15cd1404b7ec0a7f272af7c7e3aa05cbf8da76a6a3ce8c
cyclone-22-base      f02615cd5c50d81c462665866716c95904ea856439ef5af94fdcacd858f0a3c0
cyclone-22-base256   350e684956593c0cc60676b75caad43013110fe16497703d75bbca8096ff9f19
```

## What the tests turned up

- **A header's first byte turned to zero is always refused by `header-crc`,** one byte on. My plan and Q3 said
  "`stream` or `header-crc`"; the first cannot happen, since the CRC is tested before the stream is believed.
- **Of single-bit damage, the checks behind the header CRC never fire.** In 209,752 single-bit changes no file was
  refused by `stream`, `length`, `reserved` or `number`: those need a header that is wrong and still passes its
  CRC, which noise does not make. They catch a sender's mistakes, and packets lost, repeated or out of order.
- **A file cut off at a frame's end reads as a good file with fewer frames** (C12). Nothing in the link or the
  frames says how many frames are coming.
- **Changed data under two right checks is a good file of other frames** (C15): the link's checks protect against
  noise, not against a wrong sender. Only `--compare` tells.

## What I would change in `HOST-LINK.md`

For the owner. None is needed for the packetizer to be right; each is something a second implementer has to guess
today.

1. **Define the checks exactly,** and take the dagger off: CRC-32C with its parameters and the check value
   0xE3069283 for "123456789" (`NOTES.md` section 3 has the wording), each stored least significant byte first.
2. **Say what each check is over, in bytes:** the data checksum over the data's bytes in the order they go on the
   link; the header CRC over header bytes 0 to 11.
3. **The data checksum of a packet of length 0 is 0x00000000,** the CRC-32C of no bytes, and it is checked. This
   closes an item under "Open".
4. **A packet of length 0 takes no number** (the owner's ruling): its number field holds the number the stream's
   next data packet will carry. Under "A packet", and it answers part of "Open"'s last-but-one item.
5. **A stream's first packet is number 1.** The table says "1 to 0xFFFFE, then 1 again" but not where it starts;
   II says it.
6. **A packet's number is used only when both checks pass.** "A packet counts only when both pass" implies it; the
   order of the tests (header CRC, the fields, the data checksum, the number) should be stated, since it decides
   between `crc_error` and `sequence_error`.
7. **What a receiver does with a header that passes its CRC and is not legal:** stream 0 or 3 to 15, a length over
   32, a reserved bit set. "When a packet is bad" has two rows, both for failed checks. This is a third: not noise
   but a sender's fault. II has no error bit for it.
8. **Idle is zero bytes in any number,** not whole words only (the recovery's "the first byte that is not zero"
   already means this). And its consequence: a header whose first byte is hit and becomes zero is taken for idle,
   and the receiver then fails a header CRC one byte on, which is the "lost my place" case. "The first byte is never
   zero" is how the receiver finds a packet's start; it is not a check it can make.
9. **A byte table beside the bit table:** the packet number and the acknowledge field share byte 4, the number's
   top four bits in its low half. `NOTES.md` section 2 has one.
10. **The flags and the acknowledge field in a file with no way back:** the packetizer writes 0. Whether Ingress
    ignores them or refuses them is not said; gold's reader ignores and counts them.
11. **The overhead,** for "About 1 GB a second": 16 bytes in 528, so the frames get 32/33 of the link.

And in `INGRESS-INTERFACE.md`:

1. "Terms": a packet has **0 to 32** words of data (ruled, Q1).
2. "In v1.37": the first packet's number is 1 and **a packet of length 0 takes no number** (Q2); what the black box
   does at the end of the file, and with zero bytes between packets (skips them, Q3).
3. "Errors": which bit a legal-CRC, illegal-field header sets (item 7 above), and that the data checksum is tested
   before the number (Q4).

## For the integrator

- `updater_wip/README.md`'s session table still says WORKER2 is not started; it is the integrator's to change.
- `test/frames/guests.sh` and `workloads.sh` (WORKER1's) each gained a hook of a few lines, by the owner's choice.
- The build is 2 minutes where it was 92 s: the 80 guest files' round trips are 160 more runs of the JVM.
