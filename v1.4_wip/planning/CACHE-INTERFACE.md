# The cache from outside: the look-up port, the fill port, and what the cache promises

Written 2026-11-16 (`updater_wip`, WORKER5) and **agreed at the review with the owner the same day,** with one
change: a client's number is the same on the look-up port and on AXI. It is what WORKER5 builds the cache to and
WORKER6 the Updater's controller. A dagger (†) marks a choice that is proposed here and not yet
ruled on. Where a line says (owner), the design notes record it as the owner's ruling. The open questions are at the
end.

**What it is for.** With `AXI-INTERFACE.md` and `UPDATER-INTERFACE.md`, this is all a worker needs to build the
Updater's controller so that it works with the cache's controller the first time they meet. It says what the cache
does, seen from its ports, and nothing of how it is built.

**The C++ types** are `v1.37_wip/src/units/lookup_port.h`: no logic, what both workers compile against.

**Where it comes from:** `DESIGN-NOTES-2026-11-15.md` section 3 (the cache and its look-up interface);
`DESIGN-NOTES-2026-11-16.md` sections 1 to 4 (the cache as a controller and a cache memory; flush and discard;
updates that write through; the sequence of an `update`) and 7; `UPDATER-INTERFACE.md` ("Its other connections");
`V136-SCOPE.md` section 4 (the fill port); v1.36's `fill` (`v1.36/spec/machine-spec.md` 3, 23.6;
`v1.36/spec/jolt/tracer.md` 17.9.5, 17.9.14). Where the notes disagree, the later stands.

```
 the Updater ─────── look-up port 1 ──┐                               ┌── the fill port ── the Tracer
 the RW engine ───── look-up port 2 ──┤   ┌─────────────────────┐     │   (v1.36's, unchanged)
 the Binder (later)─ look-up port 3 ──┼──►│ the cache's          │◄────┘
                                      │   │ controller           │── its AXI port (ID 0) ── the AXI arbiter
 the shared reply bus ◄───────────────┘   │ + the cache memory   │
   (to every look-up client)              └─────────────────────┘
```

## Terms

| term | meaning |
|---|---|
| **a file** | a tree of 4 KB pages: index pages of 1,024 entries above the data pages, its depth 0 to 3 following its length (machine-spec 23.5). A file of depth *d* has 2^(10 *d*) pages |
| **an entry** | 32 bits, the stored form: [30:0] the address of the page it names, in 128-byte units, [31] the **owned** bit, set when that page is the file's own and clear when it is shared with another file; **0xFFFF_FFFF is no page, a hole,** at every level (second note, section 3) |
| **a root** | the address of a file's top page, 31 bits in 128-byte units. The root page is always the file's own: the file system makes it (`UPDATER-INTERFACE.md`) |
| **a page number** | a page's place in its file, from 0 |
| **a level** | **counts from the top** (owner): the entry at level 0 is in the root page; the entry at level *L* < depth − 1 names the index page of level *L* + 1; the entry at level depth − 1 names the data page |
| **the path** | page *p*'s entries, one a level: at level *L*, entry *i* = (*p* >> 10 (depth − 1 − *L*)) & 1023 of the index page at level *L*. It is 32-bit word *i* & 31 of that page's line *i* >> 5, bits [32 *w* + 31 : 32 *w*] of the line, *w* = *i* & 31, little-endian, as the file store keeps it |
| **a line** | 128 bytes of LPDDR at a multiple of 128. Its **line address** is its byte address over 128: the 128-byte unit |
| **a client** | a unit with a look-up port: the Updater, the RW engine (for the file system), the Binder later |

A depth-0 file is its root alone, with no entry: † every request with depth 0 is answered `beyond` (section 2).

## 1. The look-up port

**A request port for each client, and one shared reply bus** (owner). About 100 bits out and 36 back (second note).

### The request, client to cache

| signal | bits | meaning |
|---|---|---|
| `valid` | 1 | a request is offered |
| `ready` | 1, cache to client | the cache takes it |
| `op` | 2 | 0 look up, 1 update, 2 discard a page; 3 reserved †, undefined |
| `root` | 31 | the file's root. For a discard, the page's address: a multiple of 32, bits [4:0] ignored † |
| `depth` | 2 | the file's depth, 0 to 3 (the request carries it: the cache keeps nothing about files) |
| `page` | 30 | the page number; at depth 3, 30 bits reach every page |
| `level` | 2 | for an update, the level of the entry to change; ignored otherwise |
| `entry` | 32 | for an update, the new entry, in the stored form; ignored otherwise |

99 bits and `valid`.

### The reply, on the shared bus

| signal | bits | meaning |
|---|---|---|
| `valid` | 1 | a reply is on the bus in this cycle |
| `client` | 2 | whose reply: its port's number |
| `status` | 3 | below |
| `level` | 2 | where the walk stopped, or the level changed (section 2's tables) |
| `entry` | 32 | the entry there |

39 bits and `valid`: the second note's 36, with the client and a third bit of status.

| `status` | name | |
|---|---|---|
| 0 | `ok` | |
| 1 | `hole` | |
| 2 | `not_owned` | |
| 3 | `beyond` | |
| 4 | `lpddr_error` | |
| 5 to 7 | reserved | |

0 to 3 are the fill port's numbers for the same cases (17.9.5).

### The ports †

| port, and its replies' `client` | client |
|---|---|
| 0 | none: 0 is the cache's own number on AXI. Spare: a test's |
| 1 | the Updater |
| 2 | the RW engine, for the file system |
| 3 | the Binder |

**A client has one number everywhere** (agreed at the review, 2026-11-16): its look-up port's number is its AXI ID
(`AXI-INTERFACE.md`, section 1).

### The handshake

- **A request moves in the cycle `valid` and `ready` are both 1.** The client raises `valid` without waiting for
  `ready`, and holds `valid` and the request's fields until then (machine-spec 3, "Handshakes").
- **A client has one request outstanding** (owner). Its port's `ready` is 0 from the cycle after its handshake until
  the cycle after its reply, at the earliest. It may be 0 at other times too, for a bounded time: while an update
  waits for the one update slot (section 3), or while the cache serves others.
- **Every request has exactly one reply,** in a cycle after its handshake, the next at the earliest.
- **The bus has no `ready`: a client always takes its reply** (owner), in the cycle it is on the bus with the
  client's number.
- **At most one reply a cycle.** Replies to different clients come in any order.
- **Every output is a flip-flop:** the cache's `ready` and the reply bus, and the client's request.

## 2. The three operations

**Look up, update and discard a page** (third note, section 2). Flush is gone: updates write through (section 3).

### Look up

The walk goes down from the root, one level at a time, and **stops at the first entry that is a hole or is not the
file's own.** It reads lines from the cache memory, fetching those it lacks from LPDDR, and changes nothing.

| case | `status` | `level` | `entry` |
|---|---|---|---|
| **found:** every entry on the path is the file's own | `ok` | depth − 1 | the data page's entry, owned bit 1 |
| **a hole** at level *L*, every entry above it the file's own | `hole` | *L* | 0xFFFF_FFFF |
| **a page that is not the file's own:** the entry at level *L* has its owned bit clear, every entry above it the file's own | `not_owned` | *L* | that entry: the shared page's address, owned bit 0 |
| **beyond:** the page number is 2^(10 · depth) or more, or the depth is 0 † | `beyond` | 0 | 0 |
| **an LPDDR error:** a line on the path could not be fetched | `lpddr_error` | that line's level | 0 |

- "Found" is the bottom level with the owned bit set (second note, section 3).
- **At `not_owned`, nothing below level *L* is the file's own** (`UPDATER-INTERFACE.md`: once a level is not owned,
  nothing below it is), so the walk need not go on. The client makes that page its own: it copies the page the entry
  names, then changes the entry by an update.
- **This walk is not the fill port's.** The Tracer's walk goes on through an entry that is not owned, since the
  Tracer reads shared pages (section 5).

### Update

The request's `level` *L* and `entry` *E*. The walk goes down to level *L* as a look-up does, checking every entry
above *L*; then the entry at level *L* becomes *E*, whatever it was: a hole, a shared page or the file's own page.

| case | `status` | `level` | `entry` | what is written |
|---|---|---|---|---|
| **the path above *L* is the file's own:** the entry changed | `ok` | *L* | the entry as it was before † | the line, through to LPDDR, before the reply (section 3) |
| a hole at level *k* < *L* | `hole` | *k* | 0xFFFF_FFFF | nothing |
| **an entry not owned at level *k* < *L*** | `not_owned` | *k* | that entry | **nothing** (third note, section 4, proposed: the walk checks ownership, so that a client's mistake cannot change a frozen file) |
| beyond: the page number past the depth's reach, *L* ≥ depth, or depth 0 † | `beyond` | 0 | 0 | nothing |
| an LPDDR error fetching a line on the path, the target's line included | `lpddr_error` | that line's level | 0 | nothing |
| **the line's write answered with an error** | `lpddr_error` | *L* | the entry as it was before | the cache holds *E*; LPDDR's line is undefined. Fatal to the client (third note, section 4: `ERRORS.lpddr_error`) |

- The index page holding the entry at level *L* is the file's own whenever every entry above it is: the root is.
- An update changes 32 bits of a line, and writes the whole line to LPDDR (whole beats only, `AXI-INTERFACE.md`).

### Discard a page

The request's `root` is the page's address; its other fields are ignored.

| case | `status` | `level` | `entry` |
|---|---|---|---|
| always | `ok` | 0 | 0 |

- **Every line of the page that is in the cache is thrown away, written or not:** a clean line and a dirty one alike,
  and nothing is written to LPDDR (third note, section 2: a page that is handed out again is discarded, not
  flushed). The discard makes no LPDDR traffic of its own.
- **A line with a write in flight is not discarded until its write's response has come** (third note, section 4: not
  evicted and not discarded until then). The discard's reply waits for it.
- **The reply comes when none of the page's 32 lines is in the cache.**

## 3. An update: its sequence, its guarantees, and what it asks of clients

### The sequence

The working design (third note, section 4). The owner asked for the sequence so that the reply does not return until
the write is complete, and left the working out to the session that wrote it; the cache's simulation tests it.

1. **The request is taken** from a client's port: its root, depth, page number, level and new entry. That client's
   port is busy.
2. **The walk to the level:** one look-up in the cache memory for each index line on the path; a line that is not in
   the cache is fetched from LPDDR, and the walk goes on.
3. **The target line:** if an earlier update's write to it is still in flight, the update waits for its response.
4. **The entry is changed in the cache,** 32 bits of the line, and the line is marked "write in flight". It is never
   marked dirty.
5. **The whole line is sent to LPDDR** on the cache's AXI port: an 8-beat burst.
6. **The update is parked,** and the cache serves other requests meanwhile.
7. **The write's response arrives.** The line's mark is cleared; the line is clean.
8. **The reply:** `ok`, or `lpddr_error` if the response was an error. The client's port is free again.

**Nothing before step 7 can produce the reply.**

### What the cache guarantees

| | |
|---|---|
| **write through** (owner) | an update's line reaches LPDDR before its reply. **Once a client has the reply, LPDDR holds the new entry:** a read raised after the reply sees it, through the cache or straight from LPDDR on any AXI port (third note, section 3) |
| **why a direct read sees it** | because of the wait, not because of any order on the bus: the cache's write and a client's later read are on different AXI ports, and AXI orders nothing between them (third note, section 4) |
| **an index line is never dirty** | only the Tracer's data lines are (third note, section 3). So LPDDR's index pages are always current, but for a line whose update has not yet been answered |
| **one update in flight** | at a time, in the whole cache (third note, section 4). A second update waits at its port, its `ready` 0, until the first's reply |
| **other requests go on** | look-ups and the Tracer's fills are served while an update waits for its response (step 6). One that reads the line in flight sees the new entry, from the cache |
| **a line in flight stays** | it is not evicted, not discarded and not changed until its response (step 7) |

### What the cache asks of its clients

| | |
|---|---|
| **the data is in LPDDR before the index points to it** | a client writes a page on its own AXI port, waits for that write's response, and only then sends the update that names it (third note, section 4; second note, section 4: the Updater idles until the write's confirmation). So whoever sees the new entry, in the cache or in LPDDR, finds the page behind it |
| **one writer for a file at a time** | two clients updating one index page would each read it directly while the other's update was in flight. The file system keeps it from happening; **the cache does not check** (third note, section 4) |
| **an owned index page changes only by `update`** † | a client writes an index page whole, on its AXI port, only while the page is new: taken from the free page queue, discarded since its last use, and named by no entry yet. Otherwise the cache could hold a line of it that LPDDR no longer has |
| **a page is used only after its discard's reply** † | the file system discards a page when it hands it out (second note, section 3, proposed; third note, section 2), and hands it out after the reply. So a write-back of an old dirty line cannot land on the page's new contents |
| **a data page the Tracer reads is not written directly while it does** | the Updater and the RW engine move data pages on their own AXI ports, and the Tracer reads them through the cache. They never meet on a page (owner, second note, section 3): a file is finished by the Updater before the Tracer starts on it, and the only pages the Tracer dirties are its own copy's |
| **a page the Tracer has written is not read directly** | its newest lines may be only in the cache, dirty. Nothing reads them today (third note, section 2, open: a flush at the end of a trace) |

## 4. What the cache never does

- **It never handles a page fault.** It never allocates a page and never builds one (owner). A hole or a page that is
  not the file's own is reported to the client, which takes a free page, builds it and sends the update.
- **It never writes LPDDR** but for an update's line and a dirty line written back when it is evicted.
- **It never reads a data line for the look-up port:** a look-up and an update walk index lines only.
- **It never flushes:** there is no flush (third note, sections 2 and 3).
- **It never checks the one-writer rule,** and never orders its writes against another client's AXI traffic but by
  holding an update's reply.
- **It keeps nothing about files:** no lengths, no list of roots. A request carries its root and depth.
- **It never answers a request taken before a reset after it** (as `fill`, machine-spec 3).

## 5. The fill port to the Tracer

**v1.36's `fill`, unchanged** (machine-spec 3; 17.9.5). The Tracer is built (owner, `RTL-CHANGE-REQUESTS.md`): its
side is not this design's to change.

| channel | signals | |
|---|---|---|
| `read_request` | `valid`, `ready`, `root` (31), `depth` (2), `line` (35) | one 128-byte line of a file |
| `read_response` | `valid`, `data` (1,024), `status` (2) | no `ready`: the Tracer takes every one |
| `write_request` | `valid`, `ready`, `root`, `depth`, `line`, `data` (1,024) | a whole line |
| `write_response` | `valid`, `status` (2) | no `ready` |

- `line` is the line's place in the file, from 0: line *l* is line *l* & 31 of page *l* >> 5. `data` carries
  the line's byte *b* at bits [8 *b* + 7 : 8 *b*]. The Tracer's roots and depths are its files word's
  (`memory_root`, `memory_depth`, `map_root`, `map_depth`; 17.9.2).
- **Its rules, kept:** at most one read and one write outstanding from the Tracer; each request has exactly one
  response, in a cycle after its handshake, the next at the earliest; every request answered in bounded time; **a
  read of a line sees a write of it already accepted;** nothing accepted before a reset is answered after it.

**What the cache does with it:**

| | |
|---|---|
| **a read** | the walk of (root, depth, line), and the line. Its status is 17.9.5's: `ok` when every entry on the path is the file's own; `hole` at a hole on the path, with zeros; `not_owned` when an entry on the path is not owned, with the line: **unlike the look-up port's walk, this one goes on through a shared page;** `beyond` when the line is 2^(5 + 10 · depth) or more, with undefined data |
| **a write** | the same walk. With `ok` the line is written into the cache whole, **marked dirty, and allocated without an LPDDR read** if it was not there; the response is `ok`. With any other status nothing is written and the response is `error` (as v1.36's model) |
| **the Tracer's writes stay dirty in the cache** | until their line is evicted, and are then written back on the cache's AXI port (owner, third note, section 3). They are never written through |
| **an LPDDR error on a read** † | the fill port's read has no status for it (its four are used). The read is answered `beyond`, so the Tracer sets `ERRORS.page_missing` and stops (17.9.5), and the cache's error flag is set (section 6) |
| **an LPDDR error on a write's walk** | `error`: the line is not written |
| **an LPDDR error on a write-back** | the dirty line is lost (17.9.5 says so of a write-back's `error`), and the cache's error flag is set: the Tracer had its `ok` long before |

The fill port's timing was v1.36's model's, 6 cycles a hit and 80 a miss (17.9.14); it is now the cache's. The Tracer
counts on none of it.

## 6. The cache from outside

| | |
|---|---|
| **its size** | 512 KB: 8 ways by 512 sets of 128-byte lines (owner, second note, section 3) |
| **keyed by LPDDR line address** | the set is the line address's bits [8:0]; the tag the rest. A page's 32 lines fall in 32 sets |
| **what it holds** | index lines and data lines alike, from every client: the Updater's look-ups and the Tracer's walks share the index lines they both read |
| **its replacement** † | the least recently used of the ways that may be evicted |
| **empty at the reset** † | and at no other time: nothing makes it cold between segments |

**A line's states:**

| state | |
|---|---|
| invalid | no line in the way |
| clean | the line as LPDDR has it |
| dirty | a data line the Tracer wrote through the fill port: newer than LPDDR |
| write in flight | being written to LPDDR: an update's line, or a dirty line being written back. It holds the newest contents, is read as any line is, and is not evicted, not discarded and not changed until its write's response † (a Tracer's write to it waits) |
| being filled † | a way taken for a line being fetched from LPDDR |

- **Never evicted:** a line with a write in flight, and a way being filled. If every way of a set is one or the
  other, a request that needs a way there waits.
- **A dirty victim is written back first, in its place,** marked "write in flight"; the fetch that wants its way
  waits for the response. There is no list of pending writes (owner, second note, section 3).
- **A discard and each state:** an invalid way, nothing; a clean or a dirty line, invalid at once, nothing written; a
  line with a write in flight, invalid once the response has come; a way being filled, invalid once filled †. A
  client that reads a page while it is discarded has broken section 3's rule, and gets what it gets.
- **The cache's error flag** †: sticky until the reset, set by an LPDDR error that has no client to tell (a
  write-back's) and by a fill read's. It is an output level, like a peripheral's `ERRORS` ≠ 0: on the chip, the
  memory side's error line to the master tile (machine-spec 18).

## 7. Timing: what a client may assume, and what it may not

| a client may assume | a client may not assume |
|---|---|
| every request answered, in bounded time | any latency: a look-up may hit in a few cycles, or wait for its lines from LPDDR, for a dirty victim's write-back, for an update in flight, for the Tracer's fills and other clients' requests |
| a reply in a cycle after its handshake, the next at the earliest | that `ready` is 1 in any given cycle, even with nothing outstanding |
| an update's reply after LPDDR holds the new entry | that a line, any line, stays in the cache |
| its own requests answered in order: it has one at a time | an order between its replies and another client's |
| | that a look-up sees, or does not see, another client's update not yet answered. It sees the new entry once the cache has made it (step 4); the one-writer rule makes it moot |

What Part B's cache is expected to give, as a guide and not a promise: a look-up of a depth-2 file whose two index
lines are in the cache in under 10 cycles; a line from LPDDR about 80 more (`AXI-INTERFACE.md`, section 5); an
update a walk and one line's write, about 80, more.

## 8. The stand-in: the simplest correct cache

**The simplest correct cache is none at all.** The Updater's controller is built and tested against it (third note,
section 5: the Updater first, against a simple stand-in for the cache; then the cache's controller replaces it). Seen
from the look-up port, the stand-in and the cache are the same thing: **the same reply to the same request on the
same LPDDR,** differing only in when.

| request | the stand-in |
|---|---|
| look up | walks LPDDR's current contents from the root, line by line, and replies as section 2's table says |
| update | walks LPDDR to the level as section 2 says; if the path above is the file's own, writes the new entry into LPDDR, and replies once the write is complete there, with the entry as it was before |
| discard | replies `ok`: it holds nothing |

- **Its timing is free,** but for two rules: a reply comes in a cycle after its request's handshake, and an update's
  write is in LPDDR before its reply. Suggested: one request at a time, 80 cycles for each line it reads and 80 for
  the write, so that a client is not built on replies faster than the cache's.
- **It may read and write LPDDR's memory directly** (a functional stand-in), or **over an AXI port of its own** on
  the arbiter. Either is a correct cache. A functional stand-in writes LPDDR's memory at once, so the rule holds.
- **It keeps section 1's handshake exactly:** one request outstanding a client, `ready` a flip-flop, one reply a
  cycle on the shared bus, its `client` field set.
- **The same stand-in is the cache's reference in Part B:** random traffic against it, every reply and LPDDR's
  contents at the end equal (`updater_wip/planning/sessions/WORKER5/NOTES.md`).

## 9. The C++ header

`v1.37_wip/src/units/lookup_port.h`, types and constants only:

| in it | what |
|---|---|
| `Op` | look up, update, discard |
| `Status` | `ok`, `hole`, `not_owned`, `beyond`, `lpddr_error` |
| `Client`, `CLIENTS` | the ports of section 1 |
| the stored forms | `NO_PAGE`, `OWNED`, `ADDRESS`; a page's 32 units; 1,024 entries a page, 32 a line |
| the widths | section 1's fields |
| `Request`, `Reply` | a request port's signals and the reply bus's in one cycle; `ready` is a `bool` of the cache's |

## Open

1. **How the front end chooses** between the fill port and the look-up ports (third note, section 7). † Round robin
   among the sources with a request waiting, the fill port's read and write two of them, so that every request is
   taken in bounded time. The alternative is the fill port first: the Tracer is the one that stalls.
2. **One update slot, or one for each client** (third note, section 4). One, as the note proposes.
3. **Who sends a discard.** The file system, when it hands a page out (proposed in the second note, still proposed).
   In v1.37 the master tile's core hands the pages out through the free page queue and has no look-up port; it frees
   no page, so no page is handed out twice and none is needed. For v1.4: a look-up port for the file system's core,
   or the Updater discarding each page it takes from the free page queue. `WORKER5/QUESTIONS.md`.
4. **An LPDDR error on a fill read** has no status on `fill`: `beyond` and the error flag (taken at the review). A
   third bit of status is a change to the built Tracer: a comment to Tony (`RTL-CHANGE-REQUESTS.md`, E), not a
   request.
5. **Where the cache's error flag goes** †: the memory side's error line, as a peripheral's errors do. `WORKER5/QUESTIONS.md`.
6. **Depth 0** † answered `beyond`: no file of depth 0 arises (every image is depth 2; gold gives every file an index
   level).
7. **An update's reply carries the entry as it was before** †; the new one is the alternative. A client may ignore
   it.
8. **Whether a look-up should go on past a page that is not owned** to the bottom. It stops: the Updater needs only
   the first such level.
9. **A flush of the Tracer's dirty lines** at the end of a trace or at shutdown (third note, section 2, open): not an
   operation here; nothing reads them.
10. **Empty at the reset only** †. v1.36's model was made cold at each segment's start; the cache need not be, since
    every line it holds is current.
11. **Write-back first, then fetch,** for a dirty victim (owner: no list of pending writes). Keeping each set's next
    victim clean ahead of time, as v1.36's model does, is an inside matter that changes nothing here; Part B's
    figures will say whether it is worth it.
12. **The number of look-up ports,** four †, and whether the master tile's core needs one (3).
