# Overview: what we are building, and where it stands

*v1.37. Two pages by design; read it before `machine-spec.md`. The text is v1.35's overview (2026-11-12, confirmed
by the owner) in v1.37's terms, with sections 2 to 6 brought up to date. Sources: **[spec]** the specification;
**[plan]** the analytical studies in `jolt-asic-planning`, not measured; **[owner]** the owner's statements of
2026-11-12; **[RTL]** what the RTL (Snarky One) built and measured in simulation. Sizes are binary (KiB, MiB); the
two link rates, in GB/s, are decimal.*

## 1. The goal

An ASIC in TSMC 12 nm that proves a 2²⁹-row execution of the **Jolt zkVM** in **under 100 ms** [owner]. The
reference is a16z's Jolt and jolt-cpp, Snarkify's GPU implementation [spec 17.8]. **Proofs are pipelined**: the
next one is traced while the previous one is proved, so 100 ms is the interval each stage keeps, not the latency
end to end [owner; plan].

**It should be retargetable** [owner]. Proving moves fast and another proof system may become the better target,
so the stack is a **platform** (tiles, the NoC, control, memory, the peripherals' interface) with the
Jolt-specific peripherals plugged in (section 4).

## 2. The system

- **A host** (x86) runs the guest and cuts the run into **128 segments of 2²² rows, one per chip**. Per proof it
  sends **one byte stream, the same for every chip**: per segment the processor state, the advice and the memory
  words the segment wrote (about 2.5 and 12 MiB of updates, compressed, on the two reference workloads) [spec;
  DECISIONS-v1.3 15, 16].
- **The network** [owner]: card FPGAs (a cheap part) and LVDS, **8 lines each way per link, about 1 GB/s**. Chips
  are **daisy-chained,** probably no more than 4 to a chain: every link is a link of its own, with checked link
  packets, sending again, recovery and "stop", on two streams, the background (the next proof's set-up) and the
  foreground (the proof being worked on), neither blocking the other [spec 28]. The host's side (a switch, or a
  large FPGA of its own) is open.
- **The frames** [spec 29]: per program the image, per proof a header, the jump table, the data image, the
  register states, the advice logs and every segment's memory updates, the same for every chip; at its segment's
  end each chip replies with its end state.
- **128 chips**, a 1 GHz target, each with one LPDDR package (about 17 GB/s). A chip is **64 compute tiles on an
  8 × 8 mesh** and three special tiles, every one the same base tile, the special tiles attached to tiles 0, 7
  and 15 [spec 2.1]:

| tile | what it is for |
|---|---|
| compute tile (× 64) | an RV32 core, 4 MiB of scratchpad, units that stream data in and out of it, a router, up to three peripherals; peripheral 1 is the **replayer** |
| master tile | sequences the phases (the barrier: arrive, release), boots the chip, owns the link to the outside (**Ingress/Egress**) and builds the memory images from the host's frames (**the Updater**) |
| memory tile | **the memory system:** the 512 KiB cache shared by the chip, the AXI arbiter and LPDDR's port; v1.36's RW engine; the file system (deferred) |
| tracer tile | the **Tracer** |
| NoC, request tree | 256-bit packets between tiles; a separate small tree, with credits, from tiles to the special tiles |

## 3. One proof, on one chip

1. **Memory.** The master tile's Updater applies the frames' updates, as they arrive, to copy-on-write files in
   LPDDR: the segment's start image, the final image and the Tracer's copy, about 4 M to 17 M cycles on the
   reference workloads, paced by the link [spec 17.10, 23; meas: v1.37's simulator].
2. **Trace.** The Tracer executes its segment once, as 64 windows of 2¹⁶ rows, and writes each window's record
   and its three compressed streams. About 21 M cycles on v1.35's simulator, on an optimistic memory model; 22 M
   to 23 M on the RTL's chip and on v1.36's simulator, on the RTL's tests' model [spec 17.9; RTL].
3. **Replay.** Each compute tile holds one window's record and streams and **regenerates its 65,536 rows on
   demand**, in about 0.2 ms a pass at 1 GHz, 3.1 cycles a row [spec 17.8, measured]. The passes a proof needs
   depend on the sumcheck and the round; about 45 [plan].
4. **Sumchecks** [plan; not designed]. Seven stages, each a batch of sumchecks over a 128-bit field. A tile folds
   its own rows, then the 6 tile bits bind over the mesh, then the 7 chip bits across chips. **Chip-wide sorts**
   serve the checks that need address order, RAM read-write above all [owner]; sorting 2²² records takes
   1.23 M cycles on v1.35's simulator and 1.31 M on the RTL and on v1.36's simulator [spec 27, measured].
5. **Across chips** [plan; a goal]. Per round, coefficients go out, summed on the way, and a challenge comes
   back, about 328 times a proof. No bulk data crosses between chips; to be confirmed once Akita is designed.
6. **Commitment and opening (Akita), and the proof's way out:** not designed.
7. **The check.** The host verifies every proof; one that fails is run again [owner].

**Why replay:** a segment's rows are 80 bytes each, 320 MiB, against 256 MiB of scratchpad; streamed from LPDDR
they would cost about 0.9 s a proof [derived]. The record and streams are about 21 MiB per chip. **Why trace on the
chip:** the host sends one small stream to all chips instead of a different 21 MiB of streams to each.

**The budget** [plan]: the sumcheck stages take about 82 ms at one field multiply per tile per cycle and 23 ms at
four, nearly all of it arithmetic; commitment and opening are not counted. Tracing (about 22 ms) and replay (10 ms)
fit.

## 4. Platform, and what plugs into it

**Platform:** the base tile, the NoC, the master tile and the barrier, the request tree, the memory system, the
Updater, the RW engine and the file system behind them, the host link and its frames' format, and **the peripherals'
interface** (a region, a register window with
the control block, events, errors, an optional DMA; spec 17). **Jolt-specific:** the replayer, the Tracer and
their tables; later the field accelerator and Akita. Two things are on the wrong side of that line today: a
peripheral has **no port of its own into the scratchpad**, which a compute engine needs; and the sort's own parts
(the gather unit, `AUGMENT`, the `HIST` table) sit in the base tile.

## 5. Status

- **v1.36 is the hardware the RTL implements** [spec; RTL]: the base tile and every module of it, the four tile
  kinds, the request tree and the chip top, on which the sort runs end to end, a segment is traced and its windows
  are replayed. A passing simulation is not a working chip: there is no PDK, so macro shapes and physical
  latencies are placeholders.
- **v1.37 specifies the full system around it** [spec]: the host link along a chain of ASICs (28) and its frames
  (29); Ingress/Egress's host side, with the forward count that takes the master tile's core out of the bulk
  data's path, and Egress (17.6); the Updater (17.10); the memory system, the cache and the AXI arbiter (17.11, 23);
  the links between the special tiles (16.1); the master tile's firmware as a contract (30). They are designed
  here and not built in the RTL. **Asked of the RTL:** the UART's redesign, the line between the special tiles, a
  DMA's input on south, the memory tile's two peripherals, IDs on `lpddr` (`RTL-CHANGE-REQUESTS.md`).
- **The simulator** is behind the specification where `FOR-THE-SIMULATOR.md` says: the link's protocol, the chain
  and Egress are built and searched on their own [meas: the model] but not in the chip; the cache's and the
  Updater's controllers are built and work alone, behind settings, and have not met.
- **Changed from v1.36** (the guide, `CHANGES-v1.36-to-v1.37.md`): v1.36's host interface removed; the UART off at
  the reset; the RW engine the memory tile's peripheral 2; the special tiles joined by a line.
- **For the owner to rule on:** v1.36's three reversals of recorded rulings (items 160 to 162), and the open items
  (spec 26), item 181 above all: **what follows an error,** which v1.37 does not design. Every error stops the chip.
- **Not built, and still deferred or open:** the file system; hard and soft reset; the master tile's boot over
  LVDS; the foreground stream's frames; the field accelerator; the consumer of the replayer's rows.
- **Two mesh regimes** [owner and Tony]. **Strict rotation** [spec], for sorting: a schedule says who sends when,
  nothing is back-pressured, a full router queue is an error. **Elastic** [proposed; in neither the spec, the RTL
  nor meshsim], for traffic into the file system: a per-tile flag masks that error, and a full queue
  back-pressures across the NoC to the address generator, never to the core. The line between the special tiles
  is strict too until the file system needs otherwise (item 182).
- **Accepted trade-offs** [owner]: no ECC, since verification catches a corrupted proof; the link's checks cover the
  line, not the memories it passes through.
- **Expected to change:** the scratchpad's geometry and ports. It waits on the PDK and the field accelerator.
- **Not designed:** the field accelerator and the sumchecks' schedule on the chip; Akita; the path for each
  round's coefficients and challenges off the chip; the file system's internals; landing traced windows in LPDDR,
  which pipelining needs; the host's side; telling a one-off upset from a failing tile.

## 6. Who decides what, and the simulator

The owner works on the architecture, the algorithms and the high-level simulation; Tony on the RTL and the
low-level design [owner]. **The spec and meshsim are the high-level design; the RTL owns what is buildable and
the exact timing.** What the RTL work turns up comes back into the next version: v1.36 was that step, taken
whole; v1.37 is the other direction, the design of what the RTL has still to build, specified first and then
brought into the simulator, the firmware and the tests [owner]. **The RTL is the truth about the hardware:** where the specification and the RTL differ, the RTL wins and
the specification is corrected (`GUIDELINES.md`, section 1). v1.36's specification is the RTL's, and its
simulator gives the RTL's results and its cycle counts (`CHANGES-v1.35-to-v1.36.md`, the timing table).

**meshsim** is the cycle-level simulator every change is checked on. v1.36's is C++ (`src/`): v1.35's, brought
stage by stage to the specification and, where that is silent, to the RTL's sources. The measured figures in the
spec are the RTL's, which the simulator reproduces, or an earlier version's, each marked. The point of the
simulator stays: shorter iterations, and trying a new peripheral or a new target before any RTL exists.
