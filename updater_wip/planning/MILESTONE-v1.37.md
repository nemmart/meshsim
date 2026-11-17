# The v1.37 milestone: two ASICs on a chain, each proving its segment

The owner's plan of 2026-11-17, written up by the design session. The work is done in this project
(`updater_wip`), the code in `v1.37_wip/`, and what comes out is frozen as v1.37 (owner).

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
- **The links are shared memory** (owner): a ring of bytes for each direction of each link, and each process's
  cycle number.
- **A sync every 64 cycles** (owner). † The wire's delay is that window: bytes sent in one window arrive in the
  next. Every run is then the same to the cycle.
- **The arguments** (owner): `--clients 2 --client 0`, or `1`. † And `--link <name>`, the shared region's name, so
  that two runs on one machine do not meet.
  - Client 0's neighbour up the chain is the driver. Client *k* has a link down the chain only if
    *k* < `--clients` − 1.
  - † *s* is not the client number: on the real system a chain holds about 4 ASICs of 128. Here it defaults to
    it.

## The driver (owner)

1. **Reads gold's data and sends the packets** to ASIC0, at the link's rate. Gold's packet file already has the
   numbers of the driver's link.
2. **Obeys ASIC0's stop bit.** Today the black box makes the file wait when a queue is full; with a driver the
   hold-back travels in packets coming back, as `HOST-LINK.md` has it.
3. **Collects what comes back:** checks each packet, sorts by hops, and rebuilds each ASIC's reply.
4. **Checks each reply against gold,** and fails on a difference or when a reply does not come.

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
| the header with 18-bit numbers and hops | yes |
| a link up the chain and a link down, each with its own numbers | yes |
| forwarding: check, write the link's fields, add one to hops, seal again | yes, in both directions |
| a forwarded packet before the ASIC's own | yes |
| "stop" for each link, carried in packets, the driver included | yes |
| Egress: the outgoing queue, `EGRESS_COUNT`, the credit | yes |
| acknowledgements, sending again, "lost" and recovery | † **no.** Nothing in the simulation damages a packet. Later, with a test that does |
| v1.36's stream words taken out; the UART as redesigned | † not needed for the chain. Before the freeze, or after: the owner's call (Open) |

## The order of the work

| step | what | who |
|---|---|---|
| 1 | **Read Quests 217 to 219,** then merge `updater-w6` and `updater-w7` | the integrator |
| 2 | **The header change** in gold's packetizer and reader, the vectors and `host_link.h` (the message is written; it had not reached WORKER7) | WORKER7, carried on |
| 3 | **Join the controllers:** the Updater's controller on the arbiter and the real cache, fed by Ingress's forwarder; its shim taken out; the proof runs with both controllers on | † WORKER8 |
| 4 | **The design for steps 5 and 6:** the reply frame in `HOST-FRAMES.md`; a header for the wire between two link ends, as `word_port.h` is for the word port; the two briefs | the design session |
| 5 | **The chain in the IEU black box, Egress, and the reply:** two link ends; forwarding; the outgoing queue; the master's firmware sending the reply frame; tested as two black boxes wired back to back, with no chips | † WORKER9 |
| 6 | **The processes:** the shared-memory wire with its 64-cycle sync, `--clients`, `--client` and `--link`; the driver; tested against a stand-in ASIC that only answers | † WORKER10 |
| 7 | **Put 5 and 6 together:** DRIVER ↔ ASIC0 ↔ ASIC1 on the real chips; the runner's task | the integrator |
| 8 | **Freeze v1.37:** the design documents brought into its specification; the two golds made one | the integrator, with the owner |

- **Steps 2 and 3 can run side by side,** and so can 5 and 6. Step 5 waits for 2: it is in the same black box.
- **Why 5 and 6 are two workers:** they meet at one small interface, a byte a cycle each way, and each can be
  tested alone. The chain's logic is then tested without three processes, and the processes without a chip.
- † The joining was to be the integrator's. A fresh worker is proposed for it: it is a day's coding, and this
  session is long.

## How the milestone is checked

| check | |
|---|---|
| the images | each ASIC's, every one equal to gold's, as in the proof runs today |
| the trace | each ASIC's last record equal to record *s* + 1 |
| **the replies** | the driver receives one from each ASIC, whole and in order, sorted by hops, each equal to what gold's files give |
| the same to the cycle | the same run twice gives the same cycles in all three processes |
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
- How many cores the runner has: three processes that wait on each other want three.
