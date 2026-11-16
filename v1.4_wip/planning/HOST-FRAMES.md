# The frames from the host

Draft for review, 2026-11-15. What gold (Java) is to produce for one program and one proof, for v1.4. It replaces
DECISIONS-v1.3 section 16's frames 1 to 7. A dagger (†) marks a field or rule that is proposed and not yet ruled on.

**Written in v1.35's terms, before v1.36 was read:** `V136-SCOPE.md`.

## Frame types

In the order sent. Frames 1 and 2 are sent once per program; frames 3 to 9 once per proof.

| # | frame | when | goes to | data |
|---|---|---|---|---|
| 1 | ELF | per program | a black box, straight into LPDDR | the program image, as pages |
| 2 | base jump table | per program, optional | a black box, the pages after the program image | a jump-target table for runs to add to |
| 3 | proof header | per proof, first | C&C 1, C&C 2, C&C 3 | names, parameters, and the counts the memory frames need |
| 4 | jump table header † | per proof | C&C 2, C&C 3 | which table to start from, its hash constants, and frame 5's counts |
| 5 | jump table data | per proof | the Updater, one job | a full new table, or the pages to add to an existing one |
| 6 | data image | per proof | the Updater, one job | the input and advice areas, as update words |
| 7 | register states | per proof | C&C 3, which keeps record *s* | 128 state records |
| 8 | advice logs | per proof | C&C 3, which keeps log *s* | 128 counts, then 128 logs |
| 9 | memory segments | per proof, last | the Updater, one job | all 128 segments' update words, one contiguous stream |

*s* is the segment this ASIC runs.

---

## Every frame

| field | size | meaning |
|---|---|---|
| type | u64 | the frame's number, 1 to 9 † |
| length | u64 | the data's length in 128-bit words |
| data | | zero-padded to a whole number of 128-bit words |

- **The header is one 128-bit word** (owner, 2026-11-16): the type in its low 64 bits, the length in its high 64. With
  the data padded to 128 bits, every frame starts and ends on a 128-bit boundary, so nothing is ever split across
  Ingress's words. It was u32 type and u32 length, which put every second frame 8 bytes off (WORKER1's question 1).

- Every multi-byte value is least significant byte first.
- In the tables, a double line (═══) comes before a group of fields that repeats; a single field that repeats is
  written "× *n*".
- The data is padded to 128 bits, because Ingress sends 128-bit words, and the length counts those words. Section 16's
  header is u32 length in 32-bit words (the header included), then u32 type, with the data padded to 32 bits.
- No reserved fields anywhere: we control both ends, and a field is added when it is needed (owner).
- A string is a u32 length in bytes, then the bytes, padded to a whole number of 32-bit words. †
- Delivery is assumed correct and in order: no checksums, no end frame (owner).

---

## Update words

Frames 5, 6 and 9 carry update words: one sequence of 64-bit words, made of runs in address order.

| word | bits | meaning |
|---|---|---|
| header word | [31:0] | the start: a 64-bit-word index within the target file |
| | [63:32] | the count of values that follow |
| value × count | [63:0] | the word's new contents |

The sequence is sent in one of two forms, named by a flag in the frame that carries its counts:

| form | data |
|---|---|
| raw | the 64-bit words as they are |
| LRU | bit-packed codes, least significant bit first within each byte, against a 32-entry table that is all zeros at the frame's start |

| LRU code | bits |
|---|---|
| hit | 1, then a 5-bit index into the table |
| miss, 32-bit | 0, 0, then the value's low 32 bits (the value is zero-extended) |
| miss, 64-bit | 0, 1, then the 64-bit value |

- True LRU, move to front (spec 4.19.15's coding, section 16's frame 5).
- There is no count at the front of the data: the counts are in frame 3 or frame 4 (owner).

---

## Frame 1: ELF

The program image only, without the proof's inputs. Today's `initial.pages` holds both.

| field | size | meaning |
|---|---|---|
| file length † | u32 | the image file's length in 4 KB pages; it sets the tree's depth |
| page count | u32 | the pages that follow |
| ═══════════ | ═══════ | ═══ repeated for each page ═══ |
| offset | u64 | the page's byte offset in the file, a multiple of 4,096; ascending |
| data | 4,096 bytes | the page |

- A byte's offset in the file is its guest address − `lowest_address`.
- A page that is not listed is a hole and reads as zero.

---

## Frame 2: base jump table

Optional. A jump-target table loaded on its own, ahead of any run, for runs to name as the table they add to (frame 4).
It can be built from anything: the targets of a common run, of many runs, or the static analysis. Its layout is section
16's frame 6.

| field | size | meaning |
|---|---|---|
| k | u32 | the table has 2^k lines |
| p1 | u32 | the hash multiplier |
| p3 | u32 | the probe step |
| line × 2^k | 128 bytes | 16 entries |

| entry | size | meaning |
|---|---|---|
| key | u32 | pc − `lowest_address`; a free entry is all ones |
| base | u32 | the target's `instr_idx` |

---

## Frame 3: proof header

Nothing about the jump table is here (owner): that is frame 4.

| group | field | size | meaning |
|---|---|---|---|
| identity | format version | u32 | 1 |
| | program stem | string | the full path without an extension; `.pages` and `.sha` sit there |
| | program id | 32 bytes | must equal the stored `.sha`; computed on the host |
| | working directory | string | a full path; it must not exist |
| proof parameters | field count *n* | u32 | as section 16's frame 7 |
| | fields × *n* | u64 each | log₂ K, `lowest_address`, then a16z's `MemoryLayout` in its declaration order |
| data image | 64-bit words | u32 | the update words in frame 6 |
| | 128-bit words | u32 | frame 6's data |
| | LRU † | u32 | 0 raw, 1 LRU |
| | pages | u32 | the 4 KB pages the job takes, data and index |
| memory segments | 64-bit words × 128 | u32 each | the update words of each segment in frame 9 |
| | 128-bit words | u32 | frame 9's data, all segments |
| | LRU | u32 | 0 raw, 1 LRU, for the whole stream |
| page counts | pages written × 128 | u32 each | the 4 KB data pages each segment writes |
| | per ASIC × 128 † | 3 × u32 each | for the ASIC running segment *s*: the pages, data and index, of its start image, its final image and its Tracer's copy |

- **What C&C 2 derives for the memory-segments job** (owner): the 64-bit words before segment *s* (the sum of the first
  *s* counts), the words in *s*, and the total.
- The advice counts are in frame 8, not here (owner).

---

## Frame 4: jump table header

A run's jump table is made in one of two ways (owner): **a full new table**, or **entries added to an existing table**,
which may be a base jump table (frame 2) or an earlier run's.

| field | size | meaning |
|---|---|---|
| base table | string | the full path of the existing table to add to; length 0 means none, a full new table |
| k | u32 | the run's table has 2^k lines; with a base, the base's k |
| p1 | u32 | the hash multiplier; with a base, the base's |
| p3 | u32 | the probe step; with a base, the base's |
| 64-bit words | u32 | the update words in frame 5 |
| 128-bit words | u32 | frame 5's data |
| LRU † | u32 | 0 raw, 1 LRU |
| pages | u32 | the 4 KB pages the job takes, data and index |

- C&C 3 takes k, p1 and p3 from here for the Tracer.
- † A frame of its own, apart from frame 5, so that each frame has one destination.

---

## Frame 5: jump table data

Update words (above) against the run's table.

| | a full new table | added to an existing table |
|---|---|---|
| the run's table starts as | an empty file | a copy of the base table |
| the pages sent | every page of the table | every page that differs from the base's |

| rule | |
|---|---|
| unit of a run | whole 4 KB pages: the start and the count are multiples of 512 |
| a value | one table entry (frame 2's): the key in [31:0], the base in [63:32] |

- Gold builds the run's final table and sends pages. The chip inserts nothing. †
- Added entries must fit the base's k, p1 and p3 within the probe's three lines. If one does not, gold sends a full new
  table instead. †

---

## Frame 6: data image

Update words (above) against the cycle-0 image, which is a copy of the program image.

| rule | |
|---|---|
| a run's start | (guest address − `lowest_address`) / 8 |
| what it covers | the input, the trusted advice and the untrusted advice, as loaded by `input.bin`, `trusted_advice.bin` and `untrusted_advice.bin` |

---

## Frame 7: register states

128 records in segment order, each the Tracer's 68-word record (spec 4.21.2) as gold writes it today for section 16's
frame 2. A record is 68 × 128 bits, 1,088 bytes; the frame's data is 139,264 bytes.

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

---

## Frame 8: advice logs

| field | size | meaning |
|---|---|---|
| count × 128 | u32 each | the 64-bit advice values in each segment's log |
| log × 128 | u64 per value | the segment's advice values in row order, zero-padded to a whole number of 128-bit words |

- A log's length in 128-bit words is its count rounded up to even, then halved (owner).
- Advice is never compressed.

---

## Frame 9: memory segments

Update words (above) against the memory images: the update words of segment 0, then segment 1, through segment 127,
as one sequence.

| rule | |
|---|---|
| a run's start | (guest address − `lowest_address`) / 8 |
| a value | the word's last value in its segment |
| order | within a segment, runs in address order; segments in order |
| between segments | nothing: no padding, no count, and in the LRU form the table carries on |
| padding | to a whole number of 128-bit words, once, at the frame's end |

- The Updater finds the three stretches (before *s*, *s*, after *s*) from the counts C&C 2 derived from the proof header.

---

## Open

- The type numbers (†).
- Whether frames 5 and 6 may be LRU-coded, or are always raw.
- Frame 9 as a single frame can pass section 16's cap of 2²⁶ words (256 MiB) on a long run. The header's 64-bit length
  has no such cap; whether Ingress needs one, with the frame continuing in further frames, is open.
- The image file's length and depth: K follows the run, and the program image is loaded before any run.
- A run whose table is the base of a later run's table can't be removed before that later run's.
- Whether the per-ASIC page counts are still wanted while C&C 2's page map is a stand-in.
- How an ASIC learns its *s*.
- The advice count is in two places (frame 7's word 1 and frame 8's table); they must agree.
