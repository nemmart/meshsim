# Worker 4 (updater_wip): the threshold, in v1.37's Ingress and the master tile's firmware

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for proving
the Jolt zkVM. **v1.37 runs** (`v1.37_wip/`, `RUNNING-v1.37.md`): the simulator is given a packet file, the data the
host sends over the link; the master tile's firmware (`fw/cc/master.c`) reads the frames out of Ingress, a software
black box, and drives the Updater, another; the Tracer traces on the images the Updater built. WORKER3 built it;
its report is `updater_wip/planning/sessions/WORKER3-REPORT.md`.

**What it showed, and your part.** The firmware wakes for every packet: about 330 cycles awake against a packet's
528 on the link. The owner's cure is **the threshold**: the master tile tells Ingress how many words to gather
before it is woken. It says 1 word to start; it is woken and reads the frame's header word; it then knows how many
words are coming, and says: wake me when 128 words are available. You put that into Ingress's black box and the
firmware, and measure what it gains.

READ FIRST: the top-level `README.md`; `updater_wip/README.md`; **`v1.4_wip/planning/INGRESS-INTERFACE.md` in full**,
above all "Waking for more than a packet: the threshold"; `v1.4_wip/planning/HOST-LINK.md` ("The size of a packet",
"Holding the host back"); `v1.4_wip/planning/UPDATER-INTERFACE.md` (the segment queue and its credits);
`v1.4_wip/planning/DESIGN-NOTES-2026-11-16.md` section 15; `WORKER3-REPORT.md` and `WORKER3-NOTES.md` (sections 4.2,
5 and 6, and "Choices"); in `v1.37_wip/`: `README.md`, `RUNNING-v1.37.md`, `CHANGES-v1.37.md`, Ingress's host side
in `src/units/`, `fw/cc/master.c`, `fw/include/host.h`, `tests/test_ingress_link.cpp`, `tests/test_master.cpp` and
the proof steps of `run_tests.sh`.

THE WORK, in `v1.37_wip/`
1. **Ingress's black box:**
   - `FOREGROUND_THRESHOLD` and `BACKGROUND_THRESHOLD`, read/write, in 128-bit words, 1 at the reset;
   - `FOREGROUND_WORDS` and `BACKGROUND_WORDS` are the words waiting on the stream, in every packet that has
     arrived and passed its checks;
   - a stream's service flag is posted when the words waiting reach its threshold; after a flag, none until the
     core has read out the words the register showed; then, and whenever the threshold is written, the stream is
     looked at again;
   - one transfer of the DMA may run across packets, up to the words waiting;
   - each stream's buffer is 16 KB, 1,024 words (owner). The link waits when a buffer is full, as it does today
     at four packets.
2. **The master tile's firmware:** the owner's sequence. The threshold is 1 between frames; after the header word
   it is the smaller of 128 and the words left in the frame; at the frame's end it is 1 again. It applies to every
   frame, those that go to the Updater and those that go to the firmware's own memory or to the tracer tile. For
   the Updater a wake, a credit of 128 words and a transfer go together.
3. **The check that a frame's header word is a packet's first word** cannot be made by the firmware once a
   read-out runs across packets. † Drop it from the firmware and keep it where packets are still seen: the black
   box may refuse a file in which a frame does not end at a packet's end only if it can know frames, and it does
   not. So it is dropped; say so in your report, with what a mis-cut file now does.
4. **Measure,** on the runs WORKER3 reported: the master's cycles awake a packet and its share of the run, before
   and after; the wakes a frame; the Updater's cycles waiting on data; and what link rate the firmware could now
   keep up with. Then **run the link faster** in the black box (2, 4 and 8 bytes a cycle) to find where the
   firmware, the Updater or the memory becomes the limit, and say which.

HOW IT IS CHECKED
- **Everything v1.37 passes still passes:** every image equal to gold's, `PAGE_COUNT` 0 at every `done`, no error
  bit, the Tracer's last record; `proof-real`, `proof-matrix`, `proof-29`; `./run_tests.sh all --check` and
  `make check`. Cycle counts will change; say which recorded figures moved and why.
- **Ingress alone:** thresholds of 1, of a part of a packet, of several packets, and of the whole buffer; a
  threshold written while words wait; a read-out across a packet's end; the rule for a second flag; both streams
  at once; WORKER2's vectors and damaged files as before.
- **The firmware's tail:** frames whose length is not a multiple of 128 words or of a packet, frames shorter than
  one packet, and a frame of the header word alone.
- **The threshold at other sizes** (32, 64, 256 words) gives the same images.

WHERE THE DOCUMENTS ARE OPEN
- A dagger (†) marks what is proposed and not ruled on. Take each as written, unless it cannot work; then say so in
  `QUESTIONS.md` with the evidence, take the nearest thing that does, and carry on.
- What they do not say and you must settle: the simplest reading, recorded in `NOTES.md` under "Choices".
- **Do not edit anything in `v1.4_wip/`.** List what you would change.

RULES
- **Propose your plan first** in your `NOTES.md` and wait for the owner's go-ahead before writing code.
- The switch that keeps v1.36's runs unchanged stays, and they stay byte-identical with it off.
- Gold and the packetizer (`updater_wip/pipeline/gold/`) are not changed. Frozen folders are never edited.
- The firmware is built with the toolchain v1.36 pins (xPack `riscv-none-elf-gcc` 15.2.0-1); the runner does not
  have it, so the images you build are committed with their hashes.
- **Branch `updater-w4`**; merge `main` in often (never rebase); the integrator merges into `main`. Your notes,
  questions and report are in `updater_wip/planning/sessions/WORKER4/`.
- Exactness is the requirement: a difference in a check is a bug, never a tolerance.
- Long runs go to the runner (the Quest repository; `PIPELINE.md` there); `updater_wip/tasks/v137-check/` is the
  task to reuse. **Quest numbers:** pull Quest and re-read `NUMBERS.md` immediately before claiming, and push the
  claim before writing the task; on a rejected push, pull and take the next free number.
- Attach the repository to the session to push, or clone with the token from your user preferences. **Never write
  a token into a file.** Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, and end each turn with a clean `git status`.
- Keep replies to the owner short.

NOT YOUR WORK: the Updater's black box and its real controller; the cache; the link's sending side, "stop" and
recovery; gold and the packetizer; the specification.

DELIVERABLES
- `v1.37_wip/`: Ingress's black box and the firmware with the threshold, their tests, the images and hashes, and
  the running guide and `CHANGES-v1.37.md` brought up to date.
- `WORKER4/NOTES.md` with the plan and the "Choices" list.
- `WORKER4/REPORT.md`: what changed; each check's result; the before-and-after figures and the faster-link runs;
  and what you would change in `INGRESS-INTERFACE.md` and `HOST-LINK.md`, as lists for the owner.
