# WORKER5 report, round 3 (closing)

**Delivered:**
- `wip/spec/machine-spec.md` v1.2, with the decisions log's sections 43 and 44 folded in (9418dd5);
- `wip/spec/CHANGES-v1.2.md`, with a round-3 section;
- `WORKER5/QUESTIONS.md`, with W5-23 to W5-26 marked answered.

All on `main`.

## What changed

- **Mode 5, count only** (D43), in 3.1, 4.2, 4.5, 4.8, 4.12 and invariant 21:
  - It decrements its counter at the destination and writes nothing.
  - Any size is accepted; the payload and address are ignored.
  - From a stream the source is read and the payload discarded.
  - To the tile itself it takes a read token.
  - With counter 0 it does nothing, and meshsim flags it.
  - Modes 6–7 stay reserved, and the counter field counts modes 0–3 and 5. The front matter now lists six modes.
- **The fence** (D43; 3.1). A run that something must follow ends with a count-only packet to the tile itself, on
  a spare counter; when that counter reaches 0, everything earlier in the run has been injected. In a group run
  the fence goes in the stream that ends last. **DMA run done keeps its meaning** (the last table read).
  Count-only packets also carry "done" between tiles.
- **Ordering per sending unit** (D44; 3.2).
  - Packets from one unit in one tile, to one destination, of one kind, arrive in the order that unit sent them.
  - The units are the address generator, the send port, the GATHER engine, the read port and each sending
    peripheral's DMA.
  - Between units there is no ordering: a fence, or putting the packet in the run, imposes it.
  - Round 2's interim note is replaced, and item 136 is closed.
- **The boot** (6.2).
  - Overlays and the first command go by one unit (the command as the run's last stream), or the overlays' run
    ends with a fence that C&C 1 waits for before it sends the command.
  - **C&C 1 lands a tile's first command only when that tile should start at the next GO** (W5-26).
- **"Outstanding" is a software convention** (D44, W5-23; 3.3, 4.12, 6.7). A request is outstanding until its
  reply lands, a read until its data lands. In hardware a request holds its receive-queue slot only until the
  target pops it. Item 135 is closed.
- **The report set** (D44, W5-24; 4.13) is decided as defined: 24 reports. The header's op and sequence-number
  fields come with C&C 1's request format (item 122, now *Deferred*).

## Still *(Proposed)*, *(Open)* or *(Deferred)*

All are in the spec's section 13.

**Proposed** (written into the text; a ruling would close each):
- the table window at 0xFFFF2000 (120);
- the replayer's event and error bit positions (123);
- the handler parking in a plain loop, and `error_latency` = 1 in simulation (133);
- carried from v1.1:
  - `rp_latency` = 2 (48);
  - `stage_delay` = 32;
  - 128-bit staging packets (28);
  - 65 and 66 as C&C 2 and C&C 3, and the request-target numbering (82, 83);
  - the request FIFO's format and stall rule (64);
  - the UART's 16-byte FIFOs;
  - the replayer's v1.1 proposals (35–59).

**Open:**
- **with Tony:**
  - the rest of the CSR list and their reset values, which also settles boot part 2's `mie` bit (121, Q1);
  - the soft reset's keep list (124);
  - one-cycle program-memory and boot-ROM loads, and one-cycle 32 KB memories at 1 GHz (134, Q16).
- **for the owner or meshsim:**
  - whether a request dropped at the root is recorded in hardware (126);
  - K's margin as a meshsim check (128, S1);
  - a stream's source address (129, Q9);
  - trimming the stored reserved link bits (117);
  - whether the protected loopback lets invariant 14 retire (116);
  - signed counters, a candidate for later (125).

**Deferred:**
- the sending peripherals' router ports (118);
- the file system and the RW engine: tag windows, the window protocol, the request layout, target 3 (119);
- the report header with C&C 1's request format (122);
- the 1×4 test chip (130);
- the host-interface words.

## Questions for the owner

**None of WORKER5's.** W5-01 to W5-26 are all answered. What remains is the list above, mostly with Tony or
deferred to the file system and C&C 1's firmware.

**One note for the recipes:** the round-2 check's M3–M8 (stale bit numbers, "region 0", labels still saying
*proposed*, the build comment) are WORKER6's to pick up. Recipe 22 could now use a fence instead of putting the
command in the run; both are valid (D43).
