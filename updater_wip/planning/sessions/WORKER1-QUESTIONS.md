# WORKER1 questions

*Closed and merged into `main` on 2026-11-16. The files this one calls `NOTES.md`, `REPORT.md`, `QUESTIONS.md` and
`NEXT.md` are `WORKER1-NOTES.md`, `WORKER1-REPORT.md`, `WORKER1-QUESTIONS.md` and `WORKER1-NEXT.md`, beside it.*

## Q1. The frame header is 64 bits, but frames are moved in 128-bit words (answered)

`HOST-FRAMES.md`, "Every frame": the header is u32 type and u32 length (8 bytes), and the data is zero-padded to whole
128-bit words "because Ingress sends 128-bit words". So a frame is 8 + 16·*n* bytes, and from the second frame on
every header and every frame's data starts 8 bytes off a 128-bit boundary of the stream. Frame 9's update words and
frame 7's 128-bit records would then reach the Updater and C&C 3 split across Ingress's words.

**What I do meanwhile:** write it as written (choice C1 in `NOTES.md`). The reader and the reference don't care.

**What I would suggest:** make the header one 128-bit word (type, length, and 64 bits that are 0), or count the
header in the length, so that every frame starts and ends on a 128-bit boundary. The first adds a field that is
always 0, which the "no reserved fields" rule may rule out; the owner's call.

**Answered (owner, 2026-11-16):** the header is one 128-bit word: 64 reserved zero bits in [63:0], u32 type in
[95:64], u32 length in 128-bit words in [127:96]; lengths and counts are never 64 bits. `HOST-FRAMES.md` on main has
it; it replaces choice C1.

## Q2. Readings taken on the rulings of 2026-11-16 (no answer needed unless one is wrong)

1. **A base table's own frame 4 names no base**: a base is always established as a full new table, never by adding to
   another base. The reference refuses a base frame 4 with a base.
2. **"The least data in the form being sent" (frame 6)**: gold finds the least data for each form separately (raw and
   LRU can skip different stretches), then sends the smaller form, so frame 6's raw and LRU forms may hold different
   update words making the same image. `--verify` checks both make the same image.
3. **A tie in size is sent uncompressed.**
4. **A base table established in a frame file comes right after frames 1 and 2**, as `NEXT.md` says; the reference
   requires the run's frame 4 to name a base established in the same file.
