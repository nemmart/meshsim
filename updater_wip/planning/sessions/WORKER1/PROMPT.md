# Worker 1 (updater_wip): gold writes the host's frames

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for proving
the Jolt zkVM. A proof's run is cut into 128 segments, and each ASIC traces one of them, segment *s*. The host sends
each ASIC the program, the proof's inputs and every segment's memory changes; on the chip a unit called **the
Updater** applies them and builds the memory images the Tracer runs on. The design is in `v1.4_wip/`. The Updater
and the cache controller are to be built in simulation first, in `updater_wip/`.

**Your part is the first piece: gold.** Gold is Jolt's tracer in Java, the reference for everything the chip traces.
Today it writes v1.3's segment file. You add **the host's frames for v1.4**, as
**`v1.4_wip/planning/HOST-FRAMES.md`** lays them out: that document is the specification of this work. What you
write is what the Updater will be fed and checked against.

READ FIRST: the top-level `README.md`; `updater_wip/README.md`; `v1.4_wip/README.md`; **`HOST-FRAMES.md` in full**;
`v1.4_wip/planning/DESIGN-NOTES-2026-11-15.md` sections 4 and 5 (what the Updater does with the frames, the four
images, the base jump table); `v1.4_wip/planning/UPDATER-INTERFACE.md` (the counts the Updater is given);
`v1.36/pipeline/README.md`, `v1.36/pipeline/gold/README.md` and `v1.36/pipeline/jumpmap/README.md`; gold's
`Main.java`, `SegmentWriter.java`, `ProofParams.java`, `Program.java`, `Layout.java`, `Memory.java`, `JumpTable.java`
and `StaticTargets.java`; jumpmap's `Image.java`; `v1.3/planning/sessions/WORKER18-NOTES.md` ("The v1.3 segment
file"); `v1.3/planning/DECISIONS-v1.3.md` sections 15 and 16; `v1.36/spec/machine-spec.md` 23.5 (a file is a tree
of 1,024-entry index pages over 4 KB pages).

**The terms.** `HOST-FRAMES.md` is written in v1.35's names: C&C 1, 2 and 3 are v1.36's master, memory and tracer
tiles. Where it says "job" it means one run of the Updater. Nothing in your work depends on the names.

THE WORK
1. **The frame writer.** A `frames` command in gold that writes the nine frames for one program and one proof into
   one file, in the order of `HOST-FRAMES.md`'s first table: the ELF, the base jump table (optional), the proof
   header, the jump table header and data, the data image, the register states, the advice logs, the memory
   segments. The update words in both forms, raw and LRU. The jump table in both ways: a full new table, and pages
   added to a base table. Every count the headers carry, the page counts included (data and index pages, from the
   file's tree).
2. **The reader.** A command that decodes a frame file and prints every frame's fields, so that a person can
   inspect what was written; and a check mode that verifies the file against itself (every length, every count in
   frames 3 and 4 against the data it describes, the two places the advice counts appear).
3. **The reference images.** A plain Java model that plays a frame file into empty memory for a given *s* and
   produces **the four images**: the cycle-0 image, the image at the start of segment *s* (the Updater makes two
   identical copies of it), and the final image; and the run's jump table. It is the oracle the Updater's output
   will be compared with, so it is written to be read, not to be fast, and it is not the Updater's design. An
   image is a file's pages by their offset in the file; where a page lands in LPDDR is the chip's choice and is
   not part of it. Give each image a digest and a way to write it out; propose the format in your plan.

HOW IT IS CHECKED
- **The images against gold's own memory:** for every *s*, the reference's cycle-0, start and final images equal
  gold's memory at cycle 0, at the cut before segment *s* and at the end. A page not present reads as zero.
- **The register states and the advice logs** against the records and the logs gold already writes in the segment
  file: the same values, repacked.
- **The memory segments** against the segment file's updates: each word's last value in its segment.
- **The jump table:** every jump target the run takes is found in the run's table by the Tracer's probe (k, p1, p3,
  three lines), for a full new table and for a base table with pages added.
- **LRU:** decoding the LRU form gives the raw form, word for word.
- **Nothing old broke:** gold's existing tests, the guests, and the segment files are byte-identical to before.
- **Where it runs:** the guests in the repository (gold's and jumpmap's); the small inputs (EVM Zone trace-23,
  Cyclone input/22, with the hard-cut lists), which are built from the pinned sources as
  `v1.36/pipeline/inputs/README.md` says; both workloads at 2²⁹ on the runner.
- **Report the sizes:** each frame's bytes for each run, raw against LRU, the page counts, and the whole file.

WHERE HOST-FRAMES.md IS OPEN
- A dagger (†) marks what is proposed and not ruled on. **Take each as written.**
- Its "Open" list, and anything it leaves unsaid that you must settle to write code: choose the simplest thing,
  and record each choice in your `NOTES.md` under "Choices", with the reason and what the alternative was.
- **Do not edit `HOST-FRAMES.md`** or anything else in `v1.4_wip/`. If you believe the document is wrong, or that a
  frame can't be produced as specified, stop and say so in `QUESTIONS.md` with the evidence.

RULES
- **Propose your plan first** in your `NOTES.md` and wait for the owner's go-ahead before writing code. Start it
  with: where each frame's data comes from in gold today; what gold does not yet compute; the image format; the
  command lines.
- **Your code is in `updater_wip/pipeline/gold/`**, a copy of `v1.36/pipeline/gold/` that you make as your first
  commit, unchanged, so that every later change shows as a diff. If you need jumpmap's code, copy
  `v1.36/pipeline/jumpmap/` beside it the same way, and say in your plan what you take from it. **Frozen folders
  are never edited** (`v1.35/`, `v1.36/` and every earlier version).
- Gold stays as it is: plain Java 21, no dependencies, no native code, its own build script. The old `segments`
  command and its output are unchanged.
- **Branch `updater-w1`**; merge `main` in often (never rebase); the integrator merges into `main`. Your notes,
  questions and report are in `updater_wip/planning/sessions/WORKER1/`.
- Exactness is the requirement: a difference in a check is a bug, never a tolerance.
- Long runs go to the runner (the Quest repository; `PIPELINE.md` there). **Quest numbers:** pull Quest and re-read
  `NUMBERS.md` immediately before claiming, and push the claim before writing the task; on a rejected push, pull and
  take the next free number. The runner's data and its hashes are listed in
  `sim/planning/sessions/WORKER13-NOTES.md`. Background jobs don't survive a turn.
- Clone with the tokens from your user preferences. **Never write a token into a file.** Never `pkill -f`; use
  `pgrep` and kill by PID.
- Commit and push after each completed piece, keep the last tool calls of a turn for committing, and end each turn
  with a clean `git status`.
- Keep replies to the owner short.

NOT YOUR WORK: the simulator, the firmware, the specification, the Updater itself, and where a page lands in
LPDDR.

DELIVERABLES
- `updater_wip/pipeline/gold/`: gold with the `frames` command, the reader and the reference images; their tests
  in gold's build; its README updated with how to run them and what they are checked against.
- `WORKER1/NOTES.md`, kept current, with the plan and the "Choices" list.
- `WORKER1/REPORT.md`: what was built; each check's result for each run; the size tables; every choice made where
  `HOST-FRAMES.md` was open; what you would change in `HOST-FRAMES.md`, as a list for the owner.
