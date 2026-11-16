# The frames from the host

Draft for review, 2026-11-15; revised 2026-11-16 after gold wrote the frames (`updater_wip`, WORKER1's report) and
the owner ruled on its ten suggestions. What gold (Java) produces for one program and one proof, for v1.4. It replaces
DECISIONS-v1.3 section 16's frames 1 to 7. A dagger (†) marks a field or rule that is proposed and not yet ruled on.

**Written in v1.35's terms, before v1.36 was read:** `V136-SCOPE.md`. C&C 1, 2 and 3 are v1.36's master, memory and
tracer tiles.

## Frame types

In the order sent. Frames 1 and 2 are sent once per program; frames 3 to 9 once per proof. A base jump table is frames
4 and 5 sent on their own, ahead of any proof.

| # | frame | when | goes to | data |
|---|---|---|---|---|
| 1 | program image header † | per program | C&C 2 | where the program's image goes, its length, and frame 2's counts |
| 2 | program image data | per program | the Updater, one run | the program image, as update words in whole pages |
| 3 | proof header | per proof, first | C&C 1, C&C 2, C&C 3 | names, parameters, and the counts the memory frames need |
| 4 | jump table header | per proof; or on its own, for a base table | C&C 2, C&C 3 | where the table goes, which table it starts from, its hash constants, and frame 5's counts |
| 5 | jump table data | with frame 4 | the Updater, one run | a full new table, or the pages to add to an existing one |
| 6 | data image | per proof | the Updater, one run | the input and advice areas, as update words |
| 7 | register states | per proof | C&C 3, which keeps record *s* | *N* state records |
| 8 | advice logs | per proof | C&C 3, which keeps log *s* | *N* counts, then *N* logs |
| 9 | memory segments | per proof, last | the Updater, two runs | all *N* segments' update words, one contiguous stream |

- **The Updater makes every file** (owner, 2026-11-16): the program image and a base table are written through it, as
  a run into an empty file, so no firmware path loads them.
- † The program image was one frame, "ELF", with its pages in records of 4,104 bytes. It is now a header frame and a
  data frame, as the jump table has, so that each frame has one destination and the data starts on a 128-bit word.
- † A base jump table had a frame of its own (2). It is the same thing as a run's full new table, so it uses frames 4
  and 5.

## *N* and *s*

- ***N* is the number of ASICs** (owner): 64, 128 or 256. A proof is cut into *N* segments, one for each ASIC, so
  there are always *N* segments and *N* register states.
- ***s* is the segment this ASIC runs.**
- **Neither is in any frame** (owner). Both are the system's configuration: at start-up each ASIC is told its index
  and the total, for example 69 of 128.
- A run with fewer cuts than *N* is padded with empty segments past its end: the final registers, no advice, no update
  words.

---

## Every frame

| field | bits | meaning |
|---|---|---|
| compression | [7:0] | 0 none, 1 LRU; 2 to 255 reserved |
| reserved | [63:8] | 0 |
| type | [95:64] | the frame's number, 1 to 9 † |
| length | [127:96] | the data's length in 128-bit words |
| data | | zero-padded to a whole number of 128-bit words |

- **The header is one 128-bit word** (owner, 2026-11-16). With the data padded to 128 bits, every frame starts and
  ends on a 128-bit boundary, so nothing is ever split across Ingress's words.
- **The compression type is in the header** (owner), for the frames that carry update words (2, 5, 6 and 9). It is 0
  on the others.
- **Each such frame is sent in whichever form is smaller** (owner).
- **Lengths and counts are 32 bits, never 64** (owner): the controllers then need no 64-bit arithmetic.
- Every multi-byte value is least significant byte first.
- In the tables, a double line (═══) comes before a group of fields that repeats; a single field that repeats is
  written "× *n*".
- No reserved fields anywhere but the header's: we control both ends, and a field is added when it is needed (owner).
- A string is a u32 length in bytes, then the bytes, padded to a whole number of 32-bit words. †
- Delivery is assumed correct and in order: no checksums, no end frame (owner).
- **There is no program id and no hash** (owner): the host keeps track of what it loaded under each path.

---

## Update words

Frames 2, 5, 6 and 9 carry update words: one sequence of 64-bit words, made of runs in address order.

| word | bits | meaning |
|---|---|---|
| header word | [31:0] | the start: a 64-bit-word index within the target file |
| | [63:32] | the count of values that follow |
| value × count | [63:0] | the word's new contents |

The sequence is sent in one of two forms, named by the frame header's compression type:

| form | data |
|---|---|
| none | the 64-bit words as they are |
| LRU | bit-packed codes, least significant bit first within each byte, against a 32-entry table that is all zeros at the frame's start |

| LRU code | bits |
|---|---|
| hit | 1, then a 5-bit index into the table |
| miss, 32-bit | 0, 0, then the value's low 32 bits (the value is zero-extended) |
| miss, 64-bit | 0, 1, then the 64-bit value |

- True LRU, move to front (spec 4.19.15's coding, section 16's frame 5).
- There is no count at the front of the data: the counts are in the header frame (1, 3 or 4) (owner).

## Files, lengths and page counts

- **A file's length is fixed when it is made, and sets its depth.** No update adds a level; an update past the
  length is an error.
- **A copy has its source's length** (owner). The program image's length is in frame 1; the cycle-0, start and final
  images and the Tracer's copy all inherit it. Gold uses a16z's `max_ram_k`, which is known from the program alone.
- **A jump table's length follows from k:** 2^k lines, in pages.
- **The file system's firmware makes every root** (owner), by creating the file or copying its source, before the
  Updater runs.
- **A page count is the pages the Updater takes from the free page queue:** the data pages the run writes and the
  index pages above them, **without the root.** A page written in several segments counts once for each file.
- **The firmware allocates a run's pages ahead of time** from these counts (owner), and keeps the free page queue
  full from them.

---

## Frame 1: program image header †

| field | size | meaning |
|---|---|---|
| program stem | string | the full path without an extension; the image is `<stem>.pages` |
| file length | u32 | the image file's length in 4 KB pages |
| 64-bit words | u32 | the update words in frame 2 |
| 128-bit words | u32 | frame 2's data |
| pages | u32 | the 4 KB pages the run takes |

---

## Frame 2: program image data

Update words (above) into an empty file. The program image only, without the proof's inputs.

| rule | |
|---|---|
| unit of a run | whole 4 KB pages: the start and the count are multiples of 512 |
| a run's start | (guest address − `lowest_address`) / 8 |
| what is sent | every page that holds a non-zero byte |

- A page that is not sent is a hole and reads as zero.

---

## Frame 3: proof header

Nothing about the jump table is here (owner): that is frame 4.

| group | field | size | meaning |
|---|---|---|---|
| identity | format version | u32 | 1 |
| | program stem | string | as frame 1's |
| | working directory | string | a full path; it must not exist |
| proof parameters | field count *n* | u32 | as section 16's frame 7 |
| | fields × *n* | u64 each | log₂ K, `lowest_address`, then a16z's `MemoryLayout` in its declaration order |
| data image | 64-bit words | u32 | the update words in frame 6 |
| | 128-bit words | u32 | frame 6's data |
| | pages | u32 | the 4 KB pages the run takes |
| memory segments | 64-bit words × *N* | u32 each | the update words of each segment in frame 9 |
| | 128-bit words | u32 | frame 9's data, all segments |
| page counts | pages written × *N* | u32 each | the 4 KB data pages each segment writes |
| | per ASIC × *N* | 3 × u32 each | for the ASIC running segment *s*: the pages of its start image, its final image and its Tracer's copy (kept: owner) |

- **What C&C 2 derives for the memory-segments runs** (owner): the 64-bit words before segment *s* (the sum of the
  first *s* counts), the words in *s*, and the total.
- The advice counts are in frame 8, not here (owner).

---

## Frame 4: jump table header

A table is made in one of two ways (owner): **a full new table**, or **entries added to a base table**.

| field | size | meaning |
|---|---|---|
| path † | string | where the table goes, for a base table sent on its own; length 0 for a run's table, which goes in the proof's working directory |
| base table | string | the full path of the base table to add to; length 0 means none, a full new table |
| k | u32 | the table has 2^k lines; with a base, the base's k |
| p1 | u32 | the hash multiplier; with a base, the base's |
| p3 | u32 | the probe step; with a base, the base's |
| 64-bit words | u32 | the update words in frame 5 |
| 128-bit words | u32 | frame 5's data |
| pages | u32 | the 4 KB pages the run takes |

- **A base table is established by the host** (owner), as frames 4 and 5 with a path, ahead of any proof. It can be
  built from anything: the static analysis, the targets of a common run, or of many runs.
- **A run's base is always such a table.** A run's own table is dead when the run ends (owner): it is never a later
  run's base.
- C&C 3 takes k, p1 and p3 from here for the Tracer.

---

## Frame 5: jump table data

Update words (above) against the table.

| | a full new table | added to a base table |
|---|---|---|
| the table starts as | an empty file | a copy of the base table |
| the pages sent | every page of the table | every page that differs from the base's |

| rule | |
|---|---|
| unit of a run | whole 4 KB pages: the start and the count are multiples of 512 |
| a value | one table entry: the key in [31:0], the base in [63:32] |
| a line | 128 bytes, 16 entries |
| a key | pc − `lowest_address`; a free entry is all ones |
| a base | the target's `instr_idx` |

- Gold builds the final table and sends pages. The chip inserts nothing. †
- Added entries must fit the base's k, p1 and p3 within the probe's three lines. If one does not, gold sends a full new
  table instead. †
- On the two workloads the static table as the base takes nearly every target: a run adds 0 to 3 pages, against 257
  for a full table.

---

## Frame 6: data image

Update words (above) against the cycle-0 image, which is a copy of the program image.

| rule | |
|---|---|
| a run's start | (guest address − `lowest_address`) / 8 |
| what it covers | the input, the trusted advice and the untrusted advice, as loaded by `input.bin`, `trusted_advice.bin` and `untrusted_advice.bin` |

- **Gold sends the least data** (owner): it may end a run and start another to skip a stretch of zero words, when
  that is smaller in the form being sent.
- **What is skipped is zero underneath** (owner): the program image holds nothing in the input areas.

---

## Frame 7: register states

*N* records in segment order, each the Tracer's 68-word record (spec 4.21.2) as gold writes it today for section 16's
frame 2. A record is 68 × 128 bits, 1,088 bytes; for 128 segments the frame's data is 139,264 bytes.

| word | bits | meaning |
|---|---|---|
| 0 | [63:0] | pc |
| | [95:64] | `instr_idx` |
| | [111:96] | the next offset |
| 1 | [124:102] | the segment's advice values (the same count as frame 8's) |
| 2-65 | | registers 0 to 127, two per word, the even one low |
| 66 | [71:66] | log₂ K |
| | [102:72] | 0x8000_0000 − `lowest_address` |
| | the rest | 0: the roots and depths, which C&C 3 fills |
| 67 | | 0: the template bitmap |

- *N* is the frame's length divided by 68.

---

## Frame 8: advice logs

| field | size | meaning |
|---|---|---|
| count × *N* | u32 each | the 64-bit advice values in each segment's log |
| log × *N* | u64 per value | the segment's advice values in row order, zero-padded to a whole number of 128-bit words |

- A log's length in 128-bit words is its count rounded up to even, then halved (owner).
- Advice is never compressed.

---

## Frame 9: memory segments

Update words (above) against the memory images: the update words of segment 0, then segment 1, through segment
*N* − 1, as one sequence.

| rule | |
|---|---|
| a run's start | (guest address − `lowest_address`) / 8 |
| a value | the word's last value in its segment |
| order | within a segment, runs in address order; segments in order |
| between segments | nothing: no padding, no count, and in the LRU form the table carries on |
| padding | to a whole number of 128-bit words, once, at the frame's end |

- **The Updater applies it in two runs** (owner): the words before segment *s*, into the start image; then segment
  *s* and the rest. Between them the firmware freezes the start image and makes the final image and the Tracer's
  copy from it, while the stream waits.
- The counts come from what C&C 2 derived from the proof header.

---

## What gold measured

Both workloads at 2²⁹, 128 segments (`updater_wip`, WORKER1's `REPORT.md`, before the changes of 2026-11-16).

| | EVM Zone | Cyclone |
|---|---|---|
| the program image | 2.0 MB, 491 pages | 3.3 MB, 808 pages |
| the image file's length and depth | 32,768 pages, depth 2 | 131,072 pages, depth 2 |
| frame 9, raw | 2.6 MB | 17.7 MB |
| frame 9, LRU against raw | 0.61 | 0.53 |
| pages written over the whole proof | 1,761 | 6,093 |
| one ASIC's pages, at most: start image, Tracer's copy | 145, 84 | 1,995, 1,053 |
| pages a run adds to the static base table | 3 | 0 |

## Open

- The type numbers, the split of the program image into frames 1 and 2, and frame 4's path field (†).
- Frame 9 as a single frame can pass section 16's cap of 2²⁶ words (256 MiB) on a long run. The header's 32-bit length,
  in 128-bit words, has no such cap; whether Ingress needs one, with the frame continuing in further frames, is open.
- The advice count is in two places (frame 7's word 1 and frame 8's table); they must agree.
- The fall-back from a base table to a full new table happened on no run; gold is to test it with a made-up case.

## Closed on 2026-11-16

| was open | now |
|---|---|
| the frame header's layout | one 128-bit word |
| whether frames 5 and 6 may be LRU-coded | any update-word frame may; the header says, and the smaller form is sent |
| the image file's length and depth | fixed at creation; a copy has its source's |
| a run's table as a later run's base | no: a run's table is dead when the run ends |
| whether the per-ASIC page counts are wanted | kept; the firmware allocates from them |
| how an ASIC learns its *s* | system configuration, with *N* |
