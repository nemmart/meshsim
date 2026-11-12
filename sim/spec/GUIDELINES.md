# Guidelines: the rules the design follows

*2026-11-12; confirmed by the owner. The spec says what each unit does; this page says what every unit, and every
change, has to respect. Short by design. Each rule names where it comes from: a section of `machine-spec.md`
(v1.3), a decision, or **[owner]**: the owner's statements of 2026-11-12, first written down here. A ruling is the
current answer, not a proof: anyone may reopen one with a reason [owner].*

## 1. Authority, and how the text is meant

- **The spec states what the hardware must do observably, not how.** A mechanism in it (a buffer, an order of
  operations) is a reasonable default; an implementation that is observably the same, or better and reported back,
  is welcome (owner, 2026-11-06; the spec's preamble).
- **The spec and meshsim are the high-level design; the RTL owns what is buildable and the exact timing.** The RTL
  may diverge here and there, hopefully by little. They must agree on results and on behaviour at the architecture's
  level [owner]. What the RTL work turns up comes back as a delta into the next version (README).
- **Where a decisions log and the spec differ, the spec is the authority** (preamble).

## 2. Who checks what

- **Every rule a program must keep is one of two kinds** (owner, 2026-11-01; preamble). **Checked by the hardware:**
  it raises an `ERRORS` bit; reserved for what simulation can't see, hardware faults and mistakes that depend on data
  or timing only real runs produce. **Undefined behaviour:** the hardware does nothing special; the simulator stops
  and names the access. **There is no third kind:** never a prescribed outcome for a mistake.
- **The test for a new check:** would simulation catch this on our firmware? If so, it is UB.
- **Every error is terminal** (4.13, 6.1). There is no HALT state and nothing on the chip recovers: the handler
  reports and parks, the toast pin rises, and the outside world resets the chip. So what happens to the offending
  access is left unspecified unless it comes for free.
- **`ERRORS` means something is wrong.** On a correct program and correct hardware it reads 0 forever; anything
  informational belongs in the simulator's reports (4.13).
- **A hang has one backstop,** C&C 1's watchdog (6.1).
- **No ECC or parity.** A corrupted run gives a proof that fails verification, and is run again [owner].

## 3. One design, many instances

- **Tiles are identical in design,** differing only in big-SRAM size and in what is attached and strapped (2).
  **The three controllers are tile instances,** with the same controller, register block, toolchain and header (6.6).
- **Every chip-level function lives in a peripheral,** so the controller's register block is the same on every unit
  (6.6). **Every peripheral connects the same way:** a region, a standard register window, events, errors, an
  optional DMA (4.17).
- **The platform doesn't know what it is proving.** Tiles, mesh, control and memory are the platform; what is
  specific to a proof system is a peripheral, or firmware. A new unit goes in a peripheral slot unless it can't [owner].
- **Same words, same places.** Every block and window starts `PARAMS`, `STATUS`, `ERRORS`; registers are named by
  family; every register carrying a packet header lays it out the same way (8.1).

## 4. Addresses and registers

- **One 32-bit address map** for packets and for the controller's own loads and stores (2.2).
- **Natural alignment everywhere.** A misaligned access is UB; only a misaligned jump target traps (1, 4.1).
- **Registers are one word, one `lw` or `sw`.** Reserved words read 0 and ignore stores; a pulse register acts on a
  store of 1; configuration registers are static during a phase and undefined at reset (8.1).
- **Programs wait on events.** `STATUS` is diagnostic only: no program needs to poll it (4.12, 4.13).

## 5. Traffic

- **Anything whose landing matters is counted.** One counter per thing a program waits for; done means landed (3.1,
  4.12).
- **Order holds only within one sending unit, one destination and one kind.** Otherwise a program orders things
  itself, with a fence (3.2).
- **The mesh has two regimes** [owner; the second is proposed, not yet specified]. **Strict rotation:** a schedule
  says who sends when, nothing is backpressured, a full router queue is an error (3.2, 4.7). **Elastic,** for
  traffic into the file system: a per-tile flag masks that error and a full queue backpressures to the address
  generator, never to the controller; the flag changes only between phases, with the mesh empty.
- **Credits only where a path can't form a cycle** (the request bus, a peripheral's queues); the mesh, a torus, is
  scheduled [owner, from 3.3 and 4.19.8].
- **A new bulk pattern on the mesh comes with a schedule and a fan-in bound,** checked in simulation [owner].
- **The shapes of traffic the mesh carries today, and what keeps each safe** [owner]:

| shape | examples | kept safe by |
|---|---|---|
| one source to every tile | overlays, the replayers' expansions, commands | a broadcast can't overflow a queue; a full-rate one has the mesh to itself, single command packets may go at any time |
| one source to one or a few tiles | a traced window's delivery, file reads | the source paces it |
| many tiles to one sink that always drains | file writes | the elastic regime (proposed) |
| a few words from many tiles to one | each round's reduction | the program's pacing, inside 3.2's fan-in limits |
| many tiles to many | the sort's all-to-all | strict rotation: the 16-group schedule |

  Bulk traffic from many tiles to many outside the sort is the one shape nothing covers; a kernel that needs it
  brings its own schedule (the rule above).

## 6. Time and cost

- **Every latency is a named parameter with a default,** the same on every tile, so every tile's path from GO is
  identical (5, 6.3, 10). Physical values stay open until there is a PDK.
- **Every unit is charged to the logic budget, and additions must fit.** Tables go in SRAM; comparators, muxes and
  per-entry flops are what spend it (11).
- **A wait that only orders events may be a software constant,** tuned at bring-up, where the failure is visible [owner].

## 7. The simulator's part

- **All firmware runs in simulation before it runs on hardware,** so catching software bugs is the simulators' job
  (preamble). **Every invariant of spec 9 is a meshsim check.** In v1.35, undefined
  behaviour stops the run in every build; the other invariants are counted in every build and reported by name in
  the debug and suite builds only.
- **meshsim's values are the model's, not promises:** where the spec says "in meshsim", the RTL's measured value
  replaces it and comes back (4.8, 10).
- **Between simulator versions: identical results, cycle counts very close.** A deliberate difference is proposed,
  approved and recorded, and preferred where no reference program reaches it (D30).
- **Every measurement names what it was measured on.**

## 8. Versions

- **Each version is a folder; a frozen folder is never edited,** except for an `ERRATA.md` and point releases in
  their own folder (README).
- **The RTL moves from one frozen version to the next by a guide,** unit by unit, each item marked whether it
  affects the RTL (the `CHANGES-…-to-…` files).
- **Section and item numbers are never reused.** A section that is removed or moved leaves a one-line stub (preamble).
