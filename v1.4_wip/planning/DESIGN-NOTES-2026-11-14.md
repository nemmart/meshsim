# Design notes, 2026-11-14: the file system, the host's frames, the Updater/Binder/RW peripheral, the control row

A design discussion with the owner (the evening of 2026-11-14 into 2026-11-15), for v1.4. No code was written. Each
item is marked:

- **Decided (owner):** the owner ruled on it in the discussion.
- **Proposed:** raised in the discussion and not objected to, or asked and not yet answered; to confirm.
- **Open:** known to be undecided.

Sources read first: `fs_wip/README.md`, `fs_wip/planning/SHELL-SPEC.md`, DECISIONS-v1.3 sections 12, 15, 16, 19, 20 and
22, the spec's 3.3, 4.16, 4.17.9, 4.18, 6.6, 6.7, 7 and item 119, DECISIONS-v1.2 sections 23, 25, 33 to 35, the
simulator's `FileStore` and `MemSystem` (`sim/src/units/`), and gold and jumpmap (`sim/pipeline/`).

Link names here are **+1, −1, +8, −8** (owner: better than right, left, down, up, which the spec uses throughout;
renaming the spec is a separate edit, not decided).

## 1. Where the work stands

- Nothing of the file system is built. `fs_wip/` holds its README and the shell's spec only.
- The only file-system code is the simulator's `FileStore`: trees, owned bits, `cow_open`, the walk. It has no
  directories, names or allocator.
- **Decided (owner): no code yet.** Data structures and hardware design come first. **The first code is on the host
  side: gold (Java) generates every frame of a run** (section 5); that gives the file system's simulation its workload.

## 2. The superblock, formatting and recovery

- **Decided (owner): one superblock**, in page 0. It is an ordinary page whose only special property is its fixed
  address.
- **Proposed: page 0 by line** (128 B each):

| line | contents | written |
|---|---|---|
| 0 | identity: magic (8 bytes), version (u32), the memory's size in pages (u32), the rest 0 | once, at format |
| 1 | **the root directory's entry**, in the ordinary 128-byte format: name `/`, length, location, type directory | whenever the root changes |
| 2-31 | reserved, 0 | never |

  The root has no parent to hold its length and location, and a file's depth follows from its length, so both must be
  somewhere. With its entry in line 1 the root is opened like any file, its entry at address 1. Line 0 is never
  rewritten, so there is no torn-update case and no reason for a second copy.
- **Decided (owner): a hard reset formats, in firmware.** C&C 2 writes page 0 and an empty root directory; C&C 3 starts
  its bitmap with everything else free. Nothing is zeroed. **After a hard reset the memory is assumed to hold no valid
  data** (owner), so the format is unconditional. `mkfs` is therefore firmware; a host version is only for test images.
- **Decided (owner): a soft reset (the reset button) boots into the shell**, over the memory as it stands.
- **Follows: the firmware needs a reset-cause bit from hardware.** It can't decide by looking for the magic: after a
  hard reset with power held, LPDDR may still show the old superblock.
- **Decided (owner): free space is never stored in LPDDR, and recovery rebuilds it by walking the tree.** In normal
  operation the bitmap in C&C 3's SRAM is always current, because every hard reset starts it fresh. In recovery mode the
  bitmap is rebuilt by a walk, **always** (one path, no flag), **lazily**, at the shell's first write or `rm`, so reading
  logs never pays for it.
  - Considered and dropped: a dirty flag in the superblock with the 128 KB bitmap saved at shutdown. The walk would be
    needed anyway as the fallback, so it is the only mechanism (owner).
  - The walk is only as good as the trees in LPDDR: if the shutdown didn't flush the shared cache's dirty index lines,
    the map can be wrong. Reads are suspect then too; accepted.
  - Recovery needs 128 KB for the rebuilt bitmap, in whichever unit runs the recovery file system.
- **Open:**
  - whether normal operation can resume from the shell over the same memory, or a hard reset is the only way out (it
    decides whether a state line in page 0 is ever needed);
  - which unit runs the recovery file system: the shell is on C&C 1, LPDDR is behind C&C 2's peripheral.

## 3. Copy-on-write, as now understood

The operations are DECISIONS-v1.3 section 20's `freeze(path)` and `cow_open(source, dest)`. Clarified in the discussion:

- **A file can be deleted when nothing has been copied from it.** With a → b → c, b can't be deleted while c exists (c
  shares pages b owns), nor a while b exists. Copies form a tree, deleted leaves first: c, then b, then a.
- **Freezing loses nothing.** `freeze` sets only the read-only flag in the directory entry; the owned bits in the
  file's index entries are untouched. `cow_open(b, c)` clears owned bits only in c's copy of the root. So after c is
  deleted, b still knows which pages it created.
- **A copied index page has every owned bit cleared.** When c path-copies an index page that b owns, the copy's
  entries must all be owned 0; otherwise c would free b's pages. Section 20's wording ("keep owned 0") is loose;
  `FileStore::make_private` does clear them. To tighten in the text.
- **Every page has exactly one owner**, the file that created it, so:
  - a file's own cost is its root plus what its owned bits reach, and the sum over all files is the pages used, with
    nothing counted twice (`stat`'s `pages`);
  - **the recovery walk follows only owned entries** and stops at any entry with owned 0. A file's owned pages are a
    connected piece hanging from its root (a write makes the whole path owned), so nothing owned hides below a shared
    entry, and each page is marked once;
  - **a page marked twice is corruption**, a check for `fsck`;
  - the walk must visit every file, sources included, which holds because a source outlives its copies.

## 4. Directory entries: the count, the source, `mv`

- **Decided (owner): one count field, one rule.** An entry can be removed only when its count is 0.
  - For a file: **the copies made from it** (`cow_open` adds 1 to the source's count; removing the copy subtracts 1).
  - For a directory: **the live entries in it** (a create adds 1, a remove subtracts 1). `rmdir` checks one number and
    never scans. A directory can't be a `cow_open` source, so the field never means two things.
  - A side benefit: if a directory's count is below its length / 128, it has a free slot to reuse; otherwise a create
    appends.
  - Cost: one more line write per create or remove, to the parent's entry, whose address the path walk already has.
    The root's count is in its entry, page 0 line 1.
- **Proposed: a copy's entry records its source**, the address of the source's entry, so that removing the copy can
  find the count to lower. The docs have the count but not this field.
- **Proposed: directories are not copy-on-write.** `cow_open` and `freeze` refuse a directory. A copied directory's
  entries would point at the same file roots with no owned bit, so both would believe they own the files. Bit 31 of an
  entry's location is spare (addresses are 31 bits) and could carry an owned bit if whole-directory snapshots are ever
  wanted; every use in section 22 is a single file.
- **Decided (owner): `mv` is cheap**, entries only, no data or index pages:
  - within a directory: the name rewritten in place, one line write;
  - across directories: the 128-byte entry copied to a free slot there, then the old slot freed and both counts
    adjusted. The new entry is written first, so a failure leaves two names, never none;
  - a directory moves the same way: entries hold no pointer to their parent;
  - **not across directories while the file is open** (the open-file record holds the entry's address) **or while it
    has copies** (their source fields hold it); a rename in place is fine in both cases;
  - a directory can't move into its own subtree (the destination's path walk compares entry addresses);
  - an existing destination is refused, not replaced.
- **Open:**
  - where the frozen and sparse flags, the count and the source sit in the entry's 16 reserved bytes; the type values
    (item 119);
  - a move that puts a directory deeper against the 10-level limit: refuse it, or allow it and let over-long paths fail
    to open;
  - the counts of permanent files when a working directory is released in bulk (section 6).

## 5. What the host sends: the program and the frames of a run

- **Decided (owner): a program image and a data image.**
  - **The program image** is permanent and frozen, and is assumed to be in memory, at a path, when a proof arrives.
  - **The data image** (the input and the two advice areas) is loaded on top of a copy of it, before cycle 0. That is
    section 22's cycle-0 image.
- **Decided (owner): a program is three files at one stem**: `<stem>.pages` (the program image), `<stem>.map` (the
  static jump table), `<stem>.sha` (its id, 32 bytes). **The run frame carries the full path of the stem, without an
  extension, and the three files must be at that one spot.** A layout such as `/programs/<name>/<name>` is only the
  host's convention; the chip knows the three suffixes and nothing else.
- **Decided (owner): the id is computed on the host.** The chip never hashes: it stores the id when the program is
  loaded and compares it with the run frame's, refusing the proof on a mismatch. It guards against a different build
  under the same name. (jumpmap's `proof.json` already records the ELF's SHA-256.)
- **Decided (owner): the frames assume correct, in-order delivery and carry no checksums.** The link's framing and
  retransmission on LVDS are a separate layer beneath them, stripped by Ingress. So there is no end frame: the run
  frame says what follows.
- **Decided (owner): the frames of a run** (section 16's framing: a length, a type, the data), in order:

| frame | contents |
|---|---|
| **run** | the program's stem; the working directory's name (section 6); the page counts (below); it absorbs today's frame 7 (the proof parameters) and frame 1 (the page counts) |
| **data image** | the input and advice, to be written over a copy of the program image |
| **jump-table patch** | what this run adds to the program's static jump table |
| **per segment**, 128 times | the processor state, the advice log, the memory updates (today's frames 2, 3, and 4 or 5) |

- **The run frame's contents.** From the owner: the program's stem, the data image's page count, the table of 128
  (the pages each segment writes), the patch's page count. **Proposed additions:** a format version; the program's id
  (agreed); the per-ASIC table (128 × 3: the exact pages of the start image, the final image and the Tracer's copy,
  which section 16 already calls for and gold can now compute); each segment's advice-log length, if the log is stored
  as a file; the bind's output pages (section 7). Every page count includes index pages and the one root page each
  `cow_open` costs.
- **Proposed: the data image travels as update runs**, the same address-ordered runs of words as a segment's memory
  updates, so it is "the updates before segment 0" and the Updater applies it with no new mechanism.
- **Proposed: the patch is whole pages.** Adding keys to an existing hash table can break the three-line probe limit,
  which a table built whole never meets. Gold builds the final table and sends the pages that differ from the static
  one; the count in the run frame is then exact and the chip does no inserting.
- **What gold and jumpmap produce today, against this:**

| item | today |
|---|---|
| the program image alone | not produced: `initial.pages` is the program and the inputs together (the cycle-0 image) |
| the static jump table alone | not produced: frame 6 carries the whole table per proof |
| the program's id, and its stem in a frame | no |
| the data image | no: the inputs reach memory only inside `initial.pages` |
| the patch | no (the whole table instead) |
| the per-ASIC page counts | no: frame 1 has the per-segment table only |
| the proof parameters, the state, the advice log, the updates | yes |

- **Proposed: gold also writes the program package** (the three files), which is not part of a run's frames.
- **Open:**
  - how a program is loaded in the first place (its own stream from the host);
  - whether anything of the Tracer's decode table and ROM, or of the expansion templates C&C 3 holds, is per program,
    and whether firmware images live in the file system;
  - how the chip tells the host it refused a run (the return link isn't designed).

## 6. The working directory

- **Decided (owner): the run frame names a working directory, which must not exist.** The chip creates it and puts
  the proof's files there (the images, the patched map, the segment's state and advice log). If the path exists, the
  proof is refused. Proposed: the parent must already exist; that is the host's to arrange.
- **Tentative (owner): it is removed later, perhaps once the proof has verified.** A failed proof's directory stays
  for inspection. What follows:
  - removal is a later command from the host, not something the chip does at the proof's end;
  - several working directories can exist at once, so "per-proof pages" becomes "per-working-directory pages",
    released when that directory is removed;
  - the run frame's exact counts let C&C 3 refuse a run it has no room for, before anything is written;
  - memory is the host's to manage: failures that pile up must be removed before new runs fit.
- **Open:** on a bulk release, how the permanent files' counts come down (the cycle-0 image is a copy of the program
  image, the patched map a copy of the program's map): the entries removed one by one, which walks no trees, or the
  counts reset.

## 7. The Updater/Binder/RW peripheral

- **Decided (owner): the Updater and the RW engine are one peripheral, C&C 2's, and the 512 KB line cache is in it.**
  Its name is the **Updater/Binder/RW peripheral** (owner; not "the memory system"). Inside it, from the docs: the LPDDR
  port (its only owner), the line cache (4,096 lines of 128 B), the translator that walks a file's tree through the
  cache, the page mover (whole 4 KB pages), the Updater and the Binder. Its clients: the Tracer's fill bus, the tiles'
  read requests, C&C 2's firmware through its window, and the host's stream from Ingress.
- **The Updater's job** (DECISIONS-v1.3 sections 15, 16, 22): on the ASIC running segment *k*, it writes segments 0 to
  *k* − 1's updates into the start image and segments *k* to 127's into the final image, both copy-on-write files, as
  the data arrives. It needs startup information (not gone into yet, owner).
- **Decided (owner): three queues.**
  1. **The state to run**, analogous to the Tracer's first queue.
  2. **Segment data**, the update words, arriving as peripheral writes into its queue; **about 8 KB** (owner).
  3. **Page numbers**: free pages. **C&C 2 gets the list from C&C 3 up front and keeps this queue full** (owner).
- **Proposed: the first queue holds job records**, so C&C 2 can load work ahead of the data and firmware is out of
  the stream's path even at the switch to the final image. Job types:
  - **apply updates:** the target file's root and depth, the number of update words, flags (whether a hole may be
    filled);
  - **copy a root** (`cow_open`'s page copy, owned bits cleared), the new root's page named by C&C 2 from its own list,
    so it knows every root before anything runs;
  - **bind** (below).

  On the ASIC running segment *k* the queue reads: apply segments 0 to *k* − 1 to the start image; copy its root for
  the final image and again for the Tracer's copy; apply segments *k* to 127 to the final image. Directory entries and
  freeze flags are firmware bookkeeping off the stream's path.
- **Follows: hardware does the path copying**, taking pages from the page queue, where section 20 has only software
  allocating. Software still owns allocation: it chooses the pages.
- **Proposed:**
  - an empty page queue is a wait, not an error: the Updater pauses and the credits (section 8) hold the stream;
  - the page queue is small, with a "below half" event to wake C&C 2;
  - when the stream ends, the list and the queue are both empty; anything else means the host's counts were wrong;
  - the Updater bypasses the cache for data pages (its own page buffer) and uses the cache only for index entries:
    every image is built before the Tracer starts, so its data pages are never cached while it writes them;
  - the Updater has the lowest LPDDR priority, below the file system and the Tracer: with credits it can wait, where
    the docs gave it the highest because it could not.
- **The Binder (owner).** In RAM read/write checking, something much like a memory page is read and **every two words
  are bound with the round's randomness**: a pair (a, b) becomes a + r·(b − a), halving the table. **It lives by
  C&C 2** (owner), because it works LPDDR continuously. What follows, proposed:
  - it is a job type: a source file, a destination file and r; its output pages come from the page queue; it has no
    data queue;
  - the job record must hold a field element;
  - holes stay holes (two zeros bind to zero), so the cost follows the pages that exist, and the host can count the
    output pages exactly;
  - the first round grows (64-bit words in, field elements out), then each round halves.
- **Open:** the Updater's startup information; the job record's layout; the Binder's field and element size, its
  multiplier, how many rounds it does there, and when it runs against the Tracer and the file system; where C&C 2 keeps
  the page list (data memory, or big SRAM streamed into the queue port); the page queue's depth.

## 8. Getting the stream from Ingress to the Updater

Ingress/Egress is C&C 1's peripheral and the Updater is C&C 2's, so the stream needs flow control between them. It
**runs in the background and must not take the file system's mesh bandwidth** (owner). **The data can arrive at up to
1 GB/s, one 128-bit word every 16 cycles** (owner).

- **Decided (owner): credits on a private channel, data on the normal path.**
  1. **The Updater issues credits to Ingress over a private channel**, in hardware (for example one signal pulsed per
     word the Updater takes from its data queue, against a counter in Ingress that starts at the queue's depth).
  2. **When Ingress has data and credits, it requests service from C&C 1**, as the Tracer does from C&C 3.
  3. **C&C 1 starts the send.** Ingress's standard sender fires the packets **through its +8 link into 64, to
     destination 65, as a peripheral write** into the Updater's queue port.
- **Decided (owner): there is no special data link between 72 and 73.** 72 to 74 stay source ids, as in the spec.
- What this gives: nothing new in the sender; C&C 2's firmware out of the loop; the Updater's queue always has room
  for what was sent, so stream packets never sit stalled on the control row.
- **Open (owner: not sure yet):** whether C&C 1 starts a transfer per batch of credits, or one per frame (or per 64 KB,
  the transfer limit) with the credits metering what Ingress releases into its output queue.
- **Considered and superseded:**
  - credits in firmware: C&C 2 grants C&C 1 a running total as the queue drains;
  - a fixed pace on the sender (one packet per 16 cycles), to keep bursts out of the 16-entry router FIFOs on a shared
    route. A burst of 64 packets overflows a FIFO whose output is shared; with the control row flow-controlled
    (section 9) and the credits above, the pace is not needed;
  - a private data link from Ingress to the Updater, built from the router's link parts.

## 9. The control row and the mesh's regimes

- **Decided (owner): the two regimes (strict rotation and elastic) apply only to the compute mesh, the 64 tiles. The
  controllers' part of the mesh is always flow-controlled**, so a sort can run while the Updater runs.
- **Decided (owner): the controllers' links.**

| port | C&C 1 (64) | C&C 2 (65) | C&C 3 (66) |
|---|---|---|---|
| fifth | tile 0 | tile 1 | tile 2 |
| +8 | Ingress/Egress (72) | the Updater/Binder/RW peripheral (73) | the Tracer (74) |
| +1 | C&C 2 | C&C 3 | not connected |
| −1 | not connected | C&C 1 | C&C 2 |
| −8 | not connected | not connected | not connected |

  The row is a line, with no link from C&C 3 back to C&C 1 (the table above), which is what makes back-pressure on it
  safe from deadlock.
- **Differs from the spec as written and as v1.3 and v1.35 implement it:** 4.16 calls the link to its tile the
  controller's "only link to the mesh" and makes it the controller router's down (+8) port (item 118, 2026-11-05);
  4.17.9 puts the peripheral's sender on "a router link the unit doesn't otherwise use"; the links between controllers
  are not in the spec, whose "rest of the routing inside the controllers' routers" is deferred. The planning notes of
  2026-10-27 match the table ("each drives one engine on its +8 link"). So for v1.4: the spec's 4.16 and 4.17.9, and
  the simulator's controller routing.
- **Proposed: routing.** A controller sends a packet for another controller along the row, and everything else through
  its fifth port to its tile.
- **The elastic regime**, for reference: proposed by the owner and Tony, in `sim/spec/GUIDELINES.md` section 5,
  `sim/spec/OVERVIEW.md` section 5 and `sim/planning/V14-NOTES.md`; in neither the spec, the RTL nor the simulator. It
  stays what makes the tiles' own writes to the file system safe; the host's stream no longer depends on it.

## 10. Still open from before this discussion

Raised at its start and not reached:

- **How C&C 2's core reaches the file system's own memory.** About 672 KB of open-file records and page buffers can
  only sit in big SRAM, which the controller reaches through the read port and packets to itself, not by loads and
  stores. What the peripheral's window offers C&C 2 (line access through the cache, translation, page moves) is
  undefined.
- **Paths in requests.** A request has 118 usable bits and a path up to 1,023 bytes; small `fs_write` data has the
  same problem.
- **Code size.** The file system must fit C&C 2's 32 KB of program memory; the shell's 32 KB budget is all of C&C 1's.
- **The page manager's interface on C&C 3**, and the shutdown sequence in detail.

## 11. What this changes in other documents (not yet edited)

- `fs_wip/README.md`: its open item on the recovery allocator is decided (the walk); its open item on per-proof
  pages as one run or extents no longer matters to the Updater (the page queue takes any pages); "per-proof" becomes
  "per working directory"; `mkfs` is firmware.
- `SHELL-SPEC.md` section 14, item 3 (recovery mode's free pages): decided, the walk.
- DECISIONS-v1.3 section 20: the copied index page's owned bits; "only software allocating"; the Updater's priority.
- The spec: 4.16, 4.17.9 and item 118 (section 9 above); section 16's frames (section 5 above).

## 12. Next steps

1. Rulings on the items marked Proposed.
2. The run frame's layout and the other frames' formats, then gold: the program package and every frame of a run.
3. The file system's on-disk structures in full (the directory entry's fields), then a simulation of the file system
   driven by gold's frames.
