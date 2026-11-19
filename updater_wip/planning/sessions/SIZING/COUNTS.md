# COUNTS.md: what count.py counts (generated; do not edit)

Costs (GE, NAND2 = 1): ff 5.67, fa 5.33, ha 3.33, mux 2.33, eq 3.00, lt 4.00, zd 1.00, and2 1.33, xor 2.00, icg 7.00.

## The summary

| | named states (their flip-flops) | all state flip-flops | control word, bits | control rows / distinct words | flip-flops as coded (LOW) | spec's | flip-flops, kGE LOW-HIGH | logic, kGE LOW-HIGH | memory, bits |
|---|---|---|---|---|---|---|---|---|---|
| cache | 12 (5) | 40 | 110 | 86 / 85 | 6,652 (4,157) | 6,876 | 23.6-37.7 | 17.8-46.6 | 4,304,896 |
| updater | 18 (8) | 26 | 95 | 106 / 93 | 4,193 (4,017) | 4,193 | 22.8-23.8 | 18.8-34.9 | 40,960 |
| arbiter | 0 (0) | 45 | 49 | 12 / 11 | 1,539 (1,155) | 1,539 | 6.5-8.7 | 3.8-8.2 | 0 |
| core | | | | | 2,014 (1,917) | | 10.9-11.4 | 23.1-31.5 | 526,336 |

## LOW and HIGH

| switch | HIGH | LOW |
|---|---|---|
| hold_mux | every register with an enable holds through a mux bit (an enable flop) | registers of 8 bits or more are clock-gated: holding costs no mux, one ICG a register |
| all_ports | the cache's four look-up ports, as specified (23.6.2) | v1.37's one client: look-up port 1 only (ports 0, 2 and 3 have no client in v1.37) |
| line_regs | the cache's five 1,024-bit line registers, as coded | three: the walker's line register and the miss handler's buffer dropped (the patch made on the way to the AXI writer, R beats written lane by lane into the data memory, which has 8 lane enables) |
| redundant | the registers the code keeps that copy another (a set beside its line, a victim's tag beside its row, the Updater's look_status and look_entry, a row counter kept twice), as coded | dropped |
| lru_clear | the Updater's LRU table cleared at START bit by bit (an AND a bit) | a valid bit an entry (32 flip-flops), the read gated |
| pm_rdreg | the page memory's read register as 128 flip-flops (17.10.7 counts it) | in the SRAM macro, as a macro's output latch normally is |
| aligned_add | an address plus an offset as an adder | a page's address is a multiple of 32 units (17.10.9), so a page plus a line or a half is a concatenation: no adder |
| shared_cut | the arbiter's burst cutter in every AR and AW slot (8) | one cutter a direction, muxed (2) |
| shared_r | the arbiter's R data register in each client's outputs (4 x 128) | one R data register shared by the four clients' outputs, each with its own valid |
| ctl_row | next-state and output logic at 8 GE a control-word row, plus an OR term a bit asserted | at 4 GE a row, plus the same OR terms |
| buffering | 15% added to the logic for buffering the wide selects and timing repair | none |

## cache

### Registers

| register | bits | count | sources | kind | note |
|---|---|---|---|---|---|
| look-up port request: valid, parked, wait for the miss handler, wait for the slot | 4 | 4 | 1 | flag |  |
| look-up port request: op 2, root 31, depth 2, page 30, level 2, entry 32 | 99 | 4 | 1 | port |  |
| look-up port request: the failed line 31 and its valid bit | 32 | 4 | 1 | port |  |
| fill read register: valid, wait for the miss handler, behind_write | 3 | 1 | 1 | flag | behind_write (23.6.4's ordering rule) is in the code and not in 23.6.5's 102: +1 |
| fill read register: root 31, depth 2, line 35 | 68 | 1 | 1 | hw |  |
| fill read register: the failed line and its bit | 32 | 1 | 1 | hw |  |
| fill write register: valid, wait for the miss handler, wait for the slot | 3 | 1 | 1 | flag |  |
| fill write register: root 31, depth 2, line 35 | 68 | 1 | 1 | hw |  |
| fill write register: the line, 1,024 bits | 1,024 | 1 | 1 | hw |  |
| fill write register: the failed line and its bit | 32 | 1 | 1 | hw |  |
| front end: round-robin pointer 3, the source a miss was handled for 3 and its valid bit | 7 | 1 | 1 | hw |  |
| walker: state | 2 | 1 | 1 | flag |  |
| walker: source 3 | 3 | 1 | 1 | hw |  |
| walker: level k 2 | 2 | 1 | 2 | hw |  |
| walker: the discard's line count 6 | 6 | 1 | 2 | hw |  |
| walker: the page at level k 31 | 31 | 1 | 2 | hw |  |
| walker: the line being looked up 31 | 31 | 1 | 1 | hw |  |
| walker: the hit way 3 | 3 | 1 | 1 | hw |  |
| walker: every entry owned so far, a read pending | 2 | 1 | 1 | flag |  |
| walker: the line register | 1,024 | 1 | 2 | line | the line as read, patched for an update: LOW drops it |
| walker: a reply waiting for the bus: status 3, level 2, entry 32 | 37 | 1 | 1 | hw |  |
| miss handler: state | 3 | 1 | 1 | flag |  |
| miss handler: allocates, error, arvalid, a copy made | 4 | 1 | 1 | flag |  |
| miss handler: the line to install 31 | 31 | 1 | 1 | hw |  |
| miss handler: its set 9 | 9 | 1 | 1 | red | x[8:0]: a copy |
| miss handler: its way 3, the source it serves 3, the beats in 4 | 10 | 1 | 2 | hw |  |
| miss handler: the victim's tag 22, valid, dirty | 24 | 1 | 1 | red | the row as read holds them |
| miss handler: the set's tag row | 216 | 1 | 1 | hw |  |
| miss handler: the line buffer | 1,024 | 1 | 3 | line | R beats, the victim from the data memory, a fill write's line: LOW drops it |
| AXI writer: busy, the address taken | 2 | 1 | 1 | flag |  |
| AXI writer: the beat 4 | 4 | 1 | 2 | hw |  |
| AXI writer: the line address | 25 | 1 | 2 | hw | 25 bits at LPDDR_ADDRESS_BITS 32: AW's address is 25 bits; 23.6.5 counts 31 |
| AXI writer: the line | 1,024 | 1 | 2 | hw |  |
| AXI writer: the queue of kinds: 4 kinds, head 2, count 3 | 9 | 1 | 1 | hw | 2 would do: one update and one write-back at most are outstanding |
| update slot: valid | 1 | 1 | 1 | flag |  |
| update slot: source 2, way 3, level 2, the entry before 32, the line 31 | 70 | 1 | 1 | hw | the source is a look-up port: 2 bits (23.6.5 counts 3) |
| update slot: its set 9 | 9 | 1 | 1 | red | the line's [8:0]: a copy |
| the tag-write guard: set 9, in or after the read 1 | 10 | 1 | 1 | hw |  |
| the error | 1 | 1 | 1 | flag |  |
| outputs: the six readies | 6 | 1 | 1 | flag |  |
| outputs: the reply bus: valid 1, client 2, status 3, level 2, entry 32 | 40 | 1 | 3 | out |  |
| outputs: fill read response valid 1, status 2 | 3 | 1 | 1 | out | status 0 to 3: 2 bits (23.6.5: 3) |
| outputs: fill read response data | 1,024 | 1 | 1 | out |  |
| outputs: fill write response valid, status | 2 | 1 | 1 | out |  |
| outputs: AR valid 1, line address 25; AW the same | 52 | 1 | 1 | out |  |
| outputs: W valid 1, wdata 128, wlast 1 | 130 | 1 | 1 | out |  |
| outputs: the error | 1 | 1 | 1 | out |  |

As coded: **6,652** flip-flops (the specification: 6,876). LOW: 4,157.

Not counted:

- statistics: hits, misses, evictions, write_backs, ..., latencies, walk_lines: the simulator's own
- Held.missed_line (32 a source) and Held.t_accept (64 a source): statistics only: the hit/miss classes and latencies
- diverged_[16], n_diverged_, check_store: the stale-line check (23.6.11), not hardware
- walker row_ (216): a wire: the tag memory's output register, which only the walker reads, holds the row until the walker's next read. 23.6.5 counts it
- walker wdirty_ (1): used only by the stale-line check. 23.6.5 counts it
- walker set_ (9): a wire: x[8:0]. 23.6.5 does not count it either
- tag_read_t_, mh_tag_write_t_ (cycle numbers): hardware keeps one bit for them, counted in the guard
- bus_taken_, mh_data_read_, mh_writes_: wires within a cycle

### Datapath operators

| operator | kind | bits | count | when | GE |
|---|---|---|---|---|---|
| tag compare: 8 ways x 22 bits | eq | 22 | 8 | both | 528 |
| tag compare: valid AND, the hit's encoder 8 -> 3 | ge | 30 | 1 | both | 30 |
| the miss handler's set against the line's set | eq | 9 | 1 | both | 27 |
| the guard's set against the line's set | eq | 9 | 1 | both | 27 |
| the slot's set and way against the line's (and the victim's exclusion) | eq | 12 | 1 | both | 36 |
| the slot's way decoded 3 -> 8 for the victim | ge | 10 | 1 | both | 10 |
| the failed line: the source's, 6:1 (high) or 3:1 (low, three sources) | mux:6 | 31 | 1 | high | 361 |
| the failed line: the source's, 3:1 | mux:3 | 31 | 1 | low | 144 |
| the failed line against the line looked up | eq | 31 | 1 | both | 93 |
| the victim: the first invalid way; else age 7, or 6 if 7 is the slot's | zd | 3 | 16 | both | 48 |
| the victim: its priority | ge | 25 | 1 | both | 25 |
| touch: the touched way's age, 8:1 | mux:8 | 3 | 1 | both | 49 |
| touch: each way's age below it | lt | 3 | 8 | both | 96 |
| touch: each way's age plus one | ha | 3 | 8 | both | 80 |
| touch: each way's new age: 0, age + 1 or age | mux:3 | 3 | 8 | both | 112 |
| touch: the way, the walker's or the miss handler's | mux:2 | 3 | 1 | both | 7 |
| tag row write: the installed way's tag | mux:2 | 22 | 8 | both | 410 |
| tag row write: the row, as read or the miss handler's | mux:2 | 216 | 1 | both | 503 |
| tag row write: valid and dirty bits: set, clear | ge | 64 | 1 | both | 64 |
| tag write address: the line's set or the miss handler's | mux:2 | 9 | 1 | both | 21 |
| the entry: 32 of the line's 1,024 bits, 32:1 | mux:32 | 32 | 1 | both | 2,311 |
| the entry: a hole (all ones), owned bit | zd | 32 | 1 | both | 32 |
| the index: the page or the line's page | mux:2 | 30 | 1 | both | 70 |
| the index: its 10-bit field at the level, 3:1 | mux:3 | 10 | 1 | both | 47 |
| the next line: base (root or the page at k) | mux:2 | 31 | 1 | both | 72 |
| the next line: offset (index >> 5, line & 31, discard count), 3:1 | mux:3 | 6 | 1 | both | 28 |
| the next line: base + offset | fa | 6 | 1 | high | 32 |
| the next line: carry into the base's upper bits | ha | 25 | 1 | high | 83 |
| beyond: the page or line past the depth's reach, the level against the depth | zd | 65 | 1 | both | 65 |
| beyond: the masks by depth | ge | 40 | 1 | both | 40 |
| the discard's 32nd line; k at the depth | zd | 10 | 1 | both | 10 |
| the update's patch: the entry into its 32 bits of the line | mux:2 | 1,024 | 1 | both | 2,386 |
| the update's patch: the 5 -> 32 decode | ge | 40 | 1 | both | 40 |
| the data memory's write data: a fill write's line, the patched line, the miss handler's line (HIGH) or an R beat (LOW) | mux:3 | 1,024 | 1 | both | 4,772 |
| the data memory: lane enables | ge | 20 | 1 | both | 20 |
| the data memory: read address, the walker's or the miss handler's | mux:2 | 12 | 1 | both | 28 |
| the data memory: write address, 3:1 | mux:3 | 12 | 1 | both | 56 |
| the fill read response: the line or zeros | and2 | 1,024 | 1 | both | 1,362 |
| the W beat: 8:1 of 128 bits | mux:8 | 128 | 1 | both | 2,088 |
| the miss handler's beat lane decode | ge | 15 | 1 | both | 15 |
| the victim's tag for its write-back address, 8:1 | mux:8 | 22 | 1 | both | 359 |
| counters: k, the discard count, the beats in, the beat out | ha | 16 | 1 | both | 53 |
| front end: each source eligible; the round robin over six with the miss's source first | ge | 120 | 1 | both | 120 |
| front end: the readies | ge | 24 | 1 | both | 24 |
| the kinds queue | ge | 20 | 1 | both | 20 |

### Control

**walker.** States: idle, compare, read (W_DATA), reply. State flip-flops: 2. Control word: 51 bits, 45 rows, 44 distinct words (41 without the next state). 

Signals: next 2, pick 1, k_op 2, dk_op 2, cur_sel 2, x_ld 1, x_base 2, x_off 2, way_ld 1, own_op 2, line_op 2, pend_ld 1, tag_re 1, tag_we 1, tag_src 2, data_re 1, data_we 1, data_wsrc 1, data_1lane 1, rep_valid 1, rep_pend 1, rep_status 3, rep_level_k 1, rep_entry_e 1, frr_valid 1, frr_status 2, frr_zero 1, fwr_valid 1, fwr_status 1, req_free 1, req_wait 2, req_park 1, req_failclr 1, mh_start 1, mh_alloc 1, wr_upd 1, slot_ld 1, err_set 1.

<details><summary>its rows</summary>

- idle: nothing to pick: `nothing`
- idle: a discard starts: `pick=1 k_op=1 cur_sel=1 x_ld=1 own_op=1 tag_re=1 next=1 dk_op=1 x_base=1 x_off=2`
- idle: a look-up, an update or a fill (depth > 0) starts: `pick=1 k_op=1 cur_sel=1 x_ld=1 own_op=1 tag_re=1 next=1`
- idle: a fill of a depth-0 file starts: `pick=1 k_op=1 cur_sel=1 x_ld=1 own_op=1 tag_re=1 next=1 x_off=1`
- idle: a look-up or update beyond, the bus free: `pick=1 rep_valid=1 rep_status=3 req_free=1`
- idle: a look-up or update beyond, the bus the slot's: `pick=1 pend_ld=1 rep_status=3 next=3`
- idle: a fill read beyond: `pick=1 frr_valid=1 frr_status=3 frr_zero=1 req_free=1`
- idle: a fill write beyond: `pick=1 fwr_valid=1 fwr_status=1 req_free=1`
- compare: the row read before the miss handler's write: read again: `tag_re=1 next=1`
- compare: the same, the write not landed yet: wait: `next=1`
- compare: the miss handler's set, or a miss with it busy: wait for it: `req_wait=1`
- compare: the slot's line (a discard's or a fill write's): wait for the slot: `req_wait=2`
- compare: a hit, the ports the miss handler's this cycle: wait: `next=1`
- compare: a discard's line not in the cache; the next line: `dk_op=2 x_ld=1 x_base=1 x_off=2 tag_re=1 next=1`
- compare: a discard's line not in the cache; the 32nd: the reply: `rep_valid=1 req_free=1`
- compare: a discard's line not in the cache; the 32nd: the reply waits for the bus: `pend_ld=1 next=3`
- compare: a discard's line in the cache, invalidated; the next line: `tag_we=1 tag_src=2 dk_op=2 x_ld=1 x_base=1 x_off=2 tag_re=1 next=1`
- compare: a discard's line in the cache, invalidated; the 32nd: the reply: `tag_we=1 tag_src=2 rep_valid=1 req_free=1`
- compare: a discard's line in the cache, invalidated; the 32nd: the reply waits for the bus: `tag_we=1 tag_src=2 pend_ld=1 next=3`
- compare: a fill write's data line hits: written, dirty; answered: `data_we=1 tag_we=1 tag_src=1 fwr_valid=1 req_free=1`
- compare: a hit: the line read, the way touched: `data_re=1 tag_we=1 way_ld=1 line_op=1 next=2`
- compare: its fetch failed: lpddr_error, the bus free: `rep_valid=1 rep_status=4 rep_level_k=1 req_free=1 req_failclr=1`
- compare: its fetch failed: lpddr_error, waits for the bus: `pend_ld=1 rep_status=4 rep_level_k=1 req_failclr=1 next=3`
- compare: a fill read's fetch failed: beyond, the error set: `frr_valid=1 frr_status=3 frr_zero=1 err_set=1 req_free=1 req_failclr=1`
- compare: a fill write's fetch failed: error: `fwr_valid=1 fwr_status=1 req_free=1 req_failclr=1`
- compare: a miss: the victim and the line to the miss handler: `mh_start=1 req_wait=1`
- compare: a fill write's data line misses: allocated: `mh_start=1 mh_alloc=1 req_wait=1`
- read: a fill read's data line: ok: `frr_valid=1 req_free=1`
- read: a fill read's data line: not_owned: `frr_valid=1 frr_status=2 req_free=1`
- read: a look-up's (or an update's above its level) hole, the bus free: `rep_valid=1 rep_status=1 rep_level_k=1 rep_entry_e=1 req_free=1`
- read: the same, hole, waits for the bus: `pend_ld=1 rep_status=1 rep_level_k=1 rep_entry_e=1 next=3`
- read: a look-up's (or an update's above its level) not_owned, the bus free: `rep_valid=1 rep_status=2 rep_level_k=1 rep_entry_e=1 req_free=1`
- read: the same, not_owned, waits for the bus: `pend_ld=1 rep_status=2 rep_level_k=1 rep_entry_e=1 next=3`
- read: a look-up's (or an update's above its level) ok, the bus free: `rep_valid=1 rep_level_k=1 rep_entry_e=1 req_free=1`
- read: the same, ok, waits for the bus: `pend_ld=1 rep_level_k=1 rep_entry_e=1 next=3`
- read: on to the next level: `cur_sel=2 k_op=2 x_ld=1 x_base=2 tag_re=1 next=1`
- read: on to the next level, not owned: `cur_sel=2 k_op=2 x_ld=1 x_base=2 tag_re=1 own_op=2 next=1`
- read: on to the next level (the data line): `cur_sel=2 k_op=2 x_ld=1 x_base=2 x_off=1 tag_re=1 next=1`
- read: on to the next level (the data line), not owned: `cur_sel=2 k_op=2 x_ld=1 x_base=2 x_off=1 tag_re=1 own_op=2 next=1`
- read: an update's target, the AXI writer busy or the port the miss handler's: wait: `next=2`
- read: an update's target: patched, written, handed to the writer, parked: `line_op=2 data_we=1 data_wsrc=1 data_1lane=1 wr_upd=1 slot_ld=1 req_park=1`
- read: a fill read's hole: `frr_valid=1 frr_status=1 frr_zero=1 req_free=1`
- read: a fill write's hole or not owned: `fwr_valid=1 fwr_status=1 req_free=1`
- reply: the bus the slot's again: wait: `next=3`
- reply: the waiting reply on the bus: `rep_valid=1 rep_pend=1 req_free=1`

</details>

**miss handler (with the AXI reader).** States: idle, read the victim, hand over, wait, fetch, fill, install, fail. State flip-flops: 3. Control word: 24 bits, 24 rows, 24 distinct words (17 without the next state). 

Signals: next 3, ld 1, data_re 1, copied_op 2, buf_src 2, got_op 2, err_op 2, ar_op 2, wr_wb 1, tag_we 1, tag_fail 1, data_we 1, guard_ld 1, fail_owner 1, wake 1, prio_set 1, err_set 1.

<details><summary>its rows</summary>

- idle: `nothing`
- idle: a dirty victim: `ld=1 next=1`
- idle: an allocation, the victim clean: `ld=1 buf_src=2 next=6`
- idle: a fetch: `ld=1 ar_op=1 next=4`
- read the victim: `data_re=1 copied_op=2 next=2`
- hand over: copied, the writer busy: `buf_src=1 copied_op=1 next=2`
- hand over: copied and handed: `buf_src=1 copied_op=1 wr_wb=1 next=3`
- hand over: the writer busy: `next=2`
- hand over: handed: `wr_wb=1 next=3`
- hand over: handed, the allocation's line in: `wr_wb=1 buf_src=2 next=3`
- wait: `next=3`
- wait: B ok, fetch: `ar_op=1 next=4`
- wait: B ok, install the allocation: `next=6`
- wait: B an error, fetch: `err_set=1 ar_op=1 next=4`
- wait: B an error, install: `err_set=1 next=6`
- fetch: arvalid: `next=4`
- fetch: AR taken: `ar_op=2 got_op=1 err_op=2 next=5`
- fill: a beat: `buf_src=3 got_op=2 next=5`
- fill: a beat with an error: `buf_src=3 got_op=2 err_op=1 next=5`
- fill: the last beat, all ok: `buf_src=3 got_op=2 next=6`
- fill: the last beat, an error: `buf_src=3 got_op=2 next=7`
- fill: no beat: `next=5`
- install: `tag_we=1 data_we=1 guard_ld=1 wake=1 prio_set=1`
- fail: `tag_we=1 tag_fail=1 guard_ld=1 fail_owner=1 wake=1 prio_set=1`

</details>

**AXI writer, B and the update slot.** States: idle, busy (the address taken or not, beats 0 to 8). State flip-flops: 6. Control word: 21 bits, 13 rows, 13 distinct words (13 without the next state). AW, W and B run beside each other: the rows are the writer's, and B's, separately

Signals: busy_op 2, aw_op 2, beat_op 2, buf_src 2, addr_src 1, kind_push 1, kind 1, kind_pop 1, slot_reply 1, slot_err 1, slot_clr 1, wake_slot 1, mh_b 1, mh_b_err 1, awvalid 1, wvalid 1, wlast 1.

<details><summary>its rows</summary>

- idle: `nothing`
- an update's line taken: `busy_op=1 aw_op=2 beat_op=1 buf_src=1 kind_push=1`
- a write-back taken: `busy_op=1 aw_op=2 beat_op=1 buf_src=2 addr_src=1 kind_push=1 kind=1`
- busy: AW and a beat offered: `awvalid=1 wvalid=1`
- busy: AW taken: `aw_op=1 wvalid=1`
- busy: a beat taken: `beat_op=2 awvalid=1 wvalid=1`
- busy: AW and a beat taken: `aw_op=1 beat_op=2 wvalid=1`
- busy: the last beat offered: `wvalid=1 wlast=1`
- busy: done: `busy_op=2`
- B: an update's, ok: `kind_pop=1 slot_reply=1 slot_clr=1 wake_slot=1`
- B: an update's, an error: `kind_pop=1 slot_reply=1 slot_err=1 slot_clr=1 wake_slot=1`
- B: a write-back's, ok: `kind_pop=1 mh_b=1`
- B: a write-back's, an error: `kind_pop=1 mh_b=1 mh_b_err=1`

</details>

**front end.** States: a source: empty, waiting, waiting for the miss handler, waiting for the slot, parked (an update in the slot), behind a write (fill's read). State flip-flops: 29. Control word: 14 bits, 4 rows, 4 distinct words (4 without the next state). the readies are each register's valid, and the reply to its port; the six sources are alike: one row a source a cycle, a request taken, freed or set waiting by the walker's and the miss handler's signals above

Signals: ld 6, ready 6, rr_ld 1, prio_clr 1.

<details><summary>its rows</summary>

- nothing: `nothing`
- a request taken at a port: `ld=1`
- a pick by round robin: `rr_ld=1 prio_clr=1`
- a pick of the miss's source: `prio_clr=1`

</details>

### Gates

| part | LOW, GE | HIGH, GE |
|---|---|---|
| flip-flops | 23,570 | 37,717 |
| registers' input muxes and holds (and LOW's ICGs) | 494 | 22,394 |
| datapath operators | 16,328 | 16,660 |
| next-state and output logic | 976 | 1,436 |
| logic, with HIGH's buffering | 17,798 | 46,563 |

Largest, HIGH: the registers' input multiplexers and holds 22,394 GE; the data memory's write data: a fill write's line, the patched line, the miss handler's line (HIGH) or an R beat (LOW) 4,772 GE; the update's patch: the entry into its 32 bits of the line 2,386 GE; the entry: 32 of the line's 1,024 bits, 32:1 2,311 GE; the W beat: 8:1 of 128 bits 2,088 GE.

Largest, LOW: the data memory's write data: a fill write's line, the patched line, the miss handler's line (HIGH) or an R beat (LOW) 4,772 GE; the update's patch: the entry into its 32 bits of the line 2,386 GE; the entry: 32 of the line's 1,024 bits, 32:1 2,311 GE; the W beat: 8:1 of 128 bits 2,088 GE; the fill read response: the line or zeros 1,362 GE.

Memories: data memory: 4,096 x 1,024, one read and one write port, 8 lane enables: 4,194,304 bits; tag memory: 512 x 216, one read and one write port: 110,592 bits.

## updater

### Registers

| register | bits | count | sources | kind | note |
|---|---|---|---|---|---|
| words 32-37: RUN_CONFIG 7, the three roots 96 | 103 | 1 | 1 | hw | written by the bus |
| WORD_COUNT, PAGE_COUNT | 64 | 1 | 2 | hw | the bus, or counted down |
| the statistics counters, 6 x 32 | 192 | 1 | 1 | hw |  |
| the control block's: busy, ERRORS 5, the done and credit pulses | 8 | 1 | 1 | flag |  |
| the input queue: 4 words of 128 | 512 | 1 | 1 | hw |  |
| the input queue: head 2, count 3, ready | 6 | 1 | 1 | flag |  |
| decoder: the bit buffer | 194 | 1 | 1 | out | loaded every cycle from the shift-and-insert network, which holds it by shifting 0 |
| decoder: its bit count 8 | 8 | 1 | 1 | out |  |
| decoder: the LRU table, entries 1 to 31 | 1,984 | 1 | 1 | hw | each loads the entry in front of it |
| decoder: the LRU table, entry 0 | 64 | 1 | 2 | hw | a hit's entry or a miss's word |
| decoder: LOW's LRU valid bits | 32 | 1 | 1 | lruv |  |
| decoder: the run of values: next index 32, values left 32 | 64 | 1 | 2 | hw |  |
| decoder: the output register: valid 1, index 32, value 64, type 2 | 99 | 1 | 1 | hw |  |
| free page queue: head 8, count 9, gone since the credit 7 | 24 | 1 | 1 | hw |  |
| state machine: state 5, level 2, file 1 | 8 | 1 | 1 | flag |  |
| state machine: for each file the look-up's level 2 and whether it takes pages | 6 | 1 | 1 | hw |  |
| state machine: for each file the look-up's status 3 and entry 32 | 70 | 1 | 1 | red | never read: look_status is not used, and look_entry[TRACER] is not; look_entry[IMAGE] equals src[IMAGE] wherever it is read |
| state machine: for each file its source at this level 32 | 64 | 1 | 2 | hw |  |
| state machine: the page memory's contents: valid 1, source 32, level 2 | 35 | 1 | 1 | hw |  |
| state machine: the next level's entry caught 32, the new page 31, the fill row 8 | 71 | 1 | 1 | hw |  |
| state machine: the held page: valid, owned, a hole; halves had and dirty | 7 | 1 | 1 | flag |  |
| state machine: the held page: number 23, where written 31, where its halves come from 31 | 85 | 1 | 3 | hw |  |
| AXI port: AR valid | 1 | 1 | 1 | flag |  |
| AXI port: AR address 32, len 8 | 40 | 1 | 1 | out | its low 11 bits and 7 of len's are constant: a half page at a multiple of 2,048, len 127 or 255 |
| AXI port: the read: active, clearing owned bits, error; row 8; the entry to catch 10 and its valid bit | 22 | 1 | 1 | hw |  |
| AXI port: AW valid | 1 | 1 | 1 | flag |  |
| AXI port: AW address 32, len 8 | 40 | 1 | 1 | out |  |
| AXI port: the write: active, error, each half's source 4; rows left 9, next row 8 | 23 | 1 | 1 | hw |  |
| AXI port: the W path: a row pending, its row 8 | 1 | 1 | 1 | flag |  |
| AXI port: the W path: the pending row 8 | 8 | 1 | 1 | red | always equal to the next row |
| AXI port: W valid, wlast, the beat 128 | 130 | 1 | 2 | out | holds while wready is 0 |
| look-up port: valid | 1 | 1 | 1 | flag |  |
| look-up port: op 2, root 31, a 23-bit page, level 2, entry 32 | 90 | 1 | 1 | out |  |
| look-up port: depth 2 | 2 | 1 | 1 | red | RUN_CONFIG's depth |
| look-up port: a reply awaited | 1 | 1 | 1 | flag |  |
| look-up port: the last reply: status 3, level 2, entry 32 | 37 | 1 | 1 | hw |  |
| the page memory's read register | 128 | 1 | 1 | pmreg |  |

As coded: **4,193** flip-flops (the specification: 4,193). LOW: 4,017.

Not counted:

- lookups, updates, read_bursts, ..., values (Ctl's counts): the simulator's figures
- shadow_uncut, cut_half_reads_run: a shadow for the comparison with the black box
- UpdaterTop.pages_seen (2^23 bits), push_src: invariant 36's checks, beside the controller, not hardware
- PageMemory.reads, writes: counts
- the root and free page checks at CHECK and TAKE, the header word's wrap check: UB the simulator stops on

### Datapath operators

| operator | kind | bits | count | when | GE |
|---|---|---|---|---|---|
| decoder: the code's bits needed, twice (this code, the next) | ge | 30 | 1 | both | 30 |
| decoder: the bits there against the bits needed, twice | lt | 8 | 2 | both | 64 |
| decoder: the bit buffer shifted right by 0, 6, 34, 64 or 66 | mux:5 | 194 | 1 | both | 1,808 |
| decoder: a 128-bit word put in at 0 to 65 (a 7-level shifter) | mux:2 | 1,120 | 1 | both | 2,610 |
| decoder: the word ORed in; the buffer cleared at START | and2 | 194 | 2 | both | 516 |
| decoder: the bit count: minus the code, plus 128 | fa | 8 | 2 | both | 85 |
| decoder: the LRU hit's entry, 32:1 of 64 bits | mux:32 | 64 | 1 | both | 4,623 |
| decoder: the move to front: which entries move (k <= i, or every one on a miss) | ge | 100 | 1 | both | 100 |
| decoder: the update word: raw, a hit, a 34-bit or a 66-bit miss | mux:4 | 64 | 1 | both | 447 |
| decoder: the LRU table cleared at START, bit by bit (HIGH) | and2 | 2,048 | 1 | high | 2,724 |
| decoder: the LRU read gated by its valid bit (LOW) | and2 | 64 | 1 | low | 85 |
| decoder: WORD_COUNT - 1, the next index + 1, the values left - 1 | ha | 32 | 3 | both | 320 |
| decoder: min(values left, WORD_COUNT) | lt | 32 | 1 | both | 128 |
| decoder: min's mux | mux:2 | 32 | 1 | both | 75 |
| decoder: the type: n >= 512, n >= 256, index & 511, index & 255, values left 0 | zd | 80 | 1 | both | 80 |
| the input queue: its head word, 4:1 of 128 | mux:4 | 128 | 1 | both | 895 |
| the input queue: write enables; head and count | ge | 30 | 1 | both | 30 |
| next: the value's page against the held page, 23 bits | eq | 23 | 1 | both | 69 |
| next: beyond (the page past 2^(10 depth)) | zd | 23 | 1 | both | 23 |
| the page memory: its row, 5:1 of 8 bits | mux:5 | 8 | 1 | both | 75 |
| the page memory: its write data: an R beat, the value (to both halves), zeros | mux:3 | 128 | 1 | both | 596 |
| the page memory: its half enables | ge | 10 | 1 | both | 10 |
| R: an index page's entries: a hole detect a lane, the owned bit cleared | zd | 128 | 1 | both | 128 |
| R: owned bits cleared | and2 | 4 | 1 | both | 5 |
| R: the entry to catch: its row against the beat's, its lane 4:1 | eq | 8 | 1 | both | 24 |
| R: the caught lane | mux:4 | 32 | 1 | both | 224 |
| W: the beat from the page memory, zeros or ones | and2 | 256 | 1 | both | 340 |
| AR and AW: the address's source, 5:1 and 4:1 of 21 bits | mux:5 | 21 | 1 | both | 196 |
| AW: the address's source | mux:4 | 21 | 1 | both | 147 |
| AR and AW: + 2,048 x half (an adder in HIGH) | ha | 21 | 2 | high | 140 |
| the look-up request: root (image or Tracer), page, level, entry | mux:2 | 54 | 1 | both | 126 |
| the look-up request: level and entry, 3:1 | mux:3 | 34 | 1 | both | 158 |
| the digit of the held page at a level, 3:1 of 10 bits | mux:3 | 10 | 1 | both | 47 |
| the page memory holds the source: pm_src against from, pm_level against k | eq | 34 | 1 | both | 102 |
| the fault's pages needed: two (depth - level), their sum, against PAGE_COUNT | fa | 3 | 3 | both | 48 |
| the fault's pages needed against PAGE_COUNT | lt | 32 | 1 | both | 128 |
| PAGE_COUNT - 1 | ha | 32 | 1 | both | 107 |
| counters: the read row, the fill row, the rows left, the next row, the free page queue's head, count, gone | ha | 57 | 1 | both | 190 |
| the fill row's last; the credit's 128th; the rows left 1 | zd | 24 | 1 | both | 24 |
| the free page queue: its write address, head + count | fa | 8 | 1 | both | 43 |
| the statistics counters + 1 | ha | 32 | 6 | both | 639 |
| the reply's status decoded; the errors | ge | 30 | 1 | both | 30 |
| the window: words 32-37 and 48-53 read, 12:1 of 32 bits | mux:12 | 32 | 1 | both | 820 |
| the window: words 32-37 written, the decode | ge | 30 | 1 | both | 30 |
| the control block: done and credit event counts (depth 1 and 2) | ge | 40 | 1 | both | 40 |

### Control

**the run.** States: idle, stopped, check, root read, root write, next, half read, fill, flush, flush read, flush write, flush update, look-up, level, load, take, new write, new update. State flip-flops: 8. Control word: 70 bits, 73 rows, 60 distinct words (47 without the next state). 

Signals: next 5, level_op 2, side_op 2, look_ld 1, src_ld 2, pm_ok_op 2, pm_src_ld 1, new_page_ld 1, fill_row_op 2, held_op 2, held_kind 2, held_page_ld 1, held_at_sel 2, have_op 3, dirty_op 2, rd_go 1, rd_addr 2, rd_half 2, rd_two 1, rd_xform 2, rd_catch 1, wr_go 1, wr_addr 2, wr_half 2, wr_two 1, wr_src0 2, wr_src1 2, lk_go 1, lk_op 1, lk_root 1, lk_page 1, lk_level 2, lk_entry 1, pm_patch 1, pm_fill 1, take 1, fp_pop 1, pc_dec 1, busy_clr 1, done 1, stop 1, err 3, stat 2, credit 1.

<details><summary>its rows</summary>

- idle: nothing: `nothing`
- stopped: nothing: `next=1`
- check: root 0: `next=5`
- check: root 1, depth > 0: a page of holes written: `wr_go=1 wr_two=1 wr_src0=2 wr_src1=2 side_op=1 next=4`
- check: root 1, depth 0: a page of zeros written: `wr_go=1 wr_two=1 wr_src0=1 wr_src1=1 side_op=1 next=4`
- check: root 2, depth > 0: the source read, owned bits cleared: `rd_go=1 rd_two=1 rd_xform=1 next=3`
- check: root 2, depth 0: the source read: `rd_go=1 rd_two=1 next=3`
- root read: waiting for the burst: `next=3`
- root read: an error: `stop=1 err=2 next=1`
- half read: waiting for the burst: `next=6`
- half read: an error: `stop=1 err=2 next=1`
- flush read: waiting for the burst: `next=9`
- flush read: an error: `stop=1 err=2 next=1`
- load: waiting for the burst: `stat=3 next=14`
- load: an error: `stop=1 err=2 next=1`
- root read: the root written: `wr_go=1 wr_two=1 side_op=1 next=4`
- root write: waiting for B: `next=4`
- root write: an error: `stop=1 err=2 next=1`
- root write: the Tracer's copy's root written: `wr_go=1 wr_addr=1 wr_two=1 side_op=2 next=4`
- root write: on: `next=5`
- next: no value, WORD_COUNT not 0: waiting for data: `stat=1 next=5`
- next: no value, a page held, or another page: flush: `next=8`
- next: no value, none held: done: `busy_clr=1 done=1`
- next: beyond: `stop=1 err=1 next=1`
- next: none held, depth 0: the root held, owned: `held_page_ld=1 pm_ok_op=2 held_op=1 held_at_sel=1 have_op=1 dirty_op=1 next=5`
- next: none held: the image's look-up: `held_page_ld=1 pm_ok_op=2 lk_go=1 side_op=1 next=12`
- next: the half not had, covered by the run: patched: `have_op=3 pm_patch=1 dirty_op=2 take=1 next=5`
- next: the half not had, a hole: filled: `fill_row_op=1 next=7`
- next: the half not had: read: `rd_go=1 rd_addr=1 rd_half=1 next=6`
- next: the value patched: `pm_patch=1 dirty_op=2 take=1 next=5`
- half read: the half had: `have_op=3 next=5`
- fill: a row filled: `pm_fill=1 fill_row_op=2 next=7`
- fill: the half's last row filled: `pm_fill=1 fill_row_op=2 have_op=3 next=5`
- flush: owned, both halves dirty: written: `wr_go=1 wr_addr=2 wr_two=1 next=10`
- flush: owned, one half dirty: written: `wr_go=1 wr_addr=2 wr_half=1 next=10`
- flush: owned, nothing dirty: released: `held_op=2 next=5`
- flush: new, halves missing: read: `rd_go=1 rd_addr=1 rd_half=2 rd_two=1 next=9`
- flush: new: written whole: `wr_go=1 wr_addr=2 wr_two=1 wr_src0=3 wr_src1=3 next=10`
- flush read: the halves had: `have_op=2 next=8`
- flush write: waiting for B: `stat=3 next=10`
- flush write: an error: `stop=1 err=2 next=1`
- flush write: owned: released: `held_op=2 next=5`
- flush write: new: its update: `lk_go=1 lk_op=1 lk_page=1 lk_level=1 next=11`
- flush update: waiting for the reply: `stat=3 next=11`
- flush update: lpddr_error: `stop=1 err=2 next=1`
- new update: waiting for the reply: `stat=3 next=17`
- new update: lpddr_error: `stop=1 err=2 next=1`
- look-up: waiting for the reply: `stat=3 next=12`
- look-up: lpddr_error: `stop=1 err=2 next=1`
- flush update: ok: released: `held_op=2 next=5`
- flush update: not_owned: `stop=1 err=4 next=1`
- look-up: the image's in, with tracer_copy: the copy's look-up: `look_ld=1 src_ld=1 lk_go=1 lk_root=1 lk_page=1 side_op=2 next=12`
- look-up: both in: out of pages: `look_ld=1 src_ld=1 stop=1 err=3 next=1`
- look-up: both in: owned, held: `look_ld=1 src_ld=1 held_op=1 held_at_sel=2 have_op=1 dirty_op=1 next=5`
- look-up: both in: a fault, its walk: `look_ld=1 src_ld=1 level_op=1 side_op=2 next=13`
- level: past the data level: the image's page held: `held_op=1 held_at_sel=2 have_op=1 dirty_op=1 next=5`
- level: nothing at this (level, file): the next, the image: `side_op=1 next=13`
- level: nothing at this (level, file): the next, the Tracer's copy a level down: `side_op=2 level_op=2 next=13`
- level: the image's data page, or a source already loaded, or none: take: `next=15`
- level: an index page read, owned bits cleared, the next entry caught: `rd_go=1 rd_addr=2 rd_two=1 rd_xform=1 rd_catch=1 pm_ok_op=2 pm_src_ld=1 next=14`
- level: a data page read: `rd_go=1 rd_addr=2 rd_two=1 pm_ok_op=2 pm_src_ld=1 next=14`
- load: loaded: `pm_ok_op=1 next=15`
- take: the queue empty: waiting: `stat=2 next=15`
- take: the image's data page: taken, held: `fp_pop=1 pc_dec=1 held_op=1 held_kind=1 held_at_sel=3 have_op=4 dirty_op=1 next=5`
- take: taken, written whole: the source as loaded: `fp_pop=1 pc_dec=1 wr_go=1 wr_addr=3 wr_two=1 new_page_ld=1 next=16`
- take: taken, written whole: holes: `fp_pop=1 pc_dec=1 wr_go=1 wr_addr=3 wr_two=1 wr_src0=2 wr_src1=2 new_page_ld=1 next=16`
- take: taken, written whole: zeros: `fp_pop=1 pc_dec=1 wr_go=1 wr_addr=3 wr_two=1 wr_src0=1 wr_src1=1 new_page_ld=1 next=16`
- new write: waiting for B: `stat=3 next=16`
- new write: an error: `stop=1 err=2 next=1`
- new write: its update: `lk_go=1 lk_op=1 lk_root=1 lk_page=1 lk_level=2 lk_entry=1 next=17`
- new update: not_owned: `stop=1 err=4 next=1`
- new update: ok: the next, the image: `src_ld=2 side_op=1 next=13`
- new update: ok: the next, the Tracer's copy a level down: `src_ld=2 side_op=2 level_op=2 next=13`

</details>

**decoder.** States: (no named states: the bit count, a run open or not, the output register full or not). State flip-flops: 10. Control word: 10 bits, 19 rows, 19 distinct words (19 without the next state). 

Signals: dec 1, need 2, hdr 1, val 1, lru_hit 1, lru_miss 1, refill 1, clear 1, ready 1.

<details><summary>its rows</summary>

- nothing: `nothing`
- a refill alone: `refill=1`
- START with continue 0: cleared: `clear=1`
- raw, a header word: `dec=1 hdr=1`
- raw, a header word, and a refill: `dec=1 hdr=1 refill=1`
- raw, a value: `dec=1 val=1`
- raw, a value, and a refill: `dec=1 val=1 refill=1`
- a hit, a header word: `dec=1 need=1 hdr=1 lru_hit=1`
- a hit, a header word, and a refill: `dec=1 need=1 hdr=1 lru_hit=1 refill=1`
- a hit, a value: `dec=1 need=1 val=1 lru_hit=1`
- a hit, a value, and a refill: `dec=1 need=1 val=1 lru_hit=1 refill=1`
- a 34-bit miss, a header word: `dec=1 need=2 hdr=1 lru_miss=1`
- a 34-bit miss, a header word, and a refill: `dec=1 need=2 hdr=1 lru_miss=1 refill=1`
- a 34-bit miss, a value: `dec=1 need=2 val=1 lru_miss=1`
- a 34-bit miss, a value, and a refill: `dec=1 need=2 val=1 lru_miss=1 refill=1`
- a 66-bit miss, a header word: `dec=1 need=3 hdr=1 lru_miss=1`
- a 66-bit miss, a header word, and a refill: `dec=1 need=3 hdr=1 lru_miss=1 refill=1`
- a 66-bit miss, a value: `dec=1 need=3 val=1 lru_miss=1`
- a 66-bit miss, a value, and a refill: `dec=1 need=3 val=1 lru_miss=1 refill=1`

</details>

**AXI channels and the look-up port.** States: AR: idle, offered, the read in progress, AW and W: idle, offered, beats pending, B awaited, look-up: idle, offered, the reply awaited. State flip-flops: 8. Control word: 15 bits, 14 rows, 14 distinct words (14 without the next state). R, W, B and the look-up port move independently: rows a channel, not a table of their products

Signals: ar_clr 1, beat_in 1, rd_last 1, rd_err 1, catch 1, aw_clr 1, w_take 1, w_fetch 1, w_hold 1, b_in 1, b_err 1, lk_taken 1, rp_ld 1, w_valid 1, w_last 1.

<details><summary>its rows</summary>

- nothing: `nothing`
- AR taken: `ar_clr=1`
- an R beat: `beat_in=1`
- an R beat caught: `beat_in=1 catch=1`
- an R beat, an error: `beat_in=1 rd_err=1`
- the last R beat: `beat_in=1 rd_last=1`
- AW taken: `aw_clr=1`
- a beat taken, the next row read: `w_take=1 w_fetch=1 w_valid=1`
- the last beat taken: `w_take=1 w_valid=1 w_last=1`
- W held: wready 0: `w_hold=1`
- B: `b_in=1`
- B, an error: `b_in=1 b_err=1`
- the request taken: `lk_taken=1`
- the reply: `rp_ld=1`

</details>

### Gates

| part | LOW, GE | HIGH, GE |
|---|---|---|
| flip-flops | 22,776 | 23,774 |
| registers' input muxes and holds (and LOW's ICGs) | 1,428 | 9,816 |
| datapath operators | 16,264 | 19,042 |
| next-state and output logic | 1,097 | 1,521 |
| logic, with HIGH's buffering | 18,789 | 34,936 |

Largest, HIGH: the registers' input multiplexers and holds 9,816 GE; decoder: the LRU hit's entry, 32:1 of 64 bits 4,623 GE; decoder: the LRU table cleared at START, bit by bit (HIGH) 2,724 GE; decoder: a 128-bit word put in at 0 to 65 (a 7-level shifter) 2,610 GE; decoder: the bit buffer shifted right by 0, 6, 34, 64 or 66 1,808 GE.

Largest, LOW: decoder: the LRU hit's entry, 32:1 of 64 bits 4,623 GE; decoder: a 128-bit word put in at 0 to 65 (a 7-level shifter) 2,610 GE; decoder: the bit buffer shifted right by 0, 6, 34, 64 or 66 1,808 GE; the registers' input multiplexers 1,309 GE; the next-state and output logic 1,097 GE.

Memories: page memory: 256 x 128, one port, two half enables: 32,768 bits; free page queue: 256 x 32: 8,192 bits.

## arbiter

### Registers

| register | bits | count | sources | kind | note |
|---|---|---|---|---|---|
| AR slot, a client: valid | 1 | 4 | 1 | flag |  |
| AR slot, a client: the next piece's beat address 28, beats left 9 | 37 | 4 | 1 | hw |  |
| AW slot, a client: valid | 1 | 4 | 1 | flag |  |
| AW slot, a client: the next piece's beat address 28, beats left 9 | 37 | 4 | 1 | hw |  |
| read list, a client: 4 x pieces 4, head 2, count 3 | 21 | 4 | 1 | hw |  |
| write list, a client: 4 x (pieces 4, response 2), head 2, count 3 | 29 | 4 | 1 | hw |  |
| a client's outputs: arready, awready, wready, rvalid, rresp 2, rlast, bvalid, bresp 2 | 10 | 4 | 1 | out |  |
| a client's outputs: rdata | 128 | 4 | 1 | shr | LOW: one register for the four (see ASSUME shared_r) |
| the round-robin pointers, AR and AW | 4 | 1 | 1 | hw |  |
| the write piece in turn: valid, client 2, beats to take 6, last piece | 10 | 1 | 1 | hw |  |
| the W queue: 2 x (beat 128, wlast), head 1, count 2 | 261 | 1 | 1 | hw |  |
| outputs to lpddr: AR valid, address 28, len 8, ID 2; AW the same | 78 | 1 | 1 | out |  |
| outputs to lpddr: W valid, wdata 128, wlast | 130 | 1 | 1 | out |  |

As coded: **1,539** flip-flops (the specification: 1,539). LOW: 1,155.

Not counted:

- read_pieces, write_pieces, read_bursts, write_bursts, read_waits, write_waits: statistics
- List entries 4 to 15 (MAX_LIST 16): CLIENT_BURSTS is 4 by default: 4 entries are reachable
- List.resp[] of the read lists: unused: R beats carry their own rresp

### Datapath operators

| operator | kind | bits | count | when | GE |
|---|---|---|---|---|---|
| a slot's next address + 32 beats (an incrementer at bit 5) | ha | 23 | 8 | high | 613 |
| a slot's beats left - 32, and below 32 | fa | 9 | 8 | high | 384 |
| a slot's piece length: min(left, 32) | mux:2 | 6 | 8 | high | 112 |
| the shared cutter: the served slot's fields, 4:1 of 37 bits, a direction | mux:4 | 37 | 2 | low | 517 |
| the shared cutter: + 32 beats, - 32 beats, min | ha | 23 | 2 | low | 153 |
| the shared cutter: - 32 beats | fa | 9 | 2 | low | 96 |
| the shared cutter: min | mux:2 | 6 | 2 | low | 28 |
| a burst's pieces: (len >> 5) + 1, a client a direction | ha | 4 | 8 | both | 107 |
| a list's head entry, 4:1 of 4 bits, its count - 1, its count at 0, a client a direction | mux:4 | 4 | 8 | both | 224 |
| a list's count - 1 and its zero | ha | 4 | 8 | both | 107 |
| a list's new entry's write decode, head and count counters, a client a direction | ge | 30 | 8 | both | 240 |
| a write list's first error kept | ge | 6 | 4 | both | 24 |
| the round robins, AR and AW, over four | ge | 40 | 2 | both | 80 |
| AR and AW to lpddr: the served slot's address and len, 4:1 of 36 bits | mux:4 | 36 | 2 | both | 503 |
| the W queue's input: the client in turn's beat, 4:1 of 129 bits | mux:4 | 129 | 1 | both | 902 |
| the W queue's output, 2:1 of 129 bits | mux:2 | 129 | 1 | both | 301 |
| the write piece in turn: beats to take - 1 and its zero; wlast's check is the simulator's | ha | 6 | 1 | both | 20 |
| R and B steered by ID: the client decode | ge | 20 | 1 | both | 20 |
| the readies: slot empty and the list below CLIENT_BURSTS | ge | 8 | 8 | both | 64 |

### Control

**the AR and AW turns, the write piece, the W queue, the lists.** States: a slot: empty or cutting, a list: 0 to 4 bursts, the write piece: none or in turn (1 to 32 beats left), the W queue: 0, 1, 2. State flip-flops: 45. Control word: 49 bits, 12 rows, 11 distinct words (11 without the next state). no state machine: per-client slots, lists and counters, each changed by its own handshake

Signals: ar_turn 1, aw_turn 1, ar_served 2, aw_served 2, slot_ld 8, list_push 8, r_beat 1, r_last_piece 1, b_piece 1, b_last 1, wq_push 1, wq_pop 1, wcur_done 1, readies 12, rvalid 4, bvalid 4.

<details><summary>its rows</summary>

- nothing: `nothing`
- an AR turn: `ar_turn=1`
- an AW turn: `aw_turn=1`
- a burst taken on AR: `list_push=1`
- a burst taken on AW: `list_push=1`
- an R beat: `r_beat=1`
- a piece's last R beat: `r_beat=1 r_last_piece=1`
- a B, not the last piece's: `b_piece=1`
- a B, the burst's last: `b_piece=1 b_last=1`
- a W beat in: `wq_push=1`
- a W beat out: `wq_pop=1`
- the piece's last beat in: `wq_push=1 wcur_done=1`

</details>

### Gates

| part | LOW, GE | HIGH, GE |
|---|---|---|
| flip-flops | 6,549 | 8,726 |
| registers' input muxes and holds (and LOW's ICGs) | 142 | 2,989 |
| datapath operators | 3,385 | 3,699 |
| next-state and output logic | 290 | 482 |
| logic, with HIGH's buffering | 3,817 | 8,246 |

Largest, HIGH: the registers' input multiplexers and holds 2,989 GE; the W queue's input: the client in turn's beat, 4:1 of 129 bits 902 GE; a slot's next address + 32 beats (an incrementer at bit 5) 613 GE; AR and AW to lpddr: the served slot's address and len, 4:1 of 36 bits 503 GE; the next-state and output logic 482 GE.

Largest, LOW: the W queue's input: the client in turn's beat, 4:1 of 129 bits 902 GE; the shared cutter: the served slot's fields, 4:1 of 37 bits, a direction 517 GE; AR and AW to lpddr: the served slot's address and len, 4:1 of 36 bits 503 GE; the W queue's output, 2:1 of 129 bits 301 GE; the next-state and output logic 290 GE.

Memories: none.

## core

| register | bits |
|---|---|
| GPRs x1-x31 | 992 |
| fetch: the PC 31, outstanding and stale bits 3 | 34 |
| fetch: the buffer, 16 bytes and their valid bits | 136 |
| decode: instruction 32, PC 31, valid, compressed | 65 |
| execute: operands 64, immediate 32, PC 31, rd 5, control 30 | 162 |
| writeback: result 32, rd 5, control 10, load control 5 | 52 |
| writeback: the four 34-bit partial products | 136 |
| data port request: address 32, data 32, byte enables 4, valid, write, the response target | 71 |
| coprocessor port request: instruction 32, sources 64, valid | 97 |
| CSRs: mstatus 2, mie 1, mtvec 30, mscratch 32, mepc 31, mcause 6, mtval 32 | 134 |
| CSRs: mcycle 64, minstret 64 | 128 |
| trap taken, WFI, the status outputs | 7 |

| operator | kind | bits | count | when | GE |
|---|---|---|---|---|---|
| GPR read ports: 2 x 32:1 of 32 bits | mux:32 | 32 | 2 | both | 4,623 |
| GPR write: an enable a register (HIGH: a hold mux a bit) | mux:2 | 992 | 1 | high | 2,311 |
| ALU: adder/subtractor 32 (SLT and branches by its carry) | fa | 32 | 1 | both | 171 |
| ALU: subtract's inversion | xor | 32 | 1 | both | 64 |
| ALU: the shifter, left, right and arithmetic | mux:2 | 240 | 1 | both | 559 |
| ALU: AND, OR, XOR | and2 | 96 | 1 | both | 128 |
| ALU: Zicond's zero test and its gating | zd | 32 | 1 | both | 32 |
| ALU: Zicond gating | and2 | 32 | 1 | both | 43 |
| ALU: the result, 8:1 | mux:8 | 32 | 1 | both | 522 |
| the branch target: PC + immediate (execute) | fa | 32 | 1 | both | 171 |
| the jump target in decode: PC + immediate | fa | 32 | 1 | both | 171 |
| PC + 2 or + 4 (fetch, the link value) | ha | 31 | 2 | both | 206 |
| forwarding: two operands, 3:1 | mux:3 | 32 | 2 | both | 298 |
| the immediate, 5 formats and RVC's | mux:6 | 32 | 1 | both | 373 |
| fetch: alignment of a halfword and a word across two | mux:3 | 32 | 1 | both | 149 |
| fetch: the buffer's control | ge | 300 | 1 | both | 300 |
| decode: RV32I, Zicsr, Zmmul, Zicond, the custom opcodes; illegal instructions | ge | 1,800 | 1 | both | 1,800 |
| decode: RVC's expander | ge | 1,100 | 1 | both | 1,100 |
| multiply: four 17 x 17 signed products, partial product bits | and2 | 289 | 4 | both | 1,537 |
| multiply: their reduction and carry-propagate (about n^2 full adders a product) | fa | 289 | 4 | both | 6,161 |
| multiply: the four products' sum in writeback (two carry-save rows and a 64-bit adder) | fa | 160 | 1 | both | 853 |
| load: the byte or halfword, extended; store: placed in its lanes | mux:4 | 56 | 1 | both | 391 |
| load and store: the byte enables, the misalignment trap | ge | 40 | 1 | both | 40 |
| CSRs: the read, 13:1 of 32 bits | mux:13 | 32 | 1 | both | 895 |
| CSRs: CSRRW/S/C's write value | ge | 130 | 1 | both | 130 |
| CSRs: mcycle and minstret + 1 | ha | 64 | 2 | both | 426 |
| CSRs: mepc, mcause, mtval at a trap | mux:2 | 69 | 1 | both | 161 |
| traps, MRET, the NMI, WFI and the wake | ge | 400 | 1 | both | 400 |
| the pipeline's stalls, the load-use and multiply waits, the redirect | ge | 1,000 | 1 | both | 1,000 |
| the data decoder's windows and the fetch address check | ge | 400 | 1 | both | 400 |

Core: 1,917-2,014 flip-flops; flip-flops 10.9-11.4 kGE; logic 23.1-31.5 kGE; in all 34.0-42.9 kGE. Memories: 526,336 bits.

Published, for cores of its kind:

- Ibex "small": RV32IMC, 2 stages, a 3-cycle multiplier: 26.06 kGE (Yosys), about 24 kGE commercial
- Ibex "maxperf": RV32IMC, a 1-cycle multiplier, branch target ALU, a writeback stage: 35.64 kGE (Yosys), about 33 kGE commercial
- the source: lowRISC Ibex README, its configuration table (latch-based register file; M includes a divider)

