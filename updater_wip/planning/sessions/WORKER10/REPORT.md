# WORKER10 report: the wire between processes

Status, 2026-11-17: **built and checked here** (2 cores); the runner's campaign is ready, not yet run (section 6).
Branch `updater-w10`. The code is `updater_wip/wire/` (its `README.md` says how to run it, with the example loop);
the plan, the region's layout, the sync's argument and the Choices are in `NOTES.md` beside this file.

## 1. What is built

| | |
|---|---|
| **the library** (`wire.h`, `wire.cpp`) | `Wire` (a process's view) and `LinkEnd` (`exchange(out, in)` once a cycle per end). A byte given in cycle *t* arrives in *t* + *W*; window 0 receives zeros; *W* (1 to 2²⁰), *B* (1 to 64 bytes a cycle) and the slots *K* (2 to 64) are the region's parameters |
| **the waiting** | spin 20 µs, yield, then a futex sleep in 100 ms ticks; no spin when there are fewer CPUs than processes |
| **the arguments** (`wire_args.h`) | `--clients`, `--client k` / `--driver`, `--link`, `--wire-fd`, optional `--window`, `--lane-bytes`; taken out of `argv` in one call |
| **the launcher** (`wire-launch`) | an anonymous region (memfd) inherited by the children; one driver command and one client command (`{k}`, `{p}`); a line per process at the end; stops the rest on a death, a failure, an exit without finishing, a stall (watchdog) or a signal; children die with it; `--debug WHO` starts one process under gdb (or another debugger) with the terminal |
| **the stand-in ends** (`standin.cpp`, `wire-node`) | forward after 1 to 300 cycles, own bursts chosen from a hash of everything received and when, silences up to 3 million cycles, about 20% busy; a digest of every byte at every end in every cycle |
| **the reference** (`wire-ref`) | the same stand-ins in one loop joined by plain delay lines; shares nothing of the wire |
| **`wire-threads`** | the library with the processes as threads, for the thread sanitizer |
| **the checks** (`tests/`) | `case.sh`, `matrix.sh`, `stopping.sh`, `plants.sh`, `threads.sh`, `bench.sh`, `run_checks.sh` (all of them), `make_task.sh` (the Quest task) |

## 2. The checks, here

On this session's machine: 2 cores (a VM; Xeon at 2.3 GHz), gcc 13.3 and clang 18.1. Every case below compares
the processes' digests with the reference's at every end; one difference fails it.

| check | result | time here |
|---|---|---|
| **the matrix, plain:** clients 1, 2, 4, 16 × windows 16, 64, 256, 1,024 × bytes 1, 2, 4, 8 × slots 2, 4; a million cycles; each case its own seed, the processes finishing at different cycles | **128 of 128 equal** | 49 s |
| **the matrix, pinned to one core** (`taskset -c 0`: 17 processes on one core for a chain of 16) | **128 of 128** | 57 s |
| **the matrix, paused:** random real-time pauses between cycles (300 per million cycles, up to 300 µs) and at the library's own steps (3,000 per million, up to 200 µs: before and after publishing, after waiting) | **128 of 128** | 108 s |
| **the matrix, loaded:** a busy loop on every core beside it | **128 of 128** | 20 min (the 16-client cases at window 16: 76 to 85 s each, from 0.8 s unloaded) |
| the matrix with clang, plain | **128 of 128** | 46 s |
| **a hundred million cycles:** clients 1, 2, 4, 16 × windows 16, 64, 1,024 × bytes 1, 8 | **24 of 24 equal** | 14 min (the 16-client cases 47 to 130 s each) |
| short runs: 0, 1, 15, 16, 17 and 50 cycles at window 16; finishing anywhere from cycle 1 (`--wide`) | all equal | |
| **stopping** (`stopping.sh`: 3 rounds with gcc, 1 with clang, 1 under asan; 35 checks a round): each of driver, first, middle and last client killed with SIGKILL at a random moment; one stopped with SIGSTOP; one stuck in a loop; one exiting 0 without finishing; one exiting 5; the launcher killed with SIGKILL; the launcher interrupted; two runs with the same name at once; the driver under gdb through the launcher | **all pass**: nothing hangs, the status names the process and what happened, every process is gone, nothing in `/dev/shm`; a death stops the run in about 0.06 s; a stall in `--stall` + `--grace` (2 + 2 s here) | 17 to 30 s a round |
| stopping under asan and ubsan, and with clang | all pass | |
| **planted bugs,** each in a build of its own, gcc and clang (`plants.sh`) | **5 of 5 caught as a difference from the reference**: 1 (a window published before its bytes are written) on the 2nd run; 2 (a read without waiting), 3 (a window one cycle out), 4 (an index that wraps wrongly), 5 (a byte a window early) each on the 1st | 30 s |
| **the thread sanitizer** on the library in threads (`wire-threads`), gcc and clang | no report on the real library; the reference's digests equal; **the four planted races (1, 2, 4, 5) each reported** (3 is not a race) | seconds |
| **asan and ubsan** on the processes: cases at 1 to 16 clients, with pauses, and the stopping tests | clean | |
| gcc and clang, `-Wall -Wextra -Werror`, every variant | build | |
| the reference under gcc and clang | the same digests (the stand-in has its own generator) | |

**In place of a thread sanitizer across processes** (it does not see them): (1) the same library, the same code,
with the processes as threads, under TSan, which sees every access between ends; (2) the check build poisons
each slot when it takes it back, so a read before the bytes are written shows as a difference, not as stale bytes
that might match; (3) pauses at the library's own steps, so that a race is hit, not hoped for (plant 1 is caught on
the 2nd run with them). A note on why (1) matters: on x86 a release store and an acquire load are plain moves, so a
wrong ordering in the source passes on the hardware; only the compiler, or TSan, would show it.

## 3. The cost

`tests/bench.sh`, release build, the best of 3. "No wire" is as many processes running the same loop at once, each
on a loop-back of the same delay (so that both see the same machine). The rate is the slowest process's.

**Two processes (the driver and one client), on the 2 cores here:**

| Mcycles a second | W = 16 | W = 64 | W = 256 | W = 1,024 |
|---|---|---|---|---|
| ends that do nothing: no wire / wire | 244 / 56 | 289 / 141 | 281 / 216 | 306 / 242 |
| **ends doing a chip's work (500 ns a cycle): no wire / wire** | 1.783 / 1.479 (**+20.6%**) | 1.778 / 1.751 (**+1.5%**) | 1.753 / 1.740 (**+0.7%**) | 1.783 / 1.791 (**0%**) |

- **The sync costs about 0.4 µs a window** (the null ends: a few cache lines change cores: `sent`, `consumed`, the
  data), plus about 1.5 ns a byte for the data itself. At 64 cycles of a chip (32 µs) that is about 1%.
- **The owner's window of 64 costs about 1.5% here.** **1,024** costs nothing measurable. **16 costs about 20%,**
  and not because of the sync's own cost: the delay is one window, so the two processes can never be more than
  about a window apart, and every hiccup of one longer than a window (9 µs at 16) stops the other. This VM has
  hiccups of over 9 µs about 1,400 times a second per core (3.3% of the time; measured). The waits (`# p ... waits`
  in `wire-node`'s output) are a third of the loss; the rest is each process's own hiccups passed on to its neighbour.
  The more slots do not help (16 slots: +18%): the data's delay, not the slots, ties them together.
- With fewer cores than processes it still works, slower: 3 processes on these 2 cores, window 64, +44%; 17 on one
  core, a million cycles of the stand-ins, about a second.
- **These are a 2-core VM's numbers.** The runner's are the ones to quote: `run_checks.sh`'s `bench` step.

## 4. Settled, and why

**A long silence.** The cost of a window does not depend on what is on the link: the sync is the same whether the
bytes are zeros or not. At window 64 and a chip's speed it is about 1.5%, through tens of millions of silent cycles
as through busy ones. **So a sync every 64 cycles is cheap enough, and no promise of silence is needed for
v1.37,** whose machine steps every cycle while the host link is on. It would matter if the chip ever skipped idle
cycles with the link on (v1.3's idle-skipping is about 18×): then each window still costs a sync, and the skipping
would be capped at a window at a time. The way to it, if wanted later: `LinkEnd::quiet_until(t)` publishing "zeros
until cycle *t*" (a `final_w` that is not final), which the far end treats as published windows of zeros. Not built.

**Who waits for whom.** **A process waits only for its two neighbours,** never for all: for the window before
(the data's delay), and for a slot to be free (*K* − 1 windows of slack). **Nothing can wait for ever:** the
process at the smallest window can always go on (NOTES section 4), a finished neighbour is never waited for, a
dead one is reported by the launcher within 10 ms (the others see it within one 100 ms tick), and one that stops
without exiting is caught by the watchdog. Neighbours are at most *K* − 1 windows apart; the chain's two ends at
most *N*(*K* − 1).

**The same every time.** Each byte a process reads comes from a window its neighbour has finished; when it is
read is up to the machine, what it is is not. The matrix under load, pinned, and with pauses at every step of the
library agrees: no difference in any run.

## 5. What the chip's side and the driver must do

**Both** (`README.md` has the loop):

- Parse with `wire_parse_args` before their own arguments; attach with `wire_attach`.
- **Call `exchange` on every end they have, once each cycle, every cycle, in order, from cycle 0;** a cycle's
  outputs are final when given. The input is the far end's of cycle *t* − *W*.
- **Call `finish()` when they stop.** From then on their neighbours read zeros and do not wait. A process that
  exits without it fails the run.
- Their exit status is their verdict: anything but 0 fails the run and the launcher says which.

**The chip (WORKER11's side, then the integrator's):** the IEU black box's link ends are where the two
`exchange` calls go: the up end's byte in is what Ingress receives; the byte out is what Egress sends. v1.37's
machine already steps every cycle with the host link on; the wire needs nothing else of it. With *B* > 1, the link
of 2, 4 or 8 bytes a cycle is `--lane-bytes` (the region's, set by the launcher).

**The driver:** process 0, with one end (down). Two things follow from the wire:

- **"Fails when a reply does not come" must be counted in cycles, not in seconds,** so that the verdict is the
  same every run. Any wall-clock limit belongs to the launcher (`--stall`), not to the driver's logic.
- It finishes when it has checked every reply (or decided one is missing), and exits 0 or not.

## 6. The runner

**The task:** `updater_wip/wire/tests/make_task.sh NNN-wire-check > NNN-wire-check.sh` (from a clean tree on
this branch), which embeds `updater_wip/wire/` and runs `tests/run_checks.sh`: build (gcc, clang, every variant),
the matrix in the four modes with `REPEATS` (5) repeats of each case under gcc and once under clang, the long runs,
stopping (`ROUNDS`, 5), the plants, the sanitizers, the cost for 1 and 2 clients. It works at whatever core count
the runner has, and writes `SUMMARY.txt` with each step's time and the core count. Tried end to end here (the plants
step: 58 s).

**This session could not attach the Quest repository** (refused), so **the integrator is asked to queue one task:**
`make_task.sh NNN-wire-check` with every step. Estimated from the times here, on 2 cores: about 2½ hours, of which the
loaded matrix is about 1½ hours (its 16-client cases: 17 processes and a busy loop a core, on 2 cores); on 17 cores
or more, well under an hour. If the runner has few cores and time matters: `REPEATS=2`.

**Cores:** everything passes on any number, down to one. The cost is only meaningful with a core for each process
(2 for `bench-1`, 3 for `bench-2`); the 16-client cases want 17 to be quick.

## 7. For `MILESTONE-v1.37.md`, for the owner

1. **"The links are shared memory: a ring of bytes for each direction of each link, and each process's cycle
   number"** → a ring of *K* windows for each direction, and for each direction two counters, windows published and
   windows read (not a cycle number per process): the data and the slot both need their own.
2. **The two † on the wire are built as proposed:** the delay is the window, and every run is the same to the
   cycle; `--link NAME` names the run. The † on `--link` can go; add **`--driver`** (owner) and the launcher's
   **`--wire-fd`** (the anonymous region, owner).
3. **Add the launcher to "The picture":** `wire-launch --clients 2 -- DRIVER ... -- ASIC ...`; it makes the
   region, starts the three, reports each one's status, stops the rest on a failure, and can start one under gdb.
4. **The driver, item 4:** "fails when a reply does not come": **in cycles,** so the verdict is the same each run.
5. **Open, the runner's cores:** "three processes that wait on each other want three" → they work on fewer
   (measured: 3 on 2 cores at window 64, 44% slower; 17 on one core, correct); three for the time to be that of
   one chip.
6. **The window:** 64 costs about 1.5% here with a chip's work; 16 about 20% (the processes are held within a
   window of each other, so every hiccup of one stops the other); 1,024 nothing measurable. The runner's figures
   to replace these.
7. **The check "the same to the cycle":** it holds by construction, and is checked with stand-in ends under load,
   on one core and with pauses (`updater_wip/wire/`); with the chips it is then a check of the chips' own
   determinism.
8. **Step 7 (the integrator):** the chips' and the driver's loops call `exchange` every cycle and `finish` at the
   end (section 5); nothing else of the wire reaches them.
