# Programming a tile: the recipe book (v1.2 draft)

**v1.2 draft, written from `planning/DECISIONS-v1.2.md` (sections 1-38, cited as D*n*).** Short, careful C for the common operations of a tile program, one recipe per operation, against the draft header `sw/mesh-v1.2-draft.h`. Each recipe gives what it does, what it relies on, the code, and a **walk**: what each tile and C&C 1 do, in order, and what can arrive when, checked against the counters' rules (arming, done early, unexpected arrival), the `done` gate, the GO-to-arming window, alignment and the events' consume rules. Where a walk showed a protocol problem the recipe is marked **[F*n*]**, a finding in `WORKER6/FINDINGS.md`; sections 37 and 38 of the log answer the first five, and the recipes follow those answers.

**The code is compiled, not run.** There is no v1.2 simulator yet. Every code block is a region of a file in `WORKER6/check/`, marked `/* RECIPE name */` … `/* END */` and named in an HTML comment before the block. `build.sh` compiles them all against the draft header (`riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -O2 -Wall -Wextra -Werror`), and `check_book.py` confirms that each block equals its region. When meshsim v1.2 exists, those files become the `rcp_*.c` tests, as in v1.1.

**Conventions.**
- **Names** are the draft header's: one struct per block, reached through a pointer (D14): `tile->EVENTS`, `tables->PTR[i]`, `replayer->EVENTS_MASK`, `cc1->PULSE_GO`. WORKER4 generates the real `mesh.h` and reconciles it with the draft. Tile programs follow `sw/CONVENTIONS.md`, and section 15 collects the rules that bite.
- **Counters:** the packet names one of four (D3, D30). These recipes keep v1.1's four as a convention (CONVENTIONS 5): **1 big SRAM, 2 controller memory (read-port results included), 3 replies, 4 peripheral writes**. `CTR_SRAM`, `CTR_CM`, `CTR_REPLY` and `CTR_PERIPH` name them. Every send names its counter: the header's macros take it as a required argument, so no send is uncounted by default.
- **Addresses** are bytes in the 32-bit map (D5). Big SRAM's engine pointers (`PTR`, `STREAM_SRC`, `SRAM_RD_ADDR_QUEUE`, `DMA_RECORD_BASE_ADDRESS`) stay in 32-bit chunks; `SRAM_ADDR(chunk)` is the packet address of a chunk.
- **Cycles:** every v1.1 measurement is void until meshsim v1.2 measures it again; section 14 marks each figure "to measure".
- Buffers in the examples sit in different big-SRAM areas, as in v1.1: area *a* starts at chunk *a* · 0x20000.

**What changed from v1.1, in brief.** Modes 0 write at address (commands and replies included), 1 by source, 2 bucket, 3 augmented, 4 gather request. The counter is named by the packet. Every 64- and 128-bit access is naturally aligned, checked in hardware. Waiting is by events, the tables included (`wait_tables()` is gone). Errors trap and the trap is terminal: no HALT and no REPORT. Every unit boots from a ROM. Requests to the controllers are limited per tile and wait for their replies (D38).

**Contents.** 0 helpers · 1 the command loop · 2 a stream run (local copy) · 3 block copy into controller memory · 4 histogram, prefix sum and bucket passes · 5 augmented transfers · 6 gather · 7 the one-word exchange and words to one tile · 8 a vector exchange · 9 an all-to-all · 10 a mesh reduction · 11 the read port · 12 parameters, status and errors · 13 the synchronised start · 14 cycles · 15 the rules that bite · 16 gaps and awkwardness · 17 write a block into big SRAM · 18 fill with pseudo-random data · 19 hash a block · 20 *retired: REPORT* · 21 the self-test · **new:** 22 the boot ROM and the boot sequence · 23 waiting for the tables by events · 24 the error handler · 25 a peripheral window through its struct · 26 the replayer's refill loop · 27 C&C 1's side of a phase.

---

## 0. Helpers

**What they do.** `wfi()` sleeps and then consumes what woke it; `wait_all()` sleeps until a set of events are all pending; `local_run()` runs the address generator locally and waits until its writes have landed and its tables are free.
**Relies on:** spec 4.12 (the wake term, `EVENTS_REQUIRED`, consuming under the mask), D3 (additive arming), D16, D17.

`wfi()` and `wait_all()` are in the draft header:

```c
static inline uint32_t wfi(void) { __asm__ volatile ("wfi" ::: "memory"); return tile->EVENTS; }
static inline uint32_t wait_all(uint32_t ev) {
    tile->EVENTS_MASK = EV_GO | ev;
    tile->EVENTS_REQUIRED = ev;
    uint32_t e;
    do e = wfi(); while ((e & ev) != ev);    /* the wake term holds only when all are pending; re-check anyway */
    tile->EVENTS_REQUIRED = 0;
    tile->EVENTS_MASK = EV_RESET_MASK;
    return e;
}
```

<!-- recipe: local_run (rcp.h) -->
```c
/* A local run of the address generator into big SRAM (spec 4.2; D3, D17): DMA_CONFIG, arm counter 1 with the n packets
 * the run writes to this tile, start, then wait for both the counter and DMA run done (EVENTS [7]): the run's writes
 * have landed and its last table read is complete, so the tables are free. n = 0 (a run that writes nothing here)
 * waits for the run alone: an unarmed counter never fires. */
static inline void local_run(uint32_t config, uint32_t n) {
    tile->DMA_CONFIG = config;
    tile->WRITE_COUNTER[CTR_SRAM - 1] = n;          /* a store adds; the count is 0 here */
    start_run();                                    /* PULSE_DMA: also clears EVENTS [7] */
    wait_all(n ? EV_CTR(CTR_SRAM) | EV_RUN_DONE : EV_RUN_DONE);
}
```

- **`wfi()`** is `WFI` then a load of `EVENTS`, which returns every pending event and consumes those in `EVENTS_MASK`. At the reset mask (GO | counters done) it consumes counters done, as in v1.1.
- **`wait_all(ev)`** puts *ev* in both `EVENTS_MASK` and `EVENTS_REQUIRED`: a required bit never wakes alone, and only a masked bit is consumed (`EVENTS_REQUIRED` plays no part in clearing, spec 4.12). Counters done ([4]) is left out of the mask on purpose: it would wake on its own as soon as the last armed counter reached 0, before DMA run done. GO stays in, but can't arrive mid-phase. The loop re-checks after every wake (D10: spurious wakes are allowed). It restores the reset mask, so a program with a standing mask of its own (recipe 26) sets it again afterwards.
- **`local_run()`** arms before it starts, so none of the run's packets can beat the counter: they all come from this tile's own run, after the start store. It waits for counter 1 **and** DMA run done, [7], which `PULSE_DMA` clears (D17), so a stale [7] can't satisfy the wait. A run that writes nothing to this tile (n = 0) waits for [7] alone: an unarmed counter never fires.

**Walk.**
1. Store `DMA_CONFIG`; store *n* to counter 1, which arms it and clears its done event [0] and counters done [4] (D3).
2. `PULSE_DMA`: the run starts, clearing [7].
3. The run's packets loop back through the director; each decrements counter 1 *K* cycles after acceptance, when its write and any `PTR`/`HIST` update are complete (D17 guarantee A).
4. [0] is set when counter 1 reaches 0. [7] is set after the run's last table read (guarantee B). Either may come first.
5. `wait_all` wakes when both are pending and consumes both.
6. Counters done [4] was also set, since every counter is at 0; it is not in the mask, so it stays pending. That is harmless: the next arming store clears it (D3), and so does the `done` pulse. **[F10]**

## 1. The command loop

**What it does.** Loops: report done, wait for GO, read the command word and its argument block, act. The boot that precedes it is recipe 22.
**Relies on:** spec 6.3 (`done`, READY, GO and `WFI`), D6 (commands are mode-0 writes to slot 0, uncounted); CONVENTIONS 4.

<!-- recipe: cmdloop (rcp_cmdloop.c) -->
```c
int main(void) {
    unsigned me = tile_index();
    for (;;) {
        uint32_t w = wait_for_command();          /* pulse done, then WFI; GO wakes it; returns the command word */
        volatile uint32_t *arg = CMD_ARGS(w);     /* this command's argument block (slots 1-7 or 8-15, by parity) */
        switch (CMD_TYPE(w)) {
        case CMD_SUM: {
            uint32_t s = me;
            for (uint32_t i = 0; i < CMD_ARG(w); i++) s += arg[i];
            res[ncmd++ & 7] = s;
            break;
        }
        case CMD_EXIT:
            sim_exit(0);                          /* simulator only: a real program loops for ever */
        }
    }
}
```

- **`wait_for_command()`** pulses `done` and executes `WFI` as two back-to-back instructions (an x0-relative store, then `wfi`), loads `EVENTS` to consume GO, then reads slot 0 once. Every wake from a command goes through it, barriers included.
- **Arguments:** C&C 1 writes them first, then the command word to slot 0 (recipe 27). Even commands use slots 1–7 and odd ones 8–15. The parity bit in the word says which block, and `CMD_ARGS(w)` picks it. A broadcast needs every tile at the same parity.
- **Exit:** `sim_exit()` stores to the simulator-only exit word (D36). `ebreak` now traps, and meshsim stops loudly on it (D27). A real program loops for ever.

**Walk.**
1. The tile pulses `done`. The `done` gate closes: from now until GO only GO can wake `WFI`, whatever the mask holds. Counters done can't be set in the window, and the pulse cleared it.
2. C&C 1's command lands. It is uncounted, so it touches no counter, and the slot-0 write landing in port B sets "command landed" (D6).
3. `done` and "command landed" are both set, so READY pulses. The order of the two doesn't matter.
4. When every expected tile is READY, C&C 1 stores `PULSE_GO`. GO clears `done` and "command landed", sets [5] and ends the gate.
5. The tile wakes and its load of `EVENTS` consumes GO. GO must be in `EVENTS_MASK`, as it is at reset; otherwise GO stays pending and the next `wait_for_command()` falls straight through.
6. It reads slot 0. C&C 1 sends the next command right after GO, but only `go_latency` + 1 cycles after its store (6.6), and the new command's arguments go to the other block. meshsim checks that no slot-0 write lands before the previous word was read.

## 2. A stream run: a local copy

**What it does.** Copies n 64-bit records from one big-SRAM buffer to another, through the address generator and the tile's own loopback.
**Relies on:** spec 4.2 (stream runs, `STREAM`), D4 (the entry: 17-bit count, header block [24:17], *d* [31:25]), D6 (mode 1 lands at `PTR[src]`), D26 (alignment), D17.

<!-- recipe: copy (rcp_copy.c) -->
```c
/* Copy n 64-bit records from src to dst in this tile's big SRAM (chunk addresses, both even: a 64-bit access is
 * 8-byte aligned, D26): one stream, mode 1 to the tile itself (d = 0), which lands at PTR[me]. src and dst must not
 * overlap (one engine run never reads and writes one buffer, spec 4.1). Called with the tables free (D17): the last
 * thing that used them was a local_run(), which waited for DMA run done. */
static void local_copy(uint32_t src, uint32_t dst, uint32_t n) {
    tables->PTR[tile_index()] = dst;
    set_stream(0, STREAM_SRC_W(src, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_SRC, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);
}
```

- **The pattern every local pass follows:** `PTR` for where it lands, one `STREAM` entry for what is read (source, destination address, count with size, counter, mode and *d*), then `local_run()`.
- **`STREAM_DEST` is ignored for modes 1–3**, region included (D26), so it is 0 here. It matters for mode 0 (recipe 3).
- **Read size:** `SIZE_32` sends one chunk per packet in the payload's low half, `SIZE_64` one record, `SIZE_128` one row.
- **Counts up to 131,071 elements** per stream (D30). Larger moves are split across entries.

**Walk.** The tables are free on entry, because the last thing that used them waited for DMA run done (rule 6, section 15). `PTR[me]` is even, as every 64-bit landing pointer must be (D26, D32). A misaligned one would be a bad-packet error at the first landing, dropped but still counted, so nothing hangs (D32), and the tile traps. The rest is recipe 0's walk.

## 3. A block copy into controller memory

**What it does.** Copies words from big SRAM into data memory (parameters, tables) or program memory (an overlay), then uses them.
**Relies on:** spec 6.5, D5 (program memory is written only by packets; the controller fetches and loads it), D6 (mode 0 at an address), D26.

<!-- recipe: cmem (rcp_cmem.c) -->
```c
/* Copy n 128-bit packets (4n words) from big SRAM at chunk src to controller memory at the address dst, data memory
 * (region 0x00) or program memory (0x01): a mode-0 stream to the tile itself, advancing STREAM_DEST by 16 bytes per
 * packet (spec 6.5). src is 4-chunk aligned and dst 16-byte aligned (D26). Counted on counter 2. The code doing the
 * copy must lie outside dst's range. */
static void cmem_copy(uint32_t src, uint32_t dst, uint32_t n) {
    set_stream(0, STREAM_SRC_W(src, 0), dst, STREAM_COUNT_W(n, SIZE_128, CTR_CM, MODE_WRITE, 0));
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
    tile->WRITE_COUNTER[CTR_CM - 1] = n;            /* 1 per packet, whatever its size */
    start_run();
    wait_all(EV_CTR(CTR_CM) | EV_RUN_DONE);         /* the last word is in memory, and the tables are free */
}
```

<!-- recipe: cmem_use (rcp_cmem.c) -->
```c
cmem_copy(SRC_PAR, (uint32_t)params, NPAR / 4);     /* parameters into data memory */
cmem_copy(SRC_OVL, OVL, NOVL / 4);                  /* an overlay into program memory */
uint32_t (*overlay)(uint32_t) = (uint32_t (*)(uint32_t))OVL;
res = overlay(params[0]);
```

- **One mode for both memories:** mode 0 at the destination's address, region 0x00 or 0x01. `STREAM_DEST` advances by each packet's size, 16 bytes at read size 128.
- **Alignment (D26):** a 128-bit stream needs a 4-chunk-aligned source and a 16-byte-aligned destination. So `params` is declared `aligned(16)`, and the overlay's address is a multiple of 16. v1.1's mode 6 took any word address; that is gone.
- **Counted on counter 2** (CONVENTIONS 5), 1 per packet whatever its size.
- **The code doing the copy must lie outside the range it overwrites.** `.rodata` may live in program memory, since it is loadable (D5).

**Walk.** As recipe 0, on counter 2. A packet to region 0x01 lands through port B like any controller-memory write; the controller's own store there would be a bus error. The landing is complete when the counter decrements (*K* covers `cm_latency`, D32), so the overlay can be called as soon as `wait_all` returns.

## 4. The histogram digit, the prefix sum and bucket passes

**What it does.** Counts a key digit while records are written (count-on-arrival), turns the counts into bucket pointers, and runs mode-2 passes that bucket records by a digit while counting the next. Four passes sort by the 32-bit key.
**Relies on:** spec 4.9 (count-on-arrival), 4.10 (`HIST`, `PTR`), 4.15 (digits), D6 (mode 2), D17 (guarantee A covers `HIST`).

<!-- recipe: histogram (rcp_bucket.c) -->
```c
/* Count-on-arrival (spec 4.9): a run with DMA_CFG_HISTDIG(d) adds 1 to HIST[digit d of the key] for every record it
 * writes. HIST is not cleared by hardware: write zeros first. Here, a local copy that counts digit 1 as it goes.
 * Counter 1 covers the HIST updates too (D17 guarantee A). */
static void copy_counting(uint32_t src, uint32_t dst, uint32_t n) {
    for (int t = 0; t < 256; t++) tables->HIST[t] = 0;
    tables->PTR[tile_index()] = dst;
    set_stream(0, STREAM_SRC_W(src, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_SRC, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1) | DMA_CFG_HISTDIG(1), n);
}
```

<!-- recipe: bucket (rcp_bucket.c) -->
```c
/* One stable radix pass on digit d (1-4; 1 is the key's least significant byte, spec 4.15): n records from src into
 * dst, bucketed by mode 2 (each lands at PTR[its digit]). HIST holds digit d's counts on entry; the pass counts digit
 * next (0: none) for the pass after. Every PTR stays even: 64-bit records only (D26). */
static void bucket_pass(uint32_t src, uint32_t dst, uint32_t n, unsigned d, unsigned next) {
    uint32_t at = dst;
    for (int t = 0; t < 256; t++) {                /* the prefix sum: PTR[t] = dst + 2 * (records with digit < t) */
        uint32_t c = tables->HIST[t];
        tables->PTR[t] = at;
        at += c << 1;
        tables->HIST[t] = 0;                       /* ready for this pass's counts of digit next */
    }
    set_stream(0, STREAM_SRC_W(src, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_BUCKET, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1) | DMA_CFG_BUCKETDIG(d) | DMA_CFG_HISTDIG(next), n);
}
```

<!-- recipe: radix_sort (rcp_bucket.c) -->
```c
copy_counting(IN, X, n);                   /* X, with digit 1 counted */
bucket_pass(X, Y, n, 1, 2);
bucket_pass(Y, X, n, 2, 3);
bucket_pass(X, Y, n, 3, 4);
bucket_pass(Y, X, n, 4, 0);                /* sorted by the 32-bit key, in X */
```

- **Stable:** records with the same digit land in arrival order, which is read order, so four passes on bytes 1–4 sort by the whole key.
- **`PTR[64]` is no longer special** (D17): boot and staging land by address, so mode 2's use of all 256 entries needs no restore. v1.1's rule 5 is retired.
- **One size per pointer:** every `PTR` stays 8-byte aligned only if every packet through it is 64-bit (D26).
- **Mode 2 never shares a phase with modes 1 and 3**, which also land through `PTR`.

**Walk.** The prefix sum reads `HIST` and writes `PTR` after the previous `local_run()` returned. By then that run's last table read was complete (guarantee B), and so was every count-on-arrival update (guarantee A: an update is readable through the table window by the time its counter decrements, D17). So the prefix sum never reads a count still in flight. meshsim checks this as S1's table-window case (D32).

## 5. Augmented transfers (mode 3)

**What it does.** Sends records in mode 3. The receiver replaces each payload's high word with the record's position in its buffer (the ordinal), so the record can later fetch its value by a gather.
**Relies on:** spec 4.9 (augmenting, `DMA_RECORD_BASE_ADDRESS`), D6 (mode 3; at 128 bits a bad packet), D32 (an augmented `PTR` is record-aligned), spec 6.3–6.4 (configure and start).

**The local form, the sort's first pass:** raw 32-bit keys in, (key, *j*) records out.

<!-- recipe: aug_local (rcp_aug.c) -->
```c
/* The sort's first pass (spec 4.9): n raw 32-bit keys at keys, one per packet (read size 32, the high half 0), sent to
 * the tile itself in mode 3. Each lands at dst as the 64-bit record (key, j), where j = 0, 1, 2, ... is its position
 * in dst. PTR[me] and the base are record-aligned (even chunks, D32). */
static void augment_local(uint32_t keys, uint32_t dst, uint32_t n) {
    tables->PTR[tile_index()] = dst;
    tile->DMA_RECORD_BASE_ADDRESS = dst;           /* ordinal = (landing chunk - base) / 2 */
    set_stream(0, STREAM_SRC_W(keys, 0), 0, STREAM_COUNT_W(n, SIZE_32, CTR_SRAM, MODE_AUGMENT, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);
}
```

**The remote form:** two senders into one buffer. Each record's ordinal is its position in the buffer, not its arrival order.

<!-- recipe: aug_remote (rcp_aug.c) -->
```c
/* Configure (one command): receive the left neighbour's n_left records and then the right neighbour's into buf, in
 * mode 3: every record's high word becomes its position in buf, whichever sender it came from. Send this tile's n
 * records at src to both neighbours (two streams, d = 1 and d = 63); the start command starts the run. */
static void aug_configure(uint32_t src, uint32_t n, uint32_t buf, uint32_t n_left, uint32_t n_right) {
    unsigned me = tile_index();
    tables->PTR[(me - 1) & 63] = buf;
    tables->PTR[(me + 1) & 63] = buf + 2 * n_left;
    tile->DMA_RECORD_BASE_ADDRESS = buf;
    set_stream(0, STREAM_SRC_W(src, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_AUGMENT, 1));
    set_stream(1, STREAM_SRC_W(src, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_AUGMENT, 63));
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 2);
    expect = n_left + n_right;
}
/* Start (the next command, straight after waking: the same code on every tile up to the start store, spec 6.3). */
static void aug_start(void) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = expect;     /* the counter first, */
    start_run();                                    /* then the start */
    wait_all(EV_CTR(CTR_SRAM) | EV_RUN_DONE);       /* both neighbours' records are in; this tile's run is over */
}
```

- **The ordinal is (landing chunk − `DMA_RECORD_BASE_ADDRESS`) / 2**, so 0, 1, 2, … across the buffer, whichever sender a record came from.
- **Modes 1 and 3 may mix in one phase** (both land at `PTR[src]`); mode 2 never shares a phase with them.

**Walk (remote form).**
1. *Configure.* The tile writes `PTR`, the base, two streams and `DMA_CONFIG`, and arms nothing: arming here would make `done` at the end of configure a done-early error.
2. *Start.* GO wakes every tile in the same cycle (± its 0/+1 skew). From `WFI` to the start store every tile runs the same instructions, the counter store first.
3. A neighbour's first packet leaves only after its own start store plus the lead-in, and then crosses a link. So it reaches this tile's director after this tile's counter store, which came earlier in identical code.
4. That is the GO-to-arming window (D36): closed by timing, as in v1.1. If it ever fails, the early packet decrements a counter that is either still at 0 (unexpected arrival, `ERRORS` [2]) or already armed (done one word early, then unexpected arrival on the last word). Both trap.

## 6. A gather

**What it does.** Reorders values by an index: for each (key, ordinal) record of the index, it fetches value[ordinal] and writes the values in index order.
**Relies on:** spec 4.4 (the GATHER engine), D6 (mode 4: never counted; its counter field passes to its values), D17 guarantee C.

<!-- recipe: gather (rcp_gather.c) -->
```c
/* For each of the n (key, ordinal) records of the index at idx, fetch value[ordinal], k records, from vals (value j is
 * at vals + 2k j), and write the values in index order from out. One mode-4 request per index record goes to the tile
 * itself; its counter field (D6) is the counter the values count on: the GATHER engine sends the k value packets as
 * mode 1 to PTR[me], counted on counter 1. The requests themselves never count: n * k in all. */
static void gather(uint32_t idx, uint32_t vals, uint32_t out, uint32_t n, unsigned k) {
    tile->DMA_RECORD_BASE_ADDRESS = vals;
    tables->PTR[tile_index()] = out;
    set_stream(0, STREAM_SRC_W(idx, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_GATHER, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1) | DMA_CFG_KM1(k - 1), n * k);
}
```

- **The stream's counter field is the values' counter.** The mode-4 requests count nothing (D6); each produces k value packets, which count on counter 1 as they pass the director: n · k in all.
- **Three areas:** index, values and output in different areas keep each to one access per cycle.

**Walk.** The requests go to the GATHER engine locally. The values go out as mode-1 packets to the tile itself and loop back through the director, where they are counted like any arrival (guarantee C). A counter takes at most one decrement per cycle, so the count stays exact. The wait returns when the last value is readable and the run's last table read is done.

## 7. The one-word exchange, and several words to one tile

**What it does.** Every tile sends one word to every tile (counts, cuts, probe keys). Each lands in the receiver's reply block at the sender's pair, so the receiver copies `REPLY[2 s]` out. And: n words to one tile's data memory in one run.
**Relies on:** D6 (replies are mode-0 writes to word 2·src, counted as the receiver armed), D7 (`ROUTER_HDR`, `ROUTER_ADDR`, the no-increment bit), `schedule-4.md`.

<!-- recipe: xchg (rcp.h) -->
```c
/* The one-word exchange (D6, D7): out[d] goes to tile d as a 32-bit mode-0 write to REPLY_WORD(me), the same
 * address in every receiver, so ROUTER_ADDR is set once with no-increment and each send changes only ROUTER_HDR's
 * dst. Afterwards in[s] holds tile s's word to this tile, copied from REPLY[2 s]. Counted on counter 3 (replies),
 * which every receiver arms first in the command, by identical code: the fastest sender's word can arrive a few
 * cycles after GO. A tile keeps at most one reply outstanding to each receiver until it has been read. The
 * no-increment bit stays set until the program clears it (D37.5), so the exchange clears it when it is done. */
static void xchg(const uint32_t *out, volatile uint32_t *in) {
    unsigned me = tile_index();
    tile->WRITE_COUNTER[CTR_REPLY - 1] = 64;        /* including this tile's word to itself */
    tile->ROUTER_ADDR = REPLY_WORD(me);
    for (int g = 0; g < 16; g++)
        for (int i = 0; i < 4; i++) {
            unsigned d = (me + SCHED[g][i]) & 63;
            tile->ROUTER_HDR = ROUTER_HDR_W(d, MODE_WRITE, CTR_REPLY) | ROUTER_NOINC;
            send32(out[d]);
        }
    tile->ROUTER_HDR &= ~ROUTER_NOINC;              /* done with the fixed address: clear it (D37.5) */
    wait_all(EV_CTR(CTR_REPLY));                    /* every word to this tile is in */
    for (int s = 0; s < 64; s++) in[s] = REPLY[2 * s];
}
```

<!-- recipe: send_words (rcp_xchg.c) -->
```c
/* n words to one tile's data memory: ROUTER_HDR without no-increment, ROUTER_ADDR at the first word, then one 32-bit
 * send per word; ROUTER_ADDR advances by 4 per 32-bit send. The receiver arms counter 2 for n packets. Mixing sizes
 * in one run needs care: a 64-bit packet must start 8-byte aligned (D26, W3-29). */
static void send_words(unsigned dst, volatile uint32_t *at, const uint32_t *w, unsigned n) {
    send_to(ROUTER_HDR_W(dst, MODE_WRITE, CTR_CM), (uint32_t)at);
    for (unsigned i = 0; i < n; i++) send32(w[i]);
}
```

- **One store per reply** (D7): every receiver's landing address is the same, `REPLY_WORD(me)`, so `ROUTER_ADDR` is set once with no-increment set in `ROUTER_HDR`, and each send changes only the header's dst. That keeps v1.1's cost: one header store and one send per word.
- **The no-increment bit stays set until the program clears it** (D37.5). `xchg` clears it when it is done with the fixed address, with a read-modify-write of `ROUTER_HDR` (readable, D7), and `send_words()` stores a header without it anyway. **[F8]**
- **Schedule order** makes each step a permutation of the tiles, so no link carries more than its share.
- **That a sender writes only its own pair is now a software convention** (D6), checked by meshsim (invariant 11's form, D27).

**Walk.**
1. Every tile wakes on GO and stores 64 to counter 3 first, by identical code.
2. The fastest sender's word to this tile leaves after that sender's own counter store and a few loop instructions, and then crosses the mesh. So it arrives after this tile's counter store.
3. 64 words arrive, this tile's own included; the last one's decrement sets [2].
4. `wait_all` consumes [2], and the copy-out reads the reply block.
5. A second exchange before the copy-out would overwrite pairs unread. Within one exchange each sender writes each receiver once.

## 8. A vector exchange with the neighbours

**What it does.** Sends a short vector (an even number of words) to the four neighbours, as 64-bit writes into their data memory.
**Relies on:** D6 (mode 0), D7, D26 (64-bit writes 8-byte aligned).

<!-- recipe: xvec (rcp_xvec.c) -->
```c
/* Send vec[0..W-1] (W even) to the four neighbours, as 64-bit mode-0 writes into their data memory: the vector lands
 * in the neighbour's nb[k], k naming this tile as the neighbour sees it: 0 from its left (me - 1), 1 from its right,
 * 2 from above (me - 8), 3 from below. nb is 8-byte aligned and W even, so every packet is aligned (D26). Counted on
 * counter 2. Call it first in a command. */
static void xvec(const uint32_t *vec) {
    static const int8_t to[4] = { 1, -1, 8, -8 };  /* to me + 1, this tile is its left neighbour: nb[0] */
    unsigned me = tile_index();
    tile->WRITE_COUNTER[CTR_CM - 1] = 4 * (W / 2);  /* four vectors of W / 2 packets will arrive */
    for (int k = 0; k < 4; k++) {
        send_to(ROUTER_HDR_W((me + to[k]) & 63, MODE_WRITE, CTR_CM), (uint32_t)&nb[k][0]);
        for (int i = 0; i < W; i += 2) send64(vec[i + 1], vec[i]);
    }
    wait_all(EV_CTR(CTR_CM));
}
```

- **Each 64-bit packet carries two words** (`send64(hi, lo)`: `ROUTER_SET_W0` = lo, then `ROUTER_SEND_64` = hi sends); `ROUTER_SET_W0`–`W2` are cleared by every send.
- **`nb` is declared `aligned(8)`.** v1.1's `uint32_t nb[4][W]` was only 4-byte aligned, which would now be a bad packet at the receiver (D26).

**Walk.** Arm counter 2 for 4 · W/2 packets first, then send. A neighbour's packets can't arrive before this tile's arming, by the argument of recipe 7. `ROUTER_ADDR` advances by 8 per 64-bit packet and is restored per neighbour.

## 9. An all-to-all on the 16-group schedule

**What it does.** Every tile sends each destination its run of records, in a group run that keeps every tile in lockstep. Three commands: the counts, the configuration (R by a reduction), the start.
**Relies on:** spec 4.2 (group runs, `GROUP`, the timetable), 6.3–6.4 (configure and start), D17, `schedule-4.md`.

<!-- recipe: a2a_R (rcp_a2a.c) -->
```c
    xchg(cnt, inc);                        /* first in the command: inc[s] = records from tile s */
    for (int g = 0; g < 16; g++) {         /* this tile's largest count in each group, for R */
        uint32_t m = 0;
        for (int i = 0; i < 4; i++) { uint32_t c = cnt[(me + SCHED[g][i]) & 63]; if (c > m) m = c; }
        gmax[g] = m;
    }
    break;
case 2:
    reduce_max(gmax, R);                   /* first in the command: R[g], the largest over all tiles */
    a2a_configure();
    break;
```

<!-- recipe: a2a (rcp_a2a.c) -->
```c
/* Configure (after the counts exchange, inc[s] = records from tile s, and the reduction of the group maxima, R[g] =
 * the largest count any tile sends in group g): stream 4g + i sends this tile's run for offset SCHED[g][i]; group g
 * is a round-robin over its four streams for R[g] rotations; the receive pointers put each source's run in source
 * order. The tables stay as they are until the run ends; the start command starts the run. */
static void a2a_configure(void) {
    unsigned me = tile_index();
    uint32_t first[64], f = 0;
    for (int d = 0; d < 64; d++) { first[d] = f; f += cnt[d]; }
    for (int g = 0; g < 16; g++) {
        for (int i = 0; i < 4; i++) {
            unsigned off = SCHED[g][i], d = (me + off) & 63;
            set_stream(4 * g + i, STREAM_SRC_W(SRC + 2 * first[d], 0), 0, STREAM_COUNT_W(cnt[d], SIZE_64, CTR_SRAM, MODE_SRC, off));
        }
        set_group(g, GROUP_LO_R(R[g]), GROUP_HI(4 * g, 4, 0));     /* gap 0 (spec 4.2) */
    }
    uint32_t at = INBOX;
    expected = 0;
    for (int s = 0; s < 64; s++) { tables->PTR[s] = at; at += 2 * inc[s]; expected += inc[s]; }
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, 0, 16);
}
/* Start (the next command): from waking to the start store, the same code on every tile (spec 6.3). */
static void a2a_start(void) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = expected;   /* the counter first (a store of 0 does nothing), */
    start_run();                                    /* then the start; */
    wait_all(expected ? EV_CTR(CTR_SRAM) | EV_RUN_DONE : EV_RUN_DONE);   /* an unarmed counter never fires */
}
```

- **R[g] is the global maximum** count in group g: every tile runs every group for the same R rotations, which keeps the timetable identical everywhere. Hence the reduction (recipe 10).
- **The receiver decides where each source lands:** `PTR[s]` from the counts it received, source after source.
- **Counters are never armed in the configure command.** They are stored first in the start command, then the start store.
- **The end waits for both sides** (D17): the arrivals on counter 1 and this tile's own run, [7]. A tile receiving nothing (`expected` = 0) still waits for its run before `done`, so the next command finds its tables free. v1.1 let it pulse `done` with its run still going.

**Walk.**
1. *Counts.* `xchg` arms counter 3 first, as in recipe 7.
2. *Configure.* After `reduce_max` (recipe 10) has waited for its counter, the tile writes the tables. They are free: the last run (if any) was waited for.
3. *Start.* The counter store, then `PULSE_DMA`. Identical code from `WFI`, so every tile's start store lands within its skew, and its first request follows by the group lead-in.
4. The earliest arrival anywhere is a first packet after a start store plus the lead-in plus a link. That is later than every tile's counter store.
5. Done early can't happen: `done` follows `wait_all`.

## 10. A mesh reduction of a small vector

**What it does.** The maximum of a vector over all 64 tiles, every tile getting the result: a fixed tree along the rows toward column 0 and up column 0 to tile 0, then a broadcast.
**Relies on:** D6, D7, D26; spec 2.1 (broadcast reaches tiles 0–63).

<!-- recipe: reduce (rcp.h) -->
```c
/* Max-reduce a vector of RW words (RW even) over all 64 tiles; every tile gets the result. A fixed tree: along each row
 * toward column 0 (me + 1 is a child), up column 0 toward tile 0 (me + 8 is a child), then tile 0 broadcasts. The
 * vectors travel as 64-bit mode-0 writes into the parent's data memory, counted on counter 2, so every landing buffer
 * is 8-byte aligned (D26). reduce_init() once at start-up; reduce_max() first in a command (a leaf sends at once, so a
 * parent's counter must already be armed). */
#define RW 16
static uint32_t red_in[2][RW] __attribute__((aligned(8)));     /* from me + 1, from me + 8 */
static uint32_t red_out[RW] __attribute__((aligned(8)));       /* tile 0's broadcast */
static uint32_t red_kids, red_parent;
static void reduce_init(void) {
    unsigned me = tile_index(), col = me & 7, row = me >> 3;
    red_kids = (col < 7) + (col == 0 && row < 7);
    red_parent = col ? me - 1 : row ? me - 8 : DST_BROADCAST;
}
static void reduce_max(uint32_t *v, uint32_t *out) {
    unsigned me = tile_index();
    if (red_kids) {
        tile->WRITE_COUNTER[CTR_CM - 1] = red_kids * (RW / 2);    /* RW / 2 packets per child */
        wait_all(EV_CTR(CTR_CM));                                  /* the children's vectors are in */
        for (int i = 0; i < RW; i++) {
            if ((me & 7) < 7 && red_in[0][i] > v[i]) v[i] = red_in[0][i];
            if ((me & 7) == 0 && me < 56 && red_in[1][i] > v[i]) v[i] = red_in[1][i];
        }
    }
    uint32_t *at = (me & 7) ? red_in[0] : me ? red_in[1] : red_out;
    if (me == 0) tile->WRITE_COUNTER[CTR_CM - 1] = RW / 2;        /* tile 0 receives its own broadcast */
    send_to(ROUTER_HDR_W(red_parent, MODE_WRITE, CTR_CM), (uint32_t)at);
    for (int i = 0; i < RW; i += 2) send64(v[i + 1], v[i]);
    if (me != 0) tile->WRITE_COUNTER[CTR_CM - 1] = RW / 2;        /* the broadcast can't come before this: it needs this vector */
    wait_all(EV_CTR(CTR_CM));                                      /* the broadcast is in */
    for (int i = 0; i < RW; i++) out[i] = red_out[i];
}
```

- **Tile 0 receives its own broadcast,** so it arms before sending. Every other tile arms after sending: the broadcast needs its vector first.
- **The landing buffers are `aligned(8)`** (D26).

**Walk.**
1. A parent arms counter 2 for its children's packets before any can come: this is first in the command, and a child sends only after its own wait for its children.
2. A tile k ≠ 0 arms the broadcast's RW/2 packets right after its own sends. The broadcast can't overtake that: it needs tile k's vector to reach tile 0 first and come back.
3. Each `wait_all` consumes [1]. The second arming clears any stale counters done.

## 11. Reading big SRAM through the read port

**What it does.** Reads chunks of big SRAM into data memory without stalling: one read, and the search loop that advances many binary searches together.
**Relies on:** spec 4.3; D13 (`SRAM_RD_DEST_ADDR`: a data-memory byte offset, the size and a counter; mode 0 to the tile itself), D26, D36 (a misaligned read still sends its packet).

<!-- recipe: read_one (rcp_rdport.c) -->
```c
/* One 32-bit chunk of big SRAM into data memory: set up the read port (the result's byte offset in data memory, the
 * size and the counter, D13), queue the read, and its result comes back through the loopback as a mode-0 write,
 * counted on counter 2. Not while packets are arriving for this tile (CONVENTIONS 9). */
static uint32_t sram_read32(uint32_t chunk) {
    static volatile uint32_t result;
    tile->WRITE_COUNTER[CTR_CM - 1] = 1;
    tile->SRAM_RD_DEST_ADDR = RD_DEST((uint32_t)&result, SIZE_32, CTR_CM);
    tile->SRAM_RD_ADDR_QUEUE = chunk;
    wait_all(EV_CTR(CTR_CM));                       /* the result has landed */
    return result;
}
```

<!-- recipe: search (rcp_rdport.c) -->
```c
/* Lower bounds (the first i with keys[i] >= q) of nq <= 32 queries in n sorted 32-bit keys at chunk keys: every active
 * search issues one read per step, then one wait for all the results (spec 4.3's loop; at most 63 reads in a step).
 * Results may land in any order: each lands at its own word, res[a] for the a-th read of the step.
 * A step of its own: no packets arrive for this tile meanwhile, and every result has landed before the next exchange. */
static void search(uint32_t keys, uint32_t n, const uint32_t *q, volatile uint32_t *lo, unsigned nq) {
    static volatile uint32_t res[32];
    uint32_t l[32], h[32];
    unsigned act[32], na = 0;
    for (unsigned i = 0; i < nq; i++) { l[i] = 0; h[i] = n; if (n) act[na++] = i; }
    while (tile->SRAM_RD_ADDR_QUEUE < READ_PORT_DEPTH) ;   /* once: never waits, since earlier phases waited for their reads */
    while (na) {
        tile->WRITE_COUNTER[CTR_CM - 1] = na;
        tile->SRAM_RD_DEST_ADDR = RD_DEST((uint32_t)&res[0], SIZE_32, CTR_CM);   /* result a lands in res[a] */
        for (unsigned a = 0; a < na; a++) { unsigned i = act[a]; tile->SRAM_RD_ADDR_QUEUE = keys + ((l[i] + h[i]) >> 1); }
        wait_all(EV_CTR(CTR_CM));
        unsigned nn = 0;
        for (unsigned a = 0; a < na; a++) {
            unsigned i = act[a], mid = (l[i] + h[i]) >> 1;
            if (res[a] < q[i]) l[i] = mid + 1; else h[i] = mid;
            if (l[i] < h[i]) act[nn++] = i;
        }
        na = nn;
    }
    for (unsigned i = 0; i < nq; i++) lo[i] = l[i];
}
```

- **The destination is data memory only**, a byte offset, advancing by the result's size per queued read (D13). v1.1's packed controller-memory words (`CM_WORD`) are gone.
- **Each result is a mode-0 write to the tile itself**, counted on the counter `SRAM_RD_DEST_ADDR` names, so it lands in the same place as before but is no longer counted by its mode.
- **A misaligned read** (a 64-bit read at an odd chunk) isn't performed, but its packet still goes out with payload 0 and is counted, so nothing hangs; `ERRORS` [1] is set at the tile and traps it (D36).
- **Reading `SRAM_RD_ADDR_QUEUE`** returns the free entries. An entry is freed when its return is injected, not when it lands, so only the counter says the results are in.
- **The rule on incoming traffic** (CONVENTIONS 9) stands as in v1.1: read-port reads run in steps of their own. Whether S9's required protected loopback lets the spec retire it is a question for the spec session (REPORT).

**Walk.**
1. Arm counter 2 for *na* reads. Set the destination. Queue the reads (stores never stall).
2. The results loop back in any order, each to its own word, and each decrements counter 2 *K* after the director. By then its write is in data memory (*K* ≥ `cm_latency`, D32).
3. `wait_all` returns when the last is readable. The initial poll of `SRAM_RD_ADDR_QUEUE` never waits, since every earlier step waited for its results.

## 12. Parameters, status and errors

**What it does.** Reads `PARAMS` (constant), `STATUS` (live, diagnostic) and explains `ERRORS`, which a program no longer checks: an error traps.
**Relies on:** D18 (the three registers), D20 (traps are terminal), D27 (what the hardware checks).

<!-- recipe: status (rcp_status.c) -->
```c
/* PARAMS is constant: the endpoint id, the big-SRAM size and which peripherals are present. STATUS is live and
 * diagnostic: no program needs to poll it (D17, D18). ERRORS is not something a program checks: any bit raises the
 * hard interrupt and the trap is terminal (D20), so a program that is running has ERRORS = 0. */
static void describe(void) {
    uint32_t p = tile->PARAMS;
    me_out = PARAMS_ID(p);                          /* tile_index() */
    chunks_out = (PARAMS_SIZE(p) + 1) << 16;        /* sram_chunks(): 256 KB = 0x10000 chunks */
    present_out = PARAMS_PERIPH_PRESENT(p);         /* bit n - 1: peripheral n; its window exists only then */
    live_out = tile->STATUS;                        /* rd_running, rd_done, GATHER busy, send_busy, done, landed, request_busy */
}
```

**`ERRORS`** (D18): sticky until the reset ([13:11] live). Any bit raises the hard interrupt, whose handler reports and parks (recipe 24), and the tile's error line to C&C 1, with no software involved.

| bit | error | what the program did wrong |
|---|---|---|
| 0 | router overflow | too much traffic to one tile: fan-in, pacing |
| 1 | bad packet or misaligned access | a 64- or 128-bit access not naturally aligned; a region a packet can't reach (0x02, 0x07–0xFE, 0xFF, an absent peripheral); mode 3 at 128 bits (D26) |
| 2 | unexpected arrival | a counted packet decremented a counter at 0: armed too late or too small (detected *K* after acceptance, D3) |
| 3 | drift | a group run's request refused by the SRAM read queue: too much traffic on one area (spanning is gone, D26) |
| 4 | write-engine input overflow | an area oversubscribed |
| 5, 6 | table request overflow, GATHER queue overflow | nothing: hardware-fault detectors |
| 7 | read-port error | more than `READ_PORT_DEPTH` reads outstanding (a misaligned read sets [1], D36) |
| 8 | done early | `done` pulsed with a counter non-zero |
| 9, 10 | bus error, illegal instruction | a bad address (a store to program memory, an absent peripheral's window, a load from `PTR`, …) or instruction |
| 11–13 | peripheral 1–3 (live) | that peripheral's `ERRORS` is non-zero |
| 14 | receive underflow | `REQUEST_RECEIVE` popped an empty queue |

- **v1.1's "what a phase caused" pattern is retired.** `phase_start()` and `phase_conditions()` compared `TILE_CONDITIONS` before and after a phase; now the first error traps, so a running program always reads `ERRORS` = 0.
- **Not every bug sets a bit** (D27): `ecall`, `ebreak`, a fetch outside program memory and the boot ROM, a misaligned jump and a trap in the handler park the unit without an error line. meshsim stops loudly on each; on hardware only the watchdog notices.

## 13. A synchronised start by identical code

**What it does.** Starts a run on the same cycle on every tile (within each tile's 0/+1 skew) after set-up of any length. The set-up is one command and the start is the next. GO wakes every tile on the same cycle, and from that `WFI` to the start store every tile runs the same instructions.
**Relies on:** spec 6.3, 6.4; D14 (the base-address instruction, if any, is the same on every tile).

<!-- recipe: sync (rcp_sync.c) -->
```c
uint32_t w = wait_for_command();          /* every tile leaves WFI on GO's cycle (+ its 0/+1 skew) */
if (CMD_TYPE(w) == 2) {                    /* the start: identical code from WFI on every tile */
    tile->WRITE_COUNTER[CTR_SRAM - 1] = m; /* the counter first, */
    start_run();                           /* then the start store */
    wait_all(m ? EV_CTR(CTR_SRAM) | EV_RUN_DONE : EV_RUN_DONE);
    continue;
}
if (CMD_TYPE(w) != 1) sim_exit(0);
/* the set-up: any length, per tile; it ends in wait_for_command() */
spin(2 * me);
set_stream(1, STREAM_SRC_W(PRM, 0), (uint32_t)&par, STREAM_COUNT_W(1, SIZE_128, CTR_CM, MODE_WRITE, 0));
tile->WRITE_COUNTER[CTR_CM - 1] = 1;
tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 1, 1);
start_run();
wait_all(EV_CTR(CTR_CM) | EV_RUN_DONE);    /* the parameters are in; the tables are free */
tables->PTR[(me - 1) & 63] = DST;
set_stream(0, STREAM_SRC_W(SRC, 0), 0, STREAM_COUNT_W(par.n, SIZE_64, CTR_SRAM, MODE_SRC, 1));
tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
m = par.m;
```

**The rules of the start command** (rule 3, section 15):
- **No data-dependent path from `WFI` to the start store.** The counter store and the start store are the only work; everything else belongs in the set-up command.
- **Store the counter first**, then the start.
- **The lead-in is the same everywhere:** it depends only on the run kind, the tables and *L* = `ts_engine_latency`.

**What the compiler emits.** At `-O2`, GCC 13 compiles the start path above to: the `wfi`, the load of `EVENTS`, the load of slot 0 (its base hoisted out of the loop), a shift and a compare, the load of `m`, and two x0-relative stores. Every register access through `tile->` is a single x0-relative instruction, so W3-27's feared extra base-address instruction doesn't appear here. The same holds for every recipe in `WORKER6/check` (checked in the disassembly: no register-block base is ever loaded). Cycles: to measure.

**Walk.** The set-up's own copy (the parameters, 128 bits into `par`, which is `aligned(16)`) arms counter 2 and waits for it and for its run, so the set-up ends with every arrival in and the tables free. `done` then cannot be early. The start path is the one recipe 5 walks.

## 14. Cycles

**Every figure is to measure on meshsim v1.2.** v1.1's measurements (`v1.1/sw/RECIPES.md` section 14) don't carry over: the address map, the counter path (*K* after the director), the header, the alignment rules and the register map have all changed. Only figures that follow from a formula the log keeps are given, marked [derived].

| recipe | what | v1.2 |
|---|---|---:|
| cmdloop | a 3-argument and a 7-argument command: GO → all 64 READY | to measure |
| copy | 100 records: start store → first instruction after the wait | to measure |
| cmem | 256 words into program memory: start store → wake | to measure |
| bucket | count copy + 4 bucket passes, 200–263 records: GO → all READY (no `PTR[64]` restore now) | to measure |
| aug | local form, 64 keys; remote form, start command: GO → all READY | to measure |
| gather | k = 1 and k = 2, 50–113 values per tile: GO → all READY | to measure |
| xchg | one-word exchange, 64 words per tile; four words to one tile: GO → all READY | to measure |
| xvec | 6 words to each of 4 neighbours: GO → all READY | to measure |
| a2a | counts + group maxima; reduction + configure; start: GO → all READY | to measure |
| a2a | start (a group run): GO store → start store, every tile | to measure |
| a2a, sync | start store → first request: the lead-in | 4*L* + 3 = 7 (group run), 2*L* + 2 = 4 (stream run) at *L* = 2 [derived: spec 4.2, unchanged by the log] |
| reduce | 16 words, max over 64 tiles, broadcast back: GO → all READY | to measure |
| rdport | one read; 7 searches over 300–363 keys: GO → all READY | to measure |
| sync | start (a stream run): GO store → start store, every tile | to measure (v1.1's 8–9 assumed v1.1's register map and a 2-cycle `WAKE_EVENTS` load) |
| write_block, fill, hash | per record, marginal | to measure (write_block and fill now send mode 0 by address, with no `PTR` set-up) |
| selftest | fill, configure, start (timetable 1,024), hash, signatures to C&C 1 | to measure (the signatures now go over the request bus, recipe 21) |
| boot | reset release → all tiles running their image | to measure |
| handler | trap → last of 23 reports in C&C 1's receive queue | to measure |
| replay | refill latency: credit consumed → run started → counter 4 done | to measure |
| *K* | counter decrement after acceptance | 8 by default; meshsim errors if set below max(2·*L* + 3, *L* + 4, `cm_latency`, `rp_latency`) [D32, D36] |

## 15. The rules that bite

v1.1's five rules, rewritten, and the ones the v1.2 walks added. Each right form is a region of `rcp_rules.c` or another recipe; the wrong form is described.

**Rule 1: a phase that arms a counter waits for it before `done`** (CONVENTIONS 5): the wait proves every arrival is in.

<!-- recipe: rule_wait (rcp_rules.c) -->
```c
/* A phase that arms a counter waits for it before done (CONVENTIONS 5): the wait proves every arrival is in. Done
 * with an arrival outstanding is the done-early error (D18 [8]), which traps (D20). */
static void self_reply(void) {
    unsigned me = tile_index();
    tile->WRITE_COUNTER[CTR_REPLY - 1] = 1;
    send_to(ROUTER_HDR_W(me, MODE_WRITE, CTR_REPLY) | ROUTER_NOINC, REPLY_WORD(me));
    send32(7);
    tile->ROUTER_HDR &= ~ROUTER_NOINC;              /* the bit stays set until cleared (D37.5) */
    wait_all(EV_CTR(CTR_REPLY));                    /* the reply is in */
    rbox = REPLY[2 * me];
}
```

*Wrong:* without the wait, `done` comes with the reply still on its way. That is **done early**, `ERRORS` [8], and it now traps the tile (D20), where v1.1 only flagged it.

**Rule 2: disjoint buffers per engine run** (spec 4.1).

<!-- recipe: rule_disjoint (rcp_rules.c) -->
```c
/* One engine run never reads and writes the same buffer (spec 4.1: no coherence between a run's reads and its
 * writes). To shift records within a buffer, copy out and back: two runs, each between disjoint buffers. */
static void shift_up(uint32_t buf, uint32_t tmp, uint32_t n, uint32_t by) {
    tables->PTR[tile_index()] = tmp;
    set_stream(0, STREAM_SRC_W(buf, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_SRC, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);       /* buf -> tmp */
    tables->PTR[tile_index()] = buf + 2 * by;
    set_stream(0, STREAM_SRC_W(tmp, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_SRC, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);       /* tmp -> buf + by records */
}
```

*Wrong:* one run copying a buffer onto itself shifted by 8 records reads chunks it has already written. meshsim flags it (invariant 2).

**Rule 3: from `WFI` to the start store, identical code on every tile** (spec 6.3).

<!-- recipe: rule_start (rcp_rules.c) -->
```c
/* From the WFI that GO ends to the start store, every tile runs the same instructions (spec 6.3): no data-dependent
 * branch or loop, no stall (a send store to a full holding register, a multi-cycle wait). Counter values may differ: a
 * store costs one cycle whatever its value. Anything else belongs in the configure command. */
static void ring_start(void) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = expect;
    start_run();
    wait_all(EV_CTR(CTR_SRAM) | EV_RUN_DONE);
}
```

*Wrong:* a delay of `me & 7` loop iterations before the start store spreads the tiles' starts over the delay's range. Nothing is flagged, but a group run's timetable assumes every tile starts within its 0/+1 skew, and an early receiver's window between GO and arming grows. That is the window D36 keeps closed by timing.

**Rule 4: every 64- and 128-bit access is naturally aligned, and the hardware checks it** (D26). This replaces v1.1's "row-aligned elements in a group run", which was a timing rule.

<!-- recipe: rule_align (rcp_rules.c) -->
```c
/* Every 64- or 128-bit access is naturally aligned, checked in hardware (D26): a stream's source and every element
 * it reads, every PTR its packets land through, every packet address. Records are 64-bit at even chunks; a group may
 * still mix read sizes (here 64-bit records to this tile and 32-bit words to the right-hand neighbour). */
static void two_streams_one_group(uint32_t n, uint32_t n_left) {
    unsigned me = tile_index();
    tables->PTR[me] = C; tables->PTR[(me - 1) & 63] = C + 0x8000;
    set_stream(0, STREAM_SRC_W(A, 0), 0, STREAM_COUNT_W(n, SIZE_64, CTR_SRAM, MODE_SRC, 0));
    set_stream(1, STREAM_SRC_W(B + 3, 0), 0, STREAM_COUNT_W(n, SIZE_32, CTR_SRAM, MODE_SRC, 1));   /* 32-bit: any chunk */
    set_group(0, GROUP_LO_R(n > n_left ? n : n_left), GROUP_HI(0, 2, 0));
    local_run(DMA_CFG_RUN(RUN_GROUPS, 0, 1), n + n_left);
}
```

*Wrong:* a 64-bit stream starting at chunk 3. v1.1 split each element into two area accesses and drifted; v1.2 has no spanning access. The first read is a misaligned engine access: dropped, still counted, and `ERRORS` [1] traps the tile (D32). Its consequences, all D26:
- every landing buffer is aligned to its packets' size (`aligned(8)` or `aligned(16)` in C, recipes 3, 8, 10, 13);
- every `PTR` is aligned to the size of the packets landing through it, and one pointer takes one size;
- a send-port run doesn't go down in size and back up without re-storing `ROUTER_ADDR` (W3-29): a 32-bit send to word 4, then a 64-bit send, is now misaligned;
- a peripheral DMA transfer's word count is a multiple of 4 unless it is the last into that buffer (recipe 25).

**Rule 5 (v1.1: staging below the sort scratch, `PTR[64]` restored after a bucket pass): retired.** Staging lands by address in region 0x03 (D17), so `PTR[64]` is an ordinary pointer.

**Rule 6: a phase that runs the address generator waits for DMA run done before `done`.** A command then always starts with the tables free, and needs no `wait_tables()`. A tile that receives nothing still has its run going after its last arrival (recipe 9). The fallback for a helper that can't know what ran before is polling `STATUS.rd_running` (recipe 23).

**Rule 7: every wait follows an arming, or waits for something else.** An unarmed counter never fires its done event, and a count of 0 never makes a counters-done edge. So `a2a_start` waits for [7] alone when it expects nothing, and `local_run(…, 0)` likewise.

**Rule 8: a consuming load acts on every bit it returns** (D10, spec 4.12). A load of `tile->EVENTS` consumes every masked event, and a load of a peripheral's `EVENTS` takes one count from every set, masked bit, credits included. Either act on every bit returned (recipe 26) or narrow the mask around the load (recipe 25's `periph_dma`). Look with `EVENTS_PEEK`.

**Rule 9: keep GO in `EVENTS_MASK` whenever `done` is pulsed** (spec 4.12). The gate lets GO wake the core whatever the mask, but only a load with GO masked consumes it. A program that changes the mask (recipe 26) puts it back, with GO, before its next `wait_for_command()`.

**Rule 10: clear the no-increment bit when done with a fixed address** (D7, D37.5). It stays set until the program clears it; a later run of sends meant for consecutive addresses would otherwise land every packet at one address, counted and silent. `xchg` and `self_reply` clear it as they finish. **[F8]**

**Rule 11: at most 1, 3 and 1 requests outstanding to C&C 1, C&C 2 and C&C 3** (D38). A tile issues its next request to a target only after the previous one's reply has come back; the request names its reply's counter (D25), which the tile arms before sending. The targets' receive queues (128, 256 and 128 requests, in the request tree) then never run out of credits, so the root never stalls a row. **Error reports are the exception**: the handler sends its 23 at once and expects no reply (recipe 24). C&C 1 answers every tile request (recipe 27's `drain_requests`); a target that didn't would leave the tile waiting for ever.

## 16. Gaps and awkwardness found

v1.1 listed three. Two are gone: a stuck counter now ends in an error, the toast pin and an outside reset (D35), not in a HALT; and REPORT's settle went with REPORT. The third remains: table request overflow and GATHER queue overflow still can't be caused by a program, so they stay hardware-fault detectors.

What the v1.2 walks found is in `WORKER6/FINDINGS.md`, most important first, each with the log's answer. In brief:

- **[F1]** GO reached every tile still asleep in the boot ROM, which jumped on any GO, so units couldn't be started one at a time. *Answered by D37.1:* the ROM jumps only if a command has landed (recipe 22).
- **[F2]** Nothing enabled the hard interrupt. *Answered by D37.2:* boot part 2, the image's first command, does (recipe 22).
- **[F3]** The handler's report set was fixed (D21) but covered only present peripherals (D20). *Answered by D37.3:* every slot, zeros for an absent one (recipe 24).
- **[F4]** C&C 1's UART dump looked too slow for the FPGA monitor's 0.1 ms. *Not a limitation (D37):* with an FPGA the dump goes down LVDS and the FPGA waits.
- **[F5]** Reports could wait on the request tree behind requests to a stopped target. *Fixed by D38:* deep receive queues in the tree and per-tile limits (rule 11).
- **[F6]** Polling `STATUS.rd_running` (D17) is only as good as guarantee B, which the log states for [7] alone. *Open.*
- **[F7]** A 128-bit data overlay at address 0 wrote the reserved word. *Answered by D37.4:* 16 bytes reserved; overlays start at 0x10.
- **[F8]** The no-increment bit made a forgotten header store a silent pile-up. *Answered by D37.5:* the program clears it (rule 10).
- **[F15]** D38 lets a tile keep 3 requests outstanding to C&C 2, but its replies would share one reply-block pair. *Open:* each request names its own reply address (D25 allows it).
- **[F16]** The report set by D20's list is 23 requests, not the "about 17" D38 sizes against. *Open.*
- **[F9]–[F14]**, smaller and open: mask juggling around consuming loads; `wait_all`'s cost and the stale counters done it leaves; an extra wake per expansion in the replayer loop; no timer for C&C 1's bounded waits; a stale "command landed" in every tile's `EVENTS`; and a stream can't name the broadcast, so C&C 1 boots tiles through its send port.

## 17. Write a block from data memory into big SRAM

**What it does.** Copies records the controller has in data memory into big SRAM: 64-bit mode-0 writes to the tile's own big SRAM at an explicit address, two stores per record.
**Relies on:** D5 (region 0x03 by byte address; `SRAM_ADDR`), D6, D7.

<!-- recipe: write_block (rcp_selftest.c) -->
```c
/* n 64-bit records from data memory (w[2i] the low word, w[2i + 1] the high) into big SRAM at chunk dst (even): mode 0
 * to the tile's own region 0x03 at an explicit address, ROUTER_ADDR advancing by 8 per packet (D5, D7). No PTR, so
 * no table is touched. Counted on counter 1. */
static void write_block(uint32_t dst, const uint32_t *w, uint32_t n) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = n;
    send_to(ROUTER_HDR_W(tile_index(), MODE_WRITE, CTR_SRAM), SRAM_ADDR(dst));
    for (uint32_t i = 0; i < n; i++) send64(w[2 * i + 1], w[2 * i]);
    if (n) wait_all(EV_CTR(CTR_SRAM));              /* every record written */
}
```

- **Mode 0 by address, not mode 1 through `PTR[me]`:** v1.2's 32-bit address reaches big SRAM directly, so no table is touched and no table wait is needed. `ROUTER_ADDR` advances by 8 per packet; the destination chunk is even.

**Walk.** Arm counter 1 for n, then send. Every packet is this tile's own and goes out after the arming. The wait returns when the last write has entered its area's queue (*K* covers it, D3). A later read queues behind the write and can't overtake it (S1).

## 18. Fill a block with pseudo-random data

**What it does.** Fills big SRAM with a reproducible pseudo-random sequence, generated in registers and sent as in recipe 17, so nothing passes through data memory. The generator, a 64-bit xorshift on the 32-bit core, and the seeding are in `selftest.h`, unchanged from v1.1.
**Relies on:** recipe 17; `selftest.h`.

<!-- recipe: selftest_h (selftest.h) -->
```c
/* one xorshift64 step on (hi, lo) */
static inline void st_next(uint32_t *hi, uint32_t *lo) {
    uint32_t h = *hi, l = *lo;
    h ^= h << ST_XS_A | l >> (32 - ST_XS_A); l ^= l << ST_XS_A;      /* x ^= x << A (h from the old l first) */
    l ^= l >> ST_XS_B | h << (32 - ST_XS_B); h ^= h >> ST_XS_B;      /* x ^= x >> B (l from the old h first) */
    h ^= h << ST_XS_C | l >> (32 - ST_XS_C); l ^= l << ST_XS_C;      /* x ^= x << C */
    *hi = h; *lo = l;
}
/* a 64-bit seed to a generator state: never all zero, then ST_WARMUP steps */
static inline void st_seed(uint32_t *hi, uint32_t *lo) {
    if ((*hi | *lo) == 0) *lo = 1;
    for (int i = 0; i < ST_WARMUP; i++) st_next(hi, lo);
}
/* a tile's own seed, from its index and a base seed (0 for none) */
static inline void st_tile_seed(unsigned me, uint32_t base_hi, uint32_t base_lo, uint32_t *hi, uint32_t *lo) {
    *lo = base_lo ^ (me + 1) * ST_SEED_K0;
    *hi = base_hi ^ (me + 1) * ST_SEED_K1;
    st_seed(hi, lo);
}
/* one hash step: a word into its lane */
static inline uint32_t st_mix(uint32_t h, uint32_t w, uint32_t m) {
    __asm__ ("" : "+r"(m));                        /* keep m in a register: one mul (Zmmul), not a shift-and-add chain */
    h = (h ^ w) * m;
    return h << ST_HASH_R | h >> (32 - ST_HASH_R);
}
```

<!-- recipe: fill (rcp_selftest.c) -->
```c
/* n pseudo-random 64-bit records into big SRAM at chunk dst: selftest.h's xorshift64, generated in registers and sent
 * like write_block(), with no data memory in between. (hi, lo) is the generator's state, seeded by st_seed() or
 * st_tile_seed(); it is left where the block ended, so a second call continues the sequence. */
static void fill(uint32_t dst, uint32_t n, uint32_t *hi, uint32_t *lo) {
    uint32_t h = *hi, l = *lo;
    tile->WRITE_COUNTER[CTR_SRAM - 1] = n;
    send_to(ROUTER_HDR_W(tile_index(), MODE_WRITE, CTR_SRAM), SRAM_ADDR(dst));
    for (uint32_t i = 0; i < n; i++) { st_next(&h, &l); send64(h, l); }
    *hi = h; *lo = l;
    if (n) wait_all(EV_CTR(CTR_SRAM));
}
```

Two ways to seed: a seed per tile from C&C 1 (one unicast command each, the 64-bit seed as two arguments), or a seed made from the tile index, optionally mixed with a base seed C&C 1 broadcasts:

<!-- recipe: seeds (rcp_selftest.c) -->
```c
/* The two ways to seed. From C&C 1: one unicast command per tile, the 64-bit seed as two arguments. From the tile
 * index: one broadcast command whose two arguments are a base seed (0 for none), mixed with the index by st_tile_seed(). */
static void seed_from_cnc(volatile uint32_t *arg, uint32_t *hi, uint32_t *lo) {
    *lo = arg[0]; *hi = arg[1];
    st_seed(hi, lo);
}
static void seed_from_index(volatile uint32_t *arg, uint32_t *hi, uint32_t *lo) {
    st_tile_seed(tile_index(), arg[1], arg[0], hi, lo);
}
```

## 19. Hash a block of big SRAM

**What it does.** Reads a block through the read port and folds it into an order-sensitive 64-bit hash: two 32-bit lanes, the low and the high word of each record, h = rotl((h ^ w) × M, R).
**Relies on:** recipe 11 (the read port, counter 2), CONVENTIONS 9.

<!-- recipe: hash (rcp_selftest.c) -->
```c
/* The 64-bit hash of n records of big SRAM at chunk src (selftest.h: two 32-bit lanes, the low and the high words,
 * h = rotl((h ^ w) * M, R)), read through the read port (spec 4.3) in batches of 32 words, two buffers: while one batch
 * is folded the next is in flight. Each batch is counted on counter 2; the next batch is armed after the wait that
 * consumed the last one's done. A step of its own: no packets may arrive for this tile meanwhile (CONVENTIONS 9). */
static void hash_block(uint32_t src, uint32_t n, uint32_t *hi, uint32_t *lo) {
    static volatile uint32_t buf[2][32];
    uint32_t h0 = ST_HASH_H0, h1 = ST_HASH_H1, words = 2 * n, issued = 0, folded = 0;
    unsigned k = 0;
    while (folded < words) {
        uint32_t cur = issued - folded;            /* the batch in flight to buf[k] (0 the first time) */
        if (cur) wait_all(EV_CTR(CTR_CM));         /* it has landed */
        if (issued < words) {                      /* the next batch, to the other buffer */
            uint32_t m = words - issued < 32 ? words - issued : 32;
            tile->WRITE_COUNTER[CTR_CM - 1] = m;
            tile->SRAM_RD_DEST_ADDR = RD_DEST((uint32_t)&buf[cur ? k ^ 1 : k][0], SIZE_32, CTR_CM);
            for (uint32_t i = 0; i < m; i++) tile->SRAM_RD_ADDR_QUEUE = src + issued + i;
            issued += m;
        }
        if (!cur) continue;
        for (uint32_t i = 0; i < cur; i += 2) {    /* fold: the low word into lane 0, the high into lane 1 */
            h0 = st_mix(h0, buf[k][i], ST_HASH_M0);
            h1 = st_mix(h1, buf[k][i + 1], ST_HASH_M1);
        }
        folded += cur; k ^= 1;
    }
    *hi = h1; *lo = h0;
}
```

- **Double-buffered:** batches of 32 words; the next batch is queued before the last is folded, so its reads overlap the fold. One counter serves both buffers, because a batch is armed only after the wait that consumed the previous one's done. Two counters, one per buffer, would let two batches be in flight; the convention keeps one.
- **`st_mix` keeps M in a register**, so the compiler emits one `mul` (Zmmul).

## 20. REPORT: retired

**v1.1's REPORT debugging command is withdrawn** (D1.6), and with it `CMD_REPORT`, `report()`, `paced_to_cnc()` and C&C's `reportOnError()`/`report()` sequence. It worked only when a failed tile still reached `wait_for_command()` and the mesh still carried its reply: exactly the cases that fail. Its replacements:
- **the error line**, driven by hardware at a fixed latency, which reaches C&C 1's `ERROR_LINES_0`-`2` and its hard interrupt (D15, D23);
- **the handler's reports** over the request bus, a fixed set from every failing unit (recipe 24);
- **C&C 1's toast path** (recipe 27) and the outside reset (D35).

**The HALT material is retired with it** (D23): C&C 1's halting a tile before an uncoordinated write, HALT clearing counters and conditions, HALT → RUN restarting a faulted core, and v1.1's gap "a counter can't be given up on without HALT". There is no HALT state; the reset is the only stop.

## 21. The self-test

**What it does.** Seeds and fills a block on every tile (recipe 18), runs an operation on it (here a uniform all-to-all on the 16-group schedule), hashes what arrived (recipe 19), and sends the signature to C&C 1. The simulator computes every tile's expected signature from the generator and the all-to-all's permutation alone.
**Relies on:** recipes 9, 18, 19; spec 3.3 and 4.18 (the request bus).

<!-- recipe: selftest (rcp_selftest.c) -->
```c
/* The self-test: seed and fill a block (command 1 or 2), run an operation on it (here the uniform all-to-all on the
 * 16-group schedule: n / 64 records to each tile, configure then start), hash what arrived (command 5), and send the
 * signature to C&C 1 (command 6) as a request, which C&C 1 compares with the simulator's. The all-to-all: */
static void a2a_uniform_configure(uint32_t n) {
    unsigned me = tile_index();
    m_per = n >> 6;
    for (int g = 0; g < 16; g++) {
        for (int i = 0; i < 4; i++) {
            unsigned off = SCHED[g][i], d = (me + off) & 63;
            set_stream(4 * g + i, STREAM_SRC_W(SRC + 2 * m_per * d, 0), 0, STREAM_COUNT_W(m_per, SIZE_64, CTR_SRAM, MODE_SRC, off));
        }
        set_group(g, GROUP_LO_R(m_per), GROUP_HI(4 * g, 4, 0));   /* every count is m_per: R needs no reduction */
    }
    for (int s = 0; s < 64; s++) tables->PTR[s] = DST + 2 * m_per * s;   /* source s's block lands at DST + s blocks */
    expected = 64 * m_per;
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, 0, 16);
}
static void a2a_uniform_start(void) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = expected;   /* identical code from waking to the start store (6.3) */
    start_run();
    wait_all(expected ? EV_CTR(CTR_SRAM) | EV_RUN_DONE : EV_RUN_DONE);
}
```

<!-- recipe: result_to_cc1 (rcp.h) -->
```c
/* A result for C&C 1 (the self-test's signature): one request to target C&C 1 over the request bus (spec 3.3, 4.18),
 * which has back-pressure and never drops anything, so nothing needs pacing (v1.1 paced replies to C&C through
 * REPORT's delay loop). D38: a tile keeps at most one request outstanding to C&C 1, and issues the next only after
 * the previous one's reply has come back; the request names its reply's counter (D25). So: arm counter 3, send,
 * wait for C&C 1's reply, a 32-bit write to this tile's REPLY_WORD(DST_CC1). The format is a placeholder. */
static uint32_t result_to_cc1(uint32_t hi, uint32_t lo) {
    tile->WRITE_COUNTER[CTR_REPLY - 1] = 1;          /* before the request: the reply can't come sooner */
    request_send(0, hi, lo, REQ_CC1(REQ_OP_RESULT, CTR_REPLY, tile_index()));
    wait_all(EV_CTR(CTR_REPLY));                    /* the request is no longer outstanding */
    return REPLY[2 * DST_CC1];                      /* C&C 1's answer */
}
```

The command sequence, from C&C 1: fill (`2`, base seed, n), configure (`3`), start (`4`), hash (`5`, the receive buffer, n), signature (`6`). C&C 1 pops the 64 requests and answers each.

- **The signatures go over the request bus**, not the mesh. v1.1 sent them as replies to C&C, paced by REPORT's delay loop so nothing fanned in. The request bus has back-pressure and never drops anything, so no pacing is needed.
- **One request outstanding to C&C 1** (D38, rule 11): the tile arms counter 3, sends, and waits for C&C 1's one-word reply before `done`. C&C 1 answers each result as it drains them (recipe 27). The request's layout below its target is C&C 1's to define; the draft's `REQ_CC1` (op, reply counter, argument) is a placeholder.

**Walk.** C&C 1 sends command 6 right after the hash's GO and waits for all READY. Each tile wakes, arms counter 3 with 1, and pushes its request; 64 requests reach C&C 1's 128-entry receive queue, so none waits for a credit. C&C 1's wait loop sees [9], pops each and writes its reply to the requestor's `REPLY_WORD(64)` with the counter the request named. Each tile's counter 3 reaches 0 and it pulses `done`; READY follows. A reply can't arrive before the tile's arming, which precedes its request.
- **Checked in v1.1, to check again:** all 64 signatures equal the simulator's, and a record corrupted on one tile after the fill shows up only in its receiver's signature.

## 22. The boot ROM and the boot sequence

**What it does.** Brings the chip from reset to every tile running its image with its first data in big SRAM. Three pieces of code: the ROM every unit runs, the image's start-up, and C&C 1's side.
**Relies on:** D22, D23, D31 and D36 (the ROM, the order of step 5, one kind for overlays and command, C&C 1's own boot), D37.1 (the ROM jumps only if a command has landed), D37.2 (boot part 2 enables interrupts), D37.4 (16 reserved bytes), D6 ("command landed" when the slot-0 write lands in port B), D20 (`mtvec`, traps).

**The ROM**, as D36 lists it with D37.1's change to step 3 (assembly, since it is silicon):

<!-- recipe: bootrom (bootrom.S) -->
```asm
_reset:
    la   t0, trap_handler               /* 1. mtvec: the ROM's report-and-park handler (recipe 24) */
    csrw mtvec, t0
    lw   t1, X0REL(OFF_PARAMS)(zero)    /* 2. the endpoint id, PARAMS [26:20] */
    srli t1, t1, 20
    andi t1, t1, 0x7F
    li   t2, 64
    bgeu t1, t2, controller
    li   t3, 1                          /* 3. a tile: pulse done; "command landed" then raises READY */
3:  sw   t3, X0REL(OFF_PULSE_DONE)(zero)
1:  wfi                                 /* the done gate: only GO wakes it */
    lw   t4, X0REL(OFF_EVENTS)(zero)    /* consumes GO (the reset mask holds it), and returns [6] unconsumed */
    andi t5, t4, EV_GO
    beqz t5, 1b
    andi t5, t4, EV_COMMAND             /* D37.1: jump only if a command has landed. The event, not STATUS [5]: */
    beqz t5, 3b                         /* GO cleared the status bit. A GO for other tiles: done again, wait */
    j    jump
controller:                             /* 4. C&C 1-3 (64-66): wait for "command landed" itself */
    li   t3, EV_COMMAND
    sw   t3, X0REL(OFF_EVENTS_MASK)(zero)
2:  wfi
    lw   t4, X0REL(OFF_EVENTS)(zero)    /* consumes [6] */
    andi t5, t4, EV_COMMAND
    beqz t5, 2b
    li   t3, EV_RESET_MASK
    sw   t3, X0REL(OFF_EVENTS_MASK)(zero)
jump:                                   /* 5. slot 0, then slot 1 (the first command is even: D36), and jump */
    li   t6, COMMAND_BASE
    lw   t3, 0(t6)
    lw   t3, 4(t6)
    jr   t3
```

**The image's start-up**, where the ROM jumps:

<!-- recipe: crt0 (crt0.S) -->
```asm
_start:
    li   sp, STACK_TOP                  /* the stack grows down from below the reply block */
    la   t0, __bss_start                /* zero .bss: .bss is not loaded */
    la   t1, __bss_end
1:  bgeu t0, t1, 2f
    sw   zero, 0(t0)
    addi t0, t0, 4
    j    1b
2:  call main                           /* never returns; interrupts stay off until boot part 2 (D37.2) */
3:  j    3b
```

**Boot part 2**, the image's first command (D31 step 5, D37.2):

<!-- recipe: boot_part2 (rcp_boot.c) -->
```c
/* Boot part 2: the image's first command after the ROM's jump, CMD_STAGE with the number of 128-bit packets of
 * big-SRAM data C&C 1 sends, counted on counter 1, no sooner than stage_delay after its GO store (6.2). The counter
 * store comes first in the start path, so no packet can beat it. Then this image's handler and the hard interrupt
 * (D37.2: the ROM leaves interrupts off, and enabling them is the image's job), then the wait. */
static void boot_part2(uint32_t packets) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = packets;    /* first: the data may follow GO by stage_delay */
    __asm__ volatile ("csrw mtvec, %0" :: "r"(trap_handler));
    __asm__ volatile ("csrs mie, %0" :: "r"(1u << 11));   /* mie.MEIE (the bit is Q1's, the CSR list) */
    __asm__ volatile ("csrsi mstatus, 8");          /* mstatus.MIE: from here an error traps */
    if (packets) wait_all(EV_CTR(CTR_SRAM));        /* the data is in big SRAM; then done, in wait_for_command() */
}
```

**C&C 1's side**, firmware for later (C&C 1 is a behavioural Java model in meshsim v1.2, D36). It uses `command()`, `phase()`, `wait_all_ready()` and `go()` from recipe 27:

<!-- recipe: cc1_boot (rcp_cc1.c) -->
```c
/* After the reset (D31, D36): every unit sleeps in its ROM. C&C 1 (itself started by the outside world) boots C&C 2
 * and C&C 3 first, and waits for their "started" requests; then the tiles, overlays and first command of one kind
 * (here all broadcast), READY, GO; then the first program's big-SRAM data by announced staging (step 5). */
static void overlay(unsigned dst, uint32_t addr, const uint32_t *w, uint32_t words) {   /* 16-byte aligned, padded */
    send_to(ROUTER_HDR_W(dst, MODE_WRITE, CTR_NONE), addr);
    for (uint32_t i = 0; i < words; i += 4) send128(w[i + 3], w[i + 2], w[i + 1], w[i]);
}
static void stage(uint32_t chunk, const uint32_t *w, uint32_t words) {   /* counted: the tiles armed for it */
    send_to(ROUTER_HDR_W(DST_BROADCAST, MODE_WRITE, CTR_SRAM), SRAM_ADDR(chunk));
    for (uint32_t i = 0; i < words; i += 4) send128(w[i + 3], w[i + 2], w[i + 1], w[i]);
}
static void boot_unit(unsigned dst, const Image *im) {
    overlay(dst, REGION_PMEM, im->pmem, im->pmem_words);
    overlay(dst, REGION_DMEM + DMEM_FIRST, im->dmem, im->dmem_words);   /* 0x00-0x0F are reserved (D37.4) */
    uint32_t entry = im->entry;
    command(dst, CMD_JUMP, 0, &entry, 1);           /* the first command is even: entry in slot 1 (D36) */
}
static void cc1_boot(const Image *fs, const Image *pager, const Image *tiles,
                     const uint32_t *data, uint32_t stage_words, uint32_t stage_chunk) {
    boot_unit(DST_CC2, fs);                         /* unicast to each: overlays and command of one kind */
    boot_unit(DST_CC3, pager);
    for (unsigned started = 0; started < 2; ) {     /* their start-up sends C&C 1 "started" (D31) */
        tile->EVENTS_MASK = EV_REQ_PENDING;
        __asm__ volatile ("wfi" ::: "memory");
        while (tile->EVENTS_PEEK & EV_REQ_PENDING) {
            uint32_t w0 = tile->REQUEST_GET_W0;
            (void)tile->REQUEST_RECEIVE;
            started += REQ_OP(w0) == REQ_OP_STARTED;   /* a controller's request: no reply needed (D38) */
        }
    }
    boot_unit(DST_BROADCAST, tiles);                /* broadcast: the same kind as its first command (D36) */
    cc1->READY_EXPECT[0] = ~0u;                     /* all 64 here; one at a time works too since D37.1 */
    cc1->READY_EXPECT[1] = ~0u;
    uint32_t n = stage_words / 4;                   /* boot part 2: announce the data, READY, GO, then the data */
    phase(CMD_STAGE, n, 0, 0, 0);                   /* waits for the boot command's READY, GOs, sends CMD_STAGE */
    wait_all_ready();                               /* CMD_STAGE has landed and the images wait in wait_for_command() */
    go(0);
    delay4(STAGE_DELAY / 4);                        /* no sooner than stage_delay after the GO store (6.2) */
    stage(stage_chunk, data, stage_words);         /* each tile waits for counter 1, then done */
}
```

**Walk, step by step** (D31's numbering):

1. **The reset is asserted** by the outside world, the whole chip (D35). Everything but the memories is zeroed: `done`, the gate (open), "command landed", every counter and event; `EVENTS_MASK` = GO | counters done; `mstatus.MIE` = 0.
2. **The reset is released.** Every controller starts at 0x02000000. The outside world lands C&C 1's own overlays and first command (D36); C&C 1 takes the controller branch and jumps.
3. **C&C 2 and C&C 3** sleep in the controller branch, their mask set to "command landed" alone. C&C 1 sends each its overlays, unicast and uncounted, then its first command, also unicast. One source, one destination, one kind: the command lands last (3.2). The ROM sees [6], restores the reset mask and jumps. Each image's start-up sends C&C 1 "started", and C&C 1 waits for both.
4. **The tiles** pulsed `done` in the ROM and sleep behind the gate. C&C 1 broadcasts the overlays, then the first command, also broadcast (D36: one kind, or a unicast command could overtake a broadcast overlay). The program image goes to region 0x01; the data image to region 0x00 from byte 0x10, since the first 16 bytes are reserved (D37.4). **[F7]**
5. **READY.** Each command lands in port B after every overlay packet, sets "command landed", and with `done` set raises READY. C&C 1, with `READY_EXPECT` = all 64, sleeps until "all tiles READY", then stores `PULSE_GO`.
6. **GO** clears `done` and the status bit "command landed", and wakes each tile's ROM. The load of `EVENTS` consumes GO, so the image doesn't start with a pending GO (W1-33). The same load returns the command-landed event [6], which the reset mask doesn't consume: GO cleared the status bit, not the event. With [6] the ROM reads slot 0, then slot 1, and jumps (D37.1). Without it, the GO was for other tiles: the ROM pulses `done` again and waits for the next GO. **[F1]**
7. **The start-up** sets the stack, zeroes `.bss` and calls `main`, with interrupts still off and `mtvec` still at the ROM's handler. An error before boot part 2 raises the tile's error line, which reaches C&C 1 at once, but takes no trap. `main` calls `wait_for_command()`.
8. **Boot part 2, the first program's big-SRAM data** (D31 step 5). C&C 1 sent `CMD_STAGE` right after the boot GO; it is the image's first command, odd, in slots 8-15. The data can't come with the boot command: the start-up isn't a short fixed start path, and data arriving before `.bss` is zeroed and the counter armed would be unexpected arrival. The tile reaches `wait_for_command()`; `done` and the landed `CMD_STAGE` raise READY; GO. In its start path the tile arms counter 1 with the count, then sets `mtvec` to its own handler and enables `mie.MEIE` and `MIE` (D37.2) **[F2]**. C&C 1 waits `stage_delay` after its GO store, then broadcasts the data, counted, to region 0x03 by address. The tile waits for counter 1, then `done`.

**What the walk found, and the answers.**
- **[F1]** GO is one global net, so the next GO reaches every tile still asleep in the ROM. D36's ROM jumped on any GO, into whatever memory held. D37.1's check of [6] fixes it, which makes `READY_EXPECT`'s one-at-a-time bring-up (D23, D28) work. **C&C 1's rule:** it lands a tile's first command only when that tile is to start at the next GO, and adds it to `READY_EXPECT` then. A tile whose command has landed jumps at the next GO, whatever `READY_EXPECT` says.
- **[F2]** `MIE` is 0 at reset (W1-03), and the log didn't say who sets it. D37.2: boot part 2 does, after its counter store, so the start path stays the counter store first.
- **The first command after a reset is even** (D36), so C&C 1's per-unit parity starts at 0 after every reset.
- **The ROM's handler never writes data memory** (D31): recipe 24's handler needs none, so the same code serves in the ROM and in the image.

## 23. Waiting for the tables by events

**What it does.** Replaces v1.1's `wait_tables()`, which spun on `rd_running` and `ts_busy`. Software waits for the tables like everything else, by events.
**Relies on:** D17 (the arrival side by the write counter, the local side by DMA run done; guarantees A, B, C), D16 ([7]), spec 4.12 (`EVENTS_REQUIRED`).

<!-- recipe: tables_wait (rcp_tables.c) -->
```c
/* The tables (STREAM, GROUP, PTR, HIST) are free when both sides are done (D17): the arrivals, which update PTR and
 * HIST, by the write counter the program armed (guarantee A: K covers the update); and this tile's own run, which reads
 * STREAM and GROUP, by DMA run done, EVENTS [7] (guarantee B: it fires after the run's last table read). Neither
 * alone is enough: arrivals can finish before the tile's own run, and the run can finish before the arrivals. */
static void a2a_finish(void) {
    wait_all(expected ? EV_CTR(CTR_SRAM) | EV_RUN_DONE : EV_RUN_DONE);
}
/* A helper that can't know what ran before (a pass that starts by checking the tables are free) may poll the level
 * STATUS.rd_running instead (D17), but only when nothing counted can still be arriving: it says nothing about PTR
 * and HIST updates from arrivals. */
static void tables_free_unknown(void) {
    wait_rd_idle();
}
```

- **Both events, not one** (D17): a tile's arrivals can finish before its own run (it sends more than it receives), and its run can finish before its arrivals (it receives more). `PULSE_DMA` clears [7] (D17), as arming clears a counter's done (D3), so neither event can be stale.
- **Where the waits sit in the recipes:** at the end of every phase that ran the address generator (`local_run()`, the start commands of recipes 5, 9, 13 and 21, the replayer's refill). By rule 6 the next command starts with the tables free, so no recipe waits *before* touching them. v1.1 had about 15 `wait_tables()` calls in the sort; each becomes either nothing (the previous phase waited) or this wait.
- **The poll** is a fallback, and only half of the condition. **[F6]**

**Walk (an all-to-all's end, both orders).**
- *(a) Arrivals first.* Counter 1 reaches 0 and [0] is set; this tile's run is still sending its last streams. The wake term needs [0] and [7], so the core sleeps on. The run walks its last entry, its last table read completes, and [7] is set. Wake; the load consumes both.
- *(b) Run first.* [7] is set; arrivals continue, each updating `PTR[src]` through the table request FIFO. The last decrement comes *K* cycles after the last arrival's acceptance, by which time its `PTR` update is readable (guarantee A). [0] is set: wake. A table write now can't race an update in flight.

## 24. The error handler

**What it does.** On any error (the hard interrupt) or synchronous exception: send the fixed set of reports to C&C 1 over the request bus, then park for ever.
**Relies on:** D20 (traps are terminal; `MIE` cleared on entry and never set again, so no retrap; no `mret`; torn port sequences), D21 and D37.3 (the fixed report set; C&C 1 counts against it), D38 (reports need no reply), D10 (an absent peripheral's window is a bus error), spec 3.3 and 4.18 (128-bit requests, back-pressure).

<!-- recipe: handler (handler.S) -->
```asm
trap_handler:                           /* mtvec points here; MIE is now 0 and is never set again (D20) */
    sw   x1, W1(zero)                   /* report 0: x1, x2, x3. The main code may have left the port's words */
    sw   x2, W2(zero)                   /* half written (D20, W1-04): every report writes all four */
    li   x1, HDR(0)
    sw   x1, W0(zero)
    sw   x3, SEND(zero)                 /* stalls only while the request FIFO is full (4.18) */
    REPORT_REGS(1,  x4,  x5,  x6)
    REPORT_REGS(2,  x7,  x8,  x9)
    REPORT_REGS(3,  x10, x11, x12)
    REPORT_REGS(4,  x13, x14, x15)
    REPORT_REGS(5,  x16, x17, x18)
    REPORT_REGS(6,  x19, x20, x21)
    REPORT_REGS(7,  x22, x23, x24)
    REPORT_REGS(8,  x25, x26, x27)
    REPORT_REGS(9,  x28, x29, x30)
    mv   x1, x31                        /* report 10: x31, mepc, mcause */
    csrr x2, mepc
    csrr x3, mcause
    REPORT_123(10)
    csrr x1, mtval                      /* report 11: mtval, PARAMS, STATUS */
    lw   x2, X0REL(OFF_PARAMS)(zero)
    lw   x3, X0REL(OFF_STATUS)(zero)
    REPORT_123(11)
    lw   x1, X0REL(OFF_ERRORS)(zero)    /* report 12: ERRORS, EVENTS_PEEK (never EVENTS: it consumes), counter 1 */
    lw   x2, X0REL(OFF_EVENTS_PEEK)(zero)
    lw   x3, X0REL(OFF_WRITE_COUNTER_1)(zero)
    REPORT_123(12)
    lw   x1, X0REL(OFF_WRITE_COUNTER_1 + 4)(zero)    /* report 13: counters 2-4 */
    lw   x2, X0REL(OFF_WRITE_COUNTER_1 + 8)(zero)
    lw   x3, X0REL(OFF_WRITE_COUNTER_1 + 12)(zero)
    REPORT_123(13)
    /* reports 14-22: three per peripheral, present or not, so the count is fixed (D21; FINDINGS F3). An absent
     * peripheral's window is a bus error (D10), so PARAMS [2:0] decides, and its reports carry zeros. */
    lw   x5, X0REL(OFF_PARAMS)(zero)    /* x5: the present mask; x6: the window's x0-relative base; x7: seq */
    li   x6, X0REL(0x200)
    li   x7, 14
1:  andi x8, x5, 1
    li   x1, 0
    li   x2, 0
    li   x3, 0
    beqz x8, 2f
    add  x9, x6, zero                   /* the window: PARAMS, STATUS, ERRORS (words 0-2) */
    lw   x1, 0(x9)
    lw   x2, 4(x9)
    lw   x3, 8(x9)
2:  slli x4, x7, 21                     /* header for seq x7 */
    ori  x4, x4, 1 << 10
    sw   x1, W1(zero); sw x2, W2(zero); sw x4, W0(zero); sw x3, SEND(zero)
    addi x7, x7, 1
    li   x1, 0
    li   x2, 0
    li   x3, 0
    beqz x8, 3f
    lw   x1, 24(x9)                     /* EVENTS_PEEK (word 6), EVENTS_MASK (7), DMA_HDR (8) */
    lw   x2, 28(x9)
    lw   x3, 32(x9)
3:  slli x4, x7, 21
    ori  x4, x4, 1 << 10
    sw   x1, W1(zero); sw x2, W2(zero); sw x4, W0(zero); sw x3, SEND(zero)
    addi x7, x7, 1
    li   x1, 0
    beqz x8, 4f
    lw   x1, 36(x9)                     /* DMA_ADDR (word 9); two words spare */
4:  slli x4, x7, 21
    ori  x4, x4, 1 << 10
    sw   x1, W1(zero); sw zero, W2(zero); sw x4, W0(zero); sw zero, SEND(zero)
    addi x7, x7, 1
    srli x5, x5, 1                      /* the next peripheral */
    addi x6, x6, 0x200
    li   x8, 23
    bltu x7, x8, 1b
park:
    j    park                           /* terminal: only the reset restarts the controller (D20) */
```

**The report set** (23 requests, three data words each, in this order; D1.4 and D38 estimate "about 17" **[F16]**):

| reports | words |
|---|---|
| 0–9 | x1–x30, three per report |
| 10–11 | x31, `mepc`, `mcause`; `mtval`, `PARAMS`, `STATUS` |
| 12–13 | `ERRORS`, `EVENTS_PEEK`, counter 1; counters 2–4 |
| 14–22 | per peripheral 1–3, three reports each: `PARAMS`, `STATUS`, `ERRORS`; `EVENTS_PEEK`, `EVENTS_MASK`, `DMA_HDR`; `DMA_ADDR`, 0, 0. Zeros for an absent one |

- **No memory, no scratch register.** Every store is x0-relative (the register block is at the top of the address space). A register is sent before it is reused: once x1 and x2 are in the port's `W1` and `W2` holding registers, x1 is free to build the header. So the handler reports x1–x31 exactly as the trap found them, and the ROM can hold the same code (D31).
- **It reads `EVENTS_PEEK`, never `EVENTS`,** at both levels: a load of `EVENTS` consumes (D20). Of a window's ten standard words it reads seven; `PULSE_RUN` and `PULSE_DMA` are write-only, and `EVENTS` consumes.
- **`PARAMS` [2:0] decides** whether a window is read: an absent one is a bus error, which would trap inside the handler.
- **Absent peripherals still get their three reports, as zeros**, so every unit sends the same number (D37.3). **[F3]**
- **No replies:** reports are D38's exception to one request outstanding to C&C 1.
- **The request header** (op, sequence) is a placeholder: C&C 1's request format isn't defined.

**Walk.**
1. An error sets a sticky `ERRORS` bit. The hard interrupt rises; with `mie` and `mstatus.MIE` set (boot part 2, recipe 22) the trap is taken, even in `WFI` and even in the `done` gate (D1.4). `MIE` drops on entry.
2. In parallel, the error line reaches C&C 1's `ERROR_LINES` at a fixed latency, with no software (D1.5); C&C 1 takes its own interrupt and runs recipe 27's toast path.
3. The handler stores 23 requests. A `REQUEST_SEND` stalls only while the 128-entry FIFO is full, so all 23 fit without a stall.
4. The tree delivers them to C&C 1's 128-entry receive queue, at the tree's root, by credits (D38); C&C 1 drains it.
5. The handler parks in a jump to itself. Its peripherals carry on, unserviced, until the reset (D21).
- **Reports never wait behind other targets' traffic.** Before D38 a tile's reports could sit behind its own earlier log request to a file system that had stopped popping, with the whole row stalled. With D38's queues and per-tile limits (rule 11), normal traffic never exhausts a target's credits, so the root never stalls a row. Only a mass failure, with more than 128 reports for C&C 1 at once, stalls rows, and C&C 1 drains them one by one (accepted). **[F5]**

## 25. A peripheral window through its struct

**What it does.** Reaches a peripheral through its struct (D14): checks it is present, loads the replayer's state before a pass, and runs a peripheral's DMA.
**Relies on:** D9 and D10 (the standard window; counted events; the optional DMA; landing within *K*), D14, spec 4.19.10 (the replayer's region 0 is written while it is stopped).

<!-- recipe: periph_window (rcp_replay.c) -->
```c
/* A peripheral is reached through its window's struct (D14): its standard words first (D10), then its own. Check the
 * tile's PARAMS [2:0] before touching a window: an absent peripheral's window is a bus error. Read EVENTS_PEEK to look;
 * a load of EVENTS consumes one count from every set bit in the window's EVENTS_MASK. */
static int replayer_present(void) {
    return (PARAMS_PERIPH_PRESENT(tile->PARAMS) & 1) && PARAMS_TYPE(replayer->PARAMS) == PTYPE_REPLAYER;
}
/* Load the state and stream words (two 128-bit words, st[0..7]) into region 0x04 while the replayer is stopped
 * (STATUS [0] = 0: region 0 is written then, spec 4.19.10): mode-0 writes to the tile's own peripheral region, 16-byte
 * aligned, counted on counter 4 like any write (they land within K, D10). */
static void replayer_load_state(const uint32_t *st) {
    while (replayer->STATUS & PST_RUN) ;           /* only between passes; the loop below ends a pass by its event */
    tile->WRITE_COUNTER[CTR_PERIPH - 1] = 2;
    send_to(ROUTER_HDR_W(tile_index(), MODE_WRITE, CTR_PERIPH), RP_STATE);
    send128(st[3], st[2], st[1], st[0]);
    send128(st[7], st[6], st[5], st[4]);
    wait_all(EV_CTR(CTR_PERIPH));
}
/* A peripheral with a DMA (not the replayer): send words 32-bit words from it to dst at addr, counted there on ctr.
 * Only DMA done is in the window's mask while it waits, so the consuming load claims nothing else (D10). A transfer
 * goes out as 128-bit packets, then 64 and/or 32: keep words a multiple of 4 unless it is the last into that buffer,
 * or DMA_ADDR is left misaligned for the next transfer's 128-bit packets (D26, W3-29). */
static void periph_dma(unsigned n, unsigned dst, uint32_t addr, uint32_t words, unsigned queue, unsigned ctr) {
    volatile PeripheralStd *p = peripheral(n);
    uint32_t mask = p->EVENTS_MASK;
    p->EVENTS_MASK = PEV_DMA_DONE;
    p->DMA_HDR = DMA_HDR_W(words, queue, dst, MODE_WRITE, ctr);
    p->DMA_ADDR = addr;
    p->PULSE_DMA = 1;                               /* a count of 0 still posts DMA done (D10) */
    tile->EVENTS_MASK = EV_GO | EV_PERIPH(n);
    while (!(p->EVENTS & PEV_DMA_DONE))             /* the consuming load; a spurious wake reads 0 and waits again */
        __asm__ volatile ("wfi" ::: "memory");
    tile->EVENTS_MASK = EV_RESET_MASK;
    p->EVENTS_MASK = mask;
}
```

- **The standard words come first, in every window** (D10): `PARAMS`, `STATUS`, `ERRORS`, `PULSE_RUN`, `PULSE_DMA`, `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK`, `DMA_HDR`, `DMA_ADDR`, then the peripheral's own from word 32. The replayer's are `REPLAYER_STATUS` and `REPLAYER_OPCODE`; C&C 1's are the error, READY and watchdog words (recipe 27).
- **A peripheral is written by packets to its region** (0x04–0x06) like any memory: mode 0, counted as the program chooses (counter 4 here). The replayer's state and stream words are two 16-byte-aligned 128-bit writes.
- **The replayer has no DMA**: words 4, 8 and 9 are unused, and its `EVENTS` [1] never sets.
- **`periph_dma`** narrows the window's mask to DMA done around its consuming load, because a load of `EVENTS` takes one count from every set, masked bit (rule 8). That is three extra accesses. **[F9]**

**Walk (`periph_dma`).**
1. Set the window's mask to DMA done alone; the tile's live bit [9 + n] can only rise for it.
2. Set `DMA_HDR` and `DMA_ADDR`, then `PULSE_DMA`. The receiver armed its counter beforehand: a DMA's packets are counted at their destination like any other.
3. The DMA sends 128-bit packets, then a 64- and/or 32-bit remainder. When the last has left, it posts DMA done (depth 1), and the tile's live bit rises.
4. `WFI` wakes; the load of the window's `EVENTS` consumes DMA done. If the live bit lagged and the wake was spurious, the load returns 0 and the loop sleeps again (D10).
5. Restore both masks. Did the receiver get the words? Only the receiver's counter says so: DMA done means *sent*.

## 26. The replayer's refill loop: counted credits

**What it does.** Runs one replay pass: starts it, keeps the code, read-log, write-log and expansion queues fed from big SRAM on the replayer's credits, supplies each controller-fed expansion, and stops when the pass ends.
**Relies on:** D10 (counted events; credits depth 2; set to 2 at `PULSE_RUN` and the expansion queue's at each new expansion; every refill but a stream's last exactly half; idle queues leave the mask; spurious wakes), D16 ([10] live), D17 (refills are address-generator runs), spec 4.19.8–4.19.10 (the queues, their halves, region 0x04).

<!-- recipe: replay_loop (rcp_replay.c) -->
```c
/* One pass of the replayer (spec 4.19.9 on D10's counted credits). Each queue's credit bit holds up to 2 credits, set
 * to 2 at PULSE_RUN (the expansion queue's again at each new expansion); a credit is permission to push half a queue.
 * One load of the window's EVENTS consumes one credit from every set bit in its mask, so the loop pushes for every bit
 * the load returns, in one refill run of up to four streams, and waits for that run before it looks again. A queue
 * with nothing more to push leaves the mask, or its spare credit would keep the tile awake (W3-24). Resident streams
 * only: data staged from LPDDR is fetched into big SRAM first, on another counter (spec 4.19.13). */
typedef struct { uint32_t chunk, left, port, half, bit; } Feed;   /* the next half in big SRAM (4-chunk aligned), entries left */
static unsigned refill(Feed *f, unsigned s, uint32_t *pmask, uint32_t *armed) {
    uint32_t n = f->left < f->half ? f->left : f->half;    /* every refill but a stream's last is exactly a half */
    set_stream(s, STREAM_SRC_W(f->chunk, 0), f->port, STREAM_COUNT_W(n, SIZE_128, CTR_PERIPH, MODE_WRITE, 0));
    f->chunk += 4 * n;                              /* 128-bit entries: 4 chunks each */
    f->left -= n;
    *armed += n;
    if (f->left == 0) *pmask &= ~f->bit;            /* nothing behind its credits now: out of the mask */
    return s + 1;
}
static void replay_pass(Feed *code, Feed *rlog, Feed *wlog, int store_log) {
    Feed exp = { 0, 0, RP_Q_EXP, RP_HALF_EXP, RP_EV_EXP };
    uint32_t pmask = PEV_RUN_DONE | RP_EV_NEWEXP | (code->left ? RP_EV_CODE : 0) | (rlog->left ? RP_EV_RLOG : 0)
                   | (store_log && wlog->left ? RP_EV_WLOG : 0);   /* no write log without the store log */
    replayer->EVENTS_MASK = pmask;                  /* the expansion's bit joins at the first new expansion */
    tile->EVENTS_MASK = EV_GO | EV_PERIPH(1);       /* GO can't come mid-pass; the tile's [10] is live */
    replayer->PULSE_RUN = 1;                        /* empties the queues; every credit counter to 2 */
    for (;;) {
        __asm__ volatile ("wfi" ::: "memory");
        uint32_t ev = replayer->EVENTS;             /* consumes: one credit from each set bit under pmask */
        if (ev & PEV_RUN_DONE) break;               /* the pass has ended; no refill is in flight here */
        if (ev & RP_EV_NEWEXP) {                    /* present an expansion: its credits are 2 again */
            template_lookup(replayer->REPLAYER_OPCODE, RP_NEXT_OFFSET(replayer->REPLAYER_STATUS), &exp.chunk, &exp.left);
            if (exp.left) pmask |= RP_EV_EXP;       /* its first credit is claimed on the next wake */
        }
        unsigned s = 0;
        uint32_t armed = 0;
        if (ev & RP_EV_CODE) s = refill(code, s, &pmask, &armed);
        if (ev & RP_EV_RLOG) s = refill(rlog, s, &pmask, &armed);
        if (ev & RP_EV_WLOG) s = refill(wlog, s, &pmask, &armed);
        if (ev & RP_EV_EXP)  s = refill(&exp, s, &pmask, &armed);
        replayer->EVENTS_MASK = pmask;              /* before the next WFI */
        if (!s) continue;                           /* a spurious wake (D10), or a new expansion alone */
        tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, s);
        tile->WRITE_COUNTER[CTR_PERIPH - 1] = armed;
        start_run();
        wait_all(EV_CTR(CTR_PERIPH) | EV_RUN_DONE); /* the squelch: [10] is out of the mask while the run streams */
        tile->EVENTS_MASK = EV_GO | EV_PERIPH(1);   /* credits returned meanwhile fall through at once */
    }
    replayer->EVENTS_MASK = 0;
    tile->EVENTS_MASK = EV_RESET_MASK;
}
```

- **One wake, every credit.** The load of the replayer's `EVENTS` takes one credit from each set, masked queue bit. The loop pushes a refill for every bit returned, so no credit is consumed without its half being sent (W3-24's leak), and puts them in one stream run of up to four streams.
- **The squelch** is `wait_all`'s mask: GO, counter 4 and [7], without [10]. Credits returned while the run streams accumulate, and wake the loop as soon as it restores [10].
- **Idle queues leave the mask:** a stream with nothing left, the write log in a pass without the store log, and the expansion queue between expansions. Their spare credits then don't keep the live bit high, so `WFI` doesn't fall through (D10).
- **Resident streams only.** Data staged from LPDDR is fetched into big SRAM on counter 1 and refilled from there (spec 4.19.13); a refill waits on counter 4 alone, so an outstanding fetch doesn't hold it up.

**Walk.**
1. `PULSE_RUN` empties the queues and sets every credit counter to 2. The masked bits (code, read log, maybe write log) are set, so the tile's [10] rises.
2. *Wake 1.* The load takes one credit each from code and read log (1 left each). The loop builds two streams of a half each, arms counter 4 for their total, starts, and sleeps in the squelch.
3. The refill's packets cross the director; each lands in its queue within *K* (D10) and decrements counter 4. [3] and [7] are set; the loop restores [10], which is still high (1 credit each), so it wakes at once.
4. *Wake 2.* The second credits go: two more halves. Both queues are now full, and both credit counters are at 0.
5. The replayer pops. Each half it consumes returns one credit to that queue: [10] rises, and the next refill follows. A queue never holds more than its two halves, so a credit is never returned to a full counter (meshsim flags that).
6. *A new expansion.* The replayer presents an instruction, posts new expansion (depth 1) and sets the expansion queue's credits to 2. The load consumes the event. The expansion's bit wasn't in the mask, so no credit went with it. The loop looks the instruction up, puts the bit in the mask, and takes the first credit on the next wake, an immediate one. That extra wake per expansion is small, about 13 a window. **[F11]**
7. *An expansion of e ≤ 128 entries:* one credit, one short refill, `left` = 0, and the bit leaves the mask with its spare credit. The next new expansion resets the credits anyway, so nothing strands (W3-24).
8. *A stream's last refill* is short and leaves the mask. The replayer's later returns on that bit accumulate unseen, at most to 2.
9. *Run done* (depth 1) arrives with the pass's end. The loop is at its top, with no refill in flight, because every refill was waited for. So every push has landed before the next `PULSE_RUN` (the software rule of 4.19.8). It clears both masks and returns.

## 27. C&C 1's side of a phase

**What it does.** C&C 1's firmware for the phase cycle: send a command, wait until every expected tile is READY, GO, and on an error the toast path. C&C 1 is a behavioural model in meshsim v1.2 (D36); this is the shape of its firmware for later.
**Relies on:** D28 (C&C 1's peripheral: `ERROR_LINES`, `READY_LINES`, `READY_EXPECT`, `PULSE_GO`, `WATCHDOG`, the UART), D35-D37 (the toast pin, one watchdog, the dump down LVDS), D38 (C&C 1's 128-entry queue; tiles wait for its replies), D6 (commands), D21 (draining reports), spec 6.3 and 6.6 (the GO settle).

<!-- recipe: cc1_command (rcp_cc1.c) -->
```c
/* A command to dst (a unit, or DST_BROADCAST): the arguments into the block its parity names, then the command word
 * to slot 0, last, all uncounted mode-0 writes from C&C 1's send port (D6). Every packet of one command to a tile is
 * of one kind (3.2). A broadcast needs every tile at the same parity; parity[0] stands for all 64 then. */
static void command(unsigned dst, uint32_t type, uint32_t arg, const uint32_t *args, unsigned n) {
    unsigned u = dst == DST_BROADCAST ? 0 : dst, par = parity[u];
    send_to(ROUTER_HDR_W(dst, MODE_WRITE, CTR_NONE), COMMAND_BASE + 4 * (par ? ARGS_ODD : ARGS_EVEN));
    for (unsigned i = 0; i < n; i++) send32(args[i]);
    tile->ROUTER_ADDR = COMMAND_BASE;               /* slot 0 last: "command landed" follows the whole command */
    send32(CMD_WORD(type, par, arg));
    if (dst == DST_BROADCAST) for (unsigned t = 0; t < 64; t++) parity[t] ^= 1;
    else parity[u] ^= 1;
}
```

<!-- recipe: cc1_phase (rcp_cc1.c) -->
```c
/* C&C 1's side of a phase (6.3; D28, D36, D38). The next command was sent right after the last GO; C&C 1 sleeps until every
 * expected tile is READY, then GO. "All tiles READY" is live: (READY & EXPECT) == EXPECT, EXPECT != 0. PULSE_GO clears
 * READY 0-63, drives GO and restarts the watchdog, in one store. Requests that aren't errors (C&C 2's and 3's "started",
 * tiles' results) arrive meanwhile, and C&C 1 answers each tile's, since the tile waits for that reply before its
 * next request (D38). An error never comes this way: it raises C&C 1's hard interrupt (recipe 27's toast path).
 * Software re-checks after every wake: the live bit may lag a store by a cycle (D10). */
static void drain_requests(void) {
    while (tile->EVENTS_PEEK & EV_REQ_PENDING) {    /* popping an empty queue would be receive underflow, an error */
        uint32_t w0 = tile->REQUEST_GET_W0, w1 = tile->REQUEST_GET_W1, w2 = tile->REQUEST_GET_W2;
        (void)tile->REQUEST_RECEIVE;                /* the last word pops */
        if (REQ_OP(w0) != REQ_OP_RESULT) continue;
        unsigned t = REQ_REQUESTOR(w0) & 63;
        results[t][0] = w2; results[t][1] = w1;
        send_to(ROUTER_HDR_W(t, MODE_WRITE, REQ_REPLY_CTR(w0)), REPLY_WORD(DST_CC1));
        send32(1);                                  /* the reply ends the tile's outstanding request (D38) */
    }
}
static void wait_all_ready(void) {
    cc1->EVENTS_MASK = CC1_EV_ALL_READY | CC1_EV_WATCHDOG;
    tile->EVENTS_MASK = EV_PERIPH(1) | EV_REQ_PENDING;
    for (;;) {
        __asm__ volatile ("wfi" ::: "memory");
        drain_requests();
        uint32_t pev = cc1->EVENTS;                 /* consumes the watchdog's event; ALL_READY is live */
        if (pev & CC1_EV_WATCHDOG) for (;;) ;       /* toast is latched already (D36): the outside world resets */
        if (pev & CC1_EV_ALL_READY) return;
    }
}
static void go(uint32_t watchdog_limit) {
    cc1->WATCHDOG = watchdog_limit;                 /* above this phase's expected length; a write restarts it */
    cc1->PULSE_GO = 1;
    delay4((GO_LATENCY + 1 + 3) / 4 + 1);           /* go_latency + 1 before anything else is sent (6.6) */
}
static void phase(uint32_t type, uint32_t arg, const uint32_t *args, unsigned n, uint32_t limit) {
    wait_all_ready();                               /* every tile has finished and its next command has landed */
    go(limit);
    command(DST_BROADCAST, type, arg, args, n);     /* the command after this one, right after GO */
}
```

<!-- recipe: cc1_toast (rcp_cc1.c) -->
```c
/* C&C 1's error path (D35, D36, D37), from its trap handler: a unit's error (its line, through the unit-error bit) or its
 * own. MIE is 0 and stays 0; toast is already latched in hardware. Best effort: start the file system's flush, drain
 * the reports into data memory as they arrive (so the tree's rows don't stall behind C&C 1's 128-entry queue, D38),
 * then send the dump out: down LVDS when an FPGA is in the loop, which waits as long as it needs before resetting, or
 * over the UART when a person resets by hand (D37). The host words are deferred, so the UART stands for both here.
 * Bounded by a loop count: C&C 1 has no timer but the watchdog. C&C 2's flush request format is a placeholder. */
#define REQ_OP_FLUSH 0x10
#define MAX_REPORTS  (64 * 23)                      /* every tile failing at once, 23 reports each (recipe 24) */
static uint32_t reports[MAX_REPORTS][4];            /* 23 KB of C&C 1's 32 KB data memory */
static void uart_hex(uint32_t v) {
    for (int i = 28; i >= 0; i -= 4) cc1->UART_TX = "0123456789abcdef"[(v >> i) & 15];   /* stalls while TX is full */
    cc1->UART_TX = ' ';
}
void cc1_toast(uint32_t mcause, uint32_t mepc) {
    request_send(0, 0, 0, REQ_TARGET(TARGET_CC2) | (REQ_OP_FLUSH << 10));   /* 1. flush the logs to LPDDR (D35) */
    uint32_t n = 0;
    for (uint32_t idle = 0; idle < 20000 && n < MAX_REPORTS; idle++) {   /* 2. drain: never pop an empty queue */
        if (!(tile->EVENTS_PEEK & EV_REQ_PENDING)) continue;
        reports[n][0] = tile->REQUEST_GET_W0;       /* requestor [6:0], then the op and sequence */
        reports[n][1] = tile->REQUEST_GET_W1;
        reports[n][2] = tile->REQUEST_GET_W2;
        reports[n][3] = tile->REQUEST_RECEIVE;      /* pops */
        n++;
        idle = 0;
    }
    uart_hex(mcause); uart_hex(mepc);               /* 3. what C&C 1 knows, then the reports (LVDS or UART) */
    for (int i = 0; i < 3; i++) uart_hex(cc1->ERROR_LINES[i]);
    for (uint32_t r = 0; r < n; r++) {
        for (int i = 0; i < 4; i++) uart_hex(reports[r][i]);
        cc1->UART_TX = '\n';
    }
    for (;;) ;                                      /* 4. park: the outside world resets the chip */
}
```

- **"All tiles READY" is live:** (`READY & EXPECT`) == `EXPECT` and `EXPECT` ≠ 0. C&C 1 needn't consume it, and can't miss it whichever order it writes `READY_EXPECT_0` and `_1` in (D28). `PULSE_GO` clears the READY bits, so the event falls; the first `WFI` after GO may still see it high for a cycle, and the loop's re-check absorbs that (D10).
- **The watchdog:** 0 at reset is off. Writing *N* > 0 starts it; a write or `PULSE_GO` restarts it from the last *N*; reaching 0 posts the event and latches the toast pin (D36). `go()` writes the next phase's limit before `PULSE_GO`, so one phase's limit can differ from the next.
- **Commands from C&C 1's send port** are uncounted mode-0 writes. The arguments go to the parity's block with `ROUTER_ADDR` advancing; then one more `ROUTER_ADDR` store for slot 0. A command of three arguments or fewer at even parity could be one 128-bit packet at slot 0 (a write covering slot 0 sets "command landed", D6), which the recipe doesn't use.
- **Booting through the send port:** C&C 1's overlays go out four stores per 128-bit packet. A stream run would send one packet a cycle from C&C 1's big SRAM, but a stream's *d* can't name the broadcast. **[F14]**

**Walk (a phase).**
1. C&C 1 sent command k + 1 right after GO k, `go_latency` + 1 cycles after its store. The tiles are running phase k.
2. Each tile finishes phase k and pulses `done`; command k + 1 has landed, so READY sets its bit in `READY_LINES`.
3. When the last expected bit sets, "all tiles READY" rises, the peripheral's masked event raises C&C 1's [10], and `WFI` returns. `drain_requests` pops anything that came meanwhile and answers each tile's request, which the tile is waiting for before it can pulse `done` (D38). Without that answer the tile would never become READY. `cc1->EVENTS` returns the live bit (and would consume a watchdog expiry).
4. `go()`: the watchdog's limit, `PULSE_GO` (READY cleared, GO driven, watchdog restarted), the settle.
5. The next command goes out, and phase k + 1 runs.

**Walk (the toast path).**
1. A tile's error raises its line. C&C 1's peripheral's unit-error bit (`ERRORS` [0], `OR(ERROR_LINES)`, live) sets C&C 1's `ERRORS` [11] and the toast pin latches (D36); C&C 1's hard interrupt traps it, with `MIE` = 0 from here on.
2. `cc1_toast` asks the file system to flush (C&C 2's request format: placeholder).
3. It drains reports to data memory as they arrive, never popping an empty queue (receive underflow is an error, though with toast already latched it changes nothing). Draining fast keeps C&C 1's 128-entry queue from filling and stalling rows (D38).
4. Then it sends the dump out and parks. With an FPGA in the loop the dump goes down LVDS and the FPGA waits as long as it needs before resetting; solo, it goes over the UART and a person resets by hand (D37). The host words are deferred, so the code shows the UART. **[F4]**
- **No timer:** C&C 1's bounded waits count loop iterations; the only timer, the watchdog, ends in toast. **[F12]**
