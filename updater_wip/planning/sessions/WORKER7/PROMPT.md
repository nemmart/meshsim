# Worker 7 (updater_wip): tuning the master tile's firmware

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for proving
the Jolt zkVM. **v1.37 runs** (`v1.37_wip/`, `RUNNING-v1.37.md`): the host's data arrives over a link as packets;
the master tile's firmware (`fw/cc/master.c`) reads the frames out of Ingress and feeds the Updater, which builds
the memory images the Tracer runs on. Ingress and the Updater are software black boxes behind their real registers.

**Where it stands.** The firmware tells Ingress how many words to gather before waking it (the threshold), and
moves up to 256 words, 4 KB, a transfer. That cut its cost by three. It is still not tuned: a transfer costs 2.3
to 3 wakes (the flag, the transfer's landing, a credit every other one) and about 360 instructions. With the link
at a byte a cycle the master's core is awake for 0.17 to 0.20 of the link's time.

**The owner's target: feeding the Updater is at most 10% of the master tile's load,** so that the core has slack
for the command stream, which will come on the foreground stream. **Your part is to get there by tuning the
firmware,** and to say exactly what it took and what is left.

READ FIRST: the top-level `README.md`; `updater_wip/README.md`; in `v1.4_wip/planning/`:
**`INGRESS-INTERFACE.md`** and **`UPDATER-INTERFACE.md`** in full, and `DESIGN-NOTES-2026-11-16.md` sections 14 to
16; **`updater_wip/planning/sessions/WORKER4-REPORT.md`** and `WORKER4-NOTES.md` (the figures, the wakes a frame,
the choices C1 to C14), and `WORKER3-REPORT.md` (Stage 2); in `v1.37_wip/`: `RUNNING-v1.37.md`, `CHANGES-v1.37.md`,
`fw/README.md`, `fw/cc/master.c`, `fw/include/host.h`, `fw/port/`, `spec/machine-spec.md` 5 to 9 and 17.1 to 17.4
(events, `WFI`, counters, a peripheral's DMA), `tests/test_master.cpp`, the master's teeth script, `tests/proof_*`
and `tests/proof_numbers.py`.

THE TARGET, EXACTLY
- **The measure:** the master's cycles awake over the link's time, up to the hand-over to the tracer tile, with
  the link at a byte a cycle and the threshold at 256. `proof_numbers.py` prints it.
- **The target: 0.10 or less** on both workloads at 2²⁹ (EVM Zone and Cyclone, every *s* WORKER4 ran) and on the
  three recorded segments. That is about 53 cycles awake a packet's worth, 420 for each 4 KB.
- **Report also:** the same on the matrix of small inputs; the wakes and the instructions a transfer; the link rate
  at which the firmware is at 10%, and the rate it could keep up with at all.

THE WORK
1. **Measure first.** Where the cycles awake go, by kind of wake and by frame: the instructions a wake, what each
   is for, and what the figures' own counters cost. Put the table in your plan.
2. **Fewer wakes a transfer.** Aim at one in the steady state. Places to look, none of them an instruction:
   - the landing and `dma_done`: whether the next flag can come before the transfer has landed, and so whether
     either needs a wake of its own, or can be found done at the next one;
   - the credit: taken when the core is awake anyway, and waited for only when the allowance runs out;
   - what `EVENTS_MASK` selects in each state.
3. **Cheaper wakes.** The path from the wake to the transfer's start; what is recomputed that could be kept; the
   compiler's options as `fw/build.sh` has them.
4. **The frames that are not the Updater's** (the records, the advice, the headers) by the same measure.
5. **If tuning does not reach 0.10,** say how far it got and why, and what each of these would give, by
   measurement where you can and by argument where you cannot. Do not build them:
   - a larger credit and queue (16 KB and 512 words);
   - a transfer that discards its words (WORKER4's suggestion, for frames 7 and 8);
   - Ingress feeding the Updater under credits kept in hardware, the firmware waking once a frame.

HOW IT IS CHECKED
- **Nothing the firmware builds changes:** `test_master`, `proof-real`, `proof-matrix` and `proof-29`, every image
  equal to gold's, `PAGE_COUNT` 0 at every `done`, no error bit. The thresholds 32, 64 and 128 and the link at 2, 4
  and 8 bytes a cycle, as WORKER4 ran them.
- **Every planted bug of the master is still caught,** and each shortcut you take gets a planted bug of its own:
  a wake dropped must not become a transfer started too soon, a credit spent twice, or a frame's tail left in
  Ingress.
- **The races:** every place where the firmware now relies on an order of events (a landing before a flag, a
  credit before a transfer) is listed in your notes with why the order holds, and tested with the timing varied.
- `./run_tests.sh all --check` and `make check`, as before.

RULES
- **Propose your plan first** in `NOTES.md`, with the measurements of step 1, and wait for the owner's go-ahead
  before changing the firmware.
- **You change the master tile's firmware and its tests, and nothing else.** Ingress's and the Updater's black
  boxes and their interfaces stay as they are; if an interface is in the way, it is a finding for step 5, with the
  figure it would give. Do not edit anything in `v1.4_wip/`.
- **WORKER5 and WORKER6 are working in `v1.37_wip/`** on the cache's and the Updater's controllers. Neither changes
  `fw/cc/master.c`. Merge `main` in often (never rebase).
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

DELIVERABLES
- `v1.37_wip/`: the tuned firmware, its image and hash, its tests and planted bugs; the running guide and
  `CHANGES-v1.37.md` brought up to date.
- `WORKER7/NOTES.md`: the plan, the measurements, the orders the firmware relies on, the "Choices" list.
- `WORKER7/REPORT.md`: the figures before and after, by workload; whether the target is met; what each change
  gave; step 5's findings; and what you would change in `INGRESS-INTERFACE.md` and `UPDATER-INTERFACE.md`.
