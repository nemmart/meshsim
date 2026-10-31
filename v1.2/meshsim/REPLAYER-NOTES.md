# The replayer: model, controller and harness: notes

The replayer, peripheral type 1 (spec 4.17, 4.19; `spec/replayer-opcodes.md`), as a meshsim unit, with its tile controller program (Stage B) and the harnesses that check both against the tracer's deliveries.

> **Status: final for this milestone (replayer-cp14, 2026-10-27).** Spec: mesh-sorter-v1.1-2026-10-26p, plus 26q's operand code 145. Gold standard: jolt-cpp's interpreter rows (item 113).
> - **Stage A** (the model, an ideal controller): every delivered window of deliveries 1–3 matches byte for byte in all three record variants, 240 window runs and 15,728,640 rows, with streams consumed exactly and end states equal to `end.bin`.
> - **Stage B** (`replay_ctl` on meshsim's v1.1 tile, interim 2026-10-27): the timing windows match byte for byte.
>   - EVM Zone 2²⁹ windows 53 and 54: 3.479 and 3.492 cycles per row (Stage A 3.27).
>   - Cyclone input/25 windows 78, 83 and 84: 3.090 (Stage A 3.005).
> - **Controller tuning in software is closed.** The remaining gap is proposal P1, a spec change, with N12 tied to it, for a later milestone.

## Files

| file | what |
|---|---|
| `src/Peripheral.java` | the slot's interface (4.17): what the tile calls; shared with the meshsim session |
| `src/Replayer.java` | the replayer, cycle by cycle, implementing `Peripheral` |
| `src/ReplayerAlu.java` | what a row computes (appendix sections 1–5), pure functions |
| `test/ReplayerTest.java` | the suite: the ALU against `vec.tsv`, then the unit tests |
| `test/ReplayerUnitTests.java` | the parts, and small hand-checked passes |
| `test/ReplayWindow.java` | one window's inputs; builders; the stream encoders of 4.19.15 |
| `test/ReplayDriver.java` | Stage A's harness: a replayer fed by an ideal controller through the interface only (both N3 cases as the controller sends them) |
| `test/ReplayDelivery.java` | Stage A's loader and byte-for-byte comparison against a tracer delivery (manifest, program files, record variants, `rows.bin`, `end.bin`, `diag.bin`) |
| `sw/replay/` | the Stage B controller program (`replay_ctl.c`, cp12's fast path), its local register header, the big-SRAM layout, start-up, linker script, build and README |
| `guests/item113/` | the item 113 differential guests (RV64 assembly, two-segment link) and their expected jolt-cpp rows |
| `REPLAY-INPUT-REQUIREMENTS.md` | what the harness needs from the tracer's format (as delivered) |
| `UPSTREAM-CHECK.md` | the brief upstream check (item 113) |

**Run:**
- **Unit tests:** `javac --release 17 -d build src/*.java test/*.java && java -cp build ReplayerTest ../tools/replayer-opcodes/vec.tsv`
- **Against a delivery:** `java -Xmx2g -cp build ReplayDelivery <delivery> state|state-lru|state-pick [program ...]`
- **Stage B:** see meshsim's `test/StageB.java`; the controller builds with `sh sw/replay/build.sh`.

## Wiring it into the tile (for the meshsim session)

- The tile holds a `Peripheral` (null for type 0) and calls `runCycle(t)` **every cycle, in HALT too**: the write port lands boot loads in HALT.
- The director hands each mode-9 packet to `accept(t, src, addr, size, lo, hi)` (at most one per cycle).
- `TILE_PULSE` [2] → `run(t)` at the store's cycle; `periph_running` is `running(t)`.
- A register load issued at t with latency L reads `lines(t + L − 1)` and `status(t + L − 1, i)`. A consuming `WAKE_EVENTS` load calls `claim(t + L − 1, lines it returned under WAKE_MASK)`. `WAKE_PEEK` never claims.
- `DMA_PERIPH_REMAINING` −= `landed(t)` each cycle, except in HALT. Conditions [11]–[13] come from `conditionEvents(k)`, read at the end of the cycle like `Tile.eventCounts`.
- HALT and a halted controller call `setHalted(t, true)`; HALT → RUN calls `setHalted(t, false)`.
- `attach(host)` gives the replayer meshsim's `violate`.
- The sender: `outputQueues()` is 0 for the replayer.
- The replayer's rows leave through its own sink, one row per cycle by default (`sinkInterval`), and a `rowListener` sees each record.

## Model choices (where the spec leaves timing or structure open)

- **M1. When a row starts.** A row starts when everything it needs is present:
  - its expansion word;
  - a log word, if the word has `from_log`;
  - for a row with `next_from_value`, a read-log word or the read log's end, whether or not the row will transfer;
  - a free output-queue slot.

  Results are computed at the start. The register write, the record push and the pc/instr_idx update happen at the end of the row's last cycle: cycle 3, or 5 for class M. This keeps "stalls delay a row's start, never its schedule" (4.19.7); the cost is that a not-taken branch may wait for a read-log word it won't use.
- **M2. Front end.** The next instruction is decoded in the cycle after the current instruction's last row starts. The decode takes its code value, so the code table updates then, and a controller-fed instruction raises new expansion then. With 3-cycle rows this costs nothing.
- **M3. When entries leave their queues.** An instruction's code entries leave when its last row completes (4.19.7). A log entry leaves when its last bit is taken, or when the stream ends inside it. An expansion word leaves when its row starts.
- **M4. Landing.** A packet handed over at t lands at the end of t + 2. The core can use it at t + 3, and it counts at t + 2.
- **M5. The 64-cycle image copy** runs from t + 1 to t + 64 after a run stored at t, and the first row can start at t + 65. The lines are live and the front end decodes during the copy.
- **M6. Errors found early.** A front-end error (decode class 3, or a code pop past the stream's length) is raised when the row that needs the instruction would start. A row already in flight completes and is recorded. A row-level error (reserved code, format 6/7, opcode not executed, untraced, past ROM word 511, a log pop past its length) stops the pass before that row is recorded.

## Spec ambiguities raised (my reading is implemented; each needs a ruling)

- **N1. Expansion low-water (item 51).** Implemented as: running, and 256 − occupancy − pending ≥ 128, and a word of the current expansion has landed, and the newest landed word's `seq_remaining` is greater than pending.
  - Pending restarts at 0 when new expansion is raised, and the claim sets it to 128.
  - Effect: for an expansion of ≤ 128 words from the offset, the line never rises. For a longer one it rises once 128 words have landed, and again each time a half is used.
- **N2. `next_from_value` rows and the read log** (see M1). The question is whether a not-taken branch with an empty read log should stall. Hardware has to decide by cycle 2; I stall at the start.
- **N3. Ruled (owner; spec 26k/26n 4.19.4): the rd = x0 handling applies to every word, ROM or controller-fed.**
  - **Remap:** for a controller-fed expansion the replayer takes the offset from the first word delivered. On a mid-sequence start the controller copies the template's `rd0` into the first word it sends (this resolves N3a).
  - **NOP policy (`rd0` 1 or 3) with rd = x0:** the controller sends the single NOP word (ROM word 1) carrying the template's `rd0` in bits [47:46], and the replayer replays it as the shared NOP row, virtual per `rd0`.
  - **Model and controller:** both do this (`ReplayDriver`, `replay_ctl`). As a fallback the model also accepts a full template under a NOP policy: it replays the NOP row and drops the rest of that expansion's words as they land.
- **N4. A control transfer from a row without `next_from_value`.** The spec says the flag "must be set" but not what happens if it isn't. I pop the read log anyway, as the tracer engine writes a word for every transfer, and flag it.
- **N5. Ruled (owner): yes.** Write-log E[15] after an LD to x0 (LR.D with rd = x0). 4.19.15 says "the value it loads (popped from the read log)", so E[15] takes the popped word, not the 0 that rd and the record show. Implemented that way. The tracer's encoder must agree.
- **N6. Ruled (owner, 2026-10-27): as the spec states, which matches jolt-cpp.** A new pc of 0 from any row ends the program: pc 0 and instr_idx 0. With `next_from_value` set, the row pops one read-log word, expected 0, and meshsim flags any other value. This is implemented and unchanged.
- **N7. Ownership is judged when the packet lands** (t + 2), not at hand-over. This only matters for a packet in flight across a run or a HALT edge.
- **N8. A sticky line raised in the claim's own cycle survives** (4.17.3). Implemented as: a claim at t clears an event only if it was set before t.
- **N9. PERIPH_STATUS_0 [15:0], rows run, reads 0 after a full pass** (65,536 mod 2¹⁶). Software that checks "rows run = 65,536" must use done instead.
- **N10. Invariant 18 (one source per queue port)** is checked per pass: a second source for the same port in one pass is flagged. That is stricter than "at once"; the Stage-B program has a single source, so it doesn't matter there.
- **N11. Ruled (owner), and confirmed by the deliveries:** the record is words 0–4 in order, each 128-bit word little-endian (bits [63:0] first), so the record's byte k is bits [8k + 7 : 8k] of the 640-bit row.
- **N12. Spec candidate, tied to P1; not for this milestone.** 8.5 forbids updating `DMA_CONFIG` "while … the phase's arrivals are still landing". The rule protects the write and GATHER engines, which read its digit and *k* fields as packets arrive. It could be narrowed to arrivals that read those fields: mode-9 arrivals read neither. Any design that starts a run while another run's arrivals land needs this, as option (a) did and P1 may. meshsim doesn't check the rule today, and cp12's controller never relies on it.

## Status of the tests (1,183 checks, 0 failures)

- **ALU vs `vec.tsv` (242 rows):** 210 match the tracer.
  - 7 are inputs undefined for the replayer, and are flagged: zero shift masks (4), a non-contiguous pext mask (1), zero pext masks (2).
  - 25 don't apply: 18 DIV/REM advice opcodes, which are written as `VirtualAdvice` and taken from the log, and 7 failing assertions, on which the tracer aborts.
- **Unit tests:**
  - the decode index, the immediates, and the decompressors (random round trips, move to front, E[15], straddling values, exact ends);
  - a 19-instruction hand-checked program in six stream combinations (raw, all compressed, and one or two streams falling back to raw), plus store log off;
  - mid-sequence starts (ROM and controller-fed), LD to x0, 300-row controller-fed expansions, and an all-padding window (raw and compressed);
  - every invalid-expansion cause, the write port's bad writes, ownership and HALT, overrun, the lines and claims, and timing;
  - both N3 cases served by the controller, the debug pass length, and operand code 145 (immediate 0).
- **The suite catches injected bugs.** Nine injected bugs (E[15], offset, `is_first`, self-jump, LD memory value, write-log LRU size, expansion low-water, `pc_increment`, `rd_prev` of immediates) each make it fail.

## Upstream check

`UPSTREAM-CHECK.md` (brief). The authority for this milestone is jolt-cpp, and its Rust baseline is LayerZero-Research/jolt@735355e. a16z HEAD differences are future-compatibility notes. The model and spec follow jolt-cpp throughout (owner, 2026-10-27).

## Stage B controller

`sw/replay/`: the controller program, compiled against spec 26n (local register header). It is not run until meshsim's v1.1 tile lands; see its README.

## Spec version and pass length

The references are rebased onto mesh-sorter-v1.1-2026-10-26n. Nothing in 26n changes the model or the controller: part J's 8.11 wording now says counter writes add, which is what the controller already assumed, and parts H and I add tags. The pass length is a testing parameter, `new Replayer(me, rowsPerPass)`, 1 to 65,536 (default 65,536). The driver takes it from `ReplayWindow.rows`, which the loader sets from the manifest's `window_rows` for the tracer's debug windows.

## Stage A against the tracer's delivery 1

`test/ReplayDelivery.java` is the loader and the comparison. It reads the manifest; for each delivered window it loads the program's decode table, ROM and templates (index count = file size / 16, rd0 at byte 12), takes a state record variant (`state`, `state-lru` or `state-pick`) and the streams its flags name, and sends the record's bytes 16–1,071 unchanged. It runs one pass through `ReplayDriver`, whose ideal controller handles both N3 cases as the Stage B controller does. It then compares every row with `rows.bin`, reporting the first mismatching row, its fields and its `diag.bin` entry. It also checks that every stream was consumed exactly to its length, that the end state (pc, `instr_idx`, next offset, registers) equals `end.bin`, and that the replayer raised no violation or condition.

Run: `java -Xmx3g -cp build ReplayDelivery <delivery> state [program ...] [--keep-going]`.

### Delivery 1 results

Every delivered window matches jolt-cpp's reference rows byte for byte, in all three record variants (raw, all-compressed, the tracer's pick): the five guests, EVM Zone trace-23 (13 windows), trace-29 (14) and Cyclone input/25 (12). That is 132 window runs and 8,650,752 rows. Every run also consumed each stream exactly, ended in `end.bin`'s state (pc, `instr_idx`, next offset, 128 registers), and raised no violation or condition.

**One model fix, found by the end-state check:** `PERIPH_STATUS_0`'s next offset read 0 after a pass that ended mid-expansion (EVM Zone 23, window 0, which ends two rows into an expansion), because the pass's end dropped the instruction context. It now keeps the offset where the next row would start (4.19.9's definition), as `end.bin` has it. No row was affected.

**Windows for Stage B's controller-fed figures** (the tracer's measurement: up to 62% of a window's rows on EVM Zone, 83% on Cyclone):
- EVM Zone 23: 44 (61.2%) and 42 (58.1%) are delivered; the maximum, 43 (61.4%), isn't.
- EVM Zone 29: 53 (62.4%, the maximum) and 54 (61.4%) are delivered.
- Cyclone 25: the delivered maximum is 71 (35.9%); **the 83% windows (78, 83, 84; 54,520 rows each) aren't delivered.** They are requested from the tracer session.

### Delivery 2 results

Every delivered window matches byte for byte in all three record variants, with streams consumed exactly, end states equal to `end.bin`, and no violations:
- the five guests rebuilt from cp7;
- EVM Zone 2²⁹ (14 windows, identical to delivery 1's);
- Cyclone input/29 (13 windows; its three memory-log gaps, F4, don't touch the inputs).

That is 96 window runs and 6,291,456 rows; across both deliveries, 228 window runs and 14,942,208 rows.

**High controller-fed windows:**
- Cyclone 29's highest delivered window is 1096, at 50.7%.
- The 83% windows (1131, 1140, 1141, at 83.2%) aren't delivered.
- EVM Zone 29's maximum, 53 at 62.4%, is.

### Delivery 3 results, and the Stage B timing windows

Delivery 3 holds the high controller-fed windows: Cyclone input/25 windows 78, 83 and 84 (83.2% each) and EVM Zone trace-23 window 43 (61.4%). The program files are identical to delivery 1's. All four match byte for byte in all three variants (12 runs, 786,432 rows), with streams consumed exactly, end states equal to `end.bin`, and no violations.
- **Cyclone 78, 83 and 84** differ in every row but have identical Stage A cycles (196,978) and stalls: the same loop of BLAKE3 calls over different data.
- **Across all three deliveries:** 240 window runs, 15,728,640 rows, no mismatches.

**Stage B timing windows** get their own cycles-per-row and stall figures:

| program | window | controller-fed rows | Stage A (ideal controller) cycles/row |
|---|---|---|---|
| Cyclone input/25 (delivery 3) | 78, 83, 84 | 83.2% | 3.005 |
| EVM Zone trace-23 (delivery 3) | 43 | 61.4% | 3.267 |
| EVM Zone 2²⁹ (deliveries 1, 2) | 53 | 62.4% | 3.269 |
| EVM Zone 2²⁹ (deliveries 1, 2) | 54 | 61.4% | 3.271 |

**Operand code 145, immediate 0** (26q, 4.19.3) is accepted: an immediate slot of value 0, as for a zero beside a non-zero constant in secp256k1's two-immediate rows. Reserved codes are now 146–255. A test checks that 145 and 132 (from an R-format source) give the same row.

## Stage B: the new-expansion fast path, and a proposal

`sw/replay/replay_ctl.c` (cp12) serves a new expansion in about 22–25 cycles of controller code (18 instructions), against about 123 measured. See the README. It is to be measured with the meshsim session's StageB.java on EVM Zone 2²⁹ windows 53 and 54 and Cyclone input/25 windows 78, 83 and 84.

**Estimates.** Per expansion, about 25 cycles of code plus about 33 of template fetch is about 58 cycles of stall, against 157.

| window | expansions | Stage A | Stage B measured | Stage B fast path (estimate) |
|---|---|---|---|---|
| EVM Zone 53 | 226 | 3.27 | 3.81 | ~3.47 cycles/row |
| Cyclone 78, 83, 84 | 52 | 3.005 | (~3.13 from 157 cycles each) | ~3.05 cycles/row |

### Proposal P1 (not in the spec; a candidate for a later milestone, with N12): raise new expansion early

**Today.** The replayer presents the next controller-fed instruction only when the previous instruction's last row starts. Its whole service time (controller code plus template fetch, about 58 cycles after the fast path) shows up as a stall.

**The proposal.** The front end looks ahead in the code queue and presents the next controller-fed instruction as soon as the current controller-fed expansion's words have all been requested (its words-left count is 0), while earlier rows still run. Its words would follow the current expansion's in the expansion queue; the replayer tells the expansions apart by `seq_remaining`.

**Estimated gain.** The gaps between controller-fed instructions are hundreds of rows in EVM Zone. In Cyclone, the next BLAKE3 call is decodable during the current call's 1,048 rows. So nearly all of the service time hides behind rows: EVM Zone 53 at about 3.27–3.30 cycles per row, Cyclone at about 3.01.

**Cost (spec).**
- The front end decodes more than one instruction ahead, skipping over ROM instructions.
- `PERIPH_STATUS_0`'s next offset and `PERIPH_STATUS_1` describe the presented instruction, not the running one.
- The expansion queue's low-water rule (item 51) follows "the newest expansion" rather than "the current one".
- The new-expansion claim credits the next expansion while the current one's words may still be in the queue, so the pending-and-capacity rule must allow both.

### Stage B fast path: measured (meshsim interim 2026-10-27, StageB.java, register loads at 2 cycles)

All five timing windows match byte for byte with cp12's controller.

| window | Stage A | cp9 | cp12 | new-expansion stall per expansion, cp9 → cp12 |
|---|---|---|---|---|
| EVM Zone 2²⁹ 53 | 3.269 | 3.814 | **3.479** | 155.6 → **57.5** |
| EVM Zone 2²⁹ 54 | 3.271 | 3.825 | **3.492** | 163.4 → **65.2** |
| Cyclone input/25 78, 83, 84 | 3.005 | 3.169 | **3.090** | 184.4 → **82.3** |

**Where the remaining stall goes** (`-Dstageb.profile`, with WFI split by site; cycles per expansion, EVM Zone 53 / Cyclone 78):
- **The fast path, claim to start:** 28.0 / 30.1. My estimate was 22–25; the difference is the wake from WFI.
- **`set_active`, after the start:** 12.0 / 11.8. It overlaps the template fetch, and the first word lands before the controller reaches its own wait.
- **Waiting in the refill loop's squelched `wfi`:** 16.7 / 37.3. The new-expansion line rose while an earlier refill run, claimed just before, was still landing, so the expansion waits behind it. It is larger on Cyclone, whose 1,048-word expansions keep a chunk run going about a third of the time.

**Next steps, in order of size:**
- **(a) Software:** let a new-expansion line wake the refill wait and be served as soon as the address generator has finished issuing, adding its count to the same `DMA_PERIPH_REMAINING`. Estimated gain: about 0.06 cycles per row on EVM Zone (~3.42) and 0.03 on Cyclone (~3.06).
- **(b) Proposal P1 (lookahead):** hides the rest.

### Option (a), measured: no gain; cp12 stays

**What (a) did:** a new-expansion line wakes the refill wait, and its template stream starts as soon as the address generator has finished issuing the refill run, the count added to the same counter.

**Measured** (all rows still match byte for byte):

| window | cp12 | (a) |
|---|---|---|
| EVM Zone 53 | 3.479 | 3.502 |
| EVM Zone 54 | 3.492 | 3.515 |
| Cyclone 78, 83, 84 | 3.090 | 3.094 |

**Why** (profile, EVM Zone 53, per expansion; total 64.6 against 57.5):
- The WFI wait is gone, but the controller now spins 14.5 cycles in `wait_ag_idle`. When the line rises, the address generator is usually still issuing the refill (a run of 128–256 entries at about one per cycle), not just waiting for it to land.
- `fast_start`, now called from two places, was no longer inlined, adding about 9 cycles to the path.

The controller stays at cp12; option (a) is kept as `replay_ctl-option-a.patch`.

**What would bound the wait instead.**
- **Shorter refill runs**, e.g. 32 entries, with the fast path forced inline. Then an expansion waits at most about 32 cycles of issuing, about 16 on average, against the whole run today.
- **Estimated gain** over cp12: up to about 0.03 cycles per row on EVM Zone and 0.02 on Cyclone, less the extra refill runs' controller time.
- That is small next to proposal P1.

**A spec note for (a), and for any variant that starts a run while another's arrivals land (N12):** 8.5 forbids updating `DMA_CONFIG` "while … the phase's arrivals are still landing". The rule protects engines that read the digits and *k* as packets arrive. Mode-9 arrivals read none of them, and the controller writes those fields as 0 every time. So the rule could be narrowed to "while arrivals that read its digit or *k* fields are landing". meshsim doesn't check it.
