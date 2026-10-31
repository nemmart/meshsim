# WORKER13 notes

Working notes for the change guide (`wip/spec/CHANGES-to-v1.25.md`). Plan agreed with the owner: (1) read and diff the
specs section by section, recording every difference here and committing after each section; (2) the guide's skeleton,
overviews and per-unit entries; (3) the register tables and sections 9–10; (4) QUESTIONS.md, REPORT.md, final pull.

Spec commit at start: `4856c0b` (main). Inputs read: CHANGES-v1.2, CHANGES-v1.25, DECISIONS-v1.25.

## Status

| pass | sections | state |
|---|---|---|
| v1.2 → v1.25 | whole spec (mechanical diff) | **done** |
| v1.1 → v1.2 | front matter, 1, 2 | next |
| v1.1 → v1.2 | 3 | |
| v1.1 → v1.2 | 4.1–4.10 | |
| v1.1 → v1.2 | 4.11–4.18 | |
| v1.1 → v1.2 | 4.19, 4.20 | |
| v1.1 → v1.2 | 5, 6, 7 | |
| v1.1 → v1.2 | 8 | |
| v1.1 → v1.2 | 9, 10, 11, 12, 13 | |
| DECISIONS-v1.2 | as needed for "why" | |

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
