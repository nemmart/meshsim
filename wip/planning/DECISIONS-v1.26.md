# DECISIONS-v1.26: v1.25 plus the replayer changes designed for v1.3

**v1.26 is v1.25 with exactly the replayer changes decided for v1.3** (owner, 2026-11-01), in the spec, meshsim and the
software, and nothing else: no Tracer, no new memory system, no file system. The changes come from `DECISIONS-v1.3.md` sections
12, 23 and 24 (cited as V3-12, V3-23, V3-24). The work is done in `wip/` and frozen as `v1.26/` like every version.

## 1. The changes (owner, 2026-11-01)

1. **The state record grows to 68 words, in a new order** (V3-23; owner, 2026-11-01): **[0] the state word, [1] the stream word,
   [2-65] the register image, [66] the files word** (the memory file's and the map file's roots and depths, carried and ignored),
   **[67] the template bitmap** (moved from word 0 to last). The whole record lands in part 0 at consecutive offsets from 0x000,
   the first three where v1.25 has them; one record format for the host, the Tracer and the replayer.
2. **A stream's size is two numbers: its 128-bit words and its values** (V3-24; owner, 2026-11-01: bits aren't useful to anyone):
   **the words** as sent, padding included (what the controller sends and the queue holds), and **the values out** (the 32- or
   64-bit values it decodes to, which is where a decoder stops, so padding never matters). The stream word (word 1; v1.25: three
   bit lengths, code [23:0], read log [55:32], write log [87:64]) carries both for all three streams, **and each stream's
   compression flag** (owner, 2026-11-01), moved from the state word's [112]-[114], since it describes the stream: 16 + 17 + 1
   bits per stream, 102 bits for the three; its layout is to propose (a stream is at most about 33,800 words and 65,536 values).
3. **The read-log and write-log queues shrink from 8 KB to 4 KB** (V3-12), so every queue is 4 KB. Justified by the measured
   densities (spec 4.19.15); **confirm by meshsim's Stage B stall counts on both workloads.**
4. **Writing the state flushes every queue** (V3-24), in place of `PULSE_RUN` emptying them (spec 4.19.8). **The state stays a
   memory**, address-mapped in part 0 as at v1.25, not a queue (owner, 2026-11-01: the same on the Tracer and the replayer; the
   hardware is simplest, one-word updates and readback stay, and the DMA writes consecutive offsets anyway), and **the flushing
   write is the write to word 0, the state word, at part 0 offset 0x000** (WORKER20's proposal, owner): state-first loading writes it
   first, and a per-pass flag change resends only it, so every pass starts with clean queues.
5. **A stream's padding can be anything**, compressed or not (V3-24): streams arrive as whole 128-bit words and the decoder stops
   at the exact length.
6. **The replayer checks its pops against the sizes it was given** (V3-24): reading past a stream's words, or popping past its
   values, is an underflow error at once; reaching done with input left over is an error.
7. **New `ERRORS` bits** (V3-24), after v1.25's [0] reserved, [1] queue overrun, [2] invalid expansion: **[3] code-stream
   underflow, [4] read-log underflow, [5] write-log underflow, [6] done with input left over.**

**Out of scope** (they belong to the Tracer or to v1.3): the peripheral DMA in 128-bit words (spec 4.17.9), the broadcast of
controller-fed templates, everything about files, slots and caches.

## 2. Definition of done

- **The spec** (`wip/spec/machine-spec.md`) states all seven changes, with `CHANGES-v1.26.md` (the delta from v1.25, each item
  citing this file) and the change guide `CHANGES-to-v1.25.md` extended, or a short guide from v1.25 to v1.26 for the RTL.
- **meshsim's replayer model**, its controller firmware (recipe 26, `replay_ctl.c`), the acceptance kit, `ReplayDriver` and the
  synthetic deliveries follow the spec; tests for each change, including each new error bit and the flush.
- **The real deliveries** (WORKER9's 2²⁹ windows of EVM Zone and Cyclone, 67-word records) are converted or regenerated to 68
  words with value counts, and **every window of both workloads at 2²⁹ matches in stages A, B and F**, as v1.25's acceptance did
  (Quest 083, 084), with the 4 KB queues; the stall counts reported.
- The quick and full suites pass; the full-size sort is unchanged (p4/uniform-k128: 1,229,008 cycles).

## Work list

| # | work | worker |
|---|---|---|
| V1 | the spec and the change notes | WORKER19 |
| V2 | meshsim, the firmware, the kit, the deliveries' conversion, the acceptance runs | WORKER20 |
