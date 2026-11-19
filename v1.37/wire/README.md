# The wire between processes

WORKER10, for the v1.37 milestone (`updater_wip/planning/MILESTONE-v1.37.md`): the driver and each ASIC are
processes of their own, and the links between them are shared memory with a sync every window. This folder is the
wire and nothing above it: the shared memory, the sync, the arguments, starting and stopping. What travels on it is
bytes. The plan and the reasoning are in `updater_wip/planning/sessions/WORKER10/NOTES.md`; the results in
`REPORT.md` beside it.

```
  DRIVER  ◄──────►  ASIC0  ◄──────►  ASIC1   ...
 process 0      process 1        process 2
        link 0           link 1
```

**A link is two directions, each B bytes a cycle,** always (an idle direction carries zeros). **A byte given in
cycle *t* arrives in cycle *t* + *W*,** *W* being the window (64 by default): a process simulates a window, and what
it sent in it is what its neighbours receive in the next. **So a run is the same to the cycle every time,**
whatever the machine does.

## Building

```
make                      # gcc, the check build: build/g++/check/
make CXX=clang++          # clang: build/clang++/check/
make VARIANT=release      # for the cost; also asan, tsan, plant1..plant5
make all-variants         # all of them
```

C++20, no exceptions, no RTTI, `-Wall -Wextra -Werror`. Linux only (memfd, futex).

| program | what |
|---|---|
| `wire-launch` | makes the region, starts the driver and the clients, waits, reports each one's exit status |
| `wire-node` | a stand-in end (test traffic), as a process on the wire |
| `wire-ref` | the reference: every stand-in in one loop, joined by plain delay lines |
| `wire-threads` | the library with the processes as threads, for the thread sanitizer |

## Using it from a simulator

```cpp
#include "wire.h"
#include "wire_args.h"

int main(int argc, char** argv) {
    wire::WireArgs wa;
    char err[256];
    if (!wire::wire_parse_args(&argc, argv, &wa, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    // ... the program's own arguments: what is left in argv ...
    wire::Wire w;
    if (wa.present && !wire::wire_attach(&w, wa, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    wire::LinkEnd* up = w.up();      // toward the driver: null for the driver
    wire::LinkEnd* down = w.down();  // away from it: null for the last client

    for (uint64_t t = 0; !done; ++t) {
        uint8_t to_up = link_out_up(), to_down = link_out_down();  // this cycle's bytes, final
        uint8_t from_up = up ? up->exchange(to_up) : 0;            // what the far end gave in t - W
        uint8_t from_down = down ? down->exchange(to_down) : 0;
        machine_cycle(from_up, from_down);
    }
    w.finish();  // the neighbours read zeros from here on, and do not wait for this process
}
```

With *B* > 1, `exchange(const uint8_t* out, uint8_t* in)` moves *B* bytes each way.

**What the chip's side must promise:**

- **It calls `exchange` on every end it has, once each cycle, every cycle, in order, from cycle 0.** A cycle
  skipped on one end and not the other shifts that end's time: the byte would arrive in the wrong cycle.
- **A cycle's outputs are final when given,** and **it does not look at a cycle's input before that cycle**
  (there is no way to: `exchange` gives this cycle's input only).
- **It calls `finish()` when it stops** (the destructor does too). A process that exits without finishing is
  treated as failed, and the run is stopped.
- It may take as long as it likes over a cycle: the waiting is the library's. Only `exchange` at a window's first
  cycle can wait.

`exchange` is cheap in the middle of a window (a copy of *B* bytes each way); at a window's last cycle it publishes
the window, at its first it waits for the neighbours' last window if they are behind.

### The arguments (parsed in one place: `wire_args.h`)

| | |
|---|---|
| `--clients N` | how many ASICs are on the chain |
| `--client k` / `--driver` | which this process is; exactly one of them |
| `--link NAME` | the run's name (checked against the region's) |
| `--wire-fd FD` | the region, inherited from the launcher |
| `--window W`, `--lane-bytes B` | optional: checked against the region's |

The launcher adds all of these; a program does not normally write them. `wire_parse_args` takes them out of `argv`.

## Running a chain

```
wire-launch --clients 2 [--window 64] [--lane-bytes 1] [--slots 4] [--link NAME] [--stall 300] [--grace 2] \
    -- ./driver ARGS... \
    -- ./asic ARGS... --log asic{k}.log
```

- The client command is run once for each client; in any argument `{k}` becomes the client number (`driver` for
  the driver) and `{p}` the process number (0 the driver, *k* + 1 client *k*).
- The region is **anonymous** (a memfd) and inherited by the children: nothing is ever in `/dev/shm`, and nothing
  is left behind whatever happens, even with the launcher killed. Two runs on one machine never meet, whatever
  their names. The children are killed if the launcher dies.
- At the end it prints a line for each process (PID; exit status or signal; whether it gave up, or was stopped by
  the launcher) and one for the run. Its own status is 0 only if every process finished and exited 0.

**When something goes wrong:** a process that dies, exits with a status other than 0, or exits without finishing
stops the run: the others see it within 100 ms, print one line (`wire: client 0 (pid 812) giving up at cycle
66048: client 1 (pid 813) died of signal 9`) and exit with status 3; after `--grace` seconds the launcher stops
what is left by PID (SIGTERM, then SIGKILL). **A process that stops taking part without exiting** (stuck, or
stopped): if no process has moved for `--stall` seconds (300 by default; 0 for never), the launcher names the
slowest one and stops the run the same way. Raise `--stall` if a process spends longer than that before its first
cycle (loading a large input).

### Under a debugger

```
wire-launch --clients 2 --debug 1 -- ./driver ... -- ./asic ...
```

starts client 1 (or `--debug driver`) as `gdb --args ./asic ... --clients 2 --client 1 --link ... --wire-fd ...`,
with the terminal. The other processes get `/dev/null` as input and a process group of their own, so ^C goes to
gdb alone; the launcher ignores ^C; the stall watchdog is off (give `--stall` to have it). The others simply wait
while the process is stopped at a breakpoint. Another debugger: `--debugger "lldb --"`, or anything that takes the
command after it (`--debugger "valgrind"`, `--debugger "strace -f -o trace.txt"`). The status the launcher reports
for that process is the debugger's (gdb exits 0 whatever its program did); its state on the wire (finished or
not) is still the program's own.

## The checks

| script | what | here (2 cores) |
|---|---|---|
| `tests/case.sh BIN N W B K SEED CYCLES [REPEATS] [-- opts]` | one case against the reference | |
| `tests/matrix.sh BIN CYCLES REPEATS MODE [CLIENTS]` | clients 1, 2, 4, 16 × windows 16, 64, 256, 1,024 × bytes 1, 2, 4, 8 × slots 2, 4; MODE plain, loaded, pinned (one core), paused (random pauses), or all | see REPORT.md |
| `tests/stopping.sh BIN [ROUNDS]` | killed, stopped, stuck, early exits, the launcher killed or interrupted, two runs with one name, under gdb | about 30 s a round |
| `tests/plants.sh [CXX]` | the five planted bugs, each must be caught | |
| `tests/threads.sh BIN` | `wire-threads` (and under TSan) against the reference | |
| `tests/bench.sh BIN [CLIENTS]` | the cost | |
| `tests/run_checks.sh` | all of it, with gcc and clang, the sanitizers: the runner's task | |

`case.sh` takes `TASKSET="taskset -c 0"` (every process on one core), `WIRE_PAUSE=ppm:max_us` (pauses at the
library's own steps, check builds only), `NODE_ONLY="--pause ppm:max_us"` (pauses between cycles), and
`LAUNCH_ARGS`.

The checks pass, only slower, with fewer cores than processes, down to one: a process that has fewer CPUs than
there are processes does not spin, it yields and then sleeps.

## Files

| file | what |
|---|---|
| `wire.h`, `wire.cpp` | the library: the region, `Wire`, `LinkEnd` |
| `wire_args.h`, `.cpp` | the arguments |
| `launch.cpp` | `wire-launch` |
| `standin.h`, `.cpp` | the stand-in end's traffic and digests |
| `node_loop.h`, `.cpp` | the stand-in's loop on the wire (the example above, with test options) |
| `node_main.cpp`, `ref_main.cpp`, `threads_main.cpp`, `opts.h` | the test programs |
| `tests/` | the scripts |
