# v1.36 against the v1.4 design: a first scope

2026-11-16. v1.36 was frozen on 2026-11-15 and merged to `main` (Tony Wu, pull request 1). This note scopes what it
changes for the v1.4 design in this folder, which was written in v1.35's terms on 2026-11-14 and 2026-11-15, before
v1.36 was read.

**A first pass, from v1.36's documents only:** `v1.36/README.md`, `spec/CHANGES-v1.35-to-v1.36.md` (sections 1, 2.4,
2.7, 2.9 to 2.11, 4 and the start of 7), `spec/machine-spec.md` (2.1, the `lpddr`, `fill` and `lvds_in` rows of 3, 11.2,
16.4, 17.1, 17.6's host interface, 17.7, 19 and 23) and `OPEN-v1.36.md`'s first parts. Section 7 lists what was not
read. Nothing in the other documents of this folder has been changed for v1.36 yet.

## 1. What v1.36 is

- **The RTL's hardware, ported back.** The RTL (Snarky One) was built against v1.3 and v1.35 with a specification of
  its own. v1.36 takes that as meshsim's specification, in the RTL's names, numbering and register layouts.
- **The rule: the RTL is the truth about the hardware.** Where the specification and the RTL differ, the RTL wins.
- **Its simulator models the RTL** at the register, the packet and the cycle, and runs the RTL's firmware images.
- **It built part of what v1.35 called the memory system:** the RW engine, the memory tile's `READ` and `WRITE`
  requests, the write buffer, the `lpddr` port, and the Tracer's side of the fill port.
- **It leaves the rest deferred, as v1.35 did:** the file system, the Updater, the Binder, and the translator and
  shared cache behind the fill port (its chapter 23, items 119 and 60). That is this folder's subject.

## 2. Names

The names this folder's documents use, and v1.36's (from the guide's section 7.1).

| in this folder (v1.35) | v1.36 |
|---|---|
| C&C 1, C&C 2, C&C 3 (endpoints 64, 65, 66) | the master, memory and tracer tile (tile ids 64, 65, 66) |
| the controllers | the special tiles |
| a controller (the processor) | the core |
| a tile | a compute tile |
| program memory, data memory | IMEM, DMEM |
| big SRAM | the scratchpad (region 0x03) |
| the fifth port; tiles 0, 1 and 2 | the attach port; the attach tiles, **0, 7 and 15** |
| right, left, down, up (this folder: +1, −1, +8, −8) | east, west, south, north |
| the Updater/Binder/RW peripheral | the RW engine, for the part that is built |
| "the memory system" (C&C 2's peripheral) | what is behind `fill`: the translator and the shared cache |
| the fill bus | the fill port, `fill` |
| the tag windows | parts of the write buffer, the RW engine's region |
| a peripheral's standard words | its control block (block 0) and its DMA block (block 1) |
| `PULSE_RUN`; "run in progress"; "run done" | `START`; `STATUS.busy`; `EVENTS.done` |
| `PULSE_DMA`, `DMA_ADDR` | `DMA_START`, `DMA_DEST_ADDR` |
| the request bus | the request tree |
| the address generator's run | a transfer |
| the network, the mesh's links | the NoC |

## 3. Where v1.36 collides with the v1.4 design

Each needs a ruling before the documents here can be put in v1.36's terms; how binding each is follows the table.

| # | topic | v1.36, as built | this folder | to decide |
|---|---|---|---|---|
| 1 | **the links between the special tiles** | Each special tile reaches the mesh by one attach port, at tiles 0, 7 and 15. There are no links between special tiles. The memory and tracer tiles sit beside each other, off the ends of the mesh's first two rows (2.1, 16.1) | The control row: C&C 1 to C&C 2 to C&C 3 on their ±1 links, always flow-controlled, so the host's stream reaches the Updater without touching the compute mesh and can run during a sort (first note, sections 8 and 9) | **Ruled (owner, 2026-11-16): the links are added,** master tile to memory tile to tracer tile; Tony can add links as desired (`RTL-CHANGE-REQUESTS.md`, 1). The alternatives were a link of the stream's own, or keeping the stream out of sorts. As built, a packet from the master tile to the memory tile crosses two links of the compute mesh (tile 0 to tile 63 to tile 7, by 16.2's routes), which a sort schedules |
| 2 | **Ingress/Egress and the host's stream** | The host interface is two AXI4-Stream ports, each behind a FIFO of 4 beats; a beat is `STREAM_BYTES`, 4 by default, 1 to 16 allowed. The master tile's core reads each beat through registers. Ingress/Egress "does no work of its own and sends nothing": it has no DMA. The beats' meaning is open (17.6, item 166) | Ingress has a queue of about 8 KB, a sender that puts 128-bit words into the Updater's segment queue at up to one every 16 cycles, a credit counter fed by a private signal from the Updater, and a service request to its core (first note, section 8) | The sender, the queue and the credit input are new hardware in Ingress/Egress. `HOST-FRAMES.md` is the beats' meaning; with a 16-byte beat, a beat is one 128-bit word, and TLAST could mark a frame's end |
| 3 | **the RW engine, and the memory tile's peripheral 1** | The RW engine is the memory tile's peripheral 1, type 2: its window's words 32 to 36, its region 0x04 the write buffer (16 KiB by default). One job at a time: a read of LPDDR into a queue its DMA sends, or a write from the write buffer. It is the master of `lpddr` (17.7) | `UPDATER-INTERFACE.md` gives the Updater type 2, words 32 to 49 and region 0x04's parts 1 and 2. The figure has the RW engine as one consumer of the bus driver | The Updater becomes the memory tile's peripheral 2 (region 0x05), with a new type number; the cache perhaps peripheral 3. The bus driver goes between the RW engine's `lpddr` port and the controller |
| 4 | **`lpddr`** | AXI4 with 128-bit data and a byte address of 32 bits by default; one ID, so no ID signals, and reads answered in order; bursts INCR, none crossing 4 KiB; `rready` and `bready` always 1 (3) | The bus driver is the only thing on AXI and serves the cache's lines and the Updater's pages too; a line was to be able to pass a page (second note, section 2) | Whether the port to the controller gains IDs, or the bus driver orders everything itself with one ID and short bursts |
| 5 | **a peripheral's window and its bits** | Control block: `PARAMS` 0, `STATUS` 1, `ERRORS` 2, `EVENTS` 3, `EVENTS_PEEK` 4, `EVENTS_MASK` 5, `START` 6. DMA block: words 16 to 18. The peripheral's own words from 32. The Tracer's `EVENTS` and `ERRORS` are packed from bit 0, which reverses item 144 and awaits the owner (17.1, item 161) | `UPDATER-INTERFACE.md` uses v1.35's words (`PULSE_RUN` at 3, `EVENTS` at 5 to 7) and the bit positions v1.35 shared between units | Mechanical once item 161 is ruled: the control block as v1.36's, and the Updater's event and error bits packed or shared accordingly |
| 6 | **the Tracer's jump cache** | 64 entries of a 32-bit key and a 32-bit base, fully associative (17.9.4) | 128 entries, for consideration only (owner; second note, section 5) | If it is wanted, a request to Tony: the Tracer is built |
| 7 | **a peripheral DMA's src** | The tile's own id, which reverses item 118 and awaits the owner (16.4, item 160) | "72 to 74 stay source ids" (first note, section 8) | Nothing in the design depends on it; the wording follows the ruling |

**How to read them (owner, 2026-11-16).** v1.35 was simulation only, and to get the RTL off the ground Tony had to
pencil in units that v1.35 had left as black boxes. Those are not necessarily the units that stay: for them, this
folder's design is the proposal, and Tony's unit a starting point where it fits. What the RTL really built stays the
RTL's: a change to it is written down here and asked of Tony, not made on this side.

- **Pencilled in:** 2 (the host interface, whose meaning v1.36 lists as open), 3 (the RW engine) and 4 (`lpddr`).
- **Built:** 1 (the attachment, in the router), 5 (the window's layout), 6 (the Tracer's jump cache) and 7 (the src).

One thing moved toward the owner's picture: v1.35 made the link to the tile the controller router's down port; in
v1.36 it is the attach port, and a peripheral's DMA enters on an unused link input, as in the first note's port table.

## 4. What v1.36 already gives the v1.4 design

| what | where | use here |
|---|---|---|
| **`fill`, defined:** four channels; a read names (root 31 bits, depth 2, line 35) and returns 1,024 bits with a status (`ok`, `hole`, `not_owned`, `beyond`); a write-back returns `ok` or `error`; at most one read and one write outstanding | 3, 17.9.5 | The figure's right bus. The cache black box serves exactly this |
| **what the memory system must do for `fill`:** answer in bounded time; a read sees a write already accepted | 23.6 | The cache's contract |
| **the RW engine,** with the memory tile's `READ` and `WRITE` requests and the write buffer | 17.7, 11.5, 23.3, 23.4 | The file system's page moves, when it is built. Not used in the v1.4 deliverable |
| **the memory tile's other request operations are its firmware's** | 11.5, 23.5 | Where the file system's calls go: target 1, any `op` but `READ` and `WRITE` (v1.35 kept target 3) |
| **`lpddr`'s rules** | 3 | The model of AXI in the simulator starts from them |
| **the host interface's FIFOs and handshakes** | 17.6 | The black box of Ingress starts from them |
| **the scratchpad on a special tile is 4 MiB,** not 2 MiB | 2.2, 19 | Room for the 128 KB page map and more |
| **a simulator that models the RTL,** with `rw_engine.h`, `ingress_egress.h`, `tracer.cpp`, `mem_system.h`, `file_store.cpp` and `lru_coder.h` among its units, and gold and jumpmap under `pipeline/` | `v1.36/src/units/`, `v1.36/pipeline/` | The base for the v1.4 simulator and for gold's frames, in place of `sim/` and v1.35 |

## 5. What v1.36 leaves open that this folder decides

No collision: these fill items v1.36 lists as deferred or open.

| v1.36 | this folder |
|---|---|
| the file system, item 119 (23.5) | the first note, sections 2 to 6 |
| the Updater and the Binder, item 60 (23.5) | the second note, section 4; `UPDATER-INTERFACE.md` |
| the memory system behind `fill`: the translator and the shared cache (23.6) | the second note, sections 2 and 3 |
| the host interface's meaning, item 166 | `HOST-FRAMES.md` |
| the hard and the soft reset, item 124 (18, 22.28) | the first note, section 2: a hard reset formats, a soft reset boots into the shell |

## 6. Rulings v1.36 asks of the owner

Not this folder's, but they change what it writes.

- **Three reversals of recorded rulings** (the guide's section 4): item 160, a peripheral DMA's src is the tile's own
  id; item 161, the Tracer's `EVENTS` and `ERRORS` packed from bit 0; item 162, the segment's handshakes are requests
  to the tracer tile.
- **Decided by the RTL without a recorded ruling** (the guide's section 4.4): items 64, 107, 116, 123, 163 and 168 to
  173, among them the request queue of 4, a scratchpad size that must be a power of two, and the UART on the chip's
  clock.

## 7. Not yet read

- The guide's sections 3 (the timing table), 5 (further differences, and where the RTL's specification is silent),
  6 (errors found in v1.35), 8 and 9, and most of section 7's name table.
- The Tracer's and the replayer's chapters (`spec/jolt/`), beyond the jump cache and the fill port.
- The segment's firmware and how the tracer tile gets its state record and advice (17.9.10, 17.9.15): it decides
  where `HOST-FRAMES.md`'s frames 7 and 8 land.
- The simulator's sources, and `v1.36/planning/`.
- Whether v1.36's gold and jumpmap differ from v1.35's.

## 8. Integrating: proposed steps

1. **The owner's rulings on section 3,** the first two above all: they decide how the stream reaches the Updater.
2. **Read what section 7 lists,** and extend this note.
3. **Put this folder's documents in v1.36's terms:** the names of section 2, the control block and the peripheral
   numbers of section 3, and the notes' references to v1.35's sections.
4. **Base the v1.4 simulator and gold's frames on `v1.36/`.**
