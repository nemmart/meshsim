# Worker 5 (updater_wip): the interface documents, then the cache controller

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for proving
the Jolt zkVM. **v1.37 runs** (`v1.37_wip/`, `RUNNING-v1.37.md`): given the host's packet file, the master tile's
firmware drives **the Updater**, which builds the memory images in LPDDR; the Tracer then traces on them, reading and
writing LPDDR through **the cache**. Today both are software black boxes: the Updater's (`src/units/updater.*`)
reaches straight into the file store, and the cache is v1.36's model of the memory system behind the Tracer's fill
port (`src/units/mem_system.h`). The design of the real ones is in `v1.4_wip/planning/`.

**Now the two real controllers are built, by two workers who do not share code:** you build the cache's, and
WORKER6 builds the Updater's. What makes that possible is a fixed interface between them, and it is not written
down yet. **So your work has two parts, with a stop between them.**

- **Part A: write the interface documents, push them, and stop.** The integrator reviews them with the owner. Only
  when they are agreed is WORKER6 started, and it builds from the documents alone, never from your code.
- **Part B, after the go-ahead: the cache controller,** with the cache memory, the AXI arbiter and the LPDDR model
  behind it.

READ FIRST: the top-level `README.md`; `updater_wip/README.md`; `v1.4_wip/README.md`. In `v1.4_wip/planning/`:
`DESIGN-NOTES-2026-11-15.md` sections 2, 3, 4 and 7 (the path to LPDDR, the cache, the Updater, the AXI arbiter);
**`DESIGN-NOTES-2026-11-16.md` sections 1 to 4** (the cache as a controller and a cache memory; flush and discard;
updates that write through; the sequence of an `update`) and sections 10, 14 and 15; `UPDATER-INTERFACE.md`;
`V136-SCOPE.md` (what v1.36 gives: the `fill` port); `RTL-CHANGE-REQUESTS.md` (comments A, B and D); the figure
(stale in where it puts the Updater, right about the buses). In `v1.37_wip/`: `README.md`, `RUNNING-v1.37.md`,
`spec/machine-spec.md` 3, 17.7, 19 and 23.5; `spec/jolt/tracer.md` (the fill port and 17.9.14; the Tracer's own
caches; the files word); `src/units/mem_system.h`, `file_store.*`, `rw_engine.h` ("THE LPDDR PORT": the AXI port
type the simulator already has), `tests/axi4_slave.h`, `src/units/updater.*`, `tests/tracer_rig.*`,
`tests/segment_real.*`. And `updater_wip/planning/sessions/WORKER3-REPORT.md` and `WORKER3-NOTES.md`.

**The names.** The notes use v1.35's names in places: C&C 1, 2 and 3 are the master, memory and tracer tiles; the
"bus driver" and "splitter" are the AXI arbiter.

## Part A: the interface documents

Two documents in `v1.4_wip/planning/`, written in the manner of `UPDATER-INTERFACE.md` and `INGRESS-INTERFACE.md`:
mostly tables, every field with its bits, (owner) on what the notes record as the owner's decision, a dagger (†) on
what you propose, and an "Open" list. **A worker who reads only these two and `UPDATER-INTERFACE.md` must be able
to build the Updater's controller and have it work with your cache the first time they meet.**

**1. `AXI-INTERFACE.md`: the path to LPDDR.**
- **The AXI port a client has** (the Updater, the RW engine, the cache; the Binder later): the five channels, each
  signal and its width; 128-bit data; the address and its unit; a burst's rules (at most 256 beats, never across a
  4 KB boundary); the sizes in use (a line 8 beats, half a page 128, a page 256); IDs.
- **What a client may count on:** no order between a read and a write, so a client that reads what it wrote waits
  for the write's response first; what it may have outstanding at once; what an error response is.
- **The AXI arbiter:** round robin (owner). † A turn is one burst, with a cap (the notes suggest about 32 beats);
  reads and writes arbitrated apart; each client's transactions carry its own ID to the memory controller (the
  comment to the RTL: the `lpddr` port with several IDs). Say exactly what a long burst becomes, and what a client
  sees of it.
- **The LPDDR model in the simulator:** one memory behind the arbiter, the same contents the file store reads (in
  v1.36 the RW engine's LPDDR and the file store's are two models); its latency, as parameters; how a test makes
  it answer with an error.
- **How it relates to what v1.36 has:** the RW engine's `lpddr` port (AXI4, 128-bit data, one ID) and the slave
  models of `tests/axi4_slave.h`. Reuse the port's type if it serves; say what changes.
- **A C++ header for the port,** small and without logic, committed with the document: what both workers compile
  against.

**2. `CACHE-INTERFACE.md`: the cache, from outside.**
- **The look-up port,** which is the part still loose: a request port for each client and one shared reply bus
  (owner). The request's fields with their bits: the operation, the file's root, its depth, the page number, the
  level, the new entry. The reply's: the client, a status, the level, the entry. The handshake, and how many
  requests a client may have outstanding. Levels count from the top (owner). A root and a page reference are in
  the stored 32-bit form, 128-byte units.
- **The three operations** (third note, section 2): look up, update, discard a page. For each: exactly what comes
  back in every case (found; a hole, and at which level; a page that is not the file's own, and at which level;
  beyond what the depth reaches; an LPDDR error).
- **An `update`'s sequence and its guarantees** (third note, sections 3 and 4): it writes through, the reply comes
  after the write's response, an index line is never dirty, one update in flight. And what the cache asks of its
  clients: the data page is in LPDDR before the update is sent; one writer for a file at a time.
- **What the cache never does:** a page fault is its client's to handle.
- **The fill port** to the Tracer: v1.36's, unchanged. Say so, with its fields, and what the Tracer's writes do
  (they stay dirty in the cache until evicted).
- **The cache from outside:** 512 KB, 8 ways by 512 sets, lines of 128 bytes keyed by their LPDDR address; a
  line's states; what is never evicted; what a discard does to a line being written.
- **What a client may assume of its timing,** and what it may not.
- **A C++ header for the look-up port,** as above.
- **The stand-in WORKER6 will test against:** say what the simplest correct cache is (none at all: a look-up walks
  LPDDR, an update writes its entry straight to LPDDR and answers after the response), so that its stand-in and
  your cache are the same thing seen from the port.

**Where the notes are silent or disagree, decide, mark it †, and list it under "Open" with the alternative.** The
three notes are dated; a later one overrides an earlier one. If something cannot work as the notes have it, say so
in `QUESTIONS.md` with the evidence.

**Then:** your plan for Part B in `NOTES.md` (below), push everything on your branch, give the owner a short
summary, and **stop**.

## Part B: the cache controller (after the go-ahead)

1. **The LPDDR model and the AXI arbiter,** as `AXI-INTERFACE.md` has them.
2. **The cache memory:** the data memory and the tag memory, as plain memories with explicit read and write ports.
3. **The cache controller** (third note, section 1): the front end, the walker, the miss handler (fetch; choose a
   line to evict; write it back first if it is dirty), the page sequencer for a discard, the replies. † In two
   steps: one request at a time first, then hits served while a miss waits.
4. **In the shape of hardware.** A state enumeration, registers, and one function that advances a clock; a unit
   reads another's outputs of the last cycle and never its insides; a memory is read and written through a port, a
   line or a beat a cycle. The RTL engineer should be able to read a design off it. Say in your plan how you hold
   to this, and how a reader sees a cycle.
5. **Behind a setting,** off by default: v1.37's memory model stays, and every run passes unchanged with the
   setting off.
6. **The Updater stays a black box in your runs,** and it writes the file store directly, not through your cache.
   Say in your plan why no line in the cache can then be stale, or what you do about it.

HOW PART B IS CHECKED
- **The cache alone,** with a test driver as its clients and the Tracer's side: every operation and every case of
  a reply; a miss, an eviction, a write-back; a discard of a page with clean, dirty and in-flight lines; an update
  beside a Tracer's fill of the same line; two clients; LPDDR errors; and random traffic against a reference that
  holds no cache at all, the memory equal at the end and every reply equal throughout.
- **The arbiter alone:** fairness, the cap, IDs, a long burst, errors.
- **The Tracer on it:** what `segment-real` and the Tracer-alone steps pass with v1.37's model, they pass on your
  cache: every row and every record. Cycles differ; say by how much and why.
- **The whole chip:** `proof-real`, `proof-matrix` and `proof-29` with the setting on.
- **With the setting off,** `./run_tests.sh all --check` and `make check` as before.
- **The figures:** hits and misses by kind of line, evictions and write-backs, the walk's lengths, and the cycles
  a fill and an update take.

RULES
- **Propose your plan for Part B** in `NOTES.md` with the documents, and wait for the owner's go-ahead before
  writing code other than the two headers.
- **In `v1.4_wip/` you add the two documents and nothing else.** List what you would change in the others.
- **Your code is in `v1.37_wip/`.** WORKER4 is changing Ingress's black box and `fw/cc/master.c` there, and the
  Updater's queue size; WORKER6 will add the Updater's controller. Keep to your own files where you can, and merge
  `main` in often (never rebase).
- **Branch `updater-w5`.** Your notes, questions and report are in `updater_wip/planning/sessions/WORKER5/`. The
  integrator merges into `main`. Frozen folders are never edited.
- Gold and the packetizer (`updater_wip/pipeline/gold/`) are not changed.
- Exactness is the requirement: a difference in a check is a bug, never a tolerance.
- Long runs go to the runner (the Quest repository; `PIPELINE.md` there); `updater_wip/tasks/v137-check/` is the
  task to reuse. **Quest numbers:** pull Quest and re-read `NUMBERS.md` immediately before claiming, and push the
  claim before writing the task; on a rejected push, pull and take the next free number.
- Attach the repository to the session to push, or clone with the token from your user preferences. **Never write
  a token into a file.** Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, and end each turn with a clean `git status`.
- Keep replies to the owner short.

NOT YOUR WORK: the Updater's controller (WORKER6); Ingress and the master tile's firmware (WORKER4); the file
system; the RW engine and the Binder, beyond their place on the arbiter; the specification.

DELIVERABLES
- Part A: `v1.4_wip/planning/AXI-INTERFACE.md` and `CACHE-INTERFACE.md`, the two headers, and `WORKER5/NOTES.md`
  with the plan for Part B.
- Part B: the LPDDR model, the arbiter, the cache memory and the controller in `v1.37_wip/`, with their tests and
  the setting; the running guide and `CHANGES-v1.37.md` brought up to date.
- `WORKER5/REPORT.md`: what was built; each check's result; the figures; the choices a hardware designer must
  still make; and what you would change in the documents.
