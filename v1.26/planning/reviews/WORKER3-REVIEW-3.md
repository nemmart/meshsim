# WORKER3 review, round 3: sign-off before implementation

Scope: registers, peripherals and the software. Citations: **D l.*n*** is a line of
`wip/planning/DECISIONS-v1.2.md` at commit 50f6f46; **S l.*n*** is `wip/spec/machine-spec.md`.

## 1. Verdict

**Ready to implement, once three points are fixed. Each is a sentence or two in the log; none needs a design change.**

- **W3-34:** section 31's step 5 (arm, then READY, then the counted big-SRAM data) trips the done-early error.
- **W3-35:** the keep-alive watchdog's reset state and the "all tiles READY" event are undefined, and a literal reading raises the toast pin at power-on.
- **W3-36:** the WORKER4 prompt doesn't say whether C&C 1 is a programmed core or meshsim's behavioural model. It also points WORKER4 at the wrong section of my inventory.

Everything else in my area is consistent: the map, the standard window, the counted credits, the header block and the struct header. My round-2 findings are all closed or deliberately deferred.

## 2. Status of round-2 findings

| finding | status | evidence |
|---|---|---|
| W3-24 credits leak | **closed** | Credits are set to 2 at `PULSE_RUN` and at each new expansion; code and log refills are full halves; idle queues are masked out (D10 l.407-415). An exhausted expansion's bit is masked like any idle queue, under the "nothing more to send" rule (l.413-415). |
| W3-25 queue-of-items event | **deferred** | D33 l.1177-1178; not needed for the sort or the replayer. |
| W3-26 reset values, GO into the ROM | **closed** | The reset zeroes `done`, the gate, "command landed", pending GO and every event (D31 l.1119-1121); tiles' ROM waits for GO (l.1125-1127). |
| W3-27 C structs | **closed** | Pointers, the standard-members macro, `…Regs` names (D14 l.541-546). Wording leftovers are in section 4. |
| W3-28 C&C 1's registers | **closed** | D28 revised as suggested. D35 adds the semantics in W3-35. |
| W3-29 alignment of runs | **closed** | D32 l.1162. |
| W3-30 read-port misalignment | **closed** | [1], dropped but still counted (D32 l.1160). |
| W3-31 stale text | **closed** | Except D4, now wrong the other way (section 4). |
| W3-32 trace word | **closed** | A named simulator-only word (D32 l.1164). |
| W3-33 stream count | **closed** | 17 bits (D30 l.1107-1110). |

## 3. Findings that block implementation

### W3-34 — Boot step 5 arms counters before READY: done early, and no done event (blocker)

*D31 l.1128-1130; D18 [8] (l.702); D16 l.594; S 4.12 l.848, l.890; S 6.2 l.1754.*

Step 5 reads: "C&C 1 broadcasts how many words it will send and lands a command; each tile arms its counters and signals READY; C&C 1 sends the data counted."

A tile signals READY only by pulsing `done`. That conflicts with two rules still in force:

- **Done early.** Pulsing `done` while a counter is non-zero is the done-early error (`ERRORS` [8], a hardware check, D27 l.1036). The error traps, so every tile fails at boot.
- **The `done` gate.** If the error weren't raised, the data would arrive inside the gate, where counter done events cannot be set (S l.848, l.890). The image's wait for its counter would then never wake.

v1.1 solved the same problem in `CMD_STAGE` (S 6.2 l.1754):

1. The tile arms its counters in its start path, *after* GO.
2. C&C sends the data no sooner than `stage_delay` (32 cycles) after GO.
3. The tile waits for its counter done, then calls `wait_for_command()`.

*Suggested resolution:* restate step 5 as v1.1's announced staging. The command says how many words; after GO the tile arms; C&C 1 waits `stage_delay` before sending; the tile's READY then means the data has landed. WORKER4's harness and WORKER5's `main.c` both follow it.

### W3-35 — C&C 1's watchdog and "all tiles READY" need defined semantics (issue)

*D28 l.1072-1074, l.1081; D35 l.1228-1229, l.1242-1243; D31 l.1119.*

WORKER4 must model both, and the log leaves two states undefined.

**The watchdog.**

- The reset zeroes everything that isn't memory (D31), so the limit and the count both start at 0. Read literally, "the toast pin rises when the watchdog reaches zero" (D35) raises it at power-on, before C&C 1 has run an instruction.
- With one watchdog (D35's proposal), its expiry both raises the toast pin and posts D28's "watchdog expired" event.
- Since `PULSE_GO` restarts the count, the limit must cover the longest phase. Otherwise C&C 1, asleep waiting for READY, can't check in.

*Suggested resolution:*

- A limit of 0 (the reset value) means off.
- A write of a non-zero limit starts the count, and each check-in (a write, or `PULSE_GO`) restarts it.
- Expiry raises the toast pin, and also posts the event.
- C&C 1 sets the limit per phase, above that phase's expected length.

**"All tiles READY".** D28 defines it as `READY & EXPECT == EXPECT` without saying whether it is live or counted, or what `EXPECT` = 0 means. After the reset `EXPECT` is 0, so the condition is true. Two things must be fixed:

- If the event is set only when a READY arrives, C&C 1 can write `EXPECT` after its tiles are already READY, and the event never fires.
- If it is a level that is true while `EXPECT` is 0, it is true from the moment of reset.

*Suggested resolution:* make it a **live** bit: (`READY & EXPECT`) == `EXPECT` **and** `EXPECT` ≠ 0. It then falls when `PULSE_GO` clears the READY bits, and C&C 1 can't miss it whatever order it writes `EXPECT` in.

### W3-36 — The WORKER4 prompt: what C&C 1 is, and a wrong pointer (issue)

*`WORKER4/PROMPT.md` (INPUTS, WHAT TO BUILD 3-4); D31 l.1135-1137; D33 l.1173.*

**What C&C 1 runs in meshsim is unstated.**

- Phase 3 asks for C&C 1's registers (D28); phase 4 asks for the boot sequence (D31).
- The ROM's branch covers only tiles 0-63 and C&C 2/3, and C&C 1's own boot is deferred (D33).
- In v1.1, C&C was a behavioural Java model (`CnC.java`), not a RISC-V core.

Without a ruling, WORKER4 may start building a programmed C&C 1 and firmware that doesn't exist yet.

*Suggested resolution:* say that in v1.2 meshsim, C&C 1 stays a behavioural model. It drives D28's registers' semantics (READY, `EXPECT`, `PULSE_GO`, the error lines, the watchdog, the toast pin) from the test harness, and C&C 2/3 likewise until their firmware exists.

**The prompt points at the wrong section.** It sends WORKER4 to "`WORKER3/REVIEW-2.md`, section 4: the migration inventory". The inventory is section **6**; section 4 is the findings.

Two of the inventory's rows are also superseded by D31:

- `main.c`'s `CMD_STAGE` row ("staging lands uncounted by address") is now counted staging, per W3-34.
- `loader.S` simply goes: the boot ROM is a minimal `wait_for_command()`.

## 4. Short list: wording and small things

- **D31 l.1144-1145:** "the boot ROM uses registers only, never data memory" contradicts reading the jump address from the command block, which is in data memory. It should say: no stack or variables in data memory, and the command block is read only after GO.
- **D31 l.1145:** "the reset selects command block 0" should say what it means: the first command has parity 0, so the jump address is in slot 1. Otherwise it reads like a hardware block selector that doesn't exist.
- **D31 l.1136:** the C&C 2/3 ROM path should consume `EVENTS` [6] and restore `EVENTS_MASK` to its reset value before jumping, as tiles' `wait_for_command()` leaves it.
- **D28 l.1079-1080:** "the error pin excludes exactly that bit (24)" is superseded by D35. The toast pin covers every error; mark it.
- **D4 l.182:** "87 used" is 88 again with the 17-bit count (24 + 32 + 32).
- **D14 l.538-540, l.548:** still say `ReplayerPeripheral` and `tile.EVENTS`; l.541-546 say `ReplayerRegs` and `tile->EVENTS`. Mark the earlier wording as superseded so the generator follows the later one.
- **D30 l.1101:** counter codes 5-7 "undefined in hardware" is superseded by D32 l.1163 ("accepted and treated as uncounted"); mark it.
