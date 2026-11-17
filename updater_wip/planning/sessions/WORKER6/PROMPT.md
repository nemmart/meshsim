# Worker 6 (updater_wip): the Updater's controller

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for proving
the Jolt zkVM. **v1.37 runs** (`v1.37_wip/`, `RUNNING-v1.37.md`): given the host's packet file, the master tile's
firmware drives **the Updater**, which applies the host's update words to files in LPDDR and so builds the memory
images the Tracer runs on. Today the Updater is a software black box (`src/units/updater.*`): right, readable, and
nothing like hardware. It does a page's whole job in one call on the file store, and it never touches an AXI port
or the cache.

**Your part: the Updater's real controller.** A state machine with its one page memory, its decoder, a word port
for its data, an AXI port to LPDDR and a look-up port to the cache, **behind the black box's registers.** It is
written in the shape of hardware: the RTL engineer should be able to read a design off it.

**The data input changed on 2026-11-16 (owner), and v1.37 has not caught up.** In the design, the Updater's data
comes from Ingress by a flow-controlled port of 128-bit words: no queue a program can see, no credits. v1.37 as
merged still has the 8 KB segment queue, filled by `WRITE`s from Ingress's DMA under credits the firmware keeps.
**You build the controller to the port,** and put the merged segment queue in front of it as a shim, so that the
master tile's firmware runs on your controller unchanged. WORKER7 will put the new path into Ingress and the
firmware; the shim is deleted then.

**You and WORKER5 share no code** (owner). WORKER5 builds the cache's controller, the AXI arbiter and the LPDDR
model. What you share is these documents and headers:

| | |
|---|---|
| `v1.4_wip/planning/UPDATER-INTERFACE.md` | the Updater from outside: its registers, the data input, the free page queue and its credit, events, errors, and the runs; "In v1.37" says what the black box still has |
| `v1.4_wip/planning/INGRESS-INTERFACE.md` ("The forward count", "The word port to the Updater"), `v1.37_wip/src/units/word_port.h` | your data input: who drives it, and when a word moves |
| `v1.4_wip/planning/AXI-INTERFACE.md`, `v1.37_wip/src/units/axi_port.h` | your AXI port, client 1, and what you may count on |
| `v1.4_wip/planning/CACHE-INTERFACE.md`, `v1.37_wip/src/units/lookup_port.h` | your look-up port, port 1: every reply in every case, what the cache guarantees, **what it asks of you** (section 3), and the stand-in you test against (section 8) |

**Do not read WORKER5's branch or its code.** The two controllers meet for the first time when the integrator
joins them, and that meeting is the test of the documents. If a document is unclear or cannot work, it is a
question (`QUESTIONS.md`), not something to settle by looking.

READ FIRST: the top-level `README.md`; `updater_wip/README.md`; `v1.4_wip/README.md`; the documents and headers
above, in full; `v1.4_wip/planning/HOST-FRAMES.md` ("Update words", "Files, lengths and page counts",
frame 9); `DESIGN-NOTES-2026-11-15.md` section 4 (the page memory, the decoder and its type line, the page-fault
sequence; its first step, a flush, is gone); `DESIGN-NOTES-2026-11-16.md` sections 3, 4, 9, 10, 11, 15 and 17. In
`v1.37_wip/`: `README.md`, `RUNNING-v1.37.md`, `CHANGES-v1.37.md`; `src/units/updater.h` and `updater.cpp` (the black
box: your reference for what a run does, and for the registers); `peripheral.h`, `periph_slot.*`, `file_store.*`,
`lru_coder.h`, `rw_engine.h` (how a unit of the RTL is written here: "this cycle's decisions, from the state at its
start"); `tests/test_updater.cpp`, `tests/test_master.cpp` and the proof steps of `run_tests.sh`. And
`updater_wip/planning/sessions/WORKER3-REPORT.md` and `WORKER3-NOTES.md`, above all the choices "the RTL will have
to make its own".

**The names.** The notes use v1.35's names in places: C&C 1, 2 and 3 are the master, memory and tracer tiles; the
"bus driver" is the AXI arbiter.

WHAT THE CONTROLLER IS (owner's decisions, from the notes)
- **One 4 KB page memory,** and the work strictly in sequence. While a page is being read, nothing is taken from
  the decoder.
- **The data input** is the word port (`word_port.h`): `valid` and 128 bits from the source, `ready` from you.
  What you keep behind it is yours: a few words, so that the decoder is not starved, and nothing a program can
  see. `ready` does not depend on `busy`.
- **The decoder** sits after the data input and gives, for each update word, a 32-bit address, a 64-bit value
  and a type: words, half a page, or a whole page, from the run's start and the values still to come. A half or a
  page that the run covers whole is not read first.
- **After a page is written the Updater idles until the write's response,** and only then sends the update that
  names the page. The data is in LPDDR before the index points to it.
- **A page fault is the Updater's,** never the cache's: a hole, or a page that is not the file's own, at any level.
  It takes a free page, builds the page, writes it, and sends the update. Several levels in one fault: an index
  page that is not the file's own is read, its entries' owned bits cleared, written to a new page, and the entry
  above changed.
- **With `tracer_copy`,** each page the run changes goes twice: unchanged into the file in `TRACER_ROOT`, and
  updated into the file in `IMAGE_ROOT`; and so do the index pages above it, wherever a file does not own them.
- **A run's end touches nothing:** what is left in the data input, in the decoder and in the free page queue
  stays for the next run.
- **Roots** (`UPDATER-INTERFACE.md`, "For v1.37"): the controller makes them too.

THE WORK, in `v1.37_wip/`
1. **The controller,** as a unit beside the black box, chosen by a setting that is off by default. With it off,
   every run is as today.
   - **The shim:** the merged segment queue (region part 1, 512 entries, `EVENTS` [2] `segment_credit`, its part of
     `queue_overrun`) as a small unit of its own that drives your word port as a source. The controller knows
     nothing of it. Removing it must be deleting it and wiring Ingress to the port.
2. **In the shape of hardware:**
   - its registers in one place, the state machine's state an enumeration among them, and nothing else kept
     between cycles;
   - its outputs a function of its registers alone: every output a flip-flop (`axi_port.h` has the cycle
     convention);
   - one function that advances a clock: it reads the registers as they stood at the cycle's start and the other
     units' outputs of the cycle, and writes the next registers;
   - it reads no other unit's insides, only its ports;
   - a memory is a unit with explicit ports. The page memory is read and written a 128-bit beat or a 64-bit word a
     cycle; say what ports you give it and why;
   - the decoder takes a bounded number of bits a cycle; say how wide its shifter is and what a cycle can decode.
   Say in your plan how a reader sees a cycle, and give the state diagram.
3. **The stand-in cache** of `CACHE-INTERFACE.md` section 8, on your look-up port, and **a test LPDDR** on your AXI
   port: a slave of your own under `tests/`, on the file store's pages, keeping `AXI-INTERFACE.md`'s rules, with a
   fixed latency and a mode that varies its timing. Both are test pieces: WORKER5's cache, arbiter and LPDDR model
   take their place when the two are joined.
4. **The order of the Updater's own traffic,** worked out and written down: for each kind of fault, with and
   without `tracer_copy`, which pages are read, which written, in what order, and which updates are sent when.
   Check it against every line of "What the cache asks of its clients". A new page is written whole.

HOW IT IS CHECKED
- **The controller alone, against gold:** fed gold's frames directly on the word port, with no shim, its images
  equal gold's reference
  (`frames-images`) page for page, on every case `test_updater` runs on the black box; `PAGE_COUNT` is 0 at every
  `done`.
- **Against the black box:** with the shim: the same registers, the same queues, the same words, and at each `done`
  the same file contents, the same `PAGE_COUNT`, the same events and errors; on gold's frames and on random update
  streams
  (holes, shared pages at every level, runs that end inside a page, `continue` across runs, raw and LRU).
  - Say whether your controller takes its free pages in the black box's order. If it does, the two stores are
    compared byte for byte; if it does not, say why, and compare the files.
- **The timing varied:** every check again with the test LPDDR and the stand-in answering at random times, and the
  word port's `valid` coming and going at random. The results do not change.
- **Errors:** an error response to a read and to a write; a reply of `lpddr_error` and of `not_owned` to an
  update; a word beyond the file; a page needed with `PAGE_COUNT` at 0; a full free page queue (and, through the
  shim, a full segment queue). Each sets its bit, and the run stops as the black box's does.
- **The whole chip, with the setting on and the shim in:** `test_master`, `proof-real`, `proof-matrix` and
  `proof-29`. The master tile's firmware is not changed. Every image, every row and every record as with the black box.
- **With the setting off,** `./run_tests.sh all --check` and `make check` as before.
- **The figures:** the cycles a page takes by kind (new, copied, patched; with the Tracer's copy), the bursts and
  look-ups a page, where the Updater waits (data, pages, LPDDR, the cache), and the whole run against the black
  box's.

WHERE THE DOCUMENTS ARE OPEN
- A dagger (†) marks what is proposed and not ruled on. Take each as written, unless it cannot work; then say so in
  `QUESTIONS.md` with the evidence, take the nearest thing that does, and carry on.
- What they do not say and you must settle: the simplest reading, in `NOTES.md` under "Choices", with the reason
  and the alternative.
- **Do not edit anything in `v1.4_wip/`,** nor the three headers. List what you would change.

RULES
- **Propose your plan first** in `NOTES.md` and wait for the owner's go-ahead before writing code: the controller's
  parts and their registers, the state diagram, the page memory's ports, the decoder, the order of the traffic for
  each kind of fault, the test pieces, the checks.
- **WORKER7 will change things next to yours:** Ingress's black box (the forward count), the black box Updater's
  input, and `fw/cc/master.c`. It may run while you do. You do not touch those; it does not touch your
  controller. Merge `main` in often (never rebase). If its work reaches `main` before you finish, the integrator
  will say so: the shim then goes, Ingress drives your port, and the whole-chip checks run on the new firmware.
- **Branch `updater-w6`.** Your notes, questions and report are in `updater_wip/planning/sessions/WORKER6/`. The
  integrator merges into `main`. Frozen folders are never edited.
- The black box stays, unchanged in what it does. Gold and the packetizer are not changed. The firmware is not
  changed.
- Exactness is the requirement: a difference in a check is a bug, never a tolerance.
- Long runs go to the runner (the Quest repository; `PIPELINE.md` there); `updater_wip/tasks/v137-check/` is the
  task to reuse. **Quest numbers:** pull Quest and re-read `NUMBERS.md` immediately before claiming, and push the
  claim before writing the task; on a rejected push, pull and take the next free number.
- Attach the repository to the session to push, or clone with the token from your user preferences. **Never write
  a token into a file.** Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, and end each turn with a clean `git status`.
- Keep replies to the owner short.

NOT YOUR WORK: the cache's controller, the AXI arbiter and the LPDDR model (WORKER5); Ingress, the forward count
and the master tile's firmware (WORKER7); joining the controllers and taking the shim out (the integrator); the
file system; the specification.

DELIVERABLES
- `v1.37_wip/`: the controller, its page memory and decoder, the shim, the setting, the test pieces and the tests; the
  running guide and `CHANGES-v1.37.md` brought up to date.
- `WORKER6/NOTES.md` with the plan, the state diagram, the order of the traffic, and the "Choices" list.
- `WORKER6/REPORT.md`: what was built; each check's result; the figures; what a hardware designer must still
  choose; and what you would change in the documents, as lists for the owner.
