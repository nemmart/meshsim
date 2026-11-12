# Overview: what we are building, and where it stands

*2026-11-12; confirmed by the owner. Two pages by design; read it before `machine-spec.md`. Sources:
**[spec]** v1.3's spec and decisions, modelled in meshsim; **[plan]** the analytical studies in
`jolt-asic-planning`, not measured; **[owner]** the owner's statements of 2026-11-12, first written down here.*

## 1. The goal

An ASIC in TSMC 12 nm that proves a 2²⁹-row execution of the **Jolt zkVM** in **under 100 ms** [owner]. The
reference is a16z's Jolt and jolt-cpp, Snarkify's GPU implementation [spec 4.19]. **Proofs are pipelined**: the
next one is traced while the previous one is proved, so 100 ms is the interval each stage keeps, not the latency
end to end [owner; plan].

**It should be retargetable** [owner]. Proving moves fast and another proof system may become the better target,
so the stack is a **platform** (tiles, mesh, control, memory, a plug-in slot for peripherals) with the
Jolt-specific units plugged in (section 4).

## 2. The system

- **A host** (x86) runs the guest and cuts the run into **128 segments of 2²² rows, one per chip**. Per proof it
  sends **one byte stream, the same for every chip**: per segment the processor state, the advice and the memory
  words the segment wrote (2.6 and 12.5 MB of updates, compressed, on the two reference workloads) [spec;
  DECISIONS-v1.3 15, 16].
- **The network** [owner]: card FPGAs (a cheap part) and LVDS, **8 pairs each way per chip, about 1 GB/s**. Chips
  can be daisy-chained; direct connections are preferred as the FPGA's pins allow. The host's side (a switch, or
  a large FPGA of its own) is open.
- **128 chips**, a 1 GHz target, each with one LPDDR package (about 17 GB/s). A chip is **64 tiles on an 8 × 8 mesh** and
  three controllers, which are tile instances attached to tiles 0–2 [spec 2]:

| unit | what it is for |
|---|---|
| tile (× 64) | an RV32 controller, 4 MB of big SRAM, engines that stream data in and out of it, a router, up to three peripherals; peripheral 1 is the **replayer** |
| C&C 1 | sequences the phases (READY/GO), boots the chip, owns the link to the outside |
| C&C 2 | the file system over LPDDR; applies the host's memory updates |
| C&C 3 | the **Tracer** |
| mesh, request bus | 256-bit link words between tiles; a separate small tree, with credits, from tiles to the controllers |

## 3. One proof, on one chip

1. **Memory.** C&C 2 applies the segments' updates in order: the chip's start image, as files in LPDDR [spec 7].
2. **Trace.** The Tracer executes its segment once, as 64 windows of 2¹⁶ rows, and writes each window's start
   state and compressed **run log**. About 21 M cycles in meshsim, on an optimistic memory model [spec 4.21].
3. **Replay.** Each tile holds one window's run log and **regenerates its 65,536 rows on demand**, in 0.22 ms a
   pass [spec 4.19, measured]. The passes a proof needs depend on the sumcheck and the round; about 45 [plan].
4. **Sumchecks** [plan; not designed]. Seven stages, each a batch of sumchecks over a 128-bit field. A tile folds
   its own rows, then the 6 tile bits bind over the mesh, then the 7 chip bits across chips. **Chip-wide sorts**
   serve the checks that need address order, RAM read-write above all [owner]; sorting 2²² records takes
   1.23 M cycles [spec 14, measured].
5. **Across chips** [plan; a goal]. Per round, coefficients go out, summed on the way, and a challenge comes
   back, about 328 times a proof. No bulk data crosses between chips; to be confirmed once Akita is designed.
6. **Commitment and opening (Akita), and the proof's way out:** not designed.
7. **The check.** The host verifies every proof; one that fails is run again [owner].

**Why replay:** a segment's rows are 80 bytes each, 335 MB, against 256 MB of tile SRAM; streamed from LPDDR they
would cost about 0.9 s a proof [derived]. The run log is about 21 MB per chip. **Why trace on the chip:** the host sends
one small stream to all chips instead of a different 21 MB log to each.

**The budget** [plan]: the sumcheck stages take about 82 ms at one field multiply per tile per cycle and 23 ms at
four, nearly all of it arithmetic; commitment and opening are not counted. Tracing (21 ms) and replay (10 ms) fit.

## 4. Platform, and what plugs into it

**Platform:** tiles, mesh, C&C 1 and the phase cycle, the request bus, the file system, the host link, and **the
peripheral slot** (a region, a standard register window, events, an optional DMA; spec 4.17). **Jolt-specific:**
the replayer, the Tracer and their tables; later the field engine and Akita. Two things are on the wrong side of
that line today: a peripheral has **no direct port into big SRAM**, which a compute engine needs; and the sort's
own parts (GATHER, augmenting, the histogram tables) sit in the tile's core.

## 5. Status

- **Settled and accepted in v1.3** [spec]: the mesh, the tile's engines, the sort, control and errors (every
  error is terminal; the outside world resets the chip), the request bus, the peripheral slot, the replayer and
  the Tracer (both accepted on every window of two workloads at 2²⁹).
- **Two mesh regimes** [owner and Tony]. **Strict rotation** [spec], for sorting: a schedule says who sends when,
  nothing is backpressured, a full router queue is an error. **Elastic** [proposed; coming to the RTL, not in the
  spec or meshsim], for traffic into the file system: a per-tile flag masks that error, and a full queue
  backpressures across the mesh to the address generator, never to the controller. The flag changes only between
  phases with the mesh empty; a tile finishes its file traffic before it reports done.
- **Accepted trade-offs** [owner]: the waits that order GO, arming and the next command are software constants,
  tuned at bring-up; no ECC, since verification catches a corrupted proof.
- **Expected to change:** big SRAM's geometry and ports (today 8 areas, one 128-bit access each per cycle; the
  plan wants 32 bytes a cycle per multiplier). It waits on the PDK and the field engine.
- **Not designed:** the field engine and the sumchecks' schedule on the chip; Akita; the path for each round's
  coefficients and challenges off the chip; the file system's internals; landing traced windows in LPDDR, which
  pipelining needs; the host's side; telling a one-off upset from a failing tile.

## 6. Who decides what, and the simulator

The owner works on the architecture, the algorithms and the high-level simulation; Tony on the RTL and the
low-level design [owner]. **The spec and meshsim are the high-level design; the RTL owns what is buildable and
the exact timing.** The RTL may diverge from them here and there, hopefully by little; the two must agree on
results and on behaviour at the level of this page (DECISIONS D30). **v1.3 runs in RTL today:** it matches
meshsim's function, with timing within 5% (Tony, reported by the owner, 2026-11-12). There is no PDK yet, so
macro shapes and physical latencies are guesses.

**meshsim** is the cycle-level simulator every change is checked on. v1.3's is Java; **v1.35 is its rewrite in
C++**, checked unit by unit against v1.3: the full-size sort about 18× faster on six threads, a traced segment
about 6.5× [Quest 157, 171]. The point is shorter iterations, and trying a new peripheral or a new target before
any RTL exists.
