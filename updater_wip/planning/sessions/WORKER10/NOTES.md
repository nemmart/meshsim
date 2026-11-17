# WORKER10 notes: the plan for the wire between processes

Status, 2026-11-17: **the plan has the owner's go-ahead, as written.** Branch `updater-w10`, from main `f6ea4570`.

**The go-ahead (owner, 2026-11-17, 06:04),** with the answers to section 8:

1. **The anonymous region,** inherited from the launcher. One process must be startable under a debugger through
   the launcher; the README says how.
2. **An explicit `--driver`.**
3. **On the runner:** the checks must pass, only slower, when there are fewer cores than processes; the tasks
   work at whatever the runner has and report how long they took. (The owner is being asked the core count.)

Names: *N* clients, so *N* + 1 processes: P0 is the driver, P*k*+1 is client *k*. Link *i* joins P*i* (up) to
P*i*+1 (down); each link has a **down** direction and an **up** direction. *W* is the window in cycles (64), *B*
the bytes a cycle (1), *K* the window slots in each direction's ring. Window *w* is cycles [*wW*, (*w*+1)*W*).
A dagger (†) marks what I propose where the brief is silent.

**Read:** everything on the brief's list.

## 1. What is built, and where

Everything in `updater_wip/wire/`, nothing else touched.

| file | what |
|---|---|
| `wire.h`, `wire.cpp` | **the library:** the region's layout, `Wire` (one process's view), `LinkEnd` (one end of one link) |
| `wire_args.h`, `.cpp` | the arguments, parsed in one place for the chip, the driver and the stand-ins |
| `wire_wait.h` | the waiting: spin, yield, futex sleep |
| `wire-launch` (`launch.cpp`) | the launcher |
| `standin.h`, `.cpp` | the stand-in node: traffic that depends on what it receives and when |
| `wire-node` (`node_main.cpp`) | a stand-in as a process, on the wire |
| `wire-ref` (`ref_main.cpp`) | the reference: all the stand-ins in one loop, joined by plain delay lines; **shares no code with the library** |
| `wire-threads` | the same library with the processes as threads in one process, for the thread sanitizer (section 9) |
| `wire-bench` | the cost (section 10) |
| `tests/` | `run_checks.sh` and the stopping and planted-bug scripts |
| `Makefile`, `README.md` | gcc and clang, `-Wall -Wextra -Werror`, asan and ubsan targets; how to run; the example loop |

## 2. The interface

```cpp
WireArgs a = wire_parse_args(argc, argv);   // takes out --clients, --client/--driver, --link; leaves the rest
Wire w;  if (!w.attach(a, &err)) { fprintf(stderr, "%s\n", err); exit(2); }
LinkEnd* up   = w.up();    // null for the driver
LinkEnd* down = w.down();  // null for the last client
for (uint64_t t = 0; running; ++t) {
    uint8_t in_up[8], in_dn[8], out_up[8], out_dn[8];
    if (up)   up->exchange(out_up, in_up);   // this cycle's B bytes out, this cycle's B bytes in
    if (down) down->exchange(out_dn, in_dn);
    ...one cycle of the machine...
}
w.finish();                                  // neighbours read zeros from here on
```

- `exchange` gives this cycle's bytes and takes this cycle's: what the far end gave in cycle *t* − *W*. The
  received bytes never depend on this cycle's sent bytes (the delay is at least one window), so the order of the
  two in a call does not matter.
- **What the chip's side must promise:** it calls `exchange` on every end it has, **once each cycle, every
  cycle, in order,** from cycle 0, and the cycle's outputs are final when given. It does not look at a cycle's
  input before that cycle. (Each end counts its own cycles; the debug build checks the promise.)
- *B* = 1 has a plain `uint8_t exchange(uint8_t)`.

## 3. The region

One shared region for the run, made by the launcher. Every counter is on a cache line of its own (128 bytes, for
the adjacent-line prefetcher); **every word has one writer.**

| part | holds | written by |
|---|---|---|
| header | magic, layout version, size; *N*, *W*, *B*, *K*; a 64-bit run id; the launcher's PID; `abort` (who and why) | the launcher; `abort` by the launcher only |
| a line per process | PID; state (absent, attached, running, finished, failed); windows done (for the watchdog); what it is waiting on | that process |
| a direction, producer line | `sent`: windows published; `final`: the window it stopped at (∞ while running); a futex word `bell` and a `sleeping` flag for the consumer | the producer (the consumer writes only `sleeping` on the other line) |
| a direction, consumer line | `consumed`: windows read; `gone`: it reads no more; `bell`, `sleeping` | the consumer |
| a direction, data | *K* slots of *W* × *B* bytes | the producer |

At *N* = 16, *W* = 1,024, *B* = 8, *K* = 4: 32 directions × 32 KB = 1 MB.

**The ordering.** The producer writes window *w*'s bytes into slot *w* mod *K* with plain stores, then
`sent = w + 1` with **release**. The consumer reads `sent` with **acquire** before reading the slot: the bytes
are then visible. When the consumer has finished with window *w* it stores `consumed = w + 1` with **release**;
the producer reads it with **acquire** before writing window *w* + *K* into the same slot, so the consumer's reads
come before the producer's overwrite. `final` and `gone` are release/acquire likewise. The sleep handshake (`sleeping`
then a re-check, against store-then-check-`sleeping`) is the store-load pattern, so both sides use a **seq_cst
fence** there; the futex wait is timed (section 5), so even a lost wake costs at most one tick.

**Where the windows turn** (†): an end writes each cycle's bytes straight into the slot; it **publishes on the last
cycle of a window** (inside that cycle's `exchange`, after storing its bytes), and **waits on the first cycle of
the next.** Since every end's last-cycle call comes before any end's first-cycle call, a process publishes all of
window *w* before it waits for anything of window *w* + 1. The input for window *w* + 1 is read in place from the
neighbour's slot *w*, and `consumed` is published at the end of window *w* + 1.

Window 0 receives zeros (nothing was sent before cycle 0).

## 4. The sync, and why it cannot deadlock

**A process waits only for its neighbours,** never for all: to start window *w* it needs each neighbour's `sent`
≥ *w* (its window *w* − 1), and, to write slot *w* mod *K*, each neighbour's `consumed` ≥ *w* − *K* + 1.

**Deadlock-free:** take the process at the smallest window *m* (lowest index on a tie). Each neighbour is at *m* or
beyond, so it has published windows up to *m* − 1 (`sent` ≥ *m*) and read up to *m* − 2 (`consumed` ≥ *m* − 1 ≥
*m* − *K* + 1 when *K* ≥ 2). So the slowest process can always go on, and the run advances. *K* ≥ 2 is enforced.
Neighbours are at most *K* − 1 windows apart; the chain's two ends at most *N*(*K* − 1).

**A finished neighbour** is never waited for: windows at or past its `final` read as zeros, and its `gone` means
nobody waits for it to consume. **A dead or stuck one** is section 6.

**The same every time:** each byte a process reads is fixed by which window of which neighbour it comes from, and
that window was completed before it was read; the order in which processes run, and their speed, choose only *when*
a read happens, never what it returns.

## 5. The waiting

Spin with `pause` for about 2 µs, then `sched_yield` a few times, then **sleep on a shared futex** with a timeout of
100 ms (the timeout is for checking `abort` and the watchdog, not for correctness). The publisher wakes only if the
other side has set `sleeping`, so a run where nobody falls behind makes no system calls.

**Fewer cores than processes:** a process that has fewer CPUs in its affinity mask than there are processes skips
the spin and yields at once, so one core shared by 17 processes works (slowly). Checked pinned to one core.

## 6. Starting and stopping

**The launcher:**
`wire-launch [--clients N] [--window 64] [--lane-bytes 1] [--slots 4] [--link NAME] [--stall S] -- DRIVER ARGS... -- CLIENT ARGS...`
The client command is used for every client; `{k}` in it becomes the client number (for log names). The launcher
makes the region, appends `--clients N` and `--driver` or `--client k` and `--link ...` to each command, starts them
(fork and exec, no shell), waits for them, and prints a line for each: PID, exit status or signal, and whether the
launcher killed it. Its own status is 0 only if every process exited 0.

**A process that finishes** (`Wire::finish()`): sends zeros to the end of its current window, publishes it, sets
`final` and `gone` and its state. Its neighbours read zeros from then on and never wait for it. That is the same as
if it had gone on sending zeros, so it is still exact.

**A process that dies:** the launcher sees it in `waitpid`, sets `abort` (which process, which signal or status)
and rings every bell. Each process that is waiting sees it within a tick and exits with status 3 and one line:
`wire: client 1 (pid 4242) died of signal 9 at window 1033; client 0 giving up at cycle 66048`. After a grace
time (2 s †) the launcher kills what is left **by the PIDs it holds** (its own unreaped children, so they cannot be
reused), with SIGTERM and then SIGKILL, and removes the region. Children set `PR_SET_PDEATHSIG`, so if the
launcher itself is killed they die too.

**A process that stops taking part without exiting** (a stuck loop, SIGSTOP): the launcher watches every process's
windows done; if none moves for `--stall` seconds (300 †, 0 for off) it names the one that is not waiting on anyone
and is not moving, sets `abort`, and stops them all as above.

**A region left behind** is section 8, question 1.

## 7. The stand-in ends and the reference

**A stand-in node** (`StandIn`): a seeded generator of its own (so gcc and clang give the same run) and a running
state hashed from every byte it receives, with the cycle it came in. Each cycle, in each direction it can send:

- **forwards** what came from the other side *d* cycles ago (*d* its own, 0 to a few hundred), as the chain does;
- **mixes in bytes of its own** from the state, in bursts, so what it sends depends on what it received and when;
- **is silent** for long stretches (zeros), as a chip that traces;
- **finishes** at a cycle of its own, chosen per process, so they stop at different times.

It records a digest of every (cycle, end, byte) received and sent, one per million cycles (so a difference is
found to the million) and, in short runs, the full byte log.

**The reference** (`wire-ref`): the same `StandIn` nodes, stepped by one loop, joined by arrays of *W* × *B* bytes
that delay by *W* cycles, with the finishing rule written out again. It shares the node, not the wire. **The check
is that the processes' digests (and logs) equal the reference's, at every end.**

## 8. The arguments, and questions for the owner

`--clients N` (owner); `--client k` (owner); **† `--driver`** for the driver, which is told `--clients` too
(the alternative, "no `--client` means the driver", lets a chip that forgot its argument pose as the driver);
`--link NAME`. *W*, *B* and *K* are in the region's header, written by the launcher; a process that also gives
`--window` or `--lane-bytes` is checked against it. `wire_parse_args` removes what it takes, so the chip's own
parser sees the rest.

**Question 1, the region (†, yours to choose):**
- **(a) as proposed:** a named region, `/dev/shm/meshsim-wire-NAME`, made by the launcher with `O_EXCL`. A region
  left behind is found by its header: if its launcher PID is gone (checked with its start time, so a reused PID is
  not taken for it), it is removed and made again; if the launcher is alive, the run is refused ("another run is
  using NAME"). The children check the run id the launcher gives them, so they never attach an old region.
- **(b) better, I think:** the launcher makes an **anonymous region (`memfd`)** that the children inherit; nothing
  is ever in `/dev/shm`, nothing can be left behind even if the launcher is killed with SIGKILL, and two runs can
  never meet. `--link` then names the run in messages only. The cost: a process can only join if the launcher
  started it (a debugger is started *by* the launcher: `-- gdb --args ./sim ...`).

**Question 2:** the driver flag, `--driver` (above), or another spelling you prefer.

## 9. In place of a thread sanitizer

- **The same library in threads** (`wire-threads`): the region in ordinary memory, each process a thread; run under
  **TSan**, which sees every acquire and release. This matters because on x86 a release store and an acquire load are
  plain moves, so a wrong ordering in the source would pass on the hardware and only show under a compiler or TSan.
- **Poison:** in the check build the producer fills a slot with a pattern when it takes it back, so a read
  before the bytes are written gives a difference, not stale bytes that happen to match.
- **Pauses at the library's own steps:** in the check build a seeded random pause (0 to 500 µs) can be put
  between each internal step (write, publish, wait, read, release), not only between cycles, so the planted races
  are hit, not hoped for.

## 10. The checks, and how long they take

| check | where | estimate |
|---|---|---|
| against the reference: clients 1, 2, 4, 16 × window 16, 64, 256, 1,024 × bytes 1, 2, 4, 8 (64 cases), short (1M cycles), and *K* = 2 and 4 | here (2 cores) for 1, 2 and 4 clients; the runner for 16 | minutes here |
| the same, 100M cycles: clients 1, 2, 4, 16 × windows 16, 64, 1,024 × bytes 1 and 8 | the runner | about 2 core-hours |
| the same every time: each short case 50 times, with the machine loaded, pinned to one core, with random pauses (cycles and library steps) | the runner (the repeats); here a sample | about 4 core-hours |
| stopping: finishing at different times; one killed at a random moment (each process in turn); one SIGSTOPped; the launcher killed | here | minutes |
| planted bugs (the five of the brief), each must give a difference against the reference | here | minutes |
| gcc and clang, `-Werror`, asan+ubsan, TSan in threads | here | minutes |
| the cost (section 11) | the runner, on a quiet machine; a first look here | an hour |

**Cores:** the 16-client cases are 17 processes; they want 17 cores to measure, and are checked for exactness on
however many there are. Everything else wants 5. I will write the Quest tasks for the runner's part and say which
to queue.

## 11. The cost, and the long silence

`wire-bench`: cycles a second with ends that do nothing, at each window, against the same loop on a plain delay
line; then ends that spend about 500 ns a cycle (a chip at two million cycles a second). My expectation, to be
measured: a window's sync is a few cache-line transfers, about 0.2 to 0.5 µs, which at *W* = 64 and 500 ns a cycle
is about 1% (16: about 4%; 1,024: under 0.1%), provided no one sleeps. **The long silence:** the cost per window
does not change with silence, so at 2M cycles a second the same ~1% holds. Where silence would matter is if the
chip's idle-skipping is ever used with the link on: then a promise of silence ahead (`LinkEnd::quiet_until(t)`)
would let it skip. I will measure first and propose it only if the numbers ask for it.

## 12. The order, each piece committed and pushed

1. The library, the args, the stand-in, the reference, `wire-node`, a two-process launcher: first exact match.
2. The launcher in full, stopping, the region's lifetime.
3. The matrix here, the planted bugs, the sanitizers and `wire-threads`.
4. The cost; the Quest tasks for the runner.
5. `README.md` with the example loop; `REPORT.md`.

## Choices

(To be filled as they are made; those already proposed above are marked †.)
