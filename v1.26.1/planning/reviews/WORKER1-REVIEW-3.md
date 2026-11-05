# WORKER1 review, round 3: sign-off before implementation

Scope: errors, traps, reset and boot. **D***n* is a line of `wip/planning/DECISIONS-v1.2.md` at commit 50f6f46,
and **S***n* a line of `wip/spec/machine-spec.md`.

## 1. Verdict

**Ready to implement, with four small calls first.**

- **Can start now:** phases 1, 2 and 5 of `WORKER4/PROMPT.md` (the packet path, registers and events, and
  the meshsim-only checks). Nothing in my area blocks them.
- **Phase 4 (boot) can't run to completion as written: W1-32.** After a whole-chip reset C&C 1 boots
  through the ROM too, and nothing defines how C&C 1 itself is loaded and started.
- **WORKER4 would have to guess in three places:**
  - the boot ROM's exact behaviour (W1-33);
  - the one watchdog's reset value and semantics (W1-34), on which the toast pin depends;
  - an ordering rule for overlays against the first command (W1-31).

Each needs a sentence or two in the log, not a new design. If they are settled before WORKER4 reaches
phases 3 and 4, I sign off.

## 2. Status of round 2's findings

| finding | status | evidence |
|---|---|---|
| W1-18 traps with no `ERRORS` bit | **closed** | 27 (D1044–1048): meshsim stops on them; the hardware adds nothing; settled |
| W1-19 memory system and LPDDR across the reset | **closed** | 35's soft reset keeps the AXI bus, the LPDDR controller and the PHY (D1234–1236); the flush is best effort. The keep list is still to settle with Tony |
| W1-20 reset boundary at C&C 1 | **closed** | dissolved: the reset is always the whole chip, from outside (35, D1204) |
| W1-21 C&C 1's state across its restart | **closed** | dissolved: C&C 1 is reset with everything else (35) |
| W1-22 errors at C&C 1 caused by other units | **closed** | dissolved: every error is terminal, so whose error it is no longer matters (35, D1229–1230) |
| W1-23 the boot ROM's contract | **partly closed** | 31 settles the shape: a minimal `wait_for_command()`; the reset zeroes control state; GO clears "command landed", so there is no stale READY. The ROM's exact behaviour is still open: W1-33 |
| W1-24 the marker overtaking the image | **partly closed** | big-SRAM data is now counted, after the first command (31.5). Overlays against the first command still rely on an ordering 3.2 doesn't give across kinds: W1-31 |
| W1-25 release timing and pacing | **closed** | dissolved: C&C 1 is reset too and boots after the release |
| W1-26 loops in recovery | **closed** | dissolved: nothing recovers on chip |
| W1-27 the host's view, C&C 1's boot | **partly closed** | the toast pin and the outside resets answer the host's view (35). C&C 1's own boot is not dissolved by a whole-chip reset; it happens on every reset: W1-32 |
| W1-28 remnants | **closed** | fixed in place (D90, D805, D221). One new remnant is in the list at the end |
| W1-29 `PULSE_GO` and READY | **closed** | 28: `PULSE_GO` clears READY and drives GO in one store; `READY_EXPECT` is the expected mask; the reset clears READY |
| W1-30 one-at-a-time start-up | **closed** | 28's expected mask; the replayer's ownership rules are deferred (33) |

## 3. What blocks implementation

### W1-31: overlays and the first command need the same kind

**Decisions:** 31.3–31.4 (D1123–1127). **Spec:** 3.2 (S158).

31.3 says the overlays and the command "come from one source to one unit through port B, so the command
lands last". Port B doesn't order anything; the mesh does, and only within one kind. A unicast and a
broadcast can arrive out of order (S158).

The natural boot mixes the two kinds: **broadcast overlays** (one image for 64 tiles), then a first command
per tile, **unicast**.

1. The command can land before the last overlay packet.
2. "Command landed" raises READY.
3. C&C 1 sees all tiles READY and sends GO.
4. The tile jumps into a partial image.

meshsim models routing faithfully, so it will reproduce this if C&C 1's boot code mixes the kinds.

**Suggested resolution:** add to 31.3: "the first command has the same kind as the overlays, all broadcast or
all unicast". meshsim then checks that no overlay write to a unit lands after its "command landed" and before
its GO.

### W1-32: nothing boots C&C 1

**Severity:** blocks phase 4. **Decisions:** 31.2 (D1122), 31's branches (D1135), 33 (D1173), 34 (D1204), 35.

After a whole-chip reset "every controller starts at 0x02000000" (D1122), C&C 1 included. The ROM's branches
cover only:

- tiles 0–63: wait for GO;
- C&C 2 and C&C 3: wait for "command landed".

Endpoint 64 has no branch, and nothing writes C&C 1's overlays or its first command. 33 still defers "C&C 1's
own power-on boot (W1-27)" (D1173). 34 counts W1-27 among the findings the whole-chip reset dissolves
(D1204). After 35, C&C 1's boot runs on every reset, and the rest of the boot sequence starts from it.

WORKER4's harness "plays the outside world" but has nothing to play.

**Suggested resolution:**

- Endpoint 64 takes the C&C 2/3 branch: set the mask to "command landed", wait, jump.
- **The outside world** lands C&C 1's overlays and its first command through port B:
  - in meshsim, the test harness writes them directly;
  - on silicon, the host through Ingress/Egress, whose words are deferred.
- One ROM serves all units, and the future host path drops in without a ROM change.
- Record this in 31, and remove W1-27 from 33's list.

### W1-33: the ROM's behaviour must be exact

**Decisions:** 31 (D1125–1148), 16 (D613), 6 (D271–281). **Spec:** 6.3 (S1772–1773).

The ROM is silicon, and WORKER4 builds its image. "A minimal `wait_for_command()`" leaves four things that
change behaviour.

**(a) Consuming GO.** GO is a sticky event (16, [5]), and only a load of `EVENTS` consumes it.

- If the ROM jumps without loading `EVENTS`, the image starts with GO pending.
- From the `done` pulse until GO, a pending GO is exactly what wakes `WFI` (D613).
- So the image's first `wait_for_command()` returns at once, without a GO, and that tile runs one phase
  ahead of the rest.
- The ROM must consume GO before jumping, as `mesh.h`'s `wait_for_command()` does.

**(b) Where the jump address is.** "The reset selects command block 0" (D1145) has no hardware meaning:
there is no block selection, only the command word's parity bit choosing slots 1–7 or 8–15 (S1772–1773).

- The ROM should read slot 0, the command word, and then the address from a fixed argument slot, for
  example slot 1 for an even-parity command.
- Reading slot 0 also matters for meshsim's check that no slot-0 write lands before the previous command
  word was read (S1773). Otherwise C&C 1's next command would be flagged.

**(c) C&C 2 and C&C 3's exit state.** Their branch sets `EVENTS_MASK` to "command landed" (D1135). The ROM
should restore the reset mask (GO | counters done) and consume [6] before jumping, so their images also
"start clean", as 31.4 promises for tiles.

**(d) Wording.** "The boot ROM uses registers only, never data memory" (D1144) is untrue as written: it must
load the command block, which is in data memory. Say "never writes data memory, and reads only the command
block". The ROM's report-and-park handler (D1148) likewise must not save registers to data memory, since the
overlays are being written there.

**Suggested resolution:** write the ROM out in 31 as a short pseudo-listing:

1. set `mtvec`;
2. branch on `PARAMS`' endpoint id;
3. tiles: pulse `done`, then loop `WFI` and load `EVENTS` until GO is seen;
4. C&C 1–3: set the mask to [6], then loop `WFI` and load `EVENTS` until [6] is seen, then restore the mask;
5. load slot 0, load the address from slot 1, jump.

### W1-34: the one watchdog and the toast pin are underspecified

**Decisions:** 28 (D1074, D1080), 35 (D1228–1230, D1242), 27 (D1047).

The watchdog is defined in two places that don't agree:

- **28:** `WATCHDOG` is the *phase* watchdog's limit. A write, or `PULSE_GO`, restarts it, and its expiry is
  an event to C&C 1.
- **35:** it is the *keep-alive* watchdog, whose expiry raises the toast pin. One watchdog is only
  *proposed* (D1242).

WORKER4 must model it in phase 3 and has to guess four things:

- **The reset value.** The watchdog must be **disabled** after reset; otherwise it raises toast while C&C 1
  is still being booted (W1-32), which can take any time.
- **What a check-in is:** a write to `WATCHDOG`, `PULSE_GO`, or both.
- **The count:** its units, its width, and whether it reloads or counts down from the last value written.
- **What expiry does:** the event, toast, or both, and whether toast **latches** until reset. A live toast
  would fall if C&C 1 checked in again.

The toast pin's input also needs one line. 35 says "C&C 1's error" covers every error, the unit-error bit
included (D1229–1230), but 28 still says the error pin "excludes exactly that bit" (D1080).

**Suggested resolution:** settle these in 28 or 35:

- one watchdog;
- `WATCHDOG` = 0 at reset means disabled;
- writing *N* > 0 loads the count, and `PULSE_GO` reloads the last *N*;
- reaching 0 posts the event and latches toast;
- toast = (C&C 1's controller `ERRORS` ≠ 0, [11] included) **or** the watchdog expired, latched until the
  reset;
- delete D1080's exclusion.

## 4. Short list: wording and nice-to-haves

- **The `WORKER4/PROMPT.md` recovery line.** Line 49 says "21's recovery flow is superseded except where 34
  and 35 keep it". 34 is itself superseded by 35, and its self-reset must not be built. Suggest: "21 and 34
  are superseded except what 35 keeps".
- **The `WORKER4/PROMPT.md` error pin.** Phase 3 cites 24 for the pins. Add "35's toast pin replaces 24's
  error pin", so only one hardware pin is built.
- **Ending a test.** D775 leaves meshsim's way of ending a test to meshsim. About 23 places in
  `wip/meshsim/src` and `test` use `HALT_EBREAK` or `allEbreak`, and `ebreak` is now a loud stop (27). The
  WORKER4 prompt could point at D775, so the simulator-only exit is chosen before the suites are rewritten
  (perhaps a word beside the trace word, 32).
- **What a soft reset keeps in meshsim.** 35's keep list is hardware (AXI, LPDDR, LVDS). In meshsim it
  presumably means the LPDDR model's contents survive and everything else resets. This is worth one line
  for WORKER4.
- **D1047, a remnant.** "Only the phase watchdog notices" becomes the keep-alive watchdog (35).
