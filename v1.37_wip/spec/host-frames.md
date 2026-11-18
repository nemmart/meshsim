# The frames: machine specification, v1.37

*Chapter 29 of the machine specification (`spec/machine-spec.md`), which keeps a stub there. Section numbers are the
machine specification's. What the host sends on the link (28) for one program and one proof, and the reply the
master tile sends at the end of its segment. The design documents lead here (gold, `pipeline/gold/`, writes and reads
these frames and builds reference images from them). Nothing in this chapter is hardware but what 17.10 decodes: the
update words (29.3).*

### 29.1 Frames, *N* and *s*

**The background stream's frames,** in the order sent. Frames 1 and 2 are sent once per program; frames 3 to 9 once
per proof. A **base jump table** is frames 4 and 5 sent on their own, ahead of any proof.

| Type | Frame | When | The master tile does with it (30) | Data |
| --- | --- | --- | --- | --- |
| 1 | program image header | per program | reads it | where the program's image goes, its length, frame 2's counts |
| 2 | program image data | per program | the Updater, one run | the program image, as update words in whole pages |
| 3 | proof header | per proof, first | reads it | names, parameters, and the counts the other frames need |
| 4 | jump table header | per proof; or on its own, for a base table | reads it | where the table goes, the table it starts from, its hash constants, frame 5's counts |
| 5 | jump table data | after frame 4 | the Updater, one run | a full new table, or the pages that differ from a base table |
| 6 | data image | per proof | the Updater, one run | the input and advice areas, as update words |
| 7 | register states | per proof | keeps record *s* and record *s* + 1, drops the rest | *N* state records |
| 8 | advice logs | per proof | keeps log *s*, drops the rest | *N* counts, then *N* logs |
| 9 | memory segments | per proof, last | the Updater, three runs | all *N* segments' update words, one sequence |

**The foreground stream's frame** in v1.37 is one, from the chip: **the reply** (29.14). What the host sends on the
foreground, and every other reply, is not designed *(item 185)*.

- **The Updater makes every file** (owner): the program image and a base table are written through it, as a run into
  an empty file, so no firmware path loads them.
- **One destination a frame:** a header frame and a data frame each, so that a data frame's data starts on a 128-bit
  word.
- ***N* is the number of ASICs** (owner): 64, 128 or 256. A proof is cut into *N* segments, one for each ASIC, so
  there are always *N* segments and *N* register states. **A segment is 2²² rows,** 64 windows of 65,536, by
  definition: no frame carries it. A run with fewer cuts than *N* is padded with empty segments: the final registers,
  no advice, no update words.
- ***s* is the segment this ASIC runs.** **Neither *N* nor *s* is in any frame** (owner): both are the system's
  configuration, given to each ASIC at start-up, "69 of 128" (30.1).

### 29.2 Every frame

**A header word of 128 bits, then the data:**

| Field | Bits | Meaning |
| --- | --- | --- |
| `compression` | [7:0] | 0 none, 1 LRU; 2 to 255 reserved. Not 0 only on frames of update words, 2, 5, 6 and 9 |
| reserved | [63:8] | 0 |
| `type` | [95:64] | the frame's number (29.1) |
| `length` | [127:96] | the data's length in 128-bit words, the header word not counted |
| data | | `length` 128-bit words, zero-padded to a whole 128-bit word |

- **Every frame starts and ends on a 128-bit word,** so nothing is ever split across Ingress's words (owner).
- **A frame starts at the start of a link packet and ends at the end of one** (17.6.10): the sender's rule. So a
  frame's header word is a link packet's first word.
- **Each frame of update words is sent in whichever form is smaller,** raw or LRU, raw on a tie (owner).
- **Lengths and counts are 32 bits, never 64** (owner): a controller needs no 64-bit arithmetic.
- **Every multi-byte value is least significant byte first.** A **string** is a u32 length in bytes, then the bytes,
  zero-padded to a whole number of 32-bit words.
- **No reserved fields** but the header's: a field is added when it is needed (owner). **No program id and no hash:**
  the host keeps track of what it loaded under each path. **No checksums and no end frame:** the link (28) delivers
  correctly and in order.
- In the tables below, a double line (═══) comes before a group of fields that repeats, and a single field that
  repeats is written "× *n*".

### 29.3 Update words

Frames 2, 5, 6 and 9 carry **update words:** one sequence of 64-bit words, made of **runs** in address order.

| Word | Bits | Meaning |
| --- | --- | --- |
| header word | [31:0] | the start: a 64-bit word's index within the target file |
| | [63:32] | the count of values that follow, the run's length |
| value × count | [63:0] | the 64-bit word's new contents, at the start, the start + 1, … |

**Its two forms,** by the frame header's `compression`:

| Form | The data |
| --- | --- |
| none, 0 | the 64-bit words as they are, each in 64 bits, a 128-bit word holding two, the first in its low half |
| LRU, 1 | each 64-bit word as a code, bit-packed least significant bit first, against a table of 32 entries that is all zeros at the frame's start |

| LRU code | Bits, from the first | The word |
| --- | --- | --- |
| hit | 1, then a 5-bit index | the table's entry at the index, which moves to the front |
| miss, 32 bits | 0, 0, then 32 bits | the 32 bits, zero-extended; it goes in at the front |
| miss, 64 bits | 0, 1, then 64 bits | the 64 bits; it goes in at the front |

- **True LRU, move to front:** a hit moves its entry to the front, and a miss puts its word there; the entries in
  front of the place move back one, and on a miss the last falls out. Header words are coded and enter the table as
  values do.
- **The table and the stream's bits carry on from one Updater run to the next within a frame** (`continue`,
  17.10.2), never from one frame to the next: a frame starts the table at zeros, and its padding, under 128 bits,
  ends it.
- **There is no count at the front of the data:** the counts are in the header frames, 1, 3 and 4 (owner). A frame's
  64-bit word count, for its run, is that header's.

### 29.4 Files, lengths and page counts

- **A file's length is fixed when it is made, and sets its depth** (23.5). No update adds a level; an update past its
  depth's reach is an error (17.10.1, `beyond`).
- **A copy has its source's length** (owner). The program image's length is in frame 1; the cycle-0, start and final
  images and the Tracer's copy all inherit it. **A jump table's length follows from *k*:** 2*ᵏ* lines, in pages.
- **Whoever makes the files makes every root,** by creating the file or copying its source, before the Updater runs
  (owner). In v1.37 that is the master tile, with the Updater's `RUN_CONFIG.root` (17.10.5; 30).
- **A page count** is the pages the Updater takes from the free page queue: the data pages the run writes and the
  index pages above them, **not the root.** A page written in several segments counts once for each file.
- **The master tile allocates a run's pages ahead of time** from these counts (owner), and keeps the free page queue
  full from them (30).

### 29.5 Frame 1: program image header

| Field | Size | Meaning |
| --- | --- | --- |
| program stem | string | the full path without an extension; the image is `<stem>.pages` |
| file length | u32 | the image file's length in 4 KiB pages |
| 64-bit words | u32 | the update words in frame 2 |
| 128-bit words | u32 | frame 2's data |
| pages | u32 | the 4 KiB pages frame 2's run takes |

### 29.6 Frame 2: program image data

Update words into an empty file: the program image alone, without the proof's inputs.

| Rule | |
| --- | --- |
| a run's unit | whole 4 KiB pages: the start and the count are multiples of 512 |
| a run's start | (guest address − `lowest_address`) / 8 |
| what is sent | every page that holds a non-zero byte; a page not sent is a hole and reads as zero |

### 29.7 Frame 3: proof header

| Group | Field | Size | Meaning |
| --- | --- | --- | --- |
| identity | format version | u32 | 1 |
| | program stem | string | as frame 1's |
| | working directory | string | a full path; it must not exist |
| proof parameters | field count *n* | u32 | |
| | fields × *n* | u64 each | log₂ K, `lowest_address`, then a16z's `MemoryLayout` in its declaration order |
| data image | 64-bit words | u32 | the update words in frame 6 |
| | 128-bit words | u32 | frame 6's data |
| | pages | u32 | the 4 KiB pages frame 6's run takes |
| memory segments | 64-bit words × *N* | u32 each | each segment's update words in frame 9 |
| | 128-bit words | u32 | frame 9's data, all segments |
| page counts | pages written × *N* | u32 each | the 4 KiB data pages each segment writes |
| | per ASIC × *N* | 3 × u32 each | for the ASIC running segment *s*: the pages of its start image, its final image and its Tracer's copy |

**What the master tile derives** for frame 9's three runs (owner):

| Run (17.10.5) | 64-bit words | Pages it takes |
| --- | --- | --- |
| before segment *s* | the sum of the first *s* counts | the start image's |
| segment *s* | segment *s*'s count | twice the Tracer's copy's: a page for the final image and one for the copy |
| after segment *s* | the rest | the final image's less the Tracer's copy's |

These are exact: with them the Updater's `PAGE_COUNT` is 0 at every `done`.

### 29.8 Frame 4: jump table header

A table is made one of two ways (owner): **a full new table,** or **pages that differ from a base table.**

| Field | Size | Meaning |
| --- | --- | --- |
| path | string | where the table goes, for a base table sent on its own; of length 0 for a proof's table, which goes in the proof's working directory |
| base table | string | the full path of the base table it starts from; of length 0 for none, a full new table |
| *k* | u32 | the table has 2*ᵏ* lines; with a base, the base's |
| *p*₁ | u32 | the hash multiplier; with a base, the base's |
| *p*₃ | u32 | the probe step; with a base, the base's |
| 64-bit words | u32 | the update words in frame 5 |
| 128-bit words | u32 | frame 5's data |
| pages | u32 | the 4 KiB pages frame 5's run takes |

- **A base table is established by the host** (owner), as frames 4 and 5 with a path, ahead of any proof, from
  anything: the static analysis, or the targets of one run or many.
- **A proof's base is always such a table;** a proof's own table is dead when the proof ends, never a later one's base
  (owner).
- **Which base, without a file system:** v1.37 skips the names, and a proof's base is the last table a frame 4 with a
  path established *(item 187)*.
- The Tracer takes *k*, *p*₁ and *p*₃ from here (17.9.4; 30).

### 29.9 Frame 5: jump table data

Update words against the table:

| | A full new table | A table from a base |
| --- | --- | --- |
| the table starts as | an empty file | a copy of the base |
| the pages sent | every page of the table | every page that differs from the base's |

| Rule | |
| --- | --- |
| a run's unit | whole 4 KiB pages: the start and the count are multiples of 512 |
| a value | one table entry: the key in [31:0], the base in [63:32] |
| a line | 128 bytes, 16 entries |
| a key | pc − `lowest_address`; a free entry is all ones |
| a base | the target's `instr_idx` |

- **The host builds the final table and sends pages;** the chip inserts nothing.
- **Added entries fit the base's *k*, *p*₁ and *p*₃** within the probe's three lines (17.9.4). If one does not, the host
  sends a full new table instead.

### 29.10 Frame 6: data image

Update words against the cycle-0 image, a copy of the program image.

| Rule | |
| --- | --- |
| a run's start | (guest address − `lowest_address`) / 8 |
| what it covers | the input, the trusted advice and the untrusted advice |

- **The host sends the least data** (owner): it may end a run and start another to skip zero words, when that is
  smaller in the form sent. **What is skipped is zero underneath:** the program image holds nothing in those areas.

### 29.11 Frame 7: register states

*N* records in segment order, each the Tracer's 68-word record (17.9.2), as it is at the segment's start: 1,088
bytes; for 128 segments the frame's data is 139,264 bytes. *N* is the frame's `length` over 68.

| Word | Bits | Meaning |
| --- | --- | --- |
| 0 | [63:0] | pc |
| | [95:64] | `instr_idx` |
| | [111:96] | the next offset |
| 1 | [124:102] | the advice field: the segment's advice values, the same count as frame 8's |
| 2–65 | | registers 0 to 127, two to a word, the even one low |
| 66 | [71:66] | `log_k` |
| | [102:72] | `io_size`: 0x8000_0000 − `lowest_address` |
| | the rest | 0: the roots and depths, which whoever made the files fills in (30) |
| 67 | | 0: the template bitmap |

**Record *s* + 1 is segment *s*'s end state:** the record the Tracer holds after the segment's last run (17.9.2) is
equal to it in words 0 and 2–65, the state word and the register image. The last segment has no record *N* to
compare with.

### 29.12 Frame 8: advice logs

| Field | Size | Meaning |
| --- | --- | --- |
| count × *N* | u32 each | the 64-bit advice values in each segment's log |
| ═══ log × *N* | u64 per value | the segment's advice values in row order, zero-padded to a whole 128-bit word |

- A log's length in 128-bit words is its count rounded up to even, then halved (owner).
- **The counts are *N* 32-bit words,** zero-padded to a whole 128-bit word, so the first log starts on one. Every
  *N* in use is a multiple of 4, which needs no padding.
- Advice is never compressed. A segment's count is in two places, here and in its record's advice field: they agree.

### 29.13 Frame 9: memory segments

Update words against the memory images: the update words of segment 0, then segment 1, through segment *N* − 1, as
one sequence.

| Rule | |
| --- | --- |
| a run's start | (guest address − `lowest_address`) / 8 |
| a value | the word's last value in its segment |
| order | within a segment, runs in address order; segments in order |
| between segments | nothing: no padding, no count, and in the LRU form the table carries on |
| padding | to a whole 128-bit word, once, at the frame's end |

**The Updater applies it in three runs** (30): the words before segment *s* into the start image; segment *s* into the
final image with the Tracer's copy; the rest into the final image, with `continue`. Between the first two the master
tile makes the final image and the Tracer's copy from the start image, while the stream waits.

### 29.14 The reply

**At the end of its segment the master tile sends one frame to the host,** on the foreground: frame type 16, its
`length` 71, so 72 128-bit words on the link, three link packets (owner: "something that shows that it ran").

| Word | Bits | Holds |
| --- | --- | --- |
| 0 | | the frame header: `compression` 0, `type` 16, `length` 71 |
| 1 | [31:0] | *s* |
| | [63:32] | *N* |
| | [95:64] | the rows traced |
| | [127:96] | the Updater's `PAGE_COUNT` as the last run left it |
| 2 | [63:0] | the master tile's `mcycle` at the segment's end |
| | [95:64] | the Updater's `STAT_CYCLES` |
| | [127:96] | the Updater's `STAT_STALL_DATA` |
| 3 | [31:0] | the master tile's `ERRORS` |
| | [63:32] | Ingress/Egress's `ERRORS` |
| | [95:64] | the Updater's `ERRORS` |
| | [127:96] | the Tracer's `ERRORS` |
| 4–71 | | the Tracer's record at the segment's end, 68 words (17.9.2) |

- **A reply is sent only when the segment has run:** every error stops the chip (17.6.12), so its error words are 0.
  A report of a failure goes with the recovery that is not designed *(item 181)*.
- **What it shows:** the record in words 4–71 equals the host's record *s* + 1 (29.11), so a check of it shows the
  whole path: the images built, the segment traced, the reply returned.
- **On the chain** each ASIC's reply comes up with `hops` its place (28.12), and replies of different ASICs
  interleave link packet by link packet: the host sorts them by `hops`.

### 29.15 What the frames measure

Both reference workloads at 2²⁹, 128 segments `[meas: gold]`:

| | EVM Zone | Cyclone |
| --- | --- | --- |
| the program image | 2.0 MB, 491 pages | 3.3 MB, 808 pages |
| the image file's length and depth | 32,768 pages, depth 2 | 131,072 pages, depth 2 |
| frame 9, raw | 2.6 MB | 17.7 MB |
| frame 9, LRU against raw | 0.61 | 0.53 |
| pages written over the whole proof | 1,761 | 6,093 |
| one ASIC's pages, at most: start image, Tracer's copy | 145, 84 | 1,995, 1,053 |
| pages a proof adds to the static base table | 3 | 0 |

### 29.16 Open items (chapter 26's numbering)

| item | what is to be settled | section | status |
|---|---|---|---|
| 185 | the foreground stream's frames, towards the chip and back, but the reply | 29.1 | open |
| 187 | a base table named by its path, with a file system, or the last one established | 29.8 | open |
| 188 | frame 9 as one frame can pass 2²⁶ 128-bit words on a long run: whether a frame continues in further frames | 29.13 | open |
| 189 | whether a frame for the Updater carries its own head, the names and counts, in place of a header frame before it | 29.1 | open |
| 190 | the replayer's and the Tracer's tables and templates, and the tiles' images, are in no frame (30.1) | 29.1 | open |
