# WORKER8 (G1: the tile's control, and the fingerprint's C++ side): notes

Branch `sim-w8`. The prompt is `PROMPT.md`; the specification is `sim/tests/AUDIT-M2.md` (sections 1.2, 2, 3.3, 3.7, 5,
6). **Plan approved by the owner, 2026-11-07**, with the rulings below.

## The owner's rulings (2026-11-07)

1. **`pendingAdd` (D27): model ref as it runs.** `Tile.children` (Tile.java:119) steps the director before the core, and
   the core's store reaches `TileControl.write` (TileControl.java:186: `pendT = t`) during the core's step, so
   `pendingAdd` (TileControl.java:207) is always 0 in ref's default run: the director judges a due decrement without the
   core's same-cycle counter store. Modelled order-free: the core's store reaches the register block registered, for
   t + 1. AUDIT-M2 sections 2.5 and 3.3 corrected on this branch (W7 has closed).
2. **The ports with W9: agreed** (table below; W9's NOTES has the same). Where a latency is in doubt, the boot's
   fingerprint decides it.
3. **The port observer is framework**: W10 builds it, early; the exact request is below, the owner relays it. Until it
   lands, my units fingerprint what they hold and the bus parts are stubbed.
4. **Stubs emitting reset-state streams** for units not built yet: approved (all of tile 0 checkable from the start).
5. **JDK and clang installed** with apt in the container (OpenJDK 21.0.12.1, clang 18.1.3, g++ 13.3.0: the runner's).

## The ports with W9 (agreed, owner 2026-11-07; both build to this list)

| port | kind | from -> to | words | latency |
|---|---|---|---|---|
| link word | — | everywhere | 3: payload [63:0], the header as `LinkWord` lays it out (simulator-only bits included), payload [127:64] (D17) | — |
| send | Hold | my core -> W9's injection unit | 3 | (Hold) a refusal is retried at t + 1, the core stalls |
| send-hold status | Reg | W9's injection unit -> my register block | 1: send hold empty at the end of t | (Reg) |
| eject | Link | W9's router -> my director | 3 | `intra_tile_latency` (1) |
| GO | Link, width 0 | W9's C&C -> each register block | 0 | `go_latency + skew[k] + 1` (the +1: the block's shifted end of cycle) |
| READY, error line | Link, width 1 | each register block -> W9's row collector | 1 | `ready_latency - 2`, then `Link(1)` collector -> C&C: `ready_latency - 1` from my block's step |
| port B | — | mine alone | | |

The READY/error word (my definition, in `units/tile_ctl.h`): bit 0 = READY's rising edge at the end of t, bit 1 = the
error line's level after `endCycle(t)`; put only in a cycle where READY rises or the error line changes.

`units/linkword.h` (LinkWord.java's layout and helpers) is a shared header: I own it; W9 includes it; a change either of
us needs goes through the integrator.

## Request to W10: the port observer (relayed by the owner)

**What:** a between-cycles query (after `run` returns, never inside a step; tests and debug only, nothing in the cycle
loop) giving the contents of **one input port** of a built machine, identified by its handle as it sits in the unit
object (`sim.unit<T>(name).port`), so the canonical fingerprint (AUDIT-M2 6.2) can list "every bus that delivers into the
unit" as ref's `StateFp` does. At the end of cycle c (`sim.now() == c` after the run):

| kind | what it returns |
|---|---|
| `Link` | every entry not yet taken, in arrival order: the visible ones (in the ring) **and the in-flight ones** (in the wheel's buckets, or in an outbox / another domain's wheel when the machine is split), each with its **arrival cycle** and its words |
| `Fifo`, credit `Fifo` | the visible entries in order (words); then **the put made at c** if any (the bucket entry for c + 1, not yet judged), its words |
| `Hold` | occupied and the held words; then **the put made at c** if any, its words |
| `Reg` | the value (and whether one has landed); then the put made at c if any, its words |
| `Wire`, `RegFile`, `Count` | their current values (already readable; listed for completeness) |

**Shape (a proposal; W10 decides):**
`struct PortEntry { Cycle at; Word w[3]; };` and
`uint32_t Sim::observe(const LinkIn&, PortEntry* out, uint32_t max) const;` (likewise `FifoIn`, `HoldIn`, `RegIn`, with a
flag for the pending put), filling a caller's buffer (no allocation), returning the count; fatal if called inside a step.
Width-0 links return entries with `at` only. By-reference payloads: the slot's words (not needed for M2: every M2 port is
inline).

**Every form:** one domain, and a machine split over domains and threads (W4's form 2): an in-flight entry may sit in an
outbox or in the destination domain's wheel; the observer finds it wherever it is. **Cost:** a scan of the wheel per
query is fine (a fingerprint per cycle over tile 0 is ~40 ports).

**Why it can't be done from the units:** an input's in-flight entries exist only in the framework's buckets; a producer
keeping a shadow copy would put work in the cycle loop for a test facility.

## The plan (approved)

**1. Ports first** (this file, then headers, declarations only): `units/linkword.h` (shared), `units/tile_ctl.h` (the M2
register block unit), `units/director.h` (the director with port B's source), the core's new ports in `controller.h`.

**2. The fingerprint library** (`sim/tests/statefp.h/.cpp`):
- the hash (`fmix64`, the seed, the count), an `FpOut` that hashes or dumps with word names (names as literals: no
  allocation);
- incremental memory sums (`MemSum`: two `fmix64` and an add per written word), with a full-scan check;
- bus writers (Link, Fifo, Hold) over the observer, stubbed until it lands;
- a registry in StateFp's order (16 units per tile, then `cnc, cc1p, reqbus, ep`): each unit contributes through one
  small interface (a writer function and its object), so W9's and later groups' units plug in; stubs emit a not-yet-built
  unit's reset-state stream (computed, never a hard-coded hash) until its owner replaces them;
- the `.fp` reader: the D12 header (run, images, boot ROM, hops, every `Config` field, the start skews) checked against
  the run's configuration, refusing on any difference and naming the key and both values; then the body compared line by
  line: **the first differing cycle and unit**; then `fpdump` on both sides (ours from the library, ref's from W7's
  `FpHooks fpdump=C:UNIT`) diffed: **the named word**;
- shifted-end-of-cycle units (the block, the director's counters, port B) write their end-of-c stream by applying their
  inputs of c to a copy of their state (ref's `endCycle(c)` has run; ours runs at level 0 of c + 1);
- tests: hash vectors against Java's `StateFp.fmix`; incremental = full; D12 refusal; localisation on synthetic `.fp`
  files; W5's core stream against ref fingerprints made by a small driver (`sim/tests/ref/`) on M1 goldens.

**3. The register block in full** (`RegBlockState` extended as plain logic, shared by W5's M1 unit and the M2 unit):
sends (32/64/128: ROUTER_HDR, ROUTER_ADDR's advance, W0-W2), the write counters (a store adds at `endCycle`, wraps
flagged; the director's decrements arrive as events; D27's order), events [6:0], [7] (with the done gate), [10], [11],
EVENTS_REQUIRED, PULSE_DMA, DMA_CONFIG, DMA_RECORD_BASE_ADDRESS, SRAM_RD_*, the loads `regs` checks, STATUS, ERRORS from
`Count`s read `through_last`, READY and the error line to W9's collector. Ports to G3/G4 (DMA configuration and the AG's
pulse as `Wire`s written at level 0 of t + 1; the AG's and gather's status as `Reg`s; the read port's `Fifo(64)` and free
count; the table window) are added with those units; until then what needs them stays fatal, "not modelled in M2".

**4. The director and port B**: modes 0 and 1 (port B for regions 0x00/0x01, the write engine's input for 0x03 and
mode 1); the K-stage line as a `Link(K + 1)` of counter ids into the block (the block applies a decrement in its
`endCycle(t)`: no wake at the due cycle); port B as a `Link(cm_latency + 1)` of link words into the core (the memories stay
in the core, W5's design; ref's write lands at t + cm_latency and is seen by a load at t + cm_latency + 1); command landed
as a width-0 `Link(cm_latency + 1)` into the block. The write engine's and the gather's inputs are `Fifo`s into stubs
until G3/G4; modes 2-4 have their place in the switch, fatal until G4.

**5. Checks**: M1's 67 goldens on every build; then, with W9's chip, `regs` and `wfi` with every unit of tile 0 every
cycle against `golden-m2`, under every debug option (shuffle, both orders, step-all, the missed-wake detector).

## Log

- 2026-11-07: read the prompt's list; plan proposed; approved with the rulings above. JDK and clang installed. Branch
  `sim-w8` from `ea332a8`.
- 2026-11-07: AUDIT-M2 2.5 and 3.3 corrected (D27). **Port headers published** (declarations; gcc and clang
  `-fsyntax-only` clean): `units/linkword.h` (shared with W9), `units/director.h` (`Director`), `units/tile_ctl.h`
  (`TileCtl`, the core's M2 ports, `G1Config`, `G1IO` and `add_g1`: the handles W9's tile builder wires). Choices made
  there: the K-line is a `Link(K + 1)` of counter ids from the director to the block (no wake at the due cycle); port B and
  command landed are `Link(cm_latency + 1)` (the memories stay in the core); ports to G3/G4 are added with those units;
  the director's `we_in` and `gather` outputs exist from the start, wired to stubs; the K-line's entries are a bus, so
  `dir.line` is stubbed until the observer lands (no shadow copy in the loop).
- 2026-11-07: **the fingerprint library, first piece** (`sim/tests/statefp.h/.cpp`, `test_statefp`, 8 tests; gcc and
  clang, debug and release): the hash, `FpOut` (hash, or dump in `fpdump`'s format), `MemSum`, the bus encodings,
  `FpRegistry` (one writer per unit name, a point as ref writes it), `FpGolden` (load, the D12 header check naming the
  difference, `compare`: the first differing cycle and unit). Checked against ref's own files: the four- and five-zero
  streams equal the golden's `t0.inj`/`ep` and `t0.hist`/`t0.req` at cycle 0, and every `chip` line of `regs.fp`,
  `wfi.fp` and `a2a.fp` equals our combine of its unit lines. Next: the stubs (reset streams, checked against ref's
  `fpdump` at cycle 0), the core's and the memories' writers, the ref driver for M1 goldens.
