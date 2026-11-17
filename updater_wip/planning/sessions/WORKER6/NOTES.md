# WORKER6 notes: the Updater's controller

**Status: the plan, with the owner's go-ahead (2026-11-16). Building.**

Abbreviations: UI `UPDATER-INTERFACE.md`, II `INGRESS-INTERFACE.md`, AXI `AXI-INTERFACE.md`, CI `CACHE-INTERFACE.md`,
HF `HOST-FRAMES.md`, N15 and N16 the design notes of 2026-11-15 and 11-16. "The black box" is `src/units/updater.*`.

## 1. The pieces

```
              ┌──────────────────────────── UpdaterTop: what the slot sees (peripheral type 5) ───────────────────────────┐
 region part 1│  ┌───────────────┐ word port  ┌──────────────────────────── UpdaterCtl ───────────────────────────────┐  │
 ────────────►│  │ SegmentShim   │──valid,──► │ input FIFO ─► decoder ─► FSM ◄─► page memory                          │  │
              │  │ 512 × 128 b,  │  data      │   4 words     194-bit    │    256 × 128 b, one port                    │  │
              │  │ credit, ovrun │◄─ready──── │               shifter,   ├──► AXI port, client 1 ────────────────────────┼──► test LPDDR
              │  └───────────────┘            │               LRU 32     ├──► look-up port 1 ◄── reply bus ─────────────┼──► stand-in cache
 region part 2│ ────────────────────────────► │ free page queue 256 × 32 b                                            │  │
 window       │ ────────────────────────────► │ block 2 and 3 registers; busy, errors, two event pulses               │  │
              │  events: done, free_page_credit from the controller; segment_credit from the shim; ERRORS ORed        │  │
              └──────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

| piece | file | what |
|---|---|---|
| `UpdaterCtl` | `src/units/updater_ctl.h`, `.cpp` | the controller: every register in one struct, one `edge()` |
| `PageMemory` | `src/units/page_memory.h` | the 4 KB memory as a unit with one port (section 4) |
| `SegmentShim` | `src/units/segment_shim.h` | the merged segment queue as a word-port source. Knows nothing of the controller |
| `UpdaterTop` | `src/units/updater_top.h`, `.cpp` | the slot's peripheral: the window, the region's two parts, the control block's events (counted, as the black box's) from the pulses, the UB checks the black box makes. **The only file that knows the shim:** deleting the shim is deleting it here and wiring Ingress's source to the port |
| `TestLpddr` | `tests/test_lpddr.h` | AXI section 5's model for one client, on the file store's pages: fixed and jitter modes, faults, the rule checks |
| `StandInCache` | `tests/standin_cache.h` | CI section 8, functional on the file store, one request at a time; fixed 80 a line and 80 the write, or random |
| the setting | `MachConfig::updater_ctl` → `G1Config` → `PeriphSlotConfig::updater_ctl` | off by default: the slot installs the black box as today. On: `UpdaterTop`, with the test LPDDR and the stand-in attached by the harness, as `RwEngine::attach` |

The black box is untouched. The benches (`updater_bench`, `proof_bench`) get a small common view of either (store,
`on_done`, `errors`, queue contents) so that every check runs on both.

## 2. How a reader sees a cycle

The convention of `axi_port.h`. In `UpdaterCtl`:

- `struct Regs` holds **everything kept between cycles**: block 2 and 3, `busy`, the error bits, the FSM's state (an
  enum) and its few counters, the FIFO, the decoder, the AXI and look-up output registers, the free page queue. The
  page memory's array is in `PageMemory`, which has registers of its own (its read data).
- `Outputs out() const` is a function of `Regs` alone: `ready`, the AXI `MasterOut`, the look-up `Request`, the two
  event pulses, `busy`, `errors`. Every one a register, never computed from an input.
- `void edge(int64_t t, const Inputs& in)`: `in` is the other units' outputs of cycle t (the word source's
  `SourceOut`, the AXI `SlaveOut`, the cache's `ready` and the reply bus, the page memory's read data). It reads
  `const Regs& q` (the start of t) and writes `Regs d` (the end of t), in one function, top to bottom: the FIFO, the
  decoder, the FSM, the AXI channels, the look-up port, the statistics. Then `q = d`.
- `UpdaterTop::step(t)`, called by the slot: take every unit's outputs of t first (controller, shim, test LPDDR,
  stand-in, page memory), then step each edge with the others' outputs. No unit reads another's insides.

## 3. The controller's registers

| group | registers | bits |
|---|---|---|
| block 2 | `RUN_CONFIG` 7, `WORD_COUNT` 32, `PAGE_COUNT` 32, `IMAGE_ROOT`, `TRACER_ROOT`, `SOURCE_ROOT` 32 each | written only while idle (UI) |
| block 3 | the six statistics, 32 each | |
| control | `busy`, `stopped`; errors `beyond`, `lpddr_error`, `out_of_pages`, `not_owned`, `overrun` (the free page queue's); pulses `done`, `free_page_credit` | |
| input FIFO | 4 × 128, head 2, count 3, `ready` 1 | `ready` = count after the edge < 4: a flip-flop, not depending on `busy` |
| decoder | bit buffer 194, its count 8, LRU table 32 × 64, `run_addr` 32, `run_left` 32; output register: valid, address 32, value 64, type 2 | section 5 |
| free page queue | 256 × 32, head 8, count 9, `left` 7 (entries since the last credit) | |
| FSM | `state` (enum), `level` 2, `side` 1 (image or Tracer's copy), the look-up results for each side (status, level, entry), `pm_src` (what the page memory holds), `pm_next` (the entry for the next level, caught as it streamed in), `new_page` 31 | section 6 |
| the held page | `held`, `held_page` 30 (its number), `held_at` 31 (where it is written), `held_src` 31 (where its missing halves come from), `held_owned`, `held_hole`, `have[2]`, `dirty[2]` | |
| AXI | AR: valid, address, length; AW: valid, address, length; W: valid, data 128, last; the burst engine: beats left, the page memory row of the next beat each way, a fetch pending (as the RW engine's W path), `w_const` (W from a constant, below), `transform` (clear owned bits on the way in) | one burst at a time each way |
| look-up | the request register (valid, op, root, depth, page, level, entry), `waiting` | one request outstanding (CI) |

## 4. The page memory

**One port, 256 rows of 128 bits, a read or a write a cycle, with a write enable for each 64-bit half of a row; a
read's data is in its register the next cycle.**

- **Why one port:** the work is strictly in sequence (owner). R beats come in, values are patched, W beats go out,
  and never two of them in the same cycle. A second port would buy nothing.
- **Why 128 bits:** a beat a cycle each way is the AXI port's rate; a patch is a 64-bit write with one enable.
- **Around it, not in it:** on the way in, the owned-bit clear of an index page (bit 31 of each 32-bit lane cleared
  unless the lane is 0xFFFF_FFFF), and the catch of the next level's entry (a compare of the row with digit / 4). On
  the way out, a mux: the row, or a constant (0xFF… for a page of holes, 0 for a hole's data). So a page of holes and
  an empty root are written with no page memory at all, and a hole's data half that is never touched goes out as
  zeros.
- **W beats back to back:** the RW engine's scheme: a row read in c is pending in c + 1 and taken into the W register
  if it is empty or its beat leaves, else read again.

## 5. The input and the decoder

- **The FIFO, 4 words.** Takes a word in a cycle in which `valid` and `ready` are 1, whether busy or not (UI).
  Only the reset empties it.
- **The bit buffer, 194 bits.** A code is at most 66 bits; a word is 128. **The shifter** puts a FIFO word in at
  the buffer's count after this cycle's decode, which is 0 to 65: a 128-bit word shifted by 0 to 65.
- **A cycle decodes one code at most,** from the bits present at its start: raw 64; LRU a hit 6, a miss 34 or 66.
  The take is a mux of four fixed shifts. The LRU table moves to front in the same cycle. **So one update word a
  cycle;** raw or LRU, the FIFO is drawn at most every other cycle.
- **Refills are lazy, to the bit:** a word comes out of the FIFO only when the next code's bits are not all there,
  judged from the two bits after this cycle's code (a second fixed mux). Never otherwise, so a frame's padding stays
  in the buffer and the next frame's first word stays in the FIFO, for `continue` 0 to clear the one and keep the
  other.
- **It decodes only while busy and `WORD_COUNT` is not 0,** and counts `WORD_COUNT` down for each word, header or
  value, as the black box does. A header word sets `run_addr` and `run_left` and gives nothing out.
- **Its output register** holds the next value with its address and **type**. It decodes one value ahead.
- **The type counts only the values this run will deliver** (owner, at the go-ahead). A run of the Updater can end
  inside a run of values: `WORD_COUNT` reaches 0 first, and the held page is written at the run's end. So with *n* =
  min(`run_left`, `WORD_COUNT`), both counted with this value (the values of a run of values come before any header
  word, so `WORD_COUNT` bounds them): page if the address is a multiple of 512 and *n* ≥ 512; half if a multiple of
  256 and *n* ≥ 256; words otherwise. A half that the run leaves part-written is read before it is patched.
  - **The black box does not do this:** its "covered" test is `run_left` ≥ 256 alone (`updater.cpp`, `patch`). Its
    images are still right, because its page memory always holds the true page, but it does not count the read it
    should have made, so its `STAT_PAGES_READ` is one low for each such half. The comparison adds those halves, which
    the bench counts exactly from the stream it sent (section 9). A finding for the report; the black box is not
    changed.
  - Gold's frames never reach the case (a run ends at a segment's end, where its runs of values end); the random
    streams do, and a fixed case ends a run 1, 255 and 256 words into a covered half and 511 into a covered page.
- `continue` 0 at `START` clears the bit buffer, the table and the run; 1 keeps them (UI).

## 6. The state machine

```
                       START
   IDLE ───────────────────────────► CHECK ──bad root──► STOPPED (busy stays 1, no done)
    ▲                                  │
    │                         root 1/2 │ root 0
    │                                  ▼
    │                ROOT_READ ─► ROOT_WRITE ─► ROOT_B ─► (tracer_copy: ROOT_WRITE again) ─┐
    │                                                                                       ▼
   DONE ◄── word count 0, nothing held ─────────────────────────────────────────────── NEXT_VALUE ◄──────────┐
    ▲                                                                                       │                │
    │    held, word count 0          another page, held                 page beyond         │ the value      │
    └──── FLUSH ◄───────────────────────────────────────────────────── BEYOND ─► STOPPED     │                │
           │                                                                                ▼                │
           │ (not owned: READ_MISSING) ─► WRITE_HELD ─► HELD_B ─► (new page: UPDATE_HELD)   same page?        │
           │                                                          │                    │ no: LOOKUP     │
           └──────────────────────────────────────────────────────────┘                    ▼                │
                    then LOOKUP, or DONE                                     LOOKUP (image, then Tracer's)   │
                                                                                            │                │
                                                       owned ──────────────────────────────►│ HELD set       │
                                                       a fault: count pages; > PAGE_COUNT → STOPPED          │
                                                                                            ▼                │
                                            per level, per side:  TAKE_PAGE ─► LOAD (read or constant)       │
                                                                  ─► WRITE_NEW ─► NEW_B ─► UPDATE ─► REPLY   │
                                                                  (data level, image: TAKE_PAGE only)        │
                                                                                            ▼                │
                                                        HALF_READ / HALF_FILL (a half first touched, ────────┤
                                                        not covered) ─► PATCH (one cycle) ───────────────────┘
```

- **NEXT_VALUE** takes the decoder's output register. Its page past the depth's reach is `beyond`: the run stops
  with the held page unwritten, as the black box's (C6).
- **A page is held until a value needs another, or the run ends** (the black box's C7). A run of values in the same
  page costs one look-up, one read at most and one write.
- **PATCH** writes the value into its 64-bit word: one cycle a value.
- **Errors stop the run:** `STOPPED` keeps `busy` 1, posts no `done`, and still sinks any R beat or B response in
  flight. What stops it: a root or a free page that is not a page's address in LPDDR (the black box's check), a
  read or write answered with an error, a reply of `lpddr_error` or `not_owned`, `beyond`, `out_of_pages`.

## 7. The order of the Updater's traffic

A look-up's levels and its `not_owned` / `hole` reply are CI section 2's. *L* is where a side's walk stopped; a
side **takes** at every level from *L* down. "Write *X*" is a burst on the AXI port; "B" its response; "update
(*k*, *E*)" a request on the look-up port and its reply. Every new page is the free page queue's next entry,
`PAGE_COUNT` − 1.

**Every page:** look up the image (and with `tracer_copy`, the Tracer's copy). The pages the two need are counted
first: more than `PAGE_COUNT` stops the run before any is taken (the black box's order).

| kind | the traffic, in order |
|---|---|
| **owned** | the halves first touched and not covered: read each (a half burst). The patches. Write each dirty half in place (both dirty: one page burst); B. No update |
| **a hole at *L*** | for each index level *k* = *L* … depth − 2: take *P*; write *P* from the constant (holes); B; update (*k*, *P* \| owned); reply. The data level: take *P*, held as a hole. Halves first touched and not covered: filled with 0 in the page memory, no read. At the end: write *P* whole (untouched halves as the constant 0); B; update (depth − 1, *P* \| owned); reply |
| **shared at *L*** | for each index level *k*: read the shared page (the reply's entry at *L*, then the entry caught as the page above streamed in), owned bits cleared on the way in; take *P*; write *P*; B; update (*k*, *P* \| owned); reply. The data level: take *P*, held, its source the shared page. Halves read lazily as for owned. At the end: read the halves still missing (one burst for both); write *P* whole; B; update; reply |
| **with `tracer_copy`**, at each level where both take | the Tracer's copy first: load the page once (read and clear, or the constant); take *Q*; write *Q*; B; update the copy (*k*, *Q* \| owned); reply; then the image: take *P*; write *P* from the same page memory; B; update the image; reply. **Read once, written twice** (N15). At the data level the source is read whole, written unchanged to *Q*, the copy updated; then *P* is taken and held with both halves present; patched, written and updated at the end as above |
| where only one side takes | that side alone; if the two sides' sources differ, the page memory is loaded again for the second |
| **roots** (UI "For v1.37") | `root` 1: write `IMAGE_ROOT` from the constant; B. `root` 2: read `SOURCE_ROOT` with owned bits cleared; write `IMAGE_ROOT`; B; with `tracer_copy` write `TRACER_ROOT`; B. Before the first value |
| **the run's end** | the held page written as its kind says; then `done` |

**The free pages are taken in the black box's order** (top down; at each level the Tracer's copy before the image;
the data page when the page is brought in). So the store after a run is the black box's **byte for byte,** and the
checks compare the stores whole, not only the files.

**Against "What the cache asks of its clients" (CI section 3):**

| the ask | how the order keeps it |
|---|---|
| the data is in LPDDR before the index points to it | every update naming a page is sent after that page's B; the controller idles meanwhile |
| one writer for a file at a time | one Updater, one run at a time, and the master writes no index page |
| an owned index page changes only by `update`; written whole only while new | index pages are written whole only just after being taken from the free page queue, before any entry names them. **Except the roots of v1.37** (UI "For v1.37"): new pages from the master's bit array, named by nothing, but not from the free page queue. Same in spirit; the ask's wording does not cover them (a document change) |
| a page used only after its discard's reply | **not met, and not needed in v1.37:** nothing is freed, so no page is handed out twice. The controller sends no discard (CI Open 3). A question for v1.4 |
| the Tracer's pages | the third run writes the final image while the Tracer reads its copy: they share only pages neither writes |

The test LPDDR and the stand-in check the first and third rows as they run (section 8).

## 8. The test pieces

- **`TestLpddr`:** a client port (no IDs, one client). AXI section 5's rules: latencies 70 and 70, 16 outstanding
  each way, a read beat's data taken from the store as it is sent, a write's beats landing as its response is sent,
  order kept within the one ID. **Jitter** (seeded): each latency 20 to 200, each ready 0 one cycle in four.
  **Faults:** `LpddrFault` as AXI says. A burst that breaks a rule (4 KB, alignment, `wlast`, a handshake withdrawn)
  stops the simulator. A read of a page never written returns zeros and is counted; a test fails on it.
- **`StandInCache`:** CI section 8, functional on the store: look up walks the store; update walks, writes the entry
  into the store and replies after its time; discard replies `ok`. One request at a time, `ready` a flip-flop, the
  reply on the bus with `client` 1. Timing: 80 a line read and 80 the write, or random (seeded) from 1 to 200 a line.
  Injected replies for the error tests (`lpddr_error`, `not_owned` on the *n*th update).
- **The rule checks,** in the test pieces: an update whose entry names a page with a write accepted and not answered
  (the data before the index); a write to a page that is neither a page taken from the free page queue and named by
  no entry yet, nor a root being made, nor an owned data page of the run's files.
- **The word-port feeder** (tests): offers a frame's words, `valid` always or at random.

## 9. The checks

| check | how | on what |
|---|---|---|
| **the controller alone against gold** | `updater_bench` with the controller, the feeder on the port (no shim), the test LPDDR and the stand-in; the images against gold's `frames-images`, `PAGE_COUNT` 0 at every `done` | every fixture `test_updater` runs on the black box, at its four *s*; `updater_frames --ctl` on any frame file |
| **against the black box** | the same driver for both, with the shim. At each `done`: block 2 and the errors equal; the stores byte for byte; the `done` and free page credits posted; the stream not yet decoded equal (the shim's entries, the FIFO's and the bit buffer's against the black box's queue and bits); `STAT_PAGES_WRITTEN` equal, and `STAT_PAGES_READ` equal to the black box's plus the halves its "covered" test skips (section 5). The cycle statistics are not compared (the black box's timing is not a design's) | gold's fixtures; random streams: depths 1 to 3, holes, shared pages at every level (files built by copying roots of trees made first), runs ending inside a page, `continue` across runs at odd counts, raw and LRU, `tracer_copy`, the three root modes |
| **the timing varied** | every check above again with the test LPDDR's jitter, the stand-in at random, and the feeder's `valid` at random, several seeds: the same results | |
| **errors** | an error on a read and on a write; `lpddr_error` and `not_owned` to an update; a word beyond; a page with `PAGE_COUNT` 0; a full free page queue; through the shim a full segment queue. Each against the black box where it has the case: the same bits, stopped, no `done` | |
| **the whole chip** | the setting on and the shim in: `test_master`, `proof-real`, `proof-matrix`, `proof-29` (`proof_run --updater-ctl`). The firmware unchanged. Every image, row and record as with the black box | the `ctl` steps of `run_tests.sh`, on the runner |
| **the setting off** | `./run_tests.sh all --check` and `make check` as before; `all` gains only the controller's own test, `updater-ctl` | |
| **teeth** | bugs planted in the controller (an update before B, the owned bits not cleared, a half not read, the decoder pulling a word early), each caught | `tests/updater_ctl_teeth.sh` |

**The figures:** cycles a page by kind (new, copied, patched in place; with the Tracer's copy), the bursts and
look-ups a page, the four waits (data, pages, LPDDR, the cache), the whole run against the black box's; from the
statistics registers and the test pieces' counts, on the fixtures and the proof steps.

## 10. The order of the work

1. `PageMemory`, the decoder and the FIFO, with their tests (random streams against a reference decoding).
2. `TestLpddr` and `StandInCache`, with their own tests.
3. `UpdaterCtl` and `UpdaterTop` without the shim; the controller against gold on the port.
4. The shim and the setting; the comparison with the black box, then random streams, the varied timing, the
   errors, the teeth.
5. The whole chip: `test_master` and `proof_run` with the setting; the `ctl` steps; the long runs to the runner.
6. The figures, the running guide and `CHANGES-v1.37.md`, `REPORT.md`.

Merging `main` in between pieces; a commit and push after each.

## The go-ahead (owner, 2026-11-16)

- **Go ahead as written,** with two notes. (1) The decoder's type must allow for a run that ends inside a run of
  values: section 5, and a random-stream case that hits it. (2) WORKER5 and WORKER7 edit the same shared files (the
  config structs, `periph_slot`, `run_tests.sh`, the makefiles): **my edits there as small as possible, and in one
  place each.**
- **The two departures from CI section 3** (the roots of v1.37 not from the free page queue; no discards) are
  accepted as document changes, to be listed in the report.

## Choices

| | choice | why; the alternative |
|---|---|---|
| C1 | the input FIFO is 4 words | enough to cover the port's gaps at a decode of one word a cycle; 2 would do at the steady rate |
| C2 | lazy refills, judged from the next code's first two bits | so that no word of the next frame is taken early; refilling whenever the buffer is low would take it, and `continue` 0 would lose it |
| C3 | the decoder decodes one value ahead into its output register | hides the decode behind the FSM; none would cost a cycle a page change |
| C4 | a half read when first touched and not covered (lazily), not when the page is brought in | the type of the first value says nothing of the other half, which a later run of values may cover; the same reads as the black box |
| C5 | a look-up for every page brought in, and the page memory held across runs of values in a page | the black box's (WORKER3's C7 and its report's list); remembering the last index page would save look-ups that hit the cache anyway |
| C6 | `beyond` is checked by the controller when the value is taken, before the held page is written; the cache never sees such a request | the depth is the controller's; the black box stops there too, so the held page is lost on that error |
| C7 | a fault's pages are counted from the look-ups before any is taken; then taken one at a time, waiting on an empty queue | the black box stops on `PAGE_COUNT` at the same point; it waits until all are queued, which changes nothing but when |
| C8 | the next level's entry is caught as the page above streams in, not looked up again | a look-up a level more; the catch is a compare and a 32-bit register |
| C9 | holes and empty roots written from a constant, not the page memory; a hole's untouched data half goes out as zeros | a fill of the memory costs 128 cycles a half; the mux nothing |
| C10 | an owned page written back by its dirty halves only | a half burst is one of AXI's three sizes; the black box counts it so |
| C11 | depth 0: the root is the data page, owned, with no look-up | the cache answers depth 0 `beyond` (CI †); no file is depth 0 |
| C12 | a root or a free page that is not a page's address in LPDDR is `lpddr_error`, before the burst | the black box's check (WORKER3 C10); on the AXI port it would break the 4 KB rule and stop the model |
| C13 | a reply that cannot come (`hole` or `beyond` to an update, `beyond` to a look-up) stops the simulator | it means the controller or the stand-in is wrong, not the run |
| C14 | no discards | nothing is freed in v1.37 (CI Open 3) |
| C15 | the shim's credit and overrun reach the control block through `UpdaterTop`, beside the controller's own pulses | the controller then knows nothing of the shim; `EVENTS` [2] is unused in UI |
| C16 | the black box and the controller compared on the stream not yet decoded, not on the queues' counts | the shim hands words to the FIFO before the decoder needs them; the queues' counts differ by up to four words, the bits by the buffer |
| C17 | the `ctl` steps are a set of their own in `run_tests.sh`; `all` gains only `updater-ctl` | "the setting off, as before"; the whole-chip runs with the setting on are long and go to the runner |

## Questions

None yet: nothing in the documents has failed to work on paper. `QUESTIONS.md` is made when one does.
