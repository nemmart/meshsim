# WORKER5 notes: the interface documents, and the plan for the cache controller

Status, 2026-11-16: **Part A done, waiting for the go-ahead.** Branch `updater-w5`.

| Part A's deliverable | where |
|---|---|
| the path to LPDDR: a client's AXI port, the arbiter, the controller's port with IDs, the LPDDR model | `v1.4_wip/planning/AXI-INTERFACE.md` |
| the cache from outside: the look-up port, the three operations, an update's sequence, the fill port, the stand-in | `v1.4_wip/planning/CACHE-INTERFACE.md` |
| their C++ headers, types and constants only | `v1.37_wip/src/units/axi_port.h`, `v1.37_wip/src/units/lookup_port.h` (both compile alone with gcc and clang, `-Wall -Wextra -Werror`) |
| where the notes cannot work as they stand | `QUESTIONS.md` (three questions) |
| the plan for Part B | below |

Names: AI is `AXI-INTERFACE.md`, CI `CACHE-INTERFACE.md`, UI `UPDATER-INTERFACE.md`, all in `v1.4_wip/planning/`.
Paths without a folder are in `v1.37_wip/`. The first, second and third notes are the design notes of 2026-11-14,
-15 and -16.

## 1. The choices in Part A

Every † in AI and CI is listed in its "Open". The ones that shape Part B most:

| | choice | the alternative |
|---|---|---|
| A1 | a client's port is `lpddr`'s, with no ID signals; the arbiter gives client *c* the ID *c* | IDs on the clients' ports |
| A2 | a long burst is cut by the arbiter into pieces of 32 beats; the client sees one burst, one response | clients keep their own bursts to 32 beats |
| A3 | reads: a piece's address goes as soon as it has its turn; writes: one piece at a time, address then beats | write addresses running ahead of their beats |
| A4 | the LPDDR model is the file store's pages; its R channel chooses round robin among the IDs that are due | first come, first served across IDs (a line then waits behind every piece taken before it) |
| C1 | look-up statuses: the fill port's four, and `lpddr_error`; a look-up stops at the first hole or page not owned | going on to the bottom |
| C2 | an update checks every entry above its level: not owned, or a hole, and nothing is written | |
| C3 | an LPDDR error on a fill read: `beyond`, and the cache's error flag (Q1) | a third bit of status: a change to the Tracer |
| C4 | a page is used only after its discard's reply; in v1.37 nobody discards (Q3) | the Updater discards what it takes |
| C5 | the front end: round robin among the sources waiting | the fill port first |

## 2. What Part B meets in v1.37

| | today | what it means for Part B |
|---|---|---|
| the Tracer's fill port | `Tracer::FillPort` (`src/units/tracer.h`): at a request's handshake a callback returns the cycle of its response. `MemSystem::port_read` and `port_write` answer that way | a clocked cache does not know the cycle at the handshake: the hook needs a response polled each cycle (section 6) |
| the memory in proof runs | `MemSystem` on the one `FileStore` the Updater writes (WORKER3's C18) | the LPDDR model reads and writes that store |
| the Updater | a black box on the store: its page writes and its index entries go straight into the store, with no cache and no AXI | it stays so (the brief); section 7 says why no line can be stale |
| `segment-real` | the RTL test's `Fill` (`tests/segment_bench.h`): a map of lines with made-up roots, no index pages | the cache walks real trees: section 8 |
| `tracer_block`, `suite_tracer` | the Tracer rig's `FileStore` images, the fill at fixed latencies or `MemSystem`'s | the cache serves the same store |
| the RW engine | its `lpddr` port, with `ChipLpddr` in the chip tests; idle in the proof runs | an adapter puts it on the arbiter (AI, section 6) |

## 3. The pieces and their files (all new but the last row)

| piece | file | tested by |
|---|---|---|
| the LPDDR model: AXI with IDs on the file store, the two modes, faults, the rules checked | `src/units/lpddr_model.h`, `.cpp` | `tests/test_lpddr_model.cpp` |
| the AXI arbiter, and the RW engine's adapter | `src/units/axi_arbiter.h`, `.cpp` | `tests/test_axi_arbiter.cpp` |
| the cache memory: the tag memory and the data memory | `src/units/cache_memory.h` | inside `test_cache` |
| the cache controller | `src/units/cache_ctl.h`, `.cpp` | `tests/test_cache.cpp` |
| the stand-in of CI section 8, the reference | `tests/cache_ref.h` | `test_cache` uses it |
| a driver for the look-up ports and the fill port | `tests/cache_driver.h` | `test_cache` |
| the setting and the wiring | `tests/mach.h`, `mach.cpp`; `src/units/tracer.h`, `tracer.cpp` (the polled response, off unless set); the harnesses' flags | the steps of section 8 |

## 4. In the shape of hardware

**Every unit is written the same way:**

- **its registers in one struct,** the state machine's state an enumeration among them; nothing else is kept between
  cycles;
- **its outputs, a function of its registers alone** (every output a flip-flop, as AI and CI require): `out()`,
  `const`;
- **one function that advances a clock,** `edge(t, in)`: it reads the registers as they stood at the cycle's start
  and the other units' outputs of the cycle, works out the cycle's decisions as named values, and writes the next
  registers at its end, as `rw_engine.h`'s `step` does ("this cycle's decisions, from the state at its start");
- **no unit reads another's insides,** only its outputs;
- **a memory is a unit of its own with explicit ports:** the tag memory and the data memory are each one read port
  and one write port (†, a hardware designer's choice: one port would cost a cycle on each read-modify-write). A read
  issued in cycle *t* gives its row at the memory's output in *t* + 1; a write in *t* lands at the edge. At most one
  read and one write a cycle each, checked (FATAL past it). The data memory's row is a line, 1,024 bits, written with
  eight 128-bit lane enables, so a beat from AXI lands as one lane and a Tracer's line as eight;
- **fixed arrays, no allocation** in an edge.

**The cache memory:**

| memory | rows | a row |
|---|---|---|
| tags | 512, a set each | 8 ways of a valid bit, a dirty bit and a 22-bit tag; and the set's replacement order, 8 ages of 3 bits |
| data | 4,096, set × 8 + way | 1,024 bits |

"Write in flight" and "being filled" are not in the tag memory: at most a few lines are so at once, so they are
registers (the update slot, the write-back's line, the fetch's line), compared with a set and way. A designer may move
them; the reports will say what each costs.

**The controller's parts** (third note, section 1) are blocks of its state and of `edge`:

| part | its registers | its states |
|---|---|---|
| the front end | the request being served: its source, op, root, depth, page or line, level, entry, a fill write's line; the round robin pointer; each port's `ready` | `IDLE`, `ACCEPT` |
| the walker | the level, the page address at that level, the set and way found | `TAG`, `COMPARE`, `DATA`, `NEXT` |
| the miss handler | the fetch's line, way and beats received; the write-back's line and way | `VICTIM`, `WRITE_BACK`, `FETCH`, `FILL` |
| the update | the one slot: its client, line, set and way, the entry before, its write's response | `UPDATE_WRITE`; the slot waits outside the state machine (step 6 of CI section 3) |
| the page sequencer | the page and its line counter | `DISCARD` |
| the replies | the reply bus's register, the fill port's two response registers | |
| its AXI port | AR, AW and W's registers; the line being sent, held in a 1,024-bit register so that the cache may change the line meanwhile | |

**How a reader sees a cycle:** `edge` reads top to bottom as the hardware does in the cycle: the inputs (the ports'
requests, the AXI channels, the memories' outputs from the last cycle's reads); the replies that go out; the update
slot and the write-back on their responses; the state machine's one case for the state, with its decisions; the memory
ports' reads and writes for the cycle; then the next registers. A trace option prints one line a cycle: the state, the
request, the set and way, each AXI channel's handshake, each memory port's address.

**Two steps** (third note, section 1):

1. **One request at a time,** but for the parked update (CI section 3, step 6), which the sequence needs: the third
   note says so too.
2. **Hits served while a miss waits:** one fetch outstanding; while it waits on AXI, a request that hits, in a set
   other than the fetch's, is served. A second miss waits for the first.

Step 1 is complete, checked and pushed before step 2 starts, and every check runs on both.

## 5. The arbiter and the LPDDR model

- **The arbiter:** for each client an AR slot and an AW slot (`ready` while empty), the lists of outstanding bursts
  and pieces (`CLIENT_BURSTS` each), two round robin pointers; the W mux follows the piece in turn; R and B steered by
  ID, `rlast` held back but on a burst's last piece, a write's responses gathered into one. All of AI section 3.
- **The RW engine's adapter:** a few lines that give its one address and length to AR or AW by its job's kind.
- **The LPDDR model:** AI section 5, on the store: pages by address, a read of a page nothing wrote counted; the
  memory read at each beat and written at each response; fixed and jitter modes; faults; every rule checked, with
  `tests/axi4_slave.h`'s `Handshake` on each channel.

## 6. The setting

**`MachConfig::cache_ctl`, setting `cacheCtl`, off by default.** Off, nothing is built and nothing changes: v1.37's
memory model stays, and every run passes unchanged.

| on | |
|---|---|
| the tracer tile's Tracer | its fill port is the cache's |
| the cache, the arbiter, the LPDDR model | built, on the one store; stepped each cycle after the Tracer, in its thread domain (the proof runs are one domain) |
| the arbiter's clients | the cache (ID 0); the RW engine through its adapter (ID 2); ports 1 and 3 empty |
| the Updater | the black box, unchanged, on the store: no AXI port, no look-up port (the brief) |
| `MemSystem` | not used |

**The Tracer's hook, the one change to a v1.36 unit:** `Tracer::FillPort` gains two optional callbacks, a read's and
a write's response polled in each cycle while one is outstanding; with them set, the handshake's callback returns "not
yet". The cache answers the poll from its response registers, its outputs of the cycle. Unset (every run with the
setting off), the Tracer runs the code it runs today. Idle-skipping is told the cache's next event (none while it is
busy: it does not skip).

**The Tracer-alone harnesses** (the rig, `tracer_block`) get the same setting as a flag.

## 7. The Updater stays a black box: why no line in the cache can be stale

With the setting on, the Updater writes the store directly, which is LPDDR. A line in the cache is stale if LPDDR
changed under it. It cannot, in these runs:

1. **The cache's lines come only from the Tracer's fills:** it is the cache's one client in a chip run. They are lines
   of the Tracer's copy and of the jump table's file, and of the pages those share: the start image's, the cycle-0
   image's, the program image's, the base table's.
2. **All of them are complete before the Tracer's first fill.** The tracer tile is loaded in the cycle after the done
   of segment *s*'s run (WORKER3's report), and the jump table and the Tracer's copy are finished by then.
3. **The one run after that, segment *s*'s third, writes only pages the final image owns:** pages taken from the
   free page queue, and pages the final image made its own in its runs. With `tracer_copy`, at each level the Tracer's
   copy takes its own page and the final image another (`updater.cpp`, `bring`): their own pages are disjoint, and
   what they share is the start image's, which is frozen. So nothing the Tracer can reach is written after it starts.
4. **No page is used twice** (v1.37 frees nothing), and the cache is empty at the reset, so no line of an earlier
   use of a page can be found.

**And what I do about it anyway:** a check, on in every test with the setting on. At every hit on a clean line, and
every line the cache writes back, the line is compared with the store: a clean line that differs from LPDDR stops the
simulator ("a stale line"). A dirty line must be in a page the Tracer may write. The check costs a 128-byte compare a
hit, and can be turned off for speed. When the Updater's controller joins (the project's step 6), the black box's
direct writes go and the same check stays.

## 8. How Part B is checked

| check (the brief's) | how |
|---|---|
| **the cache alone:** every operation and every case of a reply | `test_cache`: a store built by hand with files of depth 1 to 3, holes, shared pages and pages of other files at each level; every row of CI section 2's tables, each on a hit and on a miss |
| a miss, an eviction, a write-back | sets filled past 8 ways with clean and dirty lines; the write-back's beats and its response; the fetch waiting for it |
| a discard of a page with clean, dirty and in-flight lines | the in-flight line held by a slowed LPDDR; the reply after its response; nothing written for the dirty lines |
| an update beside a Tracer's fill of the same line | the fill sees the new entry from the cache while the write is in flight; the update's reply after the response |
| two clients | two look-up drivers and the fill port at once; the one update slot; round robin |
| LPDDR errors | a fault on each kind of line: a look-up's, an update's walk, an update's write, a fill read's (`beyond` and the flag), a fill write's walk, a write-back's (the flag) |
| **random traffic against a reference with no cache** | the stand-in (CI section 8), with the fill port's reads and writes made straight on its store, on a second store: the same random requests on both, from several clients and the fill port, with the jitter mode on the cache's LPDDR; **every reply equal throughout, and the two stores equal at the end** once the cache's dirty lines are written out by a test-only sweep. Many seeds |
| **the arbiter alone** | test clients and the RW engine through its adapter: fairness (round robin, counted turns), the cap (a page as 8 pieces, a line between them), the IDs (beats and responses back to their clients), a long burst (one `rlast`, one response, the worst `bresp`), errors (a fault in one piece), the limits (`CLIENT_BURSTS`), every AXI rule watched on both sides |
| **the Tracer on the cache** | `tracer_block` and `suite_tracer`'s guests with the flag: every row and record as with `MemSystem`. `segment-real`'s three segments: as `proof-real`, from their packet files, every row and record against the recorded files (the RTL test's `Fill` has no trees to walk); and † `segment-real` itself, if its guest can be put in a store as the rig puts its own. Cycles differ; the report gives how much and why, segment by segment |
| **the whole chip** | `proof-real`, `proof-matrix` and `proof-29` with the setting on: every image, every row, every record; on the runner (`updater_wip/tasks/v137-check/`, a setting added to the task) |
| **with the setting off** | `./run_tests.sh all --check` and `make check`, as before: no golden changes |

**The figures,** from counters in the cache: hits and misses by kind of line (the memory file's index and data lines,
the map's, the look-up port's); evictions and write-backs, clean and dirty; the walks' lengths; the cycles a fill and
an update take, as a distribution; the AXI channels' busy cycles and the arbiter's waits.

## 9. The order of the work

Each step one or more commits, pushed, with `./run_tests.sh quick --check` passing and `main` merged in (never
rebased).

1. The LPDDR model and its test.
2. The arbiter, the adapter and their test.
3. The cache memory, the controller's step 1, the stand-in and `test_cache`, the random test with it.
4. The setting, the Tracer's hook; the Tracer-alone runs and the proof runs on the cache; the figures. The Quest
   tasks: Quest's `NUMBERS.md` pulled and re-read just before claiming, the claim pushed before the task.
5. The controller's step 2; everything again.
6. `RUNNING-v1.37.md`, `CHANGES-v1.37.md`, `REPORT.md`.

## 10. What I would change in the other documents (not done: in `v1.4_wip/` I add the two documents only)

| document | change |
|---|---|
| `UPDATER-INTERFACE.md` | "Its other connections": cite AI and CI; its AXI port is ID 1. `ERRORS.not_owned` is the cache's `not_owned` reply to an update, and `ERRORS.lpddr_error` also the cache's `lpddr_error` reply. Whether the Updater discards the pages it takes (CI, Open 3) |
| `RTL-CHANGE-REQUESTS.md` | comment A: specified in AI section 4 (`ID_BITS` 2, one ID a client). Comment B: the turns and the pieces of AI section 3. A new comment if the owner wants it: `fill`'s read status for an LPDDR error (Q1) |
| `V136-SCOPE.md` | outcome 4: specified in AI |
| `v1.4_wip/README.md` | the two documents in its table |
| the figure | the cache split into controller and memory (third note, open), the arbiter with its ports and IDs, the Updater on the master tile |
| `updater_wip/README.md` | WORKER5's row (the integrator's) |
| the specification, later (the spec session's) | machine-spec 3's `lpddr` as the controller's port with IDs and the clients' ports; 23.6 and 17.9.14 from CI |
