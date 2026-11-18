# The v1.37 milestone: two ASICs on a chain, each proving its segment

The owner's plan of 2026-11-17, written up by the design session, and brought up to date on 2026-11-18 with what
has been built since: the link's protocol (WORKER8) and the wire between processes (WORKER10). The work is done
in this project (`updater_wip`), the code in `v1.37_wip/`, and what comes out is frozen as v1.37 (owner).

A dagger (†) marks what the design session proposes and the owner has not ruled on. (owner) marks what the owner
said. The design itself is in `v1.4_wip/planning/`: `HOST-LINK.md`, `INGRESS-INTERFACE.md`,
`UPDATER-INTERFACE.md`, `HOST-FRAMES.md`, `AXI-INTERFACE.md`, `CACHE-INTERFACE.md`.

## What v1.37 is (owner)

| part | in v1.37 |
|---|---|
| the cache's controller, the Updater's controller | **full simulation of the state machines** |
| AXI: the arbiter and LPDDR | a model |
| Ingress/Egress and the UART (IEU) | a black box behind its real registers |
| the master tile | **its real firmware** |
| the memory tile | nothing runs on it; no RW engine |
| at the end of a segment | **the master writes a frame out,** with data that shows the segment ran |
| the daisy chain | **DRIVER ↔ ASIC0 ↔ ASIC1.** ASIC0 runs segment 0 and ASIC1 segment 1 |

## The picture

```
            packets                      packets
  DRIVER  ─────────────►   ASIC0   ─────────────►   ASIC1
 (a process) ◄───────────  (a process) ◄───────────  (a process)
            replies                      replies
```

- **Three processes** (owner): the driver and one for each ASIC. Each ASIC is the whole chip's simulation, so the
  two run on two cores in about the time of one.
- **The links are shared memory** (owner). **Built** (WORKER10, `updater_wip/wire/`): for each direction of each
  link a ring of windows, and two counters, the windows published and the windows read.
- **A sync every 64 cycles** (owner). The wire's delay is that window: a byte given in cycle *t* arrives in
  cycle *t* + 64. Every run is the same to the cycle, however the machine schedules the processes.
  - **A process waits only for its two neighbours,** never for all, and nothing can wait for ever: a finished
    neighbour is not waited for, and a dead or stuck one stops the run.
  - **What the sync costs** (WORKER10, on a 2-core machine, each end doing a chip's work of about 500 ns a
    cycle): about 1.5% at a window of 64; nothing measurable at 1,024; about 20% at 16, where every hiccup of one
    process longer than a window stops its neighbour.
  - **No promise of silence is needed.** A chip traces for tens of millions of cycles with nothing on the link,
    and the sync costs the same through them as through busy ones.
- **The launcher** (WORKER10): `wire-launch --clients 2 -- DRIVER ... -- ASIC ...`. It makes the shared region,
  starts the three, reports each one's status, stops the rest when one dies, fails, stalls or exits without
  finishing, and can start one of them under a debugger.
- **The arguments** (owner): `--clients 2 --client 0`, or `1`; `--driver` for the driver (owner). The launcher
  passes the region to its children as `--wire-fd`; `--link <name>` names a run where the processes are started
  by hand, so that two runs on one machine do not meet.
  - Client 0's neighbour up the chain is the driver. Client *k* has a link down the chain only if
    *k* < `--clients` − 1.
  - † *s* is not the client number: on the real system a chain holds about 4 ASICs of 128. Here it defaults to
    it.
- **What a process does with the wire:** it calls `exchange` on each of its link ends once every cycle from
  cycle 0, giving the byte it sends and taking the byte that arrives, and `finish` when it stops. Nothing else
  of the wire reaches the chip or the driver. A process's exit status is its verdict.

## The driver (owner)

1. **Reads gold's data and sends the packets** to ASIC0, at the link's rate.
   - † It takes gold's frame file and lets WORKER8's host end make the packets, each frame starting a new
     packet. Gold's packet file is from before the final protocol (numbers for each stream, no mask) until step
     2c, and a link end would refuse it.
2. **Obeys ASIC0's stop bit.** Today the black box makes the file wait when a queue is full; with a driver the
   hold-back travels in packets coming back, as `HOST-LINK.md` has it.
3. **Collects what comes back:** checks each packet, sorts by hops, and rebuilds each ASIC's reply.
4. **Checks each reply against gold,** and fails on a difference or when a reply does not come. "Does not come"
   is counted in cycles, not in seconds, so that the verdict is the same every run; any limit in real time is
   the launcher's.

† In C++ beside the simulator, so that it shares the link's code. It needs only the two files gold already writes,
the packet file and the frame file.

## The reply frame †

The owner: "something that shows that it ran." Proposed: **the Tracer's end-state record, with a few counts.** The
record at the end of segment *s* must equal gold's record *s* + 1, which the driver has in frame 7. So the driver
checks it exactly, and a pass shows the whole path: the images built, the segment traced, the reply returned.

| word | holds |
|---|---|
| 0 | the frame's header word: a new type on the foreground stream, 16 †; length 72 |
| 1 | *s*; a status code; the rows traced |
| 2 | the cycle at the segment's end; `PAGE_COUNT` as the last run left it |
| 3 | the error words: the master tile's, Ingress/Egress's, the Updater's; the tracer tile's status |
| 4 to 71 | the end-state record, 68 words |

- **72 words is three packets,** so the two ASICs' replies interleave on the way back and the driver has to sort
  them. Hops 0 is ASIC0's, hops 1 is ASIC1's.
- **How it is sent** (owner): the master builds the frame in its own memory, stores its length in `EGRESS_COUNT`,
  and sends the words into the outgoing queue as the credits allow.
- † The tracer tile sends its last record to the master tile with its result; the master does not reach into the
  tracer tile.
- It goes into `HOST-FRAMES.md` when the fields are agreed.

## What the IEU black box does, and what it leaves out

| | in this milestone |
|---|---|
| the header as `HOST-LINK.md` now has it: one run of numbers for a link, both stop bits, the restart flag, hops; the data masked | yes |
| a link up the chain and a link down, each with its own numbers | yes |
| forwarding: check, write the link's fields, add one to hops, seal again | yes, in both directions |
| a forwarded packet before the ASIC's own | yes |
| "stop" for each link, carried in packets, the driver included | yes |
| Egress: the outgoing queue, `EGRESS_COUNT`, the credit | yes |
| acknowledgements, sending again, "lost" and recovery, the restart | **from WORKER8's link end** (`updater_wip/link/hostlink.h`). The protocol has been shown to hold (`HOST-LINK.md`, "Where this document stands"). The milestone's own runs damage no packet |
| v1.36's stream words taken out; the UART as redesigned | † not needed for the chain. Before the freeze, or after: the owner's call (Open) |

## The order of the work

| step | what | who |
|---|---|---|
| 1 | **Merge `updater-w6`.** Done 2026-11-17: Quests 216 to 219 pass | the integrator |
| 2 | **The header change** (18-bit numbers, hops) in gold's packetizer and reader, the vectors and `host_link.h`. Done, and merged 2026-11-17 with step 3's first piece | WORKER7 |
| 2c | **The final protocol in gold and in v1.37's header:** one run of packet numbers for a link (owner, 2026-11-17; the header stays 16 bytes: both stop bits move to byte 7, restart takes the old stop bit's place), **the mask on the data, and the restart packet,** in gold's packetizer and reader, the vectors and v1.37's link header. WORKER8 has confirmed the header and the mask, with worked examples in `HOST-LINK.md` to check against. To be done when `v1.37_wip/` is free of WORKER9, or in gold alone before | not assigned |
| 2b | **The protocol on its own** (owner, 2026-11-17: "if this has some hidden flaw, and we go out to hardware with it, we'll be absolutely miserable"): a link end as a state machine, chains of up to 16, wires that misbehave, consumers that stall for up to 1 ms, random runs and an exhaustive search of a shrunken model. No chip. Its link end becomes the black box's link part. **Done** (2026-11-18): the protocol was broken several times and mended, and now holds; the link end has an interface to build on and its hardware is counted. Merged 2026-11-18, with counters, the cause of an error and the reset in a running chain added after the specification's review; the last of its long runs on the runner are still to come | **WORKER8** (`planning/sessions/WORKER8/`) |
| 3 | **Join the controllers.** (The first piece is done and merged, 2026-11-17: Quests 222 to 226.) **Its first piece is merging `updater-w7`,** which is not a plain merge: WORKER7 took the segment queue out of the black box, and WORKER6's bench and view feed the black box through it (`tests/ctl_bench.h`, `tests/updater_view.h`; conflicts in `proof_bench.cpp` and `proof_run_main.cpp`). So: merge it; take the shim out; feed both Updaters by the word port; give `UpdaterTop` the two sink methods the slot wires. Then the Updater's controller on the arbiter and the real cache, and the proof runs with both controllers on | **WORKER9** (`planning/sessions/WORKER9/PROMPT.md`) |
| 4 | **The design for steps 5 and 6:** the reply frame in `HOST-FRAMES.md`; the two briefs. (The header for the wire between two link ends is no longer wanted: WORKER8's `hostlink.h` and WORKER10's `wire.h` are the two interfaces, a byte a cycle each way.) | the design session |
| 5 | **The chain in the IEU black box, Egress, and the reply:** two link ends; forwarding; the outgoing queue; the master's firmware sending the reply frame; tested as two black boxes wired back to back, with no chips. Built on WORKER8's link end | † WORKER11 |
| 6a | **The wire between processes:** the shared memory with its 64-cycle sync, `--clients`, `--client` and `--link`, the launcher; tested with stand-in ends against a reference in one process. No chip, no protocol. **Built** (2026-11-17) and merged 2026-11-18: every check passes on its own machine (the same to the cycle in 128 cases plain, pinned to one core, paused and under load; stopping; planted bugs; the thread sanitizer). No campaign on the runner (owner) | **WORKER10** (`planning/sessions/WORKER10/`) |
| 6b | **The driver:** reads gold's packet file, speaks the link's protocol with WORKER8's host end, sorts the replies by hops and checks them against gold | † with step 5 or after it |
| 7 | **Put 5 and 6 together:** DRIVER ↔ ASIC0 ↔ ASIC1 on the real chips; the runner's task. The black box's two link ends are where the chip's two `exchange` calls go: the upper end's byte in is what Ingress receives, its byte out what Egress sends | the integrator |
| 8 | **Freeze v1.37:** the design documents brought into its specification; the two golds made one | the integrator, with the owner |

- **Steps 2, 2b, 3 and 6a can run side by side,** and so can 5 and 6b. Step 5 waits for 2, which is in the same black
  box, and for 2b, whose link end it uses.
- **Where it stands (2026-11-18):** 1, 2, 2b and 6a are done and merged, with the first piece of 3. Still to do: the rest of 3 (the Updater's controller on the arbiter and the real cache),
  2c, 4, 5, 6b, 7 and 8. Step 6b needs only what
  is built: WORKER8's host end, WORKER10's wire, and gold's files. It can be written and tested against stand-in
  ASICs made of WORKER8's `Node` before the chip's side exists.
- **Why 5 and 6 are two workers:** they meet at one small interface, a byte a cycle each way, and each can be
  tested alone. The chain's logic is then tested without three processes, and the processes without a chip.
- **The specification comes first** (owner, 2026-11-18): "we should follow the tried and true. We build a spec
  for the full system." Then the simulator is brought to it, then the firmware, then the tests. The
  specification session's brief is `planning/sessions/SPEC/PROMPT.md`. It writes v1.37's `spec/` from the
  design documents, has every † ruled on or made an open item, and lists where the simulator differs.
- **Then one session takes everything that is left of the building:** the rest of 3, then 5, 6b, 7 and 8. A
  brief for it was written before the ruling above (`planning/sessions/INTEGRATOR/PROMPT.md`) and is on hold:
  it is to be rewritten against the specification. The WORKER11 of step 5 is not started.

## How the milestone is checked

| check | |
|---|---|
| the images | each ASIC's, every one equal to gold's, as in the proof runs today |
| the trace | each ASIC's last record equal to record *s* + 1 |
| **the replies** | the driver receives one from each ASIC, whole and in order, sorted by hops, each equal to what gold's files give |
| the same to the cycle | the same run twice gives the same cycles in all three processes. The wire holds this by construction, and is checked on its own with stand-in ends (WORKER10); with the chips it is then a check of the chips' own determinism |
| a chain of one | `--clients 1`: the driver and one ASIC give the single chip's results of today |
| both controllers on | the cache's and the Updater's state machines in every run above |
| planted bugs in the chain | hops not counted; a header not sealed again; an own packet before a forwarded one; a stop bit ignored; a reply's packets out of order: each caught |
| what does not change | with the chain off and the settings off, `./run_tests.sh all --check` and `make check` as before |

On both workloads: the small inputs here, 2²⁹ on the runner.

## Open

- † **The reply frame's fields,** above.
- **Whether v1.36's stream words go, and the UART is redone, inside v1.37 or after it.** The stream words are
  agreed with Tony; the UART is asked of him and not yet answered.
- **Whether the two controllers are on by default** in v1.37 as frozen, the black boxes kept for comparison.
- **A run with two proofs,** so that the Updater building the next proof overlaps the Tracer on the cache (the
  owner: in reality they overlap). Not in this milestone's picture; it belongs with step 3 if it is cheap.
- The runner has 16 cores, so the three processes have one each. On fewer they still work, slower (WORKER10: 3
  processes on 2 cores at a window of 64, 44% slower).
- † **A background packet with data that comes from below,** and the header's CRC while a pass is running
  (`HOST-LINK.md`, Open): WORKER8's two questions, neither in the milestone's way.
