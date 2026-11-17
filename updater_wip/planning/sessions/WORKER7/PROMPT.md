# Worker 7 (updater_wip): the forward count in v1.37

(This replaces WORKER7's first brief, tuning the master tile's firmware, which was withdrawn before it started: the
owner's design of 2026-11-16 takes the firmware out of the data's path instead.)

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for proving
the Jolt zkVM. **v1.37 runs** (`v1.37_wip/`, `RUNNING-v1.37.md`): the host's data arrives over a link as packets;
the master tile's firmware (`fw/cc/master.c`) reads the frames out of Ingress and feeds the Updater, which builds
the memory images the Tracer runs on. Ingress and the Updater are software black boxes behind their real registers.

**Where it stands.** The firmware moves every word of the bulk data itself: it tells Ingress how many words to
gather (a threshold), and sends them by Ingress's DMA into the Updater's 8 KB segment queue, under credits it
keeps. The master's core is awake for 0.17 to 0.20 of the link's time. The owner wants feeding the Updater to be at
most 10% of the master tile's load.

**The owner's design, which you build:** a register in Ingress, `DMA_FORWARD_COUNT`. While it is not 0, Ingress
itself moves a word a cycle from its background queue to the Updater over a flow-controlled port, or discards it.
The firmware writes a frame's length once and wakes once a frame. The thresholds, the segment queue and its
credits go.

READ FIRST: the top-level `README.md`; `updater_wip/README.md`; in `v1.4_wip/planning/`:
**`INGRESS-INTERFACE.md`** and **`UPDATER-INTERFACE.md`** in full (both rewritten for this design; each has an "In
v1.37" table of what is merged against what you build), `HOST-LINK.md` ("Holding the host back"),
`HOST-FRAMES.md`, and `DESIGN-NOTES-2026-11-16.md` sections 14 to 17; **`v1.37_wip/src/units/word_port.h`** (the
port, agreed); `updater_wip/planning/sessions/WORKER4-REPORT.md` and `WORKER4-NOTES.md` (what you are replacing,
its figures and its choices) and `WORKER3-REPORT.md`; in `v1.37_wip/`: `RUNNING-v1.37.md`, `CHANGES-v1.37.md`,
`src/units/ingress_egress.h`, `host_link.h`, `updater.h` and `updater.cpp`, `fw/README.md`, `fw/cc/master.c`,
`fw/include/host.h`, `spec/machine-spec.md` 5 to 9 and 17.1 to 17.4 (events, `WFI`, counters, a peripheral's DMA),
`tests/test_ingress_link.cpp`, `tests/test_updater.cpp`, `tests/test_master.cpp`, the master's teeth script,
`tests/proof_*` and `tests/proof_numbers.py`.

**The names.** The notes use v1.35's names in places: C&C 1, 2 and 3 are the master, memory and tracer tiles.

THE WORK, in `v1.37_wip/`
1. **Ingress's black box,** to `INGRESS-INTERFACE.md`:
   - the queues at 4 KB a stream, 256 words;
   - the thresholds and their words gone;
   - `DMA_FORWARD_COUNT` at word 82: the count in [30:0], `discard` in [31]; it waits on an empty queue; a store
     replaces it and a store of 0 stops it; a load gives what remains;
   - the source side of the word port (`word_port.h`), in its cycle convention;
   - the rule for a service flag as the document has it;
   - the undefined cases stop the simulator, as the DMA's do today: a DMA transfer from the background queue with
     the count not 0, a store of a count with such a transfer in flight, a DMA count beyond the words in the queue.
2. **The black box Updater's input:** the sink side of the word port. Its segment queue, region part 1,
   `segment_credit` and the segment queue's part of `queue_overrun` go. What it keeps behind the port is a few
   words; say how many and why. `ready` does not depend on `busy`. Nothing else about the black box changes.
3. **The wiring in the chip:** Ingress's source to the Updater's sink, in a way that does not care which Updater is
   behind the port. WORKER6 is building the Updater's real controller to the same port.
4. **The master tile's firmware:** a frame's header word by the DMA; for update words, the Updater's run set up and
   the frame's length written to `DMA_FORWARD_COUNT`; the headers by the DMA, copying what is in the queue;
   frames 7 and 8 by discarding up to its own record or log, copying that, and discarding the rest. The allowance,
   the thresholds, the credits for data and the scrap area go.
   - The memory segments frame feeds three runs. Say how the count and the three `START`s are ordered, and why
     nothing is lost or taken twice between runs.
5. **The figures,** with `proof_numbers.py`: the master's cycles awake over the link's time, up to the hand-over to
   the tracer tile; the wakes a frame; the same with the link at 2, 4 and 8 bytes a cycle; and where the limit is
   now. Set them beside WORKER4's.

HOW IT IS CHECKED
- **Nothing the chip builds changes:** `test_master`, `proof-real`, `proof-matrix` and `proof-29`, every image
  equal to gold's, `PAGE_COUNT` 0 at every `done`, no error bit; the link at 1, 2, 4 and 8 bytes a cycle.
- **The black boxes on their own:** `test_ingress_link` and `test_updater` brought to the new interface. For
  Ingress: the count written before, during and after the words arrive; a count across many packets; send and
  discard; a store of 0 mid-frame; the Updater's `ready` coming and going at random; forwarding while a DMA
  transfer runs from the foreground queue (a test file with both streams); a full queue holding the link; every
  line of the service rule, with a case that would post a flag too many and one that would post one too few.
- **Every planted bug of the master is still caught,** the ones about the allowance and the credits replaced by
  ones about the count: a frame's length one short and one long, send for discard, the wrong record or log kept,
  a count written while the last is still running.
- **The orders the firmware relies on** are listed in your notes with why each holds (the count against `START`,
  a run's `done` against the next frame's flag, a copy's landing against the next discard), and tested with the
  timing varied.
- `./run_tests.sh all --check` and `make check`, as before.

WHERE THE DOCUMENTS ARE OPEN
- A dagger (†) marks what is proposed and not ruled on. Take each as written, unless it cannot work; then say so in
  `QUESTIONS.md` with the evidence, take the nearest thing that does, and carry on.
- What they do not say and you must settle: the simplest reading, in `NOTES.md` under "Choices", with the reason
  and the alternative.
- **Do not edit anything in `v1.4_wip/`,** nor `word_port.h`. List what you would change.

RULES
- **Propose your plan first** in `NOTES.md` and wait for the owner's go-ahead before writing code.
- **WORKER5 and WORKER6 are working in `v1.37_wip/`** on the cache's and the Updater's controllers, each beside its
  black box and behind a setting. You do not touch their units; they do not touch Ingress, the black box Updater's
  input or `fw/cc/master.c`. Merge `main` in often (never rebase).
- Gold and the packetizer are not changed.
- The firmware is built with the toolchain v1.36 pins (xPack `riscv-none-elf-gcc` 15.2.0-1); the runner does not
  have it, so the images you build are committed with their hashes.
- **Branch `updater-w7`.** Your notes, questions and report are in `updater_wip/planning/sessions/WORKER7/`. The
  integrator merges into `main`. Frozen folders are never edited.
- Exactness is the requirement: a difference in a check is a bug, never a tolerance.
- Long runs go to the runner (the Quest repository; `PIPELINE.md` there); `updater_wip/tasks/v137-check/` is the
  task to reuse. **Quest numbers:** pull Quest and re-read `NUMBERS.md` immediately before claiming, and push the
  claim before writing the task; on a rejected push, pull and take the next free number.
- Attach the repository to the session to push, or clone with the token from your user preferences. **Never write
  a token into a file.** Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, and end each turn with a clean `git status`.
- Keep replies to the owner short.

NOT YOUR WORK: the Updater's real controller and its shim (WORKER6); the cache's controller, the AXI arbiter and
the LPDDR model (WORKER5); the link's way back to the host; the specification.

DELIVERABLES
- `v1.37_wip/`: Ingress's black box, the Updater black box's input, the wiring, the firmware with its image and
  hash, the tests and planted bugs; the running guide and `CHANGES-v1.37.md` brought up to date.
- `WORKER7/NOTES.md`: the plan, the orders the firmware relies on, the "Choices" list.
- `WORKER7/REPORT.md`: what was built; each check's result; the figures beside WORKER4's, and whether the owner's
  10% is met; and what you would change in `INGRESS-INTERFACE.md`, `UPDATER-INTERFACE.md` and `word_port.h`, as
  lists for the owner.
