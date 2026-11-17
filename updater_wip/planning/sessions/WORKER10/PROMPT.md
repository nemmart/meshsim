# Worker 10 (updater_wip): the wire between processes

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for proving
the Jolt zkVM. The ASICs sit on a daisy chain from an FPGA host. The next milestone
(`updater_wip/planning/MILESTONE-v1.37.md`) runs that chain in simulation:

```
  DRIVER  ◄──────►  ASIC0  ◄──────►  ASIC1
```

**Each box is a process of its own** (owner): the driver, and one whole-chip simulation for each ASIC, so that
two chips run on two cores in about the time of one. **The links between them are shared memory, with a sync every
64 cycles** (owner).

**Your part is that wire, and nothing above it:** the shared memory, the sync, the arguments, and starting and
stopping the processes. What travels on it is bytes; you do not need to know what they mean. The chips and the
driver are joined to it later by others.

**It is a study of its own:** no chip, none of `v1.37_wip/`. Everything is in a new folder, `updater_wip/wire/`.

READ FIRST: the top-level `README.md`; `updater_wip/README.md`; **`updater_wip/planning/MILESTONE-v1.37.md`**
("The picture", "The driver"); `v1.4_wip/planning/HOST-LINK.md`, "The link" and "The chain" only (8 lines, a byte
a transfer; an idle line is zero bytes; every link of the chain is a link of its own);
`v1.4_wip/planning/DESIGN-NOTES-2026-11-16.md`, section 22.

WHAT THE OWNER DECIDED
- **Separate processes, joined by shared memory,** not pipes or sockets.
- **A sync every 64 cycles.**
- **The arguments:** `--clients 2 --client 0`, or `1`: how many ASICs are on the chain, and which this process is.
- Proposed by the design session, yours to confirm or better: `--link <name>` names the shared region, so that two
  runs on one machine do not meet; the driver is told `--clients` too.

WHAT THE WIRE IS
- **A link is two directions, each a byte a cycle,** always: an idle direction carries zeros. With *N* clients
  there are *N* links: the driver to client 0, and client *k* to client *k* + 1.
- **A byte sent in cycle *t* arrives in cycle *t* + *D*,** where *D* is the wire's delay. *D* is the window, 64
  by default: a process simulates a window, and what it sent in that window is what its neighbours receive in the
  next.
- **So a run is the same to the cycle every time,** whatever the machine is doing: which process runs first,
  how many cores there are, how long any of them is held up. **This is the requirement the rest hangs on.**
- **The window and the bytes a cycle are parameters** (64 and 1 by default; v1.37's runs also model links of 2, 4
  and 8 bytes a cycle).

WHAT TO BUILD, in `updater_wip/wire/`
1. **The wire, as a small library** with a plain C++ interface that a simulator's loop calls once a cycle for each
   link end: give this cycle's byte, take this cycle's byte. It hides the windows and the waiting. Say what the
   shared region holds, who writes each part, and what memory ordering each access uses and why.
2. **Waiting that behaves:** a process that is ahead spins briefly, then sleeps. It must work with fewer cores
   than processes, down to one.
3. **Starting and stopping:**
   - a launcher that makes the region, starts the driver and the clients from the commands it is given, waits
     for them, and reports each one's exit status;
   - **a process that finishes says so,** and its neighbours stop waiting for it and read zeros from then on;
   - **a process that dies must not hang the rest:** the others give up with a clear message, the launcher stops
     what is left (by PID, never by name) and removes the region;
   - a region left behind by a crashed run must not be picked up by the next.
4. **The arguments,** parsed in one place that the chip's program and the driver can both use.
5. **Stand-in ends, for testing:** small programs that send and check bytes, with traffic that depends on what
   they receive and when (a node that forwards what comes from one side to the other with a delay, and mixes in
   bytes of its own, is the chain's shape). They are test pieces, not the driver or the chip.
6. **A reference in one process:** the same stand-in ends, all stepped by one loop with the same delay. **The
   processes must give exactly what the reference gives:** every byte, in every cycle, at every end.

HOW IT IS CHECKED
- **Against the reference:** chains of 1, 2, 4 and 16 clients; short runs and runs of 100 million cycles; windows
  of 16, 64, 256 and 1,024; 1, 2, 4 and 8 bytes a cycle.
- **The same every time:** each case many times over, with the machine loaded, with the processes pinned to one
  core, and with random real-time pauses put into each process to shake the order of events. The results do not
  change.
- **Stopping:** every process finishing at a different time; one killed at a random moment; one that stops
  taking part without exiting. Nothing hangs, the status says what happened, no region is left behind.
- **Planted bugs,** each caught by the reference: a window published before its bytes are written; a read
  without waiting; a window one cycle out; an index that wraps wrongly; a byte delivered a window early.
- **The cost:** cycles a second with stand-in ends that do nothing, at each window, against the same ends with
  no wire; and with ends that do about a chip's work a cycle (v1.37 runs some two million cycles a second). The
  owner's window is 64: say what it costs, and what 16 and 1,024 would.
- Builds with gcc and clang, `-Wall -Wextra -Werror`, and passes with the address and undefined-behaviour
  sanitizers. Say what you did in place of a thread sanitizer, which does not see across processes.

THINGS TO SETTLE AND SAY
- **A long silence:** a chip traces for tens of millions of cycles with nothing on the link. Is a sync every 64
  cycles through all of it cheap enough, or should a process be able to promise silence ahead? Measure first.
- **Who waits for whom:** a process waits only for its neighbours, or for all? Say which, and why nothing can
  wait for ever.
- **How the chip's simulator will use it:** v1.37's machine steps one cycle at a time in one thread when its
  host link is on. Give a short example of a loop that uses the library, and say what the chip's side must
  promise (it calls every cycle; it never looks at a byte early).
- What the document does not say and you must settle: the simplest reading, in `NOTES.md` under "Choices", with
  the reason and the alternative.

RULES
- **Propose your plan first** in `NOTES.md` and wait for the owner's go-ahead before writing code: the region's
  layout, the sync and why it cannot deadlock, the waiting, starting and stopping, the stand-in ends and the
  reference, the checks, how long they take.
- **Branch `updater-w10`.** Your notes, questions and report are in `updater_wip/planning/sessions/WORKER10/`; the
  code in `updater_wip/wire/`, with its own makefile and a `README.md` that says how to run it. You touch nothing
  else. The integrator merges into `main`.
- C++20, as the simulator: no exceptions, no RTTI. Linux only.
- Exactness is the requirement: a difference from the reference once in a million runs is a bug.
- Long campaigns go to the runner (the Quest repository; `PIPELINE.md` there). If your session cannot attach
  Quest, do not work around it: say which runs to queue, and the integrator queues them. Say how many cores your
  checks want; the runner's count is not known to the design session.
- Attach the repository to the session to push, or clone with the token from your user preferences. **Never write
  a token into a file.** Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, and end each turn with a clean `git status`.
- Keep replies to the owner short.

NOT YOUR WORK: the link's protocol (WORKER8, in `updater_wip/link/`); the driver that reads gold's data and checks
the replies; the chip's side (WORKER9 has `v1.37_wip/`); the frames.

DELIVERABLES
- `updater_wip/wire/`: the library, the launcher, the arguments, the stand-in ends, the reference, the tests and
  planted bugs, a `README.md` with the example loop.
- `WORKER10/NOTES.md`: the plan, the region's layout, the sync's argument, the "Choices" list.
- `WORKER10/REPORT.md`: each check's result; the cost at each window; what the chip's side and the driver must do
  to use it; and what you would change in `MILESTONE-v1.37.md`, as a list for the owner.
