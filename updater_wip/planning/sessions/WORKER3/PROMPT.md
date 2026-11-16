# Worker 3 (updater_wip): v1.37, the Tracer on images the Updater built

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for proving
the Jolt zkVM. A proof is cut into segments, and each ASIC traces one, segment *s*. The host sends the program, the
proof's inputs and every segment's memory changes as frames; on the chip a unit called **the Updater** applies them
and builds the memory images the Tracer runs on. The design is in `v1.4_wip/`. v1.36 is the RTL's hardware as built,
with its simulator and firmware; it is the base.

**v1.37 is the first thing that works end to end** (owner): the simulator is started with one file, the data that
comes over the link from the host; **the master tile runs real firmware**, which reads the frames out of Ingress and
drives the Updater; the Updater builds the images; the Tracer traces segment *s* on them. Ingress and the Updater are
**software black boxes** for now, behind their real registers; the cache is the software model v1.36 already has.
Nothing runs on the memory tile. The real controllers replace the black boxes later, with everything around them
already running.

**Today a test harness does this job.** In v1.36 a harness "standing in for C&C 1 and C&C 2" applies the update
stream, makes the Tracer's copy and the jump table, and lands the segment's record and advice in the tracer tile's
memory before it boots (`fw/tracer/tracer_layout.h`, `tests/segment_real.*`, `tests/tracer_rig.*`). You put the
packet file, the master tile's firmware and the Updater in its place.

READ FIRST: the top-level `README.md`; `updater_wip/README.md`; `v1.4_wip/README.md`; in `v1.4_wip/planning/`:
**`INGRESS-INTERFACE.md`** and **`UPDATER-INTERFACE.md`** in full (the two interfaces you build), `HOST-FRAMES.md`
(what the frames hold), `HOST-LINK.md` (the packet and its two checks), `DESIGN-NOTES-2026-11-16.md` sections 9 to
14, and `DESIGN-NOTES-2026-11-15.md` section 4 (what the Updater does to a page). In `v1.37_wip/`: `README.md`,
`BUILDING.md`, `RUNNING.md`; `spec/machine-spec.md` 5.2, 5.3, 7, 11, 17.1 to 17.6 and 23; `spec/jolt/tracer.md` (the
record and its files word, the fill port, 17.9.10); `src/units/README.md`, `peripheral.h`, `periph_slot.*`,
`ingress_egress.h`, `file_store.*`, `mem_system.h`, `lru_coder.h`; `tests/segment_real.*`, `tests/segment_bench.*`,
`tests/tracer_rig.*`; `fw/README.md`, `fw/cc/cc1.c`, `fw/tracer/tracer_ctl.c`, `fw/tracer/tracer_layout.h`,
`fw/port/`. And `updater_wip/pipeline/gold/README.md` (the frames, and `frames-images`, the reference images) with
`updater_wip/planning/sessions/WORKER1-REPORT.md`.

**The names.** The design notes use v1.35's names in places: C&C 1, 2 and 3 are the master, memory and tracer
tiles. The streams are foreground and background. A packet is the link's; the mesh's is a `WRITE`.

THE WORK, in three stages. **Report at the end of each** (a section of `REPORT.md`, pushed, and a short summary to
the owner) and go on unless the owner says otherwise.

**Stage 1: the units, each tested by itself.**
1. **Ingress's host side, a black box** (`INGRESS-INTERFACE.md`). It reads a packet file, checks every packet (both
   checks, the numbers in order), and presents packets to the master tile's core: the service flags, the status
   bits, the word counts, and the DMA that reads words out. A packet that fails is `ERRORS.crc_error` or
   `sequence_error`, and nothing more is presented. Everything else in Ingress/Egress stays exactly as it is, and
   so do v1.36's stream words, which the RTL's images use.
2. **The Updater, a black box** (`UPDATER-INTERFACE.md`): the master tile's peripheral, with its registers, its two
   queues and their credits, its events and errors. Behind them it does the real work in software, on the file
   store: it decodes the update words (raw or LRU, carried on across runs by `continue`), applies them to the file
   in `IMAGE_ROOT`, makes a page or an index page the file's own when it is not (copy-on-write, by the owned bits),
   takes every new page from the free page queue in order, keeps the Tracer's copy when `tracer_copy` is set, and
   makes roots as the "For v1.37" section says. It is not the Updater's hardware design: write it to be right and
   to be read. Give it plain timing and say what it is.
3. **The file store** must place a page where it is told (today it hands out addresses itself), since the master
   tile's firmware now chooses the pages.
4. **Tests of each alone,** with no firmware: the Updater fed gold's frames directly produces images equal to
   gold's reference (`frames-images`, page for page) and uses exactly the pages the frames' counts say; Ingress
   against WORKER2's vectors and packet files, and against damaged ones.

**Stage 2: the master tile's firmware.**
1. **The page map:** a bit array, one bit for each 4 KB page of LPDDR, 128 KB, in the master tile's memory (owner).
   The firmware formats it, takes pages from it for roots and for the free page queue, and asks no other tile.
2. **The frame loop** (`INGRESS-INTERFACE.md`, "Frames" and "Knowing a transfer is complete"): on a background
   service flag, copy the frame's header word into its own memory, read the type, length and compression, and send
   the rest where it belongs; keep the Updater's credit allowance, and leave a packet in Ingress when the allowance
   does not cover it.
3. **The proof's sequence** (`UPDATER-INTERFACE.md`, "The runs"): the program image; a base jump table if the file
   has one; the proof header; the run's jump table; the data image; the memory segments as three runs, with the
   copies made at the cut before segment *s*. It refills the free page queue on its credits and sets `PAGE_COUNT`
   from the frames' counts.
4. **What it hands on.** The tracer tile needs its segment's record (frame 7) and advice (frame 8), and the roots,
   depths and hash constants of the Tracer's copy and the jump table. Make these come from the frames and from the
   Updater's results, with the smallest change to `tracer_ctl.c` and `tracer_layout.h`. **What the frames do not
   carry** (the decode table and ROM, the templates) stays as the harness lands it today; list it in your report.
5. ***N* and *s*** are the system's configuration, told to the chip at start-up and in no frame. Propose in your
   plan how the firmware is given them in the simulator.

**Stage 3: the whole chip.**
1. **The test:** the simulator is given a packet file and nothing else of the proof; the master tile's firmware
   builds the images; the tracer tile traces segment *s* and the compute tiles replay it, passing what
   `segment-real` passes today.
2. **And the images are right:** the cycle-0 image, the start image, the Tracer's copy before tracing and the final
   image equal gold's reference; `PAGE_COUNT` ends every run at 0; no error bit is set.
3. **Where it runs:** EVM Zone and Cyclone on the small inputs, for *s* = 0, 1 and one in the middle, with frames
   sent raw and LRU-coded, with and without a base table; then both workloads at 2²⁹ on the runner for a few *s*.
   Say in your plan what a run costs in simulated cycles and in wall time, and choose the *s* by that.
4. **Report the numbers** the black box can give honestly: pages read and written, the Updater's stalls on data
   and on pages, and the firmware's share of the cycles.

WHERE THE DOCUMENTS ARE OPEN
- A dagger (†) marks what is proposed and not ruled on. **Take each as written,** unless it cannot work; then say so
  in `QUESTIONS.md` with the evidence, take the nearest thing that does, and carry on.
- What the documents do not say and you must settle: the simplest reading, recorded in `NOTES.md` under "Choices"
  with the reason and the alternative. Register and bit numbers that collide with something v1.36 uses are yours to
  move; list each.
- **Do not edit anything in `v1.4_wip/`.** At the end, list what you would change in each document.
- **Write Ingress from the documents, not from WORKER2's Java.** The two are meant to check each other; where they
  disagree, the document decides, and if it does not, it is a question.

RULES
- **Propose your plan first** in your `NOTES.md` and wait for the owner's go-ahead before writing code. Start it
  with: how a real segment is set up and run today, piece by piece, and which pieces you replace; the units' files
  and tests; the firmware's structure and its memory; how *N* and *s* reach it; the black boxes' timing; the test
  list and its cost.
- **Your code is in `v1.37_wip/`,** which is v1.36 copied unchanged (main, commit 67aaeb86), so every change shows
  as a diff. It still carries v1.36's freeze marks and names; deal with them as v1.36 did when it began from v1.35,
  and say how in your plan. **Frozen folders are never edited** (`v1.36/` and every earlier version).
- **Gold is `updater_wip/pipeline/gold/`,** not the copy inside `v1.37_wip/pipeline/`. Do not change gold or the
  packetizer; if the frames or the packets lack something, it is a question.
- **Everything v1.36 passes still passes:** `./run_tests.sh all --check` and `make check`, with no golden changed.
  A black box must cost the existing runs nothing.
- **The firmware** is built as v1.36's is (`fw/README.md`, `fw/build.sh`), with the toolchain it pins, xPack
  `riscv-none-elf-gcc` 15.2.0-1. Get it in your session. The runner does not have it: the images you build are
  committed with their hashes, as v1.36's are, and the runner runs those.
- **Branch `updater-w3`**; merge `main` in often (never rebase); the integrator merges into `main`. Your notes,
  questions and report are in `updater_wip/planning/sessions/WORKER3/`.
- Exactness is the requirement: a difference in a check is a bug, never a tolerance.
- Long runs go to the runner (the Quest repository; `PIPELINE.md` there). **Quest numbers:** pull Quest and re-read
  `NUMBERS.md` immediately before claiming, and push the claim before writing the task; on a rejected push, pull and
  take the next free number. `updater_wip/tasks/v136-check/` is a task that builds the small inputs on the runner
  and runs v1.36's suites; it is your pattern. Background jobs don't survive a turn.
- Clone with the token from your user preferences, or attach the repository to the session. **Never write a token
  into a file.** Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, keep the last tool calls of a turn for committing, and end each turn
  with a clean `git status`.
- Keep replies to the owner short.

NOT YOUR WORK: the Updater's and the cache's hardware design; the file system; the memory tile's firmware; the link's
sending side, recovery and the foreground stream; gold and the packetizer; the specification.

DELIVERABLES
- `v1.37_wip/`: the two black boxes and the file store's change with their tests; the master tile's firmware and
  the changed tracer tile's, sources and images; the whole-chip test in the suites; the README and running guide
  saying how to run it.
- `WORKER3/NOTES.md`, kept current, with the plan and the "Choices" list.
- `WORKER3/REPORT.md`, a section for each stage: what was built; each check's result; the numbers; what the frames
  and the packets do not carry that the chip needs; and what you would change in `INGRESS-INTERFACE.md`,
  `UPDATER-INTERFACE.md`, `HOST-FRAMES.md` and `HOST-LINK.md`, as lists for the owner.
