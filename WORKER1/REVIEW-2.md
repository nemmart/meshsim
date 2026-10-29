# WORKER1 review, round 2: the resolutions (decisions 19–24)

This round checks how decisions 19–24 resolve round 1's findings, and reviews those sections as new design.
**D***n* is a line of `wip/planning/DECISIONS-v1.2.md` at commit 21ae57a, and **S***n* a line of
`wip/spec/machine-spec.md`. Anything inferred is marked *(inferred)*.

## 1. Summary: the five points that matter most

1. **W1-19 (blocker):** the memory system and LPDDR have no place in the reset story. Their in-flight reads land in units rebooting from ROM, a dead tile can hold the write token, and the flushed logs may not survive the reset that follows the flush.
2. **W1-18 (issue):** a trap with no `ERRORS` bit (a fetch fault, `ecall`/`ebreak`, a fault inside the handler) raises no error line. C&C 1 is never interrupted, and on C&C 1 itself the error pin stays low.
3. **W1-20 (issue):** the reset boundary at C&C 1 can tear the request bus's framing and strand credits. It can also deliver C&C 1's stale outgoing packets into units sleeping in ROM, and a stale write to command slot 0 makes the ROM jump to a garbage address.
4. **W1-21 (issue):** C&C 1's own state survives the "restart": counters that can't be cleared, sticky events, READY bits, queued packets and requests. One late counted arrival makes C&C 1 fatally failed.
5. **W1-23 (issue):** the boot ROM's contract is undefined, and the obvious version leaves "command landed" set after the jump. The image's first `done` then fires READY at once, before C&C 1 has sent a command.

## 2. Status of round 1's findings

| finding | status | evidence |
|---|---|---|
| W1-01 errors in a halted unit | **closed** | there is no halted unit (23, D764–773), and the reset clears every error |
| W1-02 interrupt semantics | **closed** | the interrupt is level-sensitive and gated by `MIE`/`mie`; `MIE` stays 0 after a trap; the park is a plain loop (20, D623–633) |
| W1-03 trap state at reset | **partly closed** | reset gives `MIE` = 0 (D670) and the ROM sets `mtvec` (D751). The CSR reset values are still Q1, and the ROM's contents are unspecified (W1-23) |
| W1-04 torn sequences | **closed** | nothing resumes (D615–620), and the handler writes every word of what it sends (D634) |
| W1-05 Q3 | **closed** | nothing stops before the reset (D679). The cost, engines running through the drain, is in W1-26 |
| W1-06 halts with packets in flight | **partly closed** | the reset clears the domain (D670), but traffic crossing its boundary is not covered: W1-19, W1-20 |
| W1-07 READY and health | **partly closed** | true only for traps that set an `ERRORS` bit. A tile that traps after `done` with no `ERRORS` bit still fires READY and raises no line (W1-18) |
| W1-08 halting cuts off reports | **partly closed** | the drain and the fixed report set (D705) answer it. Handler re-entry (D637) and the ROM's handler break the fixed count (W1-23, W1-26) |
| W1-09 mass failure | **partly closed** | draining is acknowledged (D729). The boundary queues and the timeouts are still only proposed (D736–740) |
| W1-10 C&C 1's permanent errors | **partly closed** | UART line errors are now status (D710). The host's reset path is undefined (W1-27), and C&C 1 can be failed by other units' errors (W1-22) |
| W1-11 `ecall`/`ebreak` | **partly closed** | the decision is clear (D639). Its consequence, that no line rises, is not addressed (W1-18) |
| W1-12 the `done` gate | **closed** | the park is a plain loop, not `WFI` (D629) |
| W1-13 line timing | **partly closed** | the reset hold covers the error-line latency (D714). The release has no latency or pacing rule (W1-25), and the timing bullets still describe run lines (W1-28) |
| W1-14 C&C 2/3 boot | **closed for C&C 2 and C&C 3** | they boot from ROM with a marker (22). C&C 1's own boot is still undefined (W1-27) |
| W1-15 Q4's missing items | **closed** | there is no HALT state (23). The boundary items move to W1-20 |
| W1-16 the pin | **closed** for Q2 | the error pin never pulses (D791–796). What the host sees in each failure is in W1-27 |
| W1-17 handler details | **closed** | handler conventions (D644–650) |

## 3. New findings

### W1-18: traps with no `ERRORS` bit are invisible to C&C 1

**Severity:** issue. **Decisions:** 20 (D637–643), 21 (D674–681), 24 (D791). **Spec:** 4.11 (S773).

The error line, C&C 1's interrupt and the error pin are all driven from `ERRORS`. Several traps set no `ERRORS` bit:

- `ecall` and `ebreak` (D639);
- an instruction fetch outside program memory and the boot ROM;
- a jump to a target that isn't 4-aligned *(inferred: RISC-V without C faults it)*;
- a second trap inside the handler (D637).

Bus errors and illegal instructions are covered by [9] and [10]. For the uncovered traps:

- **In a tile:** the handler's reports reach C&C 1's receive queue, and nothing interrupts C&C 1. If C&C 1's main loop doesn't pop requests, the reports back up the request-bus tree and stall the tile's row (S219). The tile is found only by the watchdog. If it trapped after `done`, its READY still fires and C&C 1 sends GO past it (W1-07).
- **In C&C 1:** C&C 1 is dead, but the error pin stays low (24 excludes nothing relevant here; there is simply no bit to see). The host sees a quiet, healthy-looking chip.

20 accepts `ecall` and `ebreak` because "we don't use them", but it doesn't say how the failure is noticed, and the other cases in the list are bugs, not choices.

**Suggested resolution:** one sticky `ERRORS` bit, "trap taken", set by the hardware on every trap entry, synchronous or not. It costs one flop and needs no ISA decoding (ask Tony). Every trap then raises the line on a tile and the pin on C&C 1, even when the handler is broken. `mcause` in the reports still says which trap it was.

### W1-19: the memory system and LPDDR across the global reset

**Severity:** blocker. **Decisions:** 21 (D658–671, D683–686). **Spec:** 3.3 (S208), 6.7 (S1844–1846, S1881), item 60.

21's reset domain is tiles 0–63, C&C 2, C&C 3 and their peripherals (D660). It doesn't place the memory system. In v1.1 the memory system is "owned by C&C" and served at C&C's port (S1846); 3.3 makes target 3 the Updater/Binder/RW engine (S208), which 15 makes C&C 2's peripheral. Either way, the recovery has gaps.

**If the memory system is outside the domain:**

- reads it is still answering deliver after the release into units asleep in their boot ROMs;
- a counted delivery sets unexpected arrival, which triggers another recovery, which is a loop *(inferred)*;
- an uncounted delivery to controller memory overwrites the image being loaded;
- a write token granted to a tile that has failed is never returned, and "a tile that never streams … stalls every later write on the chip" (S1881).

**If it is inside the domain:**

- the reset discards its cache, so the logs the flush has just "written to LPDDR" (D683) can be lost in dirty lines;
- resetting the LPDDR controller stops refresh and the PHY's training, so LPDDR's contents, the logs included, may not survive *(inferred)*.

**Suggested resolution:** decide the memory system's side of the reset explicitly. If it is outside, the reset must cancel its outstanding reads and its token, or C&C 1 must wait until its queues for the domain are empty. If it is inside, the flush must end with a cache write-back and LPDDR in self-refresh, and the LPDDR controller and PHY stay outside the reset. The same applies to the Updater/Binder/RW engine if it is the memory system.

### W1-20: the reset boundary at C&C 1

**Severity:** issue. **Decisions:** 21 (D660–666, D736–738). **Spec:** 3.3 (S213, S218), 4.18, 6.6 (S1832).

21's proposed fix is that C&C 1 "empties the queues it holds facing the reset domain" (D736). That covers C&C 1's receive side, but not these cases:

- **Request framing.** A request is 8 beats with no header and no size (S213). If the tree (in the domain) resets while a request to or from C&C 1 is part-way through, C&C 1's receiver is left with a torn request. In the other direction, C&C 1's request port, which enters at the root, continues the rest of a request as the start of the next. After that the framing is wrong for good. This is not a queue that can be emptied.
- **Credits.** The root's credit counter for target 0 resets to 16 (S218) while C&C 1's receive queue may still hold requests. Draining first fixes this only if nothing arrives during the reset, so the drain must come after the domain is held in reset, not before.
- **C&C 1's outgoing path.** Packets C&C 1 sent just before asserting the reset sit in its send holding register, or its FIFO toward tile 0's fifth port. They are delivered after the release, into units in their boot ROM. A late phase command to slot 0 sets "command landed", and the ROM jumps to whatever address the stale command block holds (W1-23).
- **No status to wait on.** C&C 1's `STATUS` gives `send_busy` and `request_busy` (full), but nothing says its request FIFO or its outgoing link FIFO is empty (18, D540–552).

**Suggested resolution:**

- Put every FIFO on C&C 1's side of a boundary link in the reset domain, or clear it with the reset line.
- Make the request port and the receive queue commit whole requests only, so a reset can never leave half of one.
- Order C&C 1's recovery as: stop sending; wait for an "outgoing idle" status (a new `STATUS` bit); assert the reset; drain; release.

### W1-21: C&C 1's own state survives its restart

**Severity:** issue. **Decisions:** 21 (D699–704), 3 (D133–144). **Spec:** 4.12 (S833).

C&C 1 "resets its stack, sets `mstatus.MIE` again and enters its main loop" (D700). Everything else in C&C 1 is outside the domain and keeps its pre-failure state:

| state | consequence |
|---|---|
| **Write counters** | they can't be cleared: a store adds, and they return to 0 only by counting down or at reset (3). A counter armed for a reply from C&C 2 or the memory system, which the reset has now killed, stays non-zero for good. A late answer arriving after C&C 1 has gone back to 0 is an unexpected arrival, which is a C&C 1 error: sticky, fatal, and the error pin (D703) |
| **`EVENTS`' sticky events and `EVENTS_MASK`** | carry over into the main loop |
| **`READY_LINES_0`–`2`** | stale bits from the failed phase. How they clear is still open (D727) |
| **The mesh receive FIFO** (S1832) | holds results and errors from the failed run |
| **C&C 1's request FIFO and receive queue** | see W1-20 |
| **The peripheral's DMA and Ingress/Egress** | may be in the middle of a transfer |

**Suggested resolution:** a C&C 1 "soft restart" that resets its controller-side state (core, counters, events, READY bits, receive FIFO, request port) but not the host link, perhaps the same reset line with C&C 1's peripheral excluded. Failing that, a written restart checklist, and a way to zero a counter on C&C 1.

### W1-22: errors at C&C 1 caused by other units become fatal C&C 1 errors

**Severity:** issue. **Decisions:** 21 (D703, D710), 24 (D791–797). **Spec:** 6.6 (S1832), 6.7 (S1888), item 75.

C&C 1's own errors are permanent and light the error pin, and 24's answer is a chip reset (D797). Some of the errors C&C 1 can raise are caused by other units:

- a counted packet sent to dst 64 by a buggy tile, which is unexpected arrival at C&C 1;
- C&C 1's receive FIFO overflowing under a tile's flood (S1832);
- a mode-3 packet without the token (S1888, item 75);
- a request for a reserved target, dropped at the root (S1888).

Today the last three set "C&C error flags". Whether those flags are C&C 1 `ERRORS`, and so fatal, is round 1's owner question 6, still unanswered. As written, a bug in one tile can force a power cycle.

**Suggested resolution:** classify errors at C&C 1's boundary that another unit causes as unit errors: status bits, or a second live bit beside the unit-error bit, excluded from the pin. Keep the pin for C&C 1's own code and hardware.

### W1-23: the boot ROM's contract

**Severity:** issue. **Decisions:** 22 (D744–762), 16 (D497), 6 (D241–248), 21 (D705). **Spec:** 6.3 (S1761–1767).

22 lists the ROM's job as "set `mtvec`, sleep in `WFI` until 'command landed', then jump to the address in the command block" (D751). Seven things are missing:

- **The wait needs a mask change.** `EVENTS_MASK` resets to GO | counters done (D497), so command landed ([5]) doesn't wake `WFI`. The ROM must set the mask, and restore the reset mask before jumping, since `mesh.h`'s helpers assume it (4.12).
- **"Command landed" stays set after the jump.** Only GO and the reset clear it (6.3, S1761–1767). The image's first `wait_for_command()` pulses `done`, and READY fires at once with no command sent. If C&C 1 counts that READY toward its first real command, it can send GO before that command lands, and the tile then reads the stale marker. Either the ROM's jump clears "command landed", or the boot READY is part of the protocol. Pick one.
- **`done`'s reset value** is unstated. It used to be forced to 1 in HALT (S1762).
- **Where the address is.** The command block slot, the parity rule (alternating argument blocks, 6.3) and whether the ROM checks the command word, as `loader.S` checks for `CMD_JUMP`.
- **A write covering slot 0** sets "command landed" (6, and the open detail at D248). An image whose data reaches the command block, or any stray uncounted write there, starts the unit on a partial image.
- **A counted stray while in ROM** sets unexpected arrival. `MIE` = 0, so the ROM doesn't trap, but the line rises, and C&C 1 starts a recovery during boot, possibly in a loop (W1-26).
- **The ROM's handler.** 21 fixes the report set and has C&C 1 count against it (D705), so a ROM handler must send the full set. That set includes each present peripheral's window, about 150 instructions *(inferred)*, not "a few dozen bytes" (D762). It is also frozen in silicon, which freezes the report protocol.

**Suggested resolution:**

- Specify the ROM fully, in the spec: mask set and restore, slot and command check, clearing "command landed" before the jump, and `done`'s reset value.
- Give the ROM handler its own one-report op (`mcause`, `mepc`, `mtval`, `ERRORS`), with the report count fixed per op rather than globally.

### W1-24: the marker can overtake the image

**Severity:** issue. **Decisions:** 22 (D756), 23 (D777–781). **Spec:** 3.2 (S157–158), 6.3 (S1771).

22 relies on "packets from one source to one destination arrive in order" (D756). 3.2 guarantees that **within one kind only**: a unicast and a broadcast can take different paths and arrive out of order (S158). The command convention requires a command's packets to be all one kind (S1771).

The natural boot mixes the two: a broadcast image for the 64 tiles, then per-unit unicast markers. That is exactly 23's "starting units one at a time" (D777). An image delivered by another source, such as the memory system or C&C 2's engine, with C&C 1's marker, has no ordering guarantee at all.

**Suggested resolution:** a rule that the marker has the same source and kind as the image's last packet. Better still, the marker carries the image's length and checksum and the ROM checks them, which also catches W1-20's stale packets.

### W1-25: release timing and pacing

**Severity:** issue. **Decisions:** 21 (D665, D714–721), 23 (D769). **Spec:** 5 (S1706), 6.6 (S1833), 10 (S2200).

23 removes `run_latency` (D769), and with it 6.6's "wait `run_latency` + 1 after a RUN/HALT store before sending" (S1833). Nothing replaces it. A packet C&C 1 sends before every router on its path has left reset is lost. The image is uncounted, so the loss is silent and the unit jumps into a hole. 21 says only "tens of cycles" for the hold (D666), which must cover:

- the reset line's propagation and skew across the die;
- the error-line latency (D714);
- big SRAM's in-flight accesses, which 19 says still complete (D582).

21's timing bullets still describe run lines (D719–721).

**Suggested resolution:** two parameters, `reset_latency` (assertion and release, with skew) and the hold, and a pacing rule: C&C 1 sends nothing until `reset_latency` + 1 after the release.

### W1-26: loops in recovery

**Severity:** issue. **Decisions:** 20 (D637), 21 (D683–697, D705–709, D739).

Four ways the recovery can fail to finish, or finish wrongly:

- **Handler re-entry.** A trap inside the handler restarts it at `mtvec` (D637). If the fault is deterministic, each pass resends the reports from the start: an unbounded stream from one tile. That breaks C&C 1's per-unit count, keeps the drain from ever ending, and holds the flush's requests behind it (D729). *Fix:* the handler sets a flag on entry with an x0-relative store, and parks at once if the flag is already set.
- **Persistent faults.** A hardware fault, or an error raised in ROM (W1-23), means reset, boot, error, reset, with no end. *Fix:* C&C 1 keeps a retry count, then gives up, shows a code on the software pin and tells the host.
- **Cascades.** Healthy tiles keep running their phase (Q3's answer) and may fail in turn: done early, unexpected arrival. That lengthens the drain and hides the first failure. *Fix:* C&C 1 snapshots `ERROR_LINES` when it takes the interrupt, as the "first failure" record.
- **The flush.** Healthy tiles keep `printf`ing into their logs (23) during the flush, so "flush complete" must mean a snapshot, not "logs empty".

### W1-27: the pins, the host's view, and C&C 1's own reset and boot

**Severity:** question. **Decisions:** 22 (D746), 24 (D787–803). **Spec:** 6.6 (S1818).

What the host sees in each failure:

| failure | error pin | software pin | host learns |
|---|---|---|---|
| a unit's error, recovered | low | whatever C&C 1 shows | only if C&C 1 tells it (open, D741) |
| C&C 1 `ERRORS` | high, steady | frozen | yes |
| C&C 1 traps with no `ERRORS` bit (W1-18) | **low** | frozen | no |
| C&C 1 hangs (a loop, a stuck store) | **low** | frozen | no |
| a recovery loop (W1-26) | low | busy | no |

"C&C 1's errors except the unit-error bit" is well defined if the pin taps C&C 1's `ERRORS` other than its peripheral bit, OR'd with the peripheral's own `ERRORS` other than the unit-error bit. Two rows of the table still leave the host blind.

Two related gaps:

- **No host reset is defined for C&C 1.** "The host steps in (chip reset)" (D797), but C&C 1 is outside the global reset, and no input lets the host reset C&C 1.
- **C&C 1's boot is undefined.** 22 gives every controller a ROM (D749), but C&C 1's image comes from the host through Ingress/Egress (6.6, S1818), its own peripheral. Nothing says what C&C 1's ROM waits for, or who writes its command block.

**Suggested resolution:** a software-pin heartbeat convention (the host treats a frozen pin as a hang), a chip-reset input that also resets C&C 1, and C&C 1's ROM and boot path in 22.

### W1-28: remnants in the log contradict section 23

**Severity:** issue. **Decisions:** as listed.

The decisions log still states HALT behaviour in several places, and an implementer reading section by section would build it.

| line | remnant |
|---|---|
| D69 | 1.4: "Only HALT recovers the tile" |
| D87–90 | 1.5: "C&C 1's HALT halts everyone but itself" and the pin argument; not marked superseded |
| D145 | 3: "HALT flushes the delay line" (now the reset) |
| D161 | 3: "staging into a halted tile" |
| D320, D557 | 10 and 18: `ERRORS` "sticky until HALT" |
| D619, D624 | 20: "Only HALT → RUN restarts the controller"; "stays high until HALT" |
| D668, D692 | 21: "Units come out of reset halted… sets their run bits"; "Per-unit run bits remain" |
| D681 | 21 step 3: `OR(ERROR_LINES & RUN_LINES)` |
| D716 | 21: "The run-bit mask (19) makes a stale line harmless anyway". **This claim is now false**: 23 removed the mask, so only the hold protects C&C 1's `MIE` re-enable (D701) |
| D719–724 | 21: run-line latency, three boot stores, the HALT-edge loss, `READY_LINES` bits 65 and 66 |
| D204 | 5: "The controller sees 0x00, 0x01 and 0xFF only", but it must fetch from the ROM at 0x02. Whether a packet to region 0x02 is a bad packet is unstated |
| D198–199 | 5: the region table's 0xFF row sits after the italic note, outside the table |

**Suggested resolution:** mark each superseded in place, and reword D716 to rest on the hold alone.

### W1-29: `PULSE_GO` and clearing the READY bits

**Severity:** question. **Decisions:** 8, 11 (D365), 21 (D723–727).

GO is `PULSE_GO`, in the controller's register block (8, 11), while the READY bits are now in C&C 1's peripheral (`READY_LINES_0`–`2`, D723). v1.1 cleared the READY bits with one store, keeping a pulse that arrived in the same cycle (6.6). That clear now needs a home, and it should be ordered with GO.

**Suggested resolution:** one store in C&C 1's peripheral that clears `READY_LINES` and pulses GO in the same cycle, plus an "expected units" mask for the "all ready" event (D727). The mask is what 23's one-at-a-time bring-up needs. `PULSE_GO` in the tile block then stays unwired, as 8 already allows.

### W1-30: units started one at a time, and the replayer's ownership rules

**Severity:** nit. **Decisions:** 23 (D777–781). **Spec:** 4.19.10, items 54 and 76.

- **Sleeping units still receive.** A running unit's counted traffic to a unit still in ROM is unexpected arrival, which triggers a recovery. Bring-up programs must not address sleeping units.
- **The replayer's rules are keyed to HALT.** Its "in HALT" ownership rules (item 54: memories take boot loads, a queue-port write is a bad peripheral write) have no state to key on now. They should be restated for an idle peripheral, `periph_running` = 0, which item 76 already covers for queue pushes.

## 4. Spec sections to update, as changed by sections 19–24

| section | change |
|---|---|
| 2 (S75–79) | RUN/HALT goes; add the global reset line and its domain, the error lines into C&C 1, the READY lines into C&C 1's peripheral, and the two status pins |
| 2.1, 3.3 | where the memory system sits relative to the reset (W1-19) |
| 3.3, 4.18 (S1131) | the request port and the receive queue across the reset: whole requests only, credits (W1-20); "HALT keeps the receive queue" goes |
| 4.11 (S746–795) | the boot ROM at region 0x02, the reset vector 0x02000000; the resident loader at 0x1F00 and "where HALT → RUN starts" go; traps terminal; the handler conventions; the CSR reset values |
| 4.12 (S829, S833, S892) | every HALT rule becomes a reset rule: counters, events, `EVENTS_MASK` ([4] \| [3] at reset); "a halted tile counts nothing" and "HALT is the recovery path for a stuck count" go |
| 4.13 (S941, S946) | errors sticky until the reset; the same-cycle rule against the reset; the handler and parking (20) |
| 4.14 | "boot loads arrive while the tile is halted" becomes loading in ROM |
| 4.17.7, 4.19.10, 4.19.11 | "HALT holds the peripheral in reset" becomes the reset; nothing stops a failed tile's peripherals (21); the ownership rules keyed to HALT (W1-30) |
| 4.18 | "HALT" paragraph becomes the reset |
| 5 (S1706), 10 (S2200) | `run_latency` goes; add `reset_latency`, the reset hold and the error-line latency (W1-25) |
| 6.1 (S1720–1735) | rewritten: the global reset and its domain, the boot ROM ("There is no boot ROM", S1732, is reversed), what reset clears; "no packet in flight" goes |
| 6.2 (S1737–1757) | boot from ROM with a marker (22); "halt a tile before an uncoordinated write" goes (also in 4.12); staging "while halted" goes |
| 6.3 (S1761–1767) | `done` "forced to 1 in HALT" and "cleared by HALT → RUN" go; "command landed" after the ROM's jump (W1-23); READY for the phase cycle only |
| 6.6 (S1815–1840) | C&C 1 as recovery manager (21); REPORT and "C&C then halts the machine" go; the RUN/HALT register becomes `RESET`; READY bits and GO (W1-29); the pacing rule becomes the release rule (W1-25); the pins (24); the host link (W1-27) |
| 6.7 | the memory system across the reset (W1-19); the C&C error flags (W1-22) |
| 8.2 (S1978) | `done` "forced to 1 in HALT" goes |
| 9 | invariant 7 is withdrawn or restated for the reset; invariant 15's "C&C's error bit"; invariant 16's replayer clause on runs |
| 13 | items 54, 64, 91 and 109 (HALT rules); item 11 (C&C's register block: `RESET`, `READY_LINES`, `LED`) |

## 5. Questions for the owner

1. **The memory system and LPDDR across the reset** (W1-19). Which side of the domain are they on, and what makes the flushed logs survive the reset?
2. **A hardware "trap taken" error bit** (W1-18), so every trap raises the line and the pin? This partly reverses 20's "no hardware for `ecall`/`ebreak`" (D639).
3. **Errors at C&C 1 caused by other units** (W1-22). Are they fatal C&C 1 errors, or unit errors? Round 1's question 6, on the C&C error flags, is the same question.
4. **The host's view** (W1-27). Is there a chip-reset input that resets C&C 1? How is C&C 1 itself booted? Should C&C 1 report each recovery to the host (D741)?
5. **The boot READY** (W1-23). Should the ROM clear "command landed" before jumping, or is the image's first READY part of the boot protocol?
6. **A C&C 1 soft restart** (W1-21): worth a reset input of its own, excluding the host link?
7. **Retries** (W1-26). How many recoveries before C&C 1 gives up?

For Tony: the CSR reset values (Q1, still open), and whether a "trap taken" bit on trap entry is free in his core (W1-18).
