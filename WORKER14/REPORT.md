# WORKER14 report: real cores for C&C 1-3 in meshsim

Branch `cc-cores` (from `main`, with `main` merged in at b060637 and 8224b97). Status: phases 0-5 done, except two items
that wait for WORKER11's branch s8 (below, "Pending"). Notes per turn: `NOTES.md`; questions: `QUESTIONS.md` (W14-01 to
W14-05).

## For the merge: shared simulator code changed

Everything below is inert with `Config.ccCores` off (the default), except the first item, which changes behaviour for any
core that stores to C&C 1's `UART_TX`.

- **The `UART_TX` store stall (check this one).** 8.14 says a store to `UART_TX` stalls while the TX FIFO is full.
  `Cnc1Peripheral.writeWord` already returned -2 for that case, but nothing had ever stored to it through a core (the Java
  model wrote its FIFO directly), and `PeripheralSlot.write` treated any negative return as an undefined word: the store
  was dropped and flagged as a violation. The fix stalls the core instead, the same way a `REQUEST_SEND` into a full
  request FIFO stalls (4.18):
  - `Peripheral.storeStalls(t, w)`, a new default method returning false;
  - `PeripheralSlot.storeStalls(t, n, w)`, which asks peripheral n for words 32-127;
  - `Cnc1Peripheral.storeStalls`: `w == UART_TX && tx.size() >= FIFO`;
  - `Controller`'s store path: one check beside the `REQUEST_SEND` stall, for word stores into a peripheral window
    (`instructions--; cyclesStalled++; return`, retried next cycle).
  The regression steps it touches (ctrl, riscv, recipes, cond, regdoc, hooks, periph, reqbus) gave the baseline's counts;
  task 090 runs the full set.
- `Router`: a controller's routing when `me >= 64` (W14-01), and a null link input skipped in `runCycle`.
- `SRAMReadQueue.loops`: a controller's broadcast takes no loopback token (it never reaches itself).
- `Tile`: a second constructor taking peripheral 1 (the old one calls it).
- `RequestPort`: a controller's receive side reads its target's queue (`target`, -1 on a mesh tile, as before).
- `RequestBus`: `ccTiles`, the controllers' request ports feeding the root's controller inputs (null: unchanged).
- `Cnc1Peripheral.unitErrors`: C&C 1's own `ERRORS` latch the toast pin.
- `Config`: `ccCores` (false), `ccLinkLatency` (1).
- `Chip`: seven hook lines beside `parked` (build `CcCores` before the bus registry closes; one more child; its end of
  cycle in place of `cc1p.runCycle`; its checks, overflows, quiescence and reset).
- `run_tests.sh`: `cc-build` (sw/build_cc.sh), `cccores` and `cccores-shuf` in the quick suite, `ccbench` in the full
  extras; `tools/run_full.sh` lists `ccbench`.

New: `src/CcCores.java`, `test/CcCoresTest.java`, `test/CcBench.java`, `sw/cc/cc1.c`, `sw/cc/cc23.c`,
`sw/build_cc.sh`; `test/Mach.java` gained `bootCores`, `phase`, `cc1Command`, `mbox`, `toastInfo`.

## What was built

**The controllers are tile instances** (spec 6.6): with `Config.ccCores`, `CcCores` builds three `Tile`s, ids 64-66, each
with the tile's controller, memories, register block, boot ROM, big SRAM and engines. Peripheral 1 is C&C 1's existing
`Cnc1Peripheral` (READY/GO, `ERROR_LINES`, watchdog, toast pin, UART), now in C&C 1's slot and driven by its core; C&C 2's
and C&C 3's are stubs of types 2 and 3 (`CcCores.Stub`: present, typed, raising nothing). With the switch off the chip is
as before and `CnC.java` drives it.

- **The link (W14-01):** each controller's router takes tile *c*'s to-controller output and sends into its
  from-controller route block, one cycle each way, no back-pressure. Inside the controller's router, a packet from its own
  injection point for itself is ejected and any other goes out on the link (broadcasts included, rooted at tile *c*);
  everything from the link is ejected.
- **The host port (W14-02):** the outside world (the harness) lands C&C 1's overlays and commands as uncounted mode-0
  writes from src 64, one per cycle, through a third router input, standing in for Ingress/Egress. The images C&C 1 sends
  are staged in its big SRAM by back door (W14-03).
- **The request tree:** the controllers' request ports feed the root's controller inputs, and C&C *c*+1's receive side
  reads target *c*'s queue.
- **Relative destinations (W14-05, accepted):** relative to tile *c*; a controller names its own memories by absolute id.

**The firmware** (`sw/cc/`, built by `sw/build_cc.sh` against the generated `mesh.h`, with the recipes' crt0 and handler):
- `cc1.c`, C&C 1:
  - It puts the three units' first commands into its own big SRAM.
  - It streams C&C 2's and C&C 3's images in one stream run, the command last.
  - It waits for both "started" requests.
  - It broadcasts the tile image from its big SRAM, waits for all READY, then pulses GO.
  - It installs its own handler and enables its hard interrupt.
  - It then loops on the harness's commands (W14-04): `CC_PHASE` (a tile command, then READY, then GO, optionally
    repeated back to back), `CC_GO`, `CC_EXPECT`, `CC_WATCHDOG`, `CC_UART`, `CC_EXIT`. It posts completions in a mailbox.
- The error path (DECISIONS-v1.25 8d):
  - C&C 1 drains reports into data memory and counts each unit's against its set.
  - The set is told by the unit's first sequence number: 0 means 24 reports (0-23) from the image's handler; 24 means
    two reports (24-25) from the ROM's.
  - It ends when every begun set is complete and 1,000 quiet cycles pass, or after 50,000 quiet cycles. Then it writes a
    summary and a UART dump, and parks.
  - A watchdog expiry, while C&C 1 waits for READY or for its own command, takes the same path with mcause 0.
- `cc23.c`, C&C 2 and C&C 3 (one image, the id from `PARAMS`): it sends "started", installs recipe 24's handler and
  enables its hard interrupt, then pops and counts its requests.

**Tests** (`CcCoresTest`, 61 checks, plain and shuffled, about 30 s each):
- the switch off;
- the boot ROM's 64-66 branch on all three controllers;
- C&C 1 writing a tile through tile 0's fifth port;
- C&C 2's request read by C&C 1;
- the full boot with the firmware, then three phases of the `cmd` program giving every tile the Java model's sums;
- the error path: tile 9 and C&C 2 fail at once. C&C 1 traps 4 cycles later and drains 48 reports, both sets complete
  with no flags. `ERROR_LINES_0` bit 9, `ERROR_LINES_2` [1], the toast pin and the UART dump are all checked.
- the watchdog.

## Measurements (CcBench, at 5cd78a2; Config.machine())

**The boot**, reset release to all 64 tiles running their image:

| image (packets to each tile) | Java model | cores | cores, from C&C 1 running |
|---|---|---|---|
| `rcp_boot` (62) | **120** | **901** | 653 |
| `cmd` (25) | 83 | 864 | 616 |

Where the cores' 901 cycles go (`rcp_boot`):

| span | cycles |
|---|---|
| The host lands C&C 1's image (230 packets at one a cycle) and its CMD_JUMP; C&C 1 is running at | 248 |
| C&C 1's start-up, the three commands into its big SRAM, C&C 2's and C&C 3's images streamed (2 x 51 packets); both running at +347 | 595 |
| Both "started" requests through the request tree, served by C&C 1, at +108 | 703 |
| The tile image broadcast (62 packets), all READY, at +161 | 864 |
| GO at +20, all 64 running at +17 | 901 |

The model starts with C&C 1 already working and C&C 2/3 absent, so most of the difference is work the model skips. That
work is: landing C&C 1 itself, booting C&C 2 and 3, and their round trip. The tiles' own part is the last 198 cycles
against the model's 120: the broadcast plus C&C 1's firmware between steps.

Early versions spent about 600 more cycles in C&C 1's firmware. The causes were crt0 zeroing a 64-entry parity array, a
64-pass parity loop, three separate waits for the commands into big SRAM, and two stream runs for C&C 2 and C&C 3.

**A phase:** 32 back-to-back broadcast commands (the `cmd` program's type 1, one argument), each with READY then GO.

| | Java model | cores |
|---|---|---|
| GO to GO, steady state | **43** | **88** |

Where the cores' 88 go:
- About 41 cycles from the GO store to the command's last store. This is C&C 1's `go_latency` + 1 wait and the C code
  that sends the command: parity, header, address, argument, word.
- About 30 more until the broadcast lands on the farthest tile, which then raises READY (its previous command finished
  well before).
- 16 from all READY to the next GO store.

The tiles' own work is the same in both. A tighter firmware (the command words prebuilt per parity, sent straight after
`PULSE_GO`) could take perhaps 15-20 cycles off; I stopped at the obvious fixes (the READY check first in the wait, the
command copied out of the command block once).

**The error path:**
- Two units failing at once: C&C 1 traps 4 cycles after the errors, and the drain of 48 reports is done after 4,794.
  That includes the 1,000-cycle settle; the units' handlers send their 24 reports over about 3,800 cycles.
- The dump at UART divisor 1 takes about 765,000 cycles (400 a byte); at the reset divisor (434) it would be about
  330 million. The tests set divisor 1 first with `CC_UART`.

## What v1.2's known gaps become

- **The boot ROM's branch for 64-66 never ran:** it runs now on all three controllers, with the switch on (CcCoresTest),
  landing the first command through the host port (C&C 1) or C&C 1's stream (C&C 2/3).
- **C&C 2/3's "started" requests weren't modelled:** they are sent by C&C 2/3's start-up through the request tree and
  served by C&C 1 before it boots the tiles, as 6.2 step 4 says.
- **`rcp_cc1.c` is built but never executed:** still pending. It waits for s8, which rewrites recipes 22 and 24.

With the switch off, nothing changes: the Java model stays the default, and every suite runs as before (task 090 checks
the whole set; see below).

## Pending (after s8 merges)

- `rcp_cc1.c` run as C&C 1 in RecipeTest (its cc1 driver from "built" to "run"), against s8's recipes 22 and 24.
- The two-report path: a unit failing while still in its ROM. C&C 1's code for it is written (first sequence 24, set of
  two, 24-25) but untested, since the ROM on `main` still sends 24.
- Misalignment is UB (8b), so it isn't an error path to test.

## What v1.3's C&C 3 firmware can build on

DECISIONS-v1.3 section 10 makes C&C 3 the generator of long expansions for the Tracer: loop bodies emitted as template
words, paced by credits. This branch gives that firmware a place to run:
- C&C 3 is a full tile instance with id 66, its own program and data memories, big SRAM and engines.
- Its image is landed by C&C 1, and it reports "started".
- Its request port reads target 2's queue, and it has recipe 24's handler with its error line wired to C&C 1.
- Its peripheral slot holds a typed stub (type 3, the Tracer) where the Tracer's model, or a test double for the credit
  protocol, would go.
- `cc23.c` is the shell to replace: start-up and the request loop.
- The host port and `Mach.bootCores` let a test land any C&C 3 image without changing C&C 1.

What it doesn't have yet: the Tracer's side, which is the credits and the template queue in C&C 3's slot. C&C 2's
engines are stubs too.

## Runner verification

Task 090 (Quest, claimed in NUMBERS.md): the quick suite (8 at a time) and the full extras with CcBench (4 at a time),
from a kit built at Java 17 from 5cd78a2. The kit was smoke-tested locally on bus, regdoc, replayer, reqbus, riscv,
stageb, cccores, hooks, periph, cond and CcBench. Result: *pending at the time of writing* (see NOTES.md).

The full-size sort (p4/uniform-k128) was not rerun here; the sort steps of the quick and full suites are. It could ride
on s8's verification (task 089) once this branch is merged.
