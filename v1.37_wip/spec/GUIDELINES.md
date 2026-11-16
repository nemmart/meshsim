# Guidelines: the rules the design follows

*2026-11-12; confirmed by the owner. Names and section numbers are v1.36's. The spec says what each module does; this page says what every module, and every
change, has to respect. Short by design. Each rule names where it comes from: a section of `machine-spec.md`
(v1.36), a decision, or **[owner]**: the owner's statements of 2026-11-12, first written down here. A ruling is the
current answer, not a proof: anyone may reopen one with a reason [owner].*

## 1. Authority, and how the text is meant

- **The spec states what the hardware must do observably, not how.** A mechanism in it (a buffer, an order of
  operations) is a reasonable default; an implementation that is observably the same, or better and reported back,
  is welcome (owner, 2026-11-06).
- **The RTL is the truth about the hardware.** Where the spec and the RTL differ, the RTL wins and the spec is
  corrected [owner, 2026-11]. v1.36 is that rule applied whole: its platform and core chapters are the RTL's
  specification, and a difference found later, in the RTL's specification as in ours, is corrected to the RTL's
  sources and listed in the guide (`spec/CHANGES-v1.35-to-v1.36.md`, section 9).
- **The spec and meshsim are the high-level design; the RTL owns what is buildable and the exact timing.** A new
  design is tried here first, and the RTL may then diverge from it here and there, hopefully by little. They must
  agree on results and on behaviour at the architecture's level [owner]. What the RTL work turns up comes back
  into the next version (README), by the rule above; where the owner rules against the RTL, the RTL and the spec
  change together.
- **Where a decisions log and the spec differ, the spec is the authority** (v1.35's preamble).

## 2. Who checks what

- **Every rule a program must keep is one of two kinds** (owner, 2026-11-01; 1.3). **Checked by the hardware:**
  it raises an `ERRORS` bit; reserved for what simulation can't see, hardware faults and mistakes that depend on data
  or timing only real runs produce. **Undefined behaviour:** the hardware does nothing special; the simulator stops
  and names the access. **There is no third kind:** never a prescribed outcome for a mistake.
- **The test for a new check:** would simulation catch this on our firmware? If so, it is UB.
- **Every error is terminal** (6.2, 7.4). There is no HALT state and nothing on the chip recovers: the handler
  reports and parks, the toast pin rises, and the outside world resets the chip. So what happens to the offending
  access is left unspecified unless it comes for free.
- **`ERRORS` means something is wrong.** On a correct program and correct hardware it reads 0 forever; anything
  informational belongs in the simulator's reports (7.4).
- **A hang has one backstop,** the master tile's watchdog (17.6).
- **No ECC or parity.** A corrupted run gives a proof that fails verification, and is run again [owner].

## 3. One design, many instances

- **Tiles are one design,** the base tile, differing only in their straps and in the peripherals they hold (2.2).
  **The three special tiles are tile instances,** with the same core, tile window, toolchain and header (2.1).
- **Every chip-level function lives in a peripheral,** so the tile window is the same on every tile
  (2.2). **Every peripheral connects the same way:** a region, a window with the control block, events, errors, an
  optional DMA (17).
- **The platform doesn't know what it is proving.** Tiles, mesh, control and memory are the platform; what is
  specific to a proof system is a peripheral, or firmware. A new unit goes in a peripheral slot unless it can't [owner].
- **Same words, same places.** Every register window starts with the control block; registers are named by their
  unit; every register carrying a packet header lays it out the same way (1.2, 5.3, 5.4).

## 4. Addresses and registers

- **One 32-bit address map** for packets and for the core's own accesses (5.1).
- **Natural alignment everywhere.** A misaligned packet or unit access is UB; the core's own misaligned load or store traps (4.2, 6.2, 15).
- **Registers are one word, one `lw` or `sw`.** An unmapped word is an access fault; an action register acts on a
  store with bit 0 set; configuration registers are static during a phase and undefined at reset (1.2, 5.3, 5.4, 18).
- **Programs wait on events.** `STATUS` is diagnostic only: no program needs to poll it (7.1, 7.2).

## 5. Traffic

- **Anything whose landing matters is counted.** One counter per thing a program waits for; done means landed (4.2,
  12.2, 12.3).
- **Order holds only within one sending unit, one destination and one kind** (14.4). Otherwise a program orders things
  itself, with a fence (4.2).
- **The mesh has two regimes** [owner; the second is proposed, not yet specified]. **Strict rotation:** a schedule
  says who sends when, nothing is backpressured, a full router queue is an error (8.4, 16.3). **Elastic,** for
  traffic into the file system: a per-tile flag masks that error and a full queue backpressures to the address
  generator, never to the core; the flag changes only between phases, with the mesh empty.
- **Credits only where a path can't form a cycle** (the request tree, a peripheral's queues); the mesh, a torus, is
  scheduled [owner, from 11.3 and 17.8.8].
- **A new bulk pattern on the mesh comes with a schedule and a fan-in bound,** checked in simulation [owner].
- **The shapes of traffic the mesh carries today, and what keeps each safe** [owner]:

| shape | examples | kept safe by |
|---|---|---|
| one source to every tile | a broadcast load, the replayers' expansions | a broadcast can't overflow a queue; a full-rate one has the mesh to itself |
| one source to one or a few tiles | a traced window's delivery, file reads | the source paces it |
| many tiles to one sink that always drains | file writes | the elastic regime (proposed) |
| a few words from many tiles to one | each round's reduction | the program's pacing, inside the fan-in limits (22.24) |
| many tiles to many | the sort's all-to-all | strict rotation: the 16-group schedule |

  Bulk traffic from many tiles to many outside the sort is the one shape nothing covers; a kernel that needs it
  brings its own schedule (the rule above).

## 6. Time and cost

- **Every latency is a named parameter with a default,** the same on every tile, so every tile's path from the release is
  identical (6.4, 7.3, 19). Physical values stay open until there is a PDK.
- **Every module is charged to the logic budget, and additions must fit.** Tables go in SRAM; comparators, muxes and
  per-entry flops are what spend it (24).
- **A wait that only orders events may be a software constant,** tuned at bring-up, where the failure is visible [owner].

## 7. The simulator's part

- **All firmware runs in simulation before it runs on hardware,** so catching software bugs is the simulators' job
  (1.3). **Every invariant of chapter 20 is a meshsim check.** Undefined
  behaviour stops the run in every build; the other invariants are counted in every build and reported by name in
  the debug and suite builds only.
- **meshsim's values are the model's, not promises:** where the spec says "in meshsim", the RTL's measured value
  replaces it and comes back (19.1).
- **Between simulator versions: identical results, cycle counts very close.** A deliberate difference is proposed,
  approved and recorded, and preferred where no reference program reaches it (D30).
- **Where there is RTL, the simulator follows it.** v1.36's models the RTL's hardware at the register, packet and
  cycle level, runs the RTL's firmware images unmodified, and gives the RTL chip's results at its cycle counts;
  its references are the RTL's, not v1.35's simulator's [owner, 2026-11; section 1].
- **Every measurement names what it was measured on.**

## 8. Versions

- **Each version is a folder; a frozen folder is never edited,** except for an errata file and point releases in
  their own folder (README).
- **The RTL moves from one frozen version to the next by a guide,** unit by unit, each item marked whether it
  affects the RTL (the `CHANGES-…-to-…` files). v1.36's guide goes the other way: it brings the specification
  and the simulator to the RTL.
- **Item and invariant numbers are never reused.** Section numbers were v1.35's until v1.36, which takes the RTL's numbering; `spec/CHANGES-v1.35-to-v1.36.md` maps the old to the new. From v1.36 on, a section that is removed or moved leaves a one-line stub.
