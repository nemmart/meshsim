# WORKER13 notes

Working notes for the change guide (`wip/spec/CHANGES-to-v1.25.md`). Plan agreed with the owner: (1) read and diff the
specs section by section, recording every difference here and committing after each section; (2) the guide's skeleton,
overviews and per-unit entries; (3) the register tables and sections 9–10; (4) QUESTIONS.md, REPORT.md, final pull.

Spec commit at start: `4856c0b` (main). Inputs read: CHANGES-v1.2, CHANGES-v1.25, DECISIONS-v1.25.

## Status

| step | what | state |
|---|---|---|
| 1 | pass A, v1.2 → v1.25 (mechanical diff) | **done** |
| 1 | pass B, v1.1 → v1.2, front matter to 13 | **done** |
| 1 | spec fixes from pass A/B taken by WORKER10 (`0b4ba20`: 4.17.1 types, 4.19.7 renumbering, 8.1 timing, CHANGES-v1.25 errata) | **pulled; the guide uses the fixed text** |
| 2 | the guide: 0 reading guide, 1 overviews + removed/new, 2.1 link/network, 2.2 address map, 2.3 engines, 2.4 controller, 2.5 peripherals | **written**; next: 2.6 replayer (row/table v1.25, interface v1.2, few both), 2.7 control/reset/boot, 2.8 C&C 1–3 |
| 3 | register tables, sections 9–10 | |
| 4 | QUESTIONS.md, REPORT.md, final pull, spec commit stamped | |

## Pass A: v1.2 → v1.25 (spec diff)

`diff v1.2/spec/machine-spec.md wip/spec/machine-spec.md`: 183 changed lines, **all** in the front matter, 4.19, 9 (16),
10 (replayer row) and 13. Nothing outside the replayer changed, as CHANGES-v1.25 claims. Every hunk checked against
CHANGES-v1.25's table:

| hunk | spec section | in CHANGES-v1.25? | note |
|---|---|---|---|
| title, companion files, "v1.25 in brief" | front | yes | |
| "peripheral 1, type 1" → type 4 | 4.19 intro | yes | editorial |
| queue table: expansion queue "inlines and CSR"; log rows "load values", "store rows' pre-values" | 4.19.1 | yes (4.19.1, 4.19.2) | |
| reference 821e1bd, workloads, citations, item 137 | 4.19.1 | yes | |
| which rows take a log word: store row = SD or VirtualStoreB/H/W → write log | 4.19.2 | yes | RTL: the log-select decode now covers 4 store opcodes, not just SD (but see `from_log`: flag decides *whether*, opcode decides *which log*) |
| densities labelled 4f36b3d, +1/9 | 4.19.2 | yes | |
| includes-store-log [115]: "store rows" | 4.19.2 | implied | RTL: the [115]=0 suppression applies to all four store opcodes |
| opcode field 0–167 → 0–170 | 4.19.3 | yes ("0–170") | |
| advice_reg [62:56] → [61:56], reg − 146; [62] sign_imm; [63] offset_imm | 4.19.3 | yes | |
| shift_offset and sign_imm paragraphs | 4.19.3 | yes | |
| codes 146–255 "reserved in a replayer"; Tracer bank-1 | 4.19.3 | yes | still an invalid expansion in a replayer |
| row immediate = sum of slots except ShiftData… = shift_offset | 4.19.3 | yes | RTL: imm mux on ShiftData opcodes |
| conventions-as-data list adds sign_imm, offset_imm | 4.19.3 | yes | |
| lb example rewritten | 4.19.4 | yes | |
| VirtualHostIO dropped from the remap table; no operands; rd0 = 0 | 4.19.4 | yes | |
| SYSTEM index row: sel 11 "(an invalid entry)" | 4.19.5 | covered by "unused entries invalid" | |
| class field: 2 = CSRRW/CSRRS only; 3 = invalid | 4.19.5 | yes | |
| universal tables; builder rules; 306/17/1,725 | 4.19.5 | yes | |
| ROM goal and fill 388/512 | 4.19.6 | yes | |
| appendix normative: 94 opcodes, 252 vectors, three departures | 4.19.7 | yes | |
| PEXT any mask; **zero mask gives 0** | 4.19.7 | partly | **zero mask on PEXT: v1.2 undefined → v1.25 defined (0).** CHANGES-v1.25 says only "correct for any mask"; invariant 16's v1.2 text excluded zero masks too. Minor, but an RTL behaviour change. |
| the memory rows paragraph | 4.19.7 | yes | |
| class table renumbered; S range list | 4.19.7 | yes | **spec says "v1.2's [codes] from `VirtualLoadB` on were three lower": wrong.** Actual shifts (appendix tables): +6 for 116–117, +4 for 118–157 (except removed ones), +3 for 158–170. See `opcode-map.txt`. 61 row opcodes change code; 36 keep theirs. → QUESTIONS |
| executes 88 opcodes | 4.19.7 | yes | |
| edge cases: HostIO reads/writes nothing; ShiftData lane | 4.19.7 | yes | |
| load row's memory fields from rd | 4.19.7 | yes (via 4.19.12) | |
| what's hardwired: memory fields for 8 memory opcodes, narrow address rounding | 4.19.7 | yes | |
| next_from_value placement (ECALL/MRET JALR, EBREAK JAL now ROM) | 4.19.7 | yes | |
| controller loop: key(EXPANSION_OPCODE) | 4.19.9 | yes | software |
| template index: 16-byte entries (key, offset, length, rd0, class), sorted by key | 4.19.9 | partly | entry format (rd0, class bytes) not in CHANGES; software only |
| EBREAK in ROM | 4.19.9 | yes | |
| controller-fed = inlines + CSR | 4.19.9 | yes | |
| invalid-expansion row text | 4.19.11 | yes | |
| duplicated sentence removed | 4.19.11 | yes | |
| word 0 opcode VirtualAdvice 122 → 126 | 4.19.12 | yes ("VirtualAdvice is 126") | |
| word 0 [86] imm_signed; spare [127:87] | 4.19.12 | yes | |
| word 1 imm: ShiftData shift_offset sign-extended; **"0 for an advice row"** | 4.19.12 | partly | "0 for an advice row" new wording; check whether v1.2's advice rows had a non-zero immediate slot (probably always 0: wording only) |
| word 4 mem_addr: narrow rows rounded a − (a & (8 − width)) | 4.19.12 | yes | |
| word 4 mem_value for load/store rows | 4.19.12 | yes | |
| cost: ALU with PEXT any mask | 4.19.14 | yes | |
| E[15] updated by every load row | 4.19.15 | yes | |
| invariant 16 | 9 | yes | |
| section 10 replayer row | 10 | yes | |
| 13: status table, items 137–141 | 13 | yes | |

Opcode appendix: v1.1 and v1.2 `replayer-opcodes.md` are **identical** (so v1.1's codes = v1.2's). v1.2 → v1.25 code map
in `opcode-map.txt`: removed `VirtualAssertHalfwordAlignment` 112, `VirtualAssertWordAlignment` 113, `VirtualAlignAddr`
154; added `VirtualLoadB/H/W` 110–112, `VirtualStoreB/H/W` 113–115; never-occurring 126/161/165 → 130/164/168.

**For the v1.1 reader:** the whole replayer section is "both" (v1.2 rewrote its interface: peripheral window, credits,
region, ERRORS; v1.25 rewrote its row content). Need the v1.1 → v1.2 pass on 4.19 to separate them.

## Pass B: v1.1 → v1.2, section by section

### Front matter, 1, 2, 2.1, 2.2 (done)

| what | v1.1 | v1.2 (= v1.25) | in CHANGES-v1.2? | why |
|---|---|---|---|---|
| "who checks what" (hardware vs meshsim) | — | stated once in front matter | yes (front matter) | D27 |
| Packet | 32/64/128-bit payload + header, 169-bit word | same payloads, in one 256-bit link word | yes (3.1) | D2 |
| Addresses | 21-bit chunk addresses everywhere | 32-bit byte addresses in packets and the core's map; 21-bit chunks only inside engines (`STREAM_SRC`, `PTR`, `SRAM_RD_ADDR_QUEUE`, `DMA_RECORD_BASE_ADDRESS`); packet addr of chunk c = `0x03000000 \| c << 2` | yes | D5, D26 |
| Alignment | (spanning allowed) | natural alignment for 64/128-bit, every region | yes | D26 |
| Register name prefixes | `TILE_`, `WAKE_`, `PERIPH_`, `SENDER_`, `UART_` … | `PARAMS`/`STATUS`/`ERRORS`, `PULSE_`, `EVENTS_`, `ROUTER_`, `REQUEST_`, `DMA_`, `WRITE_COUNTER_`, `SRAM_` | yes | D14, D29 |
| conditions → errors; errors raise hard interrupt | conditions | errors | yes | D29, D21 |
| "unit" = any of 67 (64 tiles + C&C 1–3) | — | new term | implied | D25 |
| C&C | one C&C at endpoint 64 on tile 0's fifth port; memory system hangs off C&C | three controllers C&C 1–3, endpoints 64–66, tiles 0–2's fifth ports, each a tile instance **with 2 MB big SRAM**, engine = peripheral 1 | yes (2), but **"2 MB big SRAM" not in CHANGES** | D25 |
| Tiles' strapping | endpoint id and peripheral type strapped (8.2) | endpoint id and peripherals present strapped (`PARAMS`) | yes (8.2) | D18 |
| Tiles' request receive queue | every tile has one | tiles have none (receive queues at the root) | yes (Round 2, D38) | D38 |
| RUN/HALT net | one line, `run_latency` | gone | yes | D23 |
| GO net | C&C → every tile | C&C 1's peripheral `PULSE_GO` → every mesh tile; C&C 2/3 not wired | yes | D28 |
| READY | 64 lines into C&C | 64 lines into C&C 1's peripheral (`READY_LINES_0–1`) | yes | D28 |
| Error line | one per tile, ORed into one sticky bit at C&C | one per unit into C&C 1's peripheral (`ERROR_LINES_0–2` by endpoint id), high while `ERRORS` ≠ 0, latency `error_latency`; bit 64 unused | yes | D24 |
| Reset inputs | (chip reset) | hard and soft reset inputs; chip never resets itself | yes | D35 |
| Output pins | — | toast pin, `LED` pin | yes | D34, D28 |
| 2.1 65 | memory system, served at C&C's port (src 64) | C&C 2 | yes | D25 |
| 2.1 66 | reserved (66–125) | C&C 3; 67–125 reserved | yes | D25 |
| 2.1 126 | broadcast | broadcast **to tiles 0–63** | wording | — |
| 2.1 "72–74 gone" | v1.1's 2.1 table never listed 72–74 (73 appears only in 3.3's target table and open items 82/83) | — | CHANGES wording slightly off; harmless | — |
| 2.2 address map | — | new: regions 0x00 data 32 KB, 0x01 program 32 KB (store = bus error), 0x02 boot ROM (reset vector 0x02000000), 0x03 big SRAM, 0x04–0x06 peripherals 1–3, 0x07–0xFE reserved, 0xFF registers; packet-reachable vs core-reachable; out of bounds undefined; auto-advance past end a bug; first 16 bytes of data memory reserved, overlays at 0x10 | yes | D5, D9, D22, D26, D27, D37.4 |

### 3 Links (done)

| what | v1.1 | v1.2 (= v1.25) | in CHANGES-v1.2? | why |
|---|---|---|---|---|
| link word | 169 bits: src 7, dst 7, mode 4, addr 21 (chunk), size 2, payload 128; **no bit positions given** | 256 bits: payload [127:0], address [159:128], size [161:160], counter [164:162], mode [167:165], src [174:168], dst [181:175], reserved [255:182] sent 0, ignored, **stored in full** | yes | D2, D30 |
| header-register block | — | link [167:160] = header register [24:17] (size, counter, mode as one 8-bit block) | yes (8.1 common header) | D12 |
| modes | 4-bit: 0 invalid, 1 gather req, 2 by-src, 3 at explicit addr, 4 bucket, 5 augmented, 6 CM write, 7 command, 8 reply, 9 periph write, 10–15 reserved | 3-bit: 0 write at address (any region), 1 by-src, 2 bucket, 3 augmented, 4 gather req, **5 count only** (round 3), 6–7 reserved, discarded silently, no error, no decrement | yes | D6, D43 |
| v1.1→v1.2 mode map | 2→1, 4→2, 5→3, 1→4; 3, 6, 7, 8, 9 → mode 0 (by region/address); 0, 10–15 → (none) | | yes (implicit) | D6 |
| invalid code | mode 0 invalid | none: an all-zero header is a valid uncounted mode-0 write to 0x00000000 (reserved bytes) | yes | D6 |
| mode-4 ordinal bits | **v1.1 inconsistent:** mode table says ordinal in [63:32], fixed-payload paragraph and 4.4 say [51:32] | [51:32] (sent at 64 bits) | **not in CHANGES**: v1.1 self-contradiction resolved; RTL should use [51:32] | — |
| counting | per mode into 4 named counters; command/gather never; halted tile counts nothing | packet names counter 1–7 (field), 0 uncounted; modes 0–3, 5 counted by field; gather never (field passed on); decrement K cycles after director accepts; unexpected arrival at the decrement | yes (3.1, 4.12; D40 seven) | D3, D30, D40 |
| uncounted traffic | — | mode 0 only: commands and boot overlays; big-SRAM always counted; modes 1–3 always counted (meshsim) | yes | D17, W5-15 |
| fence | — | count-only packet to self on a spare counter ends a run | yes (round 3) | D43 |
| bad packets | mode 0/10–15; mode 5 @128; mode 3 misaligned; mode 7 @64/128; mode 8 @128; size 11 UB; all set "bad-mode-or-size" | misaligned 64/128 mode 0 any region; mode 0 to 0x02, 0x07–0xFE, 0xFF, absent peripheral; mode 3 @128; modes 1–5 ignore address; size 11 UB; modes 6–7 silent; sets `ERRORS`[1] | yes | D26, D32 |
| misaligned PTR write | — | passes director; write engine drops it, counted, sets [1] | yes (4.9) | D32 |
| 3.2 padding | 169 flops/stage | 256 | yes | D2 |
| 3.2 ordering | per sender (source→destination), one kind | per **sending unit** (address generator, send port, GATHER, read port, each peripheral DMA); none between units; fence or put in run | yes (round 3) | D44 |
| 3.3 receive queues | 16-entry queue in each target's tile (every tile has one); root has no queues; credits start at 16 | queues **at the root, in the tree**: 128 / 256 / 128 for C&C 1/2/3; credits = depth; tiles have none, receive registers read empty | yes (round 2) | D38 |
| 3.3 per-tile limits | — | 1 / 3 / 1 outstanding (software convention, meshsim invariant 17); error reports exempt | yes | D38, D44 (W5-23) |
| 3.3 target 3 | Updater/Binder/RW engine at endpoint 73 | C&C 2's peripheral, *(Deferred)* item 119 | yes | D25, D33 |
| framing, arbitration, 16-bit links, `reqbus_latency` | | unchanged | — | |

### 4 intro, 4.1–4.5 (done)

| sec | what | v1.1 | v1.2 (= v1.25) | in CHANGES? | why |
|---|---|---|---|---|---|
| 4 | unit table | | renamed rows (memories+boot ROM; write counters; errors/traps; fifth port at tiles 0–2; peripherals; replayer type 4; UART in C&C 1) | yes | — |
| 4.1 | alignment | none; spanning accesses = one request, two area accesses (part 0/part 1 ahead of every client); group runs "should" be row-aligned | natural alignment; no spanning; one request/token/area access | yes | D26 |
| 4.1 | misaligned access | n/a (mode-3 misaligned = bad packet) | mode 0 → bad packet; write via misaligned PTR → dropped, counted, [1]; misaligned read → not performed, packet sent with payload 0, [1] at issuing tile | yes | D32 |
| 4.1 | out of range | wrap mod 2²¹; above device end reads 0, writes dropped; no condition | **undefined** (no aliasing promised); no error bit; meshsim errors | yes ("out of range undefined") | D26/integrator |
| 4.1 | `mem_idle` | status bit covering whole write path; part of reporting done | gone (meshsim diagnostic only); counter done covers write path by K | yes | D18 |
| 4.1 | phase ordering | by `mem_idle` + counters | by K: a decremented write is in its area queue | yes | D3/D18 |
| 4.1 | writer | GATHER as mode 2, memory system mode 3 | GATHER as mode 1; explicit-address senders mode 0 to region 0x03 | yes | D6 |
| 4.2 | start | `TILE_PULSE`[1] | `PULSE_DMA`; also **clears DMA run done `EVENTS`[10]** | yes | D8, D12 |
| 4.2 | finish | `rd_done` when last entry walked; posts engine run done `WAKE_EVENTS`[11] | `rd_running`↓/`rd_done`↑ **in the same cycle as DMA run done**, once last table read complete (guarantee B) | yes (round 2, D41 F6) | D41 |
| 4.2 | `STREAM_SRC` | src [20:0], spare [31:21] | src [20:0] aligned to read size; **gap after [23:21]**; spare [31:24] | yes | D4, D12 |
| 4.2 | `STREAM_DEST` | dest word [20:0] (modes 3/6/9), *d* [27:21], spare [31:28] | **32-bit destination byte address [31:0]**, mode 0 only | yes | D4 |
| 4.2 | `STREAM_COUNT` | count [17:0] (18 bits), mode [21:18], read size [23:22], gap after [26:24], spare [31:27] | count [16:0] (17 bits, ≤131,071), size [18:17], counter [21:19], mode [24:22], *d* [31:25] | yes | D4, D12, D30 |
| 4.2 | *d* | 0–63 rel, 64 C&C (mode 3 only), 65–127 invalid | 0–63 rel, 64–66 controllers, **126 broadcast** (hardware ok; convention controllers only; meshsim errors from a mesh tile), 67–125 & 127 invalid | yes (D41 F14) | D41 |
| 4.2 | element / dest stepping | wraps mod 2²¹; dest word +1/2/4 for modes 3/6/9 | plain addition; mode-0 dest + 4/8/16 bytes; misaligned element not read, payload 0, [1] | yes | D26, D32 |
| 4.2 | reserved-encoding errors | read size 11, modes 0/7/8/10–15, d 65–127, d 64 non-mode-3, mode 5 @128, mis-aligned mode-3 dest | read size 11, modes 6–7, d 67–127, mode 4 with d≠0, mode 3 @128, misaligned mode-0 dest or source (all meshsim) | yes | D32 |
| 4.2 | drift | condition | `ERRORS`[3] | yes | D18 |
| 4.3 | `SRAM_RD_DEST_ADDR` | write-only; dest [13:0] packed CM word (anywhere in CM, program memory included); size [15:14] | **R/W**; dest [16:0] data-memory byte offset (region 0x00 only); size [18:17]; counter [21:19]; [31:22] reserved; reads back the advanced destination | yes | D13 |
| 4.3 | results | mode-6 to self, counted `DMA_CM_REMAINING` | mode 0 to self, counted by its counter field at K | yes | D6, D13 |
| 4.3 | misaligned read | dropped; read-port error | performed as payload-0 packet; `ERRORS`[1] (read-port error [7] now only for overflow) | yes | D32 |
| 4.3 | request queue entry | 37 bits (21+14+2) | 43 bits (21+17+2+3) | **not explicit in CHANGES** (follows from the counter) | D13 |
| 4.3 | misaligned destination | — | makes a misaligned mode-0 packet → bad packet at director | implied | D32 |
| 4.4 | gather request | mode 1, never counted | mode 4, never counted; **counter field stored with the ordinal** in the 16-entry queue (entry = 20-bit ordinal + 3-bit counter) and stamped on the gather values (mode 1) | yes | D6, D17 |
| 4.4 | `DMA_RECORD_BASE_ADDRESS` | any chunk | record-aligned; odd base → each read payload 0, [1] | yes | D32 |
| 4.4 | GATHER busy | `TILE_STATUS` | `STATUS`[2] | yes (8.2) | D18 |
| 4.4 | GATHER overflow | condition 6 | `ERRORS`[6] | yes | D18 |
| 4.5 | read-queue fields | addr: 0 / dest word / CM word | 32-bit address; **+ counter field** | yes | D10, D30 |
| 4.5 | spanning part 1 | goes ahead of every client | gone | yes | D26 |
| 4.5 | tokens | 64 *(Proposed, accepted)* | 64, decided; loopback hold **required**; broadcast-stream read keeps its token until its own copy leaves loopback FIFO; count-only to self takes a token | yes (D41 F14, D42 S9, D43) | |
| 4.5 | out of range read | payload 0 | undefined | yes | |
| 4.5 | injection sources | read returns, send port; C&C via fifth port | same two; controllers via fifth ports; peripherals' DMAs on own router port | yes | D10 |
| 4.5 | injection packet width | 7+4+21+2+128 | 7+3+3+32+2+128 (+ reserved zeros) | yes (3.1) | D2 |
| 4.5 | route to fifth port | dst 64 at tile 0 | dst 64/65/66 at tiles 0/1/2 | yes (4.6/4.7) | D25 |

### 4.6–4.10 (done)

| sec | what | v1.1 | v1.2 (= v1.25) | in CHANGES? | why |
|---|---|---|---|---|---|
| 4.6 | sixth route block | tile 0, from C&C | tiles 0–2, from C&C 1–3 | yes | D25 |
| 4.6 | routing ids | dst/src 64 → treated as 0 | 64–66 → id − 64; fifth-port eject when dst = 64 + me at tiles 0–2; broadcast not delivered to controllers | yes | D25 |
| 4.7 | FIFO entries | 169 bits | 256 bits | yes | D2 |
| 4.7 | loopback FIFO ≥ tokens | *(Proposed)* | **required** | yes (D42, S9) | D42 |
| 4.7 | router overflow | condition | `ERRORS`[0] | yes | D18 |
| 4.8 | director switch | on mode alone (0–15) | on mode, and for mode 0 on region: 0x00/0x01 port B, 0x03 write engine, 0x04–0x06 peripheral; 1–3 write engine; 4 GATHER; 5 count only; 6–7 discard; bad → [1] | yes | D6, D32, D43 |
| 4.8 | counting point | director decrements on acceptance (RUN only), `DMA_PERIPH_REMAINING` on landing/drop | **K-stage delay line** of counter ids; decrement K after acceptance; ≤1 decrement/cycle; unexpected arrival judged at decrement | yes | D3 |
| 4.8 | K | — | parameter, default 8; meshsim formula K ≥ max(**2L + 4**, L + 4, `cm_latency`, `rp_latency`) | yes, **but CHANGES has no record of the 2L+3 → 2L+4 revision** (D49, W4-08) | D32, **D49** |
| 4.8 | **K in hardware is the implementation's** | — | the requirement is only that K covers the implementation's slowest write path; the formula is meshsim's, not a hardware promise; the real K comes back in the RTL's delta | **not in CHANGES-v1.2** (D49, 2026-11-01) | **D49** |
| 4.8 | **one K for every destination** | v1.1 counted CM/periph writes at landing | CM and peripheral writes also wait K (≈6 cycles more per counter round trip; a separate 2-cycle delay not adopted) | **not in CHANGES-v1.2** (owner 2026-10-31) | owner |
| 4.8 | command landed | set by mode-7 write to slot 0 | set **when a write covering slot 0 lands in port B** (any size, starting at slot 0), from the director only; the controller's own store doesn't | yes | D36 |
| 4.8 | HALT behaviour | in HALT lands everything, counts nothing | no HALT; no state changes routing or counting | yes | D23 |
| 4.8 | src use | router, modes 2/5 PTR, mode 8 reply block | router, modes 1/3 PTR only | yes (implied) | D6 |
| 4.8 | `cm_idle`, `mem_idle` | kept by director | gone from hardware | yes | D18 |
| 4.9 | engine inputs | modes 2–5 (+ "9" row in table, a v1.1 typo for mode 3) | mode 0 (region 0x03) and 1–3 | yes | D6 |
| 4.9 | PTR[i] index | src 0–64 | src 0–66 | implied | D25 |
| 4.9 | `PTR[64]` | C&C's staging pointer; loader re-sets after mode-4 phases | not special | yes | D17 |
| 4.9 | misaligned PTR | n/a (spanning allowed, split in engine) | write dropped, still counted, [1]; **the pointer still advances by the size** | partly (advance not in CHANGES) | D32 |
| 4.9 | explicit-address write | mode 3, chunk addr, misaligned = bad packet, *never counted* | mode 0, byte offset / 4, counted by its field | yes | D6 |
| 4.9 | input FIFO overflow | condition | `ERRORS`[4] | yes | D18 |
| 4.10 | window | 0x2000–0x3FFF | 0xFFFF2000–0xFFFF35FF *(Proposed, item 120)*; same low-16-bit offsets | yes | D11 |
| 4.10 | access timing | one cycle, when `rd_running` = 0 and `ts_busy` = 0 | one access at a time at the register-load latency (**1 cycle for the table window, D45**) | partly | D11, D45 |
| 4.10 | waiting | `ts_busy`, `wait_tables()` | by events: counter done + DMA run done; `ts_busy` gone; may poll `STATUS.rd_running` | yes | D11 |
| 4.10 | guarantees A–C | — | A: K covers table update; B: DMA run done after last table read, `rd_running` falls same cycle; C: gather values counted at the director | yes | D17, D41 |
| 4.10 | table request overflow | condition | `ERRORS`[5] | yes | D18 |

**Major finding: CHANGES-v1.2 stops at Round 3 (D43–D44).** DECISIONS-v1.2 sections **45–49** (2026-10-29 to 2026-11-01)
were applied to the v1.2 spec but have no CHANGES entry:
- **D45 load latencies** (Tony): data memory, program memory, boot ROM and the table window load in **1 cycle**; the
  register block and every peripheral window word in **2 cycles**, fixed (v1.1: "1 or 2, the implementation's choice").
  Item 134 closed. → check 4.11, 8.1.
- **D46 peripheral types:** 0 none, 1 Ingress/Egress (C&C 1), 2 Updater/Binder/RW (C&C 2), 3 Tracer (C&C 3), **4 the
  replayer (v1.1: type 1)**. Hardware-visible (`PARAMS` [7:0]); **an RTL change from v1.1** that CHANGES-v1.2 doesn't
  list, and CHANGES-v1.25 calls the leftover "type 1" in 4.19's intro "an editorial fix of v1.2" (true for v1.2's text,
  but for a v1.1 reader the value really changed).
- **D47** waiting rules (software): tables free per memory; polling a landed word valid only for CM landings; plain
  `wfi()` on counters done.
- **D48** W4-06: a peripheral without a DMA reads 0 at words 8–9 and a load isn't flagged (only a store); W4-07: `mtime`'s
  rate implementation-defined.
- **D49** K: meshsim 2L + 4; hardware K the implementation's.

### 4.11–4.18 (done)

| sec | what | v1.1 | v1.2 (= v1.25) | in CHANGES? | why |
|---|---|---|---|---|---|
| 4.11 | core | RV32I + Zmmul + `WFI`; no CSRs, no traps; illegal/ecall/ebreak **halt** | + CSR instructions, machine-mode trap CSRs, `mcycle`, `mtime` (rate impl.-defined, D48); machine mode only; same core in C&C 1–3 | yes (D41 F12) | D1, D20, D41, D48 |
| 4.11 | inputs | — | hard interrupt (level from `ERRORS` ≠ 0, gated by `MIE`/`mie`, releases `WFI`); wakeup (level, from the wake term) | yes | D1 |
| 4.11 | traps | — | bus error, illegal, ecall, ebreak, fetch outside program memory/boot ROM, misaligned jump → trap to `mtvec`, terminal; last four set no `ERRORS` bit | yes | D20 |
| 4.11 | memories | 8 KB instruction (0x0000–0x1FFF, fetch only) + 8 KB data (0x4000–0x5FFF); four word-interleaved 512×32 banks each; resident loader 0x1F00 | 32 KB data (0x00000000) + 32 KB program (0x01000000, fetch **and load**, store = bus error) + boot ROM (0x02000000); banking is the implementer's | yes | D5, D22, D42 |
| 4.11 | port B | 1/2/4 consecutive words any address (4 banks) | one 128-bit (or 32/64) write per cycle, naturally aligned | yes (D42) | D42 |
| 4.11 | **load latencies** | memories 1 cycle; register block and table window "1 or 2, the implementation's discretion" (meshsim 2) | memories and boot ROM 1; **table window 1**; register block and every peripheral window word **2**, fixed | **not in CHANGES-v1.2** (D45; item 134 closed) | **D45** |
| 4.11 | sampling rule | `WAKE_EVENTS`, `REQUEST_RECEIVE`, `UART_RX` side effects | adds: a peripheral's `EVENTS` subtracts one count per masked set bit; holds for every window word | yes (4.17) | D14 |
| 4.11 | stalling stores | send port, request FIFO, `UART_TX` on a wired tile; `SENDER_SEND` never | send port, request FIFO, UART TX word (C&C 1's peripheral) | yes | D28 |
| 4.11 | bus errors | stores to instruction memory, misaligned, unmapped, sub-word to registers/table window, store to RO reg, loads from PTR/STREAM/GROUP; **halt** | `ERRORS`[9] and trap; adds store to boot ROM, regions 0x03–0xFE, unused 0xFF, **any word of an absent peripheral's window**; reserved words read 0 and ignore stores; out of bounds in CM not checked (aliases) | yes | D5, D9, D27 |
| 4.11 | CM addressing | packed 14-bit word addresses (`STREAM_DEST` mode 6, `SRAM_RD_DEST_ADDR`, `ROUTER_SET_HDR`) | byte addresses; packed form gone | yes | D5 |
| 4.11 | send port header | `ROUTER_SET_HDR` (W): dst [6:0], mode [10:7], addr [31:11] (21-bit) | `ROUTER_ADDR` (R/W, 32-bit) + `ROUTER_HDR` (R/W): no-increment [0], spare [16:1], size [18:17] reserved (reads 0), counter [21:19], mode [24:22], dst [31:25] (0–66, 126) | yes | D7, D12 |
| 4.11 | auto-increment | +1/+2/+4 words for modes 3/6/9, +1 for mode 7, none else | +4/8/16 bytes for mode 0 unless no-increment; modes 1–5 unchanged; no-increment sticky until cleared | yes (D37.5) | D7, D37 |
| 4.11 | resident loader | 64 words at 0x1F00, HALT → RUN starts there | gone; boot ROM | yes | D22 |
| 4.11 | C&C port | ignores mode/addr except mode 3 write stream | gone (controllers are tiles) | yes | D25 |
| 4.12 | command block | 16 slots at 0x5FC0; mode 7 32-bit only, uncounted | 16 slots at **0x00007FC0** (silicon constant, boot ROM reads it); mode 0, counter 0; command landed on a director write covering slot 0 landing in port B; event stays pending after first command | yes | D6, D36, D37.1 |
| 4.12 | reply block | 256 words at 0x5BC0; mode 8 lands by source at word 2·src, counted `DMA_REPLY_REMAINING` | 256 words at **0x00007BC0**; mode-0 write to `REPLY` + 8·src, counted on any counter; a **default**, not a rule (every request names its reply address); own-pair a software convention | yes (D40) | D6, D40 |
| 4.12 | counters | four: SRAM (modes 2–5), CM (6), REPLY (8), PERIPH (9, at landing/drop); done = 0 and `mem_idle`/`cm_idle` | seven `WRITE_COUNTER_1–7`, named by the packet, decremented at K; done = 0 | yes | D14, D30, D40 |
| 4.12 | arming write | additive, `next = count + N − arrive`; 20-bit; clears counters done [9] and own done | same rule against the decrement; clears [7] and own done [n−1] | yes | D30 |
| 4.12 | HALT clears | RUN→HALT clears counters, done events, qualifiers | gone; only the reset (which also clears the delay line) | yes | D23 |
| 4.12 | `EVENTS` layout | [3:0] counter done (SRAM, CM, REPLY, PERIPH), [7:4] reserved, [8] GO, [9] counters done, [10] command landed, [11] engine run done, [12] request FIFO not full, [18:13] service lines, [19] request pending, [20] sender idle, [21] UART RX not empty, [22] UART TX not full, [31:23] reserved | [6:0] counter 1–7 done, [7] counters done, [8] GO, [9] command landed, [10] DMA run done (cleared by `PULSE_DMA`), [11] request FIFO not full, [12] request pending (live), [13]–[15] peripherals 1–3 (live), [31:16] reserved | yes (D40) | D16, D40 |
| 4.12 | mask reset | [8] \| [9] | [8] \| [7] | yes | D40 |
| 4.12 | `EVENTS_REQUIRED` | `WAKE_REQUIRED` | renamed; **controller only** (no window copy) | yes | D14 |
| 4.12 | `done` pulse | clears counters done and the four done events | clears counters done and the seven done events, **no other event** | yes (round 2) | — |
| 4.12 | done gate | GO only | GO only; **errors still take the hard interrupt** during the gate | implied | D21 |
| 4.12 | spurious wakes | — | allowed (peripheral live bit may lag a cycle); hardware aims for current by next instruction | yes | D16 |
| 4.13 | registers | `TILE_STATUS` (live bits, type, endpoint id, SRAM size), `TILE_CONDITIONS` | `PARAMS` (presence [2:0], endpoint [26:20], SRAM size [31:27]), `STATUS` (live), `ERRORS` | yes | D18 |
| 4.13 | `ERRORS` bits | 0 router ovf, 1 bad mode/size, 2 unexpected, 3 drift, 4 write-engine ovf, 5 table req ovf, 6 GATHER ovf, 7 read-port (overflow **or misaligned**), 8 done early, 9 bus, 10 illegal, 11 bad periph write, 12 periph cond 2, 13 periph cond 3, 14 receive underflow | same numbers; [1] = bad packet **or misaligned access**; [7] overflow only; **[11]–[13] = peripherals 1–3, live** (OR of each window's `ERRORS`) | yes | D18, D21 |
| 4.13 | clearing | entering HALT clears all | **sticky until the reset**; store to `ERRORS` = bus error | yes | D21, D23 |
| 4.13 | effect | error line to C&C | hard interrupt (once enabled) + error line (hardware, `error_latency`) | yes | D21, D24 |
| 4.13 | handler | — | terminal; 24 fixed reports; `EVENTS_PEEK` only; parks; ROM handler until image installs `mtvec`; interrupts off until boot part 2 | yes (D37.2, D37.3) | D20, D37 |
| 4.14 | commit | per-counter + `mem_idle`/`cm_idle`; `ts_busy` for tables | by K for every destination | yes | D3 |
| 4.15 | bucket write | mode 4 | mode 2 | yes | D6 |
| 4.16 | fifth port | tile 0 → C&C; FIFOs 169-bit; "to C&C" into C&C's receive FIFO | tiles 0–2 → C&C 1–3; FIFOs 256-bit; controller's internal router port *(Deferred, item 118)*; memory system at tile 1 | yes | D2, D25 |
| 4.18 | receive side | 16-entry queue in every tile, fed on receivers only | reads its target's queue in the tree; tiles read empty | yes (D38) | D38 |
| 4.18 | `request_busy` | `TILE_STATUS`[11] | `STATUS`[6] | yes (8.2) | D18 |
| 4.18 | request pending | `WAKE_EVENTS`[19] | `EVENTS`[12] | yes | D40 |
| 4.18 | HALT | keeps receive queue | reset empties FIFOs, tree and queues together | yes | D23 |
| 4.17 | whole section | one slot; type `TILE_STATUS`[15:12] (4 bits): 0 none, **1 replayer**; `PERIPH_RUN` = `TILE_PULSE`[2]; `periph_running` `TILE_STATUS`[3]; six service lines `WAKE_EVENTS`[18:13] (event / queue condition / item FIFO kinds); `PERIPH_STATUS_0–7` (0x870–0x88C); mode 9 (21-bit word addr) counted `DMA_PERIPH_REMAINING` at landing/drop; conditions [13:11]; HALT holds in reset; sender (`SENDER_SEND` 0x868: dest [6:0], mode [10:7], queue [12:11], **count [28:13] 16 bits**; `SENDER_ADDR` 0x86C 21-bit word addr; `sender_busy` `TILE_STATUS`[4]; sender idle `WAKE_EVENTS`[20]) | up to three per unit; regions 0x04–0x06; windows 0xFFFFFA00/FC00/FE00, 0x200 bytes; ten standard words (PARAMS, STATUS, ERRORS, PULSE_RUN, PULSE_DMA, EVENTS, EVENTS_PEEK, EVENTS_MASK, DMA_HDR, DMA_ADDR); counted events with depth; errors per peripheral; writes via mode 0 counted by field (dropped still counted); DMA (`DMA_HDR` count [11:0] **12 bits**, queue [13:12]) on its own router port; types 0–4 | yes (4.17 rows) | D9, D10, D14, D46 |
| 4.17 | write to absent peripheral | bad peripheral write [11], **counted** in `DMA_PERIPH_REMAINING` | **bad packet**, `ERRORS`[1], **uncounted** | implied by 3.1 bad-packet table; counting change not stated | D9, D32 |
| 4.17 | **type values** | 1 = replayer | 4.17.4 table: 0 none, 1 Ingress/Egress, 2 Updater/Binder/RW, 3 Tracer, **4 replayer** (D46) | **not in CHANGES-v1.2**; and **4.17.1 still says "1 the replayer" in v1.2 and v1.25 (spec line 1109)**: contradicts 4.17.4, 4.19, 8.11 → QUESTIONS | D46 |
| 4.17 | window words 8–9 without DMA | — | 4.17.4 says words 4, 8, 9 "unused (reserved)" = read 0, ignore stores; D48 W4-06: read 0, **load not flagged, only a store** | D48 not in CHANGES; spec wording "meshsim flags the access" for reserved words vs D48 → check | D48 |

### 4.19, 4.20 (done)

**Split for the replayer:** v1.1 → v1.2 changed **only its interface** (window, events, credits, region, errors, type);
4.19.3–4.19.7 and 4.19.12, 4.19.14 are **identical** apart from names (mode 9 → mode 0 to region 0x04;
`PERIPH_STATUS_1` → `EXPANSION_OPCODE`), and the opcode appendix is identical. **Row content changed only at v1.25.** So
per entry: interface items are step v1.2; row/table items are step v1.25; the few touched by both (expansion queue's
controller-fed set, invalid class, 4.19.9's loop) are "both".

| sec | what | v1.1 | v1.2 | v1.25 | step | in CHANGES? |
|---|---|---|---|---|---|---|
| 4.19 | type | 1 (`TILE_STATUS`[15:12]) | 4 (`PARAMS`[7:0], D46) | 4 | v1.2 | **v1.2 doc: no** (D46); v1.25 doc calls the leftover "type 1" editorial |
| 4.19.1 | interfaces | slot: `PERIPH_RUN`, `periph_running`, service lines, `PERIPH_STATUS_0–1`, `DMA_PERIPH_REMAINING`, conditions 11–13 | standard window; mode 0 to region 0x04; any write counter; events → tile `EVENTS`[13]; own `ERRORS` → tile `ERRORS`[11] | same | v1.2 | yes |
| 4.19.1 | reference | jolt-asic-planning PR #1 `4f36b3d` | same | jolt-cpp `821e1bd` (PR #2) | v1.25 | yes |
| 4.19.2 | log rows | LD read log, SD write log | same | load rows (LD, `VirtualLoadB/H/W`), store rows (SD, `VirtualStoreB/H/W`) | v1.25 | yes |
| 4.19.2 | state record | region 0 | "part 0" | same | v1.2 (name) | — |
| 4.19.2 | includes-store-log [115]=0 | write-log low-water quiet | loop leaves its credit bit out of the mask | + applies to all store rows | both | yes |
| 4.19.3 | expansion word | [55] untraced, [62:56] advice_reg, [63] spare; opcode 0–167 | same | [61:56] advice_reg (reg − 146), [62] sign_imm, [63] offset_imm; opcode 0–170 | v1.25 | yes |
| 4.19.4 | NOP word from controller | knows word from `PERIPH_STATUS_1` | `EXPANSION_OPCODE` | same; VirtualHostIO no longer remapped | both | yes |
| 4.19.5 | decode classes | 0 ROM, 1 ctl inline, 2 ctl system, 3 reserved (invalid); unused entries → controller class (builder rule); per-program tables | same | 2 = CSRRW/CSRRS only; unused → class 3 invalid; universal | v1.25 | yes |
| 4.19.6 | ROM load | mode 9, broadcast by C&C | mode 0 to region 0x04, broadcast by C&C 1 | same; fill 388/512 | v1.2 (path) + v1.25 (fill) | yes |
| 4.19.7 | rows | 91 opcodes, 85 executed, PEXT contiguous only | same | 94 in rows, 88 executed, PEXT any mask, narrow memory rows, renumbered | v1.25 | yes |
| 4.19.8 | refill protocol | low-water **live condition** (capacity − occupancy − pending ≥ half); claim by consuming `WAKE_EVENTS` adds half to pending; new expansion credits 128 words; expansion "still has words" test | **counted credits**, depth 2, half a queue each; `PULSE_RUN` sets 2 per queue; each new expansion sets expansion's to 2; replayer returns one per half queue popped; consuming load of replayer `EVENTS` takes one; loop masks out empty streams | same | v1.2 | yes |
| 4.19.8 | queue ports | mode 9 windows; 32/64-bit = bad peripheral write | region offsets; 32/64-bit = bad write [0] | same | v1.2 | yes |
| 4.19.8 | overrun | condition 12 | replayer `ERRORS`[1], still counted | same | v1.2 | yes |
| 4.19.8 | queue clears | at run, in HALT, when controller halts | at run and at the reset; **not** at a trap | same | v1.2 | yes |
| 4.19.9 | events | service lines [18:13]: code/read/write/expansion low-water (live), new expansion (sticky), done (sticky) | replayer `EVENTS`: [0] run done (d1), [1] DMA done (never), [2] code credit (d2), [3] read-log credit (d2), [4] write-log credit (d2), [5] expansion credit (d2), [6] new expansion (d1) *(Proposed, item 123)* | same | v1.2 | yes |
| 4.19.9 | words | `PERIPH_STATUS_0` rows run [15:0], next offset [31:16]; `PERIPH_STATUS_1` opcode | word 32 `ROWS_RUN`, 33 `EXPANSION_OFFSET`, 34 `EXPANSION_OPCODE` (D39) | same | v1.2 | yes |
| 4.19.9 | loop | waits on `DMA_PERIPH_REMAINING` done [3] | waits on refill counter done **and** DMA run done (`EVENTS_REQUIRED`) | + key(EXPANSION_OPCODE) lookup; inlines + CSR only | both | yes |
| 4.19.9 | template key | — (v1.1/1.2: exact instruction word, per program, ≤128) | same | normalized key, sorted universal index (75 at 821e1bd) | v1.25 | yes |
| 4.19.9 | EBREAK | controller-fed | same | ROM (with ECALL, MRET) | v1.25 | yes |
| 4.19.10 | map | 21-bit **word** addr; addr[20:18] window; state 0x00000, stream 0x00004, image 0x00008–0x00107, decode 0x00400–0x007FF, ROM 0x00800–0x00FFF, code queue 0x40000–0x7FFFF, read-log 0x80000–0x9FFFF, write-log 0xA0000–0xBFFFF, expansion 0xC0000–0xFFFFF | **byte** offsets in region 0x04; offset[22:20] part; state 0x000000, stream 0x000010, image 0x000020–0x00041F, decode 0x001000–0x001FFF, ROM 0x002000–0x003FFF, code 0x100000–0x1FFFFF, read-log 0x200000–0x27FFFF, write-log 0x280000–0x2FFFFF, expansion 0x300000–0x3FFFFF | same | v1.2 | yes ("map in byte offsets, parts at [22:20]") |
| 4.19.10 | bad write | unmapped, wrong size, **128-bit in window 0 off a multiple of 4**, single-ported memory while running, **queue port in HALT** | unmapped, wrong size, single-ported memory while running (misaligned → bad packet at director; HALT gone) | same | v1.2 | partly (the misaligned case moving to the director is implied) |
| 4.19.10 | ownership | stopped / running / HALT rows | stopped / running | same | v1.2 | yes |
| 4.19.11 | errors | tile conditions 11 bad periph write, 12 overrun, 13 invalid expansion | replayer `ERRORS` [0] bad write, [1] overrun, [2] invalid expansion → tile [11] *(Proposed, item 123)* | + class 3 meaning | v1.2 | yes |
| 4.19.11 | halt | HALT/controller halt stops at once | carries on after a trap until reset | same | v1.2 | yes |
| 4.19.12 | output row | v1.1 | unchanged | word 0 [86] imm_signed; imm for ShiftData; mem fields for 8 memory opcodes | v1.25 | yes |
| 4.19.13 | contract | LPDDR fetch mode 3 counted `DMA_SRAM_REMAINING` | mode 0 to 0x03 on its own counter; every refill but last exactly half (new rule) | same | v1.2 | yes |
| 4.19.15 | stream bound | ~8.6 Mbit | ~4.3 Mbit (W3-24, correction); pop past end → `ERRORS`[2] | same; E[15] by every load row | both | yes |
| 4.20 | UART | every tile, 0x890–0x89C, unwired no-ops; events [21], [22]; HALT doesn't reset it | only in C&C 1's peripheral (words in 8.14); line errors W1C status, never `ERRORS`; events live in peripheral `EVENTS` | same | v1.2 | yes |

### 5, 6, 7 (done)

| sec | what | v1.1 | v1.2 (= v1.25) | in CHANGES? | why |
|---|---|---|---|---|---|
| 5 | rows | counters done [9]; CM path modes 6–8; register loads 1 or 2; RUN/HALT `run_latency` | write counters at K; CM path mode 0 to 0x00/0x01; register 2 / table 1; **GO `go_latency`** (new param); error lines `error_latency`; reset → 0x02000000 | yes (5) except load latencies (D45) | D3, D23, D45 |
| 6.1 | reset/errors | RUN/HALT net, halt per tile, resident loader | no HALT; error end to end; toast pin (latched until reset); one watchdog (`WATCHDOG`, core cycles, restarted by write or `PULSE_GO`); hard/soft reset inputs (soft keeps LVDS, AXI and beyond, LPDDR ctl/PHY; list *(Open, item 124)*); what reset zeroes / leaves undefined / keeps; no reset hold | yes | D21, D23, D34, D35, D36 |
| 6.2 | boot | resident loader, HALT loads, `PTR[64]` staging | boot ROM exact (5 steps; tile branch tests the event [9] after GO; C&C branch waits for [9] with mask [9]); C&C 1 booted from outside; C&C 2/3 "started"; overlays uncounted, big SRAM always counted; staging by address in A7; `stage_delay` 32 | yes (D22, D31, D36, D37.1, D44 W5-26) | |
| 6.3 | done/READY/GO | GO from C&C register; READY into C&C | `PULSE_GO` in C&C 1's peripheral (clears READY 0–63, drives GO, restarts watchdog); READY into `READY_LINES`; "all tiles READY" live event with `READY_EXPECT` | yes | D23, D28 |
| 6.4 | receive phases | `mem_idle` before done | counters only | yes | D18 |
| 6.5 | block copy | mode 6, `DMA_CM_REMAINING` | mode 0 to 0x00/0x01 on a counter | yes | D6 |
| 6.6 | controllers | separate C&C design; RUN/HALT and GO registers; receive FIFO; error OR; REPORT | C&C 1–3 tile instances (2 MB big SRAM); chip functions in C&C 1's peripheral; C&C 1 waits `go_latency` + 1 after `PULSE_GO` before a command | yes | D1, D15, D25, D28, D36 |
| 6.7 | memory system | at C&C's port, src 64; destination kinds; mode 3/6/9 data; mode-8 replies; write tokens + mode-3 streams to C&C | C&C 2's peripheral at dst 65 (tile 1's fifth port); requests name 32-bit destination, data counter, reply counter and reply address; replies mode 0 to the named address; writes by tags into 2–4 KB windows *(Deferred, item 119)*; debug as log files | yes | D23, D25, D33, D40, W5-17 |
| 7 | memory subsystem | hangs off C&C | C&C 2's peripheral; LPDDR behind AXI survives soft reset | yes | D25, D35 |

### 8 Registers (done)

Full register maps for the guide's tables are taken from v1.1 8.1–8.13 and v1.2 8.1–8.15 (v1.25 = v1.2 here; the
replayer's words are unchanged at v1.25: only the expansion word and the output row change). Key items:

| what | v1.1 | v1.2 (= v1.25) | in CHANGES? |
|---|---|---|---|
| block | 0x800–0x89C, contiguous, unmapped (bus error) from 0x8A0 | 0x800–0x880 defined; **0x884–0x9F4 reserved (read 0, ignore stores)**; 0x9F8 exit word, 0x9FC trace word (meshsim only; reserved in hardware); windows at 0xA00/0xC00/0xE00 | yes |
| every offset | — | **every offset moved** | yes |
| v1.1 → v1.2 offset map | 0x800 TILE_STATUS → 0x800 PARAMS + 0x804 STATUS; 0x804 TILE_CONDITIONS → 0x808 ERRORS; 0x808 TILE_PULSE → 0x80C PULSE_DONE, 0x810 PULSE_DMA (+ window PULSE_RUN, C&C 1 PULSE_GO; 0x814 reserved); 0x80C ROUTER_SET_HDR → 0x828 ROUTER_HDR + 0x82C ROUTER_ADDR; 0x810–0x824 ROUTER_SET_W0..SEND_128 → 0x830–0x844; 0x828 DMA_CONFIG (W) → 0x858 (R/W); 0x82C DMA_RECORD_BASE_ADDRESS (W) → 0x85C (R/W); 0x830–0x83C DMA_*_REMAINING → 0x860–0x878 WRITE_COUNTER_1–7; 0x840 SRAM_RD_ADDR_QUEUE → 0x87C; 0x844 SRAM_RD_DEST_ADDR (W) → 0x880 (R/W); 0x848 WAKE_EVENTS → 0x818 EVENTS; 0x84C WAKE_PEEK → 0x81C EVENTS_PEEK; 0x850 WAKE_MASK → 0x820 EVENTS_MASK; 0x854 WAKE_REQUIRED → 0x824 EVENTS_REQUIRED; 0x858–0x864 REQUEST_* → 0x848–0x854; 0x868–0x86C SENDER_* → gone; 0x870–0x88C PERIPH_STATUS_0–7 → gone (window words); 0x890–0x89C UART_* → C&C 1 peripheral words 42–45 (TX, RX, DIVISOR, STATUS: **order changed**, DIVISOR before STATUS) | | yes (by name) |
| `TILE_STATUS` → `STATUS` bits | [0] rd_running, [1] rd_done, [2] GATHER busy, [3] periph_running, [4] sender_busy, [5] send_busy, [6] ts_busy, [7] mem_idle (reset 1), [8] cm_idle (reset 1), [9] done (reset 1, forced 1 in HALT), [10] command landed, [11] request_busy, [15:12] type, [19:16] spare, [26:20] endpoint, [31:27] size | `STATUS`: [0] rd_running, [1] rd_done, [2] GATHER busy, [3] send_busy, [4] done (**reset 0**), [5] command landed, [6] request_busy; `PARAMS`: [2:0] peripherals present, [26:20] endpoint (0–66), [31:27] size (7 on C&C 1–3) | yes; **`done` reset value 1 → 0** stated in v1.2 8.2 but not in CHANGES |
| `rd_running` falls | after last entry walked | after last table read completes, same cycle as DMA run done | yes (D41 F6) |
| `ERRORS` | as v1.1 conditions | [1] widened, [7] narrowed, [11]–[13] live peripheral ORs; sticky to reset | yes |
| pulses | `TILE_PULSE` bits [0] done, [1] start, [2] PERIPH_RUN, [3] GO (two actions in one store possible; "start and run act before done") | one register per action; a store of 1 fires; two actions in one store no longer possible | yes |
| `DMA_CONFIG` | write-only, reads 0, at 0x828 | R/W at 0x858; fields unchanged (bucket digit now "mode 2") | yes |
| counters | 20-bit, 4 | 20-bit, 7 | yes |
| table window | 0x2000–0x3FFF (0x3600–0x3FFF unmapped bus error) | 0xFFFF2000–0xFFFF35FF; STREAM_* layouts changed (4.2) | yes |
| `EVENTS_MASK`/`REQUIRED` width | [22:0] | [15:0] | implied |
| request port | 0x858–0x864 | 0x848–0x854; GET reads 0 on tiles (no queue) | yes |
| C header | `mesh.h` 1.1, flat registers | structs `TileRegs`, `PeripheralStd`, `ReplayerRegs`; `mesh.h` 1.2 | yes |
| general rule "a load takes 1 or 2 cycles" | | **v1.2/v1.25 8.1's general rule still says "1 or 2 cycles, fixed per register"**, while 4.11 (after D45) fixes register block and window words at 2 and table window at 1 | stale sentence → QUESTIONS (editorial) |
| 8.11 replayer window | — | words 0–2, 5–7, 32–34 as in 4.19.9 | yes |
| 8.13 UART | 0x890 TX, 0x894 RX, 0x898 STATUS, 0x89C DIVISOR | words 42 TX (0xAA8), 43 RX, **44 DIVISOR, 45 STATUS** (R/W1C) | yes |
| 8.14 C&C 1 | — | words 32–34 ERROR_LINES (bit b of word w = endpoint 32w + b; word 2 [0] unused, [1] C&C 2, [2] C&C 3), 35–36 READY_LINES, 37–38 READY_EXPECT, 39 PULSE_GO, 40 WATCHDOG, 41 LED, 42–45 UART, 46–63 reserved, 64–127 host *(Deferred)*; `ERRORS`[0] unit-error live; `EVENTS` [2] all READY live, [3] watchdog (d1), [4] RX not empty live, [5] TX not full live | yes |
| 8.15 | simulator `CMD_EXIT`, `ebreak`, trace at 0xFFFFFFF0 | exit word 0xFFFFF9F8, trace word 0xFFFFF9FC | yes |

### 9–13 (done)

**9 Invariants** (v1.1 → v1.2, then v1.25 only 16):
- v1.2 states that **every invariant is a meshsim check**; the hardware checks none as such; each invariant names its
  hardware counterpart (`ERRORS` bit) where one exists (D27).
- Restated: 1 (adds tiles 0–2's fifth-port FIFOs, the GATHER queue; hardware [0], [4], [5], [6]); 2 (spanning clause
  gone); 4 (counters only, no `mem_idle`/`cm_idle`; hardware [8]); 5 (new bad-packet set; hardware [1]); 9 (at the
  decrement; hardware [2]); 10 (mode 4); 11 (rewritten: command block only from C&C 1–3 uncounted, replies at own
  pair or the named address, CM writes inside memory and outside the blocks); 14 (C&C 1's command writes); 15 (`ERRORS`
  of every unit and every peripheral); 16 (peripherals generic + replayer); 17 (requests name counters and reply
  address; 1/3/1 limits; tag windows; d = 126 from a mesh tile; hardware [14]); 18 (modes 1–3; peripheral ports).
- **7 withdrawn** (RUN/HALT). **19–26 new:** 19 bounds; 20 alignment (hardware [1]); 21 reserved encodings (modes 6–7,
  count-only on counter 0, digit 5–7, reserved words/bits); 22 uncounted traffic mode 0 only, and nothing into data
  memory's first 16 bytes after boot; 23 no silent traps; 24 K covers the write path (2L + 4); 25 boot order; 26 no
  ignored start.
- v1.25: 16 only (contiguous-PEXT clause → statistic; offset_imm only on ShiftData; no misaligned memory row; rows equal
  jolt-cpp `821e1bd`). Zero shift masks stay undefined.

**10 Parameters** (v1.25 vs v1.1): link word 256; STREAM 17-bit count; read-port entry 43 bits; controller memories
32 KB + 32 KB, port B one 128-bit write/cycle; `reg_load_latency` 2 (block, windows) / 1 (table window); **K 8,
implementation-defined** (row cites "4.7" for K: should be 4.8, editorial); `error_latency` new; write counters 7 × 20
bits; boot ROM; watchdog; flush wait ~0.1 ms; request bus queues 128/256/128 at the root, credits = depth, limits 1/3/1;
events 16 bits used; UART C&C 1 only; command/reply blocks at 0x00007FC0/0x00007BC0; addresses 32-bit; peripherals
(3 per unit, windows, DMA ≤ 4,095 words); replayer row (v1.25 additions). **Gone:** `run_latency`, C&C receive FIFO,
packed CM word, per-tile 16-entry receive queue, the sender, per-tile UART. `go_latency` was already in v1.1's table.

**11 Logic budget:** per tile ≈ 0.52–0.61 mm² (v1.1 ≈ 0.38–0.45): +0.065 for the 256-bit word, +0.08–0.09 for the
memories, − sender, − UART; fifth port 0.04–0.07 (was 0.03–0.05); request bus + root queues ~74 Kbit. **Stale text:**
v1.25 11's controller row and 12 still say the 32 KB memories' one-cycle access "is with Tony, item 134", but item 134
is closed (D45: they meet it). Editorial → QUESTIONS.

**12 Physical:** control island ≈ 0.17–0.2 mm² (was ~0.1); receive queues at the root; no per-tile receive queue.

**13 Open items:** v1.2 keeps 1–116 with notes, adds 117–136; v1.25 adds 137–141 and a status table. Items an RTL engineer
must track: 117 (storing reserved link bits, may be trimmed), 118 (sending peripherals' router ports, deferred), 119
(file system / RW engine, deferred), 120 (table window address, proposed), 121 (CSR list and reset values, open with
Tony), 122 (report header, deferred), 123 (replayer event/error bit positions, proposed), 124 (soft-reset keep list,
open), 126 (requests dropped at root: hardware record?, open), 129 (STREAM_SRC stays 21-bit chunk?, open), 133 (misaligned
PTR advances: accepted), 139 (CSR in ROM: possible later), 140 (PEXT method, with Tony), 48 (`rp_latency` value,
proposed), 44–47 (replayer map details, proposed).

**Step 1 complete.**

## Step 2 log

- Guide sections 0, 1, 2.1–2.5 written and pushed. IDs: L (link/network), A (address map), T (engines), C (controller),
  P (peripherals). Every **[not in CHANGES]** tag is to be collected into guide section 5 and QUESTIONS.md.
- Someone added `v1.2/ERRATA.md` (E1–E4) on main while this ran: read it next turn and cite it where it overlaps.
- Still to write in the guide: 2.6 replayer, 2.7 control/reset/boot (5, 6.1–6.5), 2.8 C&C 1–3 (6.6, 6.7, 7, 8.14); 3
  register tables (block, `EVENTS`, `ERRORS`, `STATUS`/`PARAMS`, windows, C&C 1, replayer words, link word, expansion
  word, output row, opcode renumbering → appendix 2.0); 4 parameters and invariants; 5 differences not in CHANGES.
