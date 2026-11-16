# WORKER1 questions

## Q1. The frame header is 64 bits, but frames are moved in 128-bit words (not blocking)

`HOST-FRAMES.md`, "Every frame": the header is u32 type and u32 length (8 bytes), and the data is zero-padded to whole
128-bit words "because Ingress sends 128-bit words". So a frame is 8 + 16·*n* bytes, and from the second frame on
every header and every frame's data starts 8 bytes off a 128-bit boundary of the stream. Frame 9's update words and
frame 7's 128-bit records would then reach the Updater and C&C 3 split across Ingress's words.

**What I do meanwhile:** write it as written (choice C1 in `NOTES.md`). The reader and the reference don't care.

**What I would suggest:** make the header one 128-bit word (type, length, and 64 bits that are 0), or count the
header in the length, so that every frame starts and ends on a 128-bit boundary. The first adds a field that is
always 0, which the "no reserved fields" rule may rule out; the owner's call.
