# WORKER6 report, round 3 (closing)

**Everything is on `main`.** The recipes, the conventions, the draft header and the check sources follow the
decisions log through section 44 and WORKER5's `RECIPE-CHECK.md`. The book is still generated from the compiled
code: `build.sh` compiles every file with `-Werror`, and `check_book.py` confirms all **46** code blocks in
`wip/sw/RECIPES.md` equal their regions. Nothing has run (no v1.2 simulator). **All 17 findings are answered.**

## 1. WORKER5's mismatches

| # | fix |
|---|---|
| M1 | Answered by D43 and D44 (below); R22 keeps the command in the run and points to the fence |
| M2 | Nothing to do: the spec was fixed to R26's wait on counter 4 and [10] |
| M3 | `local_run`'s comment (`rcp.h`, so R0's block): DMA run done is `EVENTS` [10] |
| M4 | R25's walk: the tile's live bit is [12 + n] |
| M5 | "part 0" for the replayer's writable state, in R25's relies-on line and `replayer_load_state`'s comment; "region" is kept for the address map's |
| M6 | The header's data-memory comment and CONVENTIONS 1 now say the layout is **decided** (W5-01; spec 4.11-4.12; D37.4). Only the stack's placement is a convention. REPORT-2 §4 is superseded on this |
| M7 | The header's build comment: `-march=rv32i_zicsr_zmmul` |
| M8 | CONVENTIONS 7: a running program reads `ERRORS` = 0 **once boot part 2 has enabled interrupts**; before that an error raises the error line and the toast pin but takes no trap |

**Observations, both taken.**
- **The watchdog:** `wait_all_ready()` now calls the toast path when the watchdog expires, instead of parking.
  The expiry is an event, not an error, so nothing else would make C&C 1 dump what it has.
- **`MAX_REPORTS`** is 66 × 24: every unit but C&C 1 sends the set.

## 2. D43: mode 5, count only, and the fence

- **Header:** `MODE_COUNT` = 5 (6-7 reserved; the counter field counts modes 0-3 and 5), and `CTR_FENCE` = 7.
  A fence takes any counter the phase isn't using; 7 is request slot 2's, so a tile doesn't fence while slot 2
  is outstanding.
- **New recipe 28, count-only packets:**
  - **The fence** (`boot_unit_fenced`, in `rcp_cc1.c`): C&C 1 streams an image to a unit, ends the run with a
    count-only packet to itself on counter 7, waits for that counter (and [10]), then sends the first command from
    the send port. This is the general pattern. Recipe 22 keeps the command in the run as the boot recipe and
    points to the fence.
  - **A signal between tiles** (`signal`, in `rcp_signal.c`): one 32-bit count-only packet on a counter the
    receiver armed. It is shown where it is natural: a ring pipeline in which each tile frees its inbox for its
    left-hand neighbour with a signal (a credit), instead of a reply word nobody reads. The walk checks every
    arming against its earliest arrival, and ends with every counter at 0.
- **CONVENTIONS 6a:** the mode table has 5. **CONVENTIONS 6:** fences and signals.

## 3. D44: ordering per sending unit; reads outstanding until their data lands

- **Rule 12** (R15) and CONVENTIONS 6: ordering holds per sending unit (address generator, send port, GATHER
  engine, read port, each peripheral's DMA), not per tile. Between units there is none; impose it with the run or
  a fence. Counted data needs neither.
- **Rule 13** and CONVENTIONS 6b: a request is outstanding until everything it brings back has landed. A read,
  with no reply, is outstanding until its data counter is done.
- **`request_issue(k, w0, w2, w3, data, reply)`** now arms the slot's counter for its data packets plus 1 only if
  there is a reply. So a read's slot is freed by `request_wait` exactly when its data has landed, and the 1/3/1
  limits count it that way. `result_to_cc1` passes (0 data, 1 reply).
- **F17 is answered** in `FINDINGS.md`, with D43 and D44.

## 4. For WORKER5, if it checks again

The places that changed since `RECIPE-CHECK.md`:
- the header's `MODE_COUNT` and `CTR_FENCE`;
- R15's rules 12-13;
- R21's `request_issue` signature;
- R22's paragraph on the fence;
- R27's watchdog path and `MAX_REPORTS`;
- R28 (new);
- CONVENTIONS 1, 5, 6, 6a, 6b and 7.

Nothing else in the recipes, the ROM or the handler changed.

## 5. What remains open for later sessions

- **C&C 1's request format** (the op and sequence fields, item 122) and **C&C 2's** (the file system's, deferred
  with D33). The draft's `REQ_CC1` is a placeholder. The recipes need only "a request names its reply address and
  counter".
- **Which `mie` bit is the hard interrupt's** (Q1, item 121); the draft assumes MEIE, bit 11.
- **Running the recipes:** the files in `WORKER6/check/` become meshsim v1.2's `rcp_*.c` tests once there is a
  simulator, a linker script and a ROM model; every cycle figure in R14 is to measure then.
