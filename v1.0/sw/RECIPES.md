# Programming a tile: the recipe book (`mesh.h` version 1.0)

**meshsim v1.0.** Short, correct C for the common operations of a tile program, one recipe per operation. Each recipe gives what it does, the spec sections it relies on (`spec/machine-spec.md`), the code, and what it costs.

**Every snippet is tested code.** Each one is a region of a program in `sw/tests/` (the shared helpers are in `sw/tests/rcp.h`, the self-test's generator and hash in `sw/tests/selftest.h`, the C&C side of REPORT in `src/CnC.java`), marked `/* RECIPE name */` … `/* END */`. Each code block below is preceded by an HTML comment naming its region and file (visible in the Markdown source). `RecipeTest` runs every program on all 64 tiles of meshsim and checks the results bit-exactly, with no invariant violations (or exactly the provoked ones). It also checks that every code block here equals its region, so the book can't drift from the hardware. Run it with `java -cp build RecipeTest` (about 10 seconds); `run_tests.sh` runs it, and once more with a shuffled child order.

**Conventions.**
- Names are `mesh.h`'s. Tile programs follow `sw/CONVENTIONS.md`; this book shows how, and section 15 collects the rules that bite.
- Cycles are measured on the default machine, from C&C's GO to all 64 READY unless stated [meas: RecipeTest]. Tile 63 usually has the most data, so it sets the time.
- The recipes favour clarity over speed. Where the sort program does the same job faster, the recipe says how.
- Buffers in the tests sit in different big-SRAM areas (4.1: an area does one access per cycle): area *a* starts at chunk *a* · 0x20000.

**Contents.** 0 helpers · 1 boot and the command loop · 2 a stream run (local copy) · 3 block copy into controller memory · 4 histogram, prefix sum and bucket passes · 5 augmented transfers · 6 gather · 7 the one-word exchange and words to one tile · 8 a vector exchange · 9 an all-to-all · 10 a mesh reduction · 11 the read port · 12 status and conditions · 13 the synchronised start by identical code · 14 cycles · 15 the rules that bite · 16 gaps found · 17 write a block into big SRAM · 18 fill with pseudo-random data · 19 hash a block · 20 REPORT · 21 the self-test.

---

## 0. Two helpers

**What they do.** `wait_tables()` waits until the table memories are free to touch; `local_run()` runs one local engine run into big SRAM and waits for it.
**Relies on:** 4.10 (controller access to the tables), 4.2 (runs and the start), 4.12 (counters and the local wake).

`wait_tables()` is in `mesh.h`:

```
static inline void wait_tables(void) {
    while (status() & (TILE_ST_RD_RUNNING(1) | TILE_ST_TS_BUSY(1))) ;
}
```

<!-- recipe: local_run (rcp.h) -->
```c
/* A local run into big SRAM (spec 4.2, 4.12): DMA_CONFIG, the counter, then the start store; WFI wakes
 * when the n packets have been written (DMA_SRAM_REMAINING = 0 and mem_idle). n > 0: a count of 0 makes no edge. */
static inline void local_run(uint32_t config, uint32_t n) {
    DMA_CONFIG = config;
    DMA_SRAM_REMAINING = n;
    DMA_PULSE = DMA_PULSE_START(1);
    wfi();
}
```

- **When to call `wait_tables()`:** before writing `STREAM`, `GROUP` or `PTR`, and before reading or writing `HIST`, whenever an engine might still be using them. A tile's arrivals can finish before its own run does, so the counters alone don't say the tables are free.
- **`local_run()` needs n > 0.** A count of 0 makes no edge, and the WFI would wait for the next GO.

## 1. Boot and the command loop

**What it does.** C&C boots the image, the loader jumps to `_start`, `crt0` zeroes `.bss` and calls `main`. Then the tile loops: report done, wait for GO, read the command word and its argument block, act.
**Relies on:** 6.2 (boot), 6.3 (`done`, READY, GO and `WFI`); CONVENTIONS 3 and 4.

<!-- recipe: cmdloop (rcp_cmdloop.c) -->
```c
int main(void) {
    unsigned me = tile_index();
    for (;;) {
        uint32_t w = wait_for_command();          /* pulse done, then WFI; GO wakes it; returns the command word */
        volatile uint32_t *arg = CMD_ARGS(w);     /* this command's argument block (slots 1-31 or 32-63, by parity) */
        switch (CMD_TYPE(w)) {
        case CMD_SUM: {
            uint32_t s = me;
            for (uint32_t i = 0; i < CMD_ARG(w); i++) s += arg[i];
            res[ncmd++ & 7] = s;
            break;
        }
        case CMD_EXIT:
            return 0;
        }
    }
}
```

- **`wait_for_command()`** pulses done and executes `WFI` as two back-to-back instructions, then reads slot 0 once. Every wake from a command goes through it, barriers included.
- **Arguments:** C&C writes them first, then the command word to slot 0. Even commands use slots 1–31 and odd ones 32–63; the parity bit in the word says which, and `CMD_ARGS(w)` picks the block. A broadcast needs every tile at the same parity (C&C's `command()` checks).
- **Exit:** returning from `main` executes `ebreak`, a simulator-only exit. A real program loops for ever.

## 2. A stream run: a local copy

**What it does.** Copies n 64-bit records from one big-SRAM buffer to another, through the address generator and the tile's own loopback.
**Relies on:** 4.2 (stream runs, `STREAM`, relative destinations), 4.9 (mode 2 lands at `PTR[src]`), 4.12 (the local wake), 4.1 (no buffer both read and written in one run).

<!-- recipe: copy (rcp_copy.c) -->
```c
/* Copy n 64-bit records from src to dst in this tile's big SRAM: one stream, mode 2 to the tile itself (d = 0), which
 * lands at PTR[me]. src and dst must not overlap (one engine run never reads and writes one buffer, spec 4.1). */
static void local_copy(uint32_t src, uint32_t dst, uint32_t n) {
    wait_tables();
    PTR[tile_index()] = dst;
    set_stream(0, STREAM_LO(src, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);
}
```

- **The pattern every local pass follows:** `PTR` for where it lands, one `STREAM` entry for what is read, `DMA_CONFIG` with the run, the counter, the start store, `WFI`.
- **A stream run of several streams** sends each stream's whole run before the next begins; `DMA_CFG_RUN(RUN_STREAMS, first, count)`.
- **Read size:** `SIZE_32` sends one chunk per packet in the payload's low half, `SIZE_64` one record, `SIZE_128` one row as two packets.

## 3. A block copy into controller memory

**What it does.** Copies words from big SRAM into data memory (parameters, tables) or instruction memory (an overlay), then uses them.
**Relies on:** 6.5 (block copy), 4.11 (the packed controller-memory map, `CM_WORD`), 4.12 (`DMA_CM_REMAINING`); CONVENTIONS 8.

<!-- recipe: cmem (rcp_cmem.c) -->
```c
/* Copy n 64-bit packets (2n words) from big SRAM at src to controller memory at the byte address dst, instruction or
 * data memory: a mode-5 stream to the tile itself (spec 6.5). The code doing the copy must lie outside dst's range. */
static void cmem_copy(uint32_t src, uint32_t dst, uint32_t n) {
    wait_tables();
    set_stream(0, STREAM_LO(src, CM_WORD(dst)), STREAM_HI(n, 0, MODE_CMEM, SIZE_64, 0));
    DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
    DMA_CM_REMAINING = n;                              /* mode 5 counts here: 1 per 64-bit packet */
    DMA_PULSE = DMA_PULSE_START(1);
    wfi();                                         /* wakes when the last word is in memory */
}
```

<!-- recipe: cmem_use (rcp_cmem.c) -->
```c
cmem_copy(SRC_PAR, (uint32_t)params, NPAR / 2);    /* parameters into data memory */
cmem_copy(SRC_OVL, OVL, NOVL / 2);                  /* an overlay into instruction memory */
uint32_t (*overlay)(uint32_t) = (uint32_t (*)(uint32_t))OVL;
res = overlay(params[0]);
```

- **Each mode-5 packet writes two consecutive words** at any word address and counts 1 in `DMA_CM_REMAINING`.
- **The code doing the copy must lie outside the range it overwrites.** The sort keeps its overlays in one region (`_ov_base`) and its kernel elsewhere.
- **The sort is faster here only by size:** its overlays are about 1K words, ≈ 527 cycles each (CONVENTIONS 8).

## 4. The histogram digit, the prefix sum and bucket passes

**What it does.** Counts a key digit while records are written (count-on-arrival), turns the counts into bucket pointers, and runs mode-3 passes that bucket records by a digit while counting the next. Four passes sort by the 32-bit key.
**Relies on:** 4.9 (mode 3, count-on-arrival), 4.10 (`HIST`, `PTR`), 4.15 (digits: byte *d*, 1 the least significant), 8.5 (`DMA_CFG_BUCKETDIG`, `DMA_CFG_HISTDIG`).

<!-- recipe: histogram (rcp_bucket.c) -->
```c
/* Count-on-arrival (spec 4.9): a run with DMA_CFG_HISTDIG(d) adds 1 to HIST[digit d of the key] for every record it writes.
 * HIST is not cleared by hardware: write zeros first. Here, a local copy that counts digit 1 as it goes. */
static void copy_counting(uint32_t src, uint32_t dst, uint32_t n) {
    wait_tables();
    for (int t = 0; t < 256; t++) HIST[t] = 0;
    PTR[tile_index()] = dst;
    set_stream(0, STREAM_LO(src, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1) | DMA_CFG_HISTDIG(1), n);
}
```

<!-- recipe: bucket (rcp_bucket.c) -->
```c
/* One stable radix pass on digit d (1-4; 1 is the key's least significant byte, spec 4.15): n records from src into
 * dst, bucketed by mode 3 (each lands at PTR[its digit]). HIST holds digit d's counts on entry; the pass counts digit
 * next (0: none) for the pass after. */
static void bucket_pass(uint32_t src, uint32_t dst, uint32_t n, unsigned d, unsigned next) {
    wait_tables();
    uint32_t at = dst;
    for (int t = 0; t < 256; t++) {                /* the prefix sum: PTR[t] = dst + 2 * (records with digit < t) */
        uint32_t c = HIST[t];
        PTR[t] = at;
        at += c << 1;
        HIST[t] = 0;                               /* ready for this pass's counts of digit next */
    }
    set_stream(0, STREAM_LO(src, 0), STREAM_HI(n, 0, MODE_BUCKET, SIZE_64, 0));
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
wait_tables();
PTR[64] = stage_base();                    /* mode 3 overwrote PTR[64], C&C's staging pointer (spec 4.9) */
```

- **Stable:** records with the same digit land in arrival order, which is read order, so four passes on bytes 1–4 sort by the whole key.
- **Mode 3 uses all 256 `PTR` entries, `PTR[64]` among them,** which is C&C's staging pointer: restore it before the phase ends (section 15, rule 5).
- **The sort does this faster:** its prefix sum is unrolled four ways (`local.c`), and its first pass counts digit 1 while the input arrives, so no counting copy is needed. Here the prefix sums dominate: about 2,100 cycles each (≈ 8 per entry), against about 220 for the pass itself at 200 records, and the counting copy's `HIST` clear about 1,050 [meas: RecipeTest bucket, tile 0].

## 5. Augmented transfers (mode 4)

**What it does.** Sends records in mode 4; the receiver replaces each payload's high word with the record's position in its buffer (the ordinal), so the record can later fetch its value by a gather.
**Relies on:** 4.9 (augmenting, `DMA_RECORD_BASE_ADDRESS`), 4.2 (read size 32), 3.1 (modes).

**The local form, the sort's first pass:** raw 32-bit keys in, (key, *j*) records out.

<!-- recipe: aug_local (rcp_aug.c) -->
```c
/* The sort's first pass (spec 4.9): n raw 32-bit keys at keys, one per packet (read size 32, the high half 0), sent to
 * the tile itself in mode 4. Each lands at dst as the record (key, j), where j = 0, 1, 2, ... is its position in dst. */
static void augment_local(uint32_t keys, uint32_t dst, uint32_t n) {
    wait_tables();
    PTR[tile_index()] = dst;
    DMA_RECORD_BASE_ADDRESS = dst;                    /* ordinal = (landing chunk - base) / 2 */
    set_stream(0, STREAM_LO(keys, 0), STREAM_HI(n, 0, MODE_AUG, SIZE_32, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);
}
```

**The remote form:** two senders into one buffer; each record's ordinal is its position in the buffer, not its arrival order.

<!-- recipe: aug_remote (rcp_aug.c) -->
```c
/* Configure (one command): receive the left neighbour's n_left records and then the right neighbour's into buf, in
 * mode 4: every record's high word becomes its position in buf, whichever sender it came from. Send this tile's n
 * records at src to both neighbours (two streams, d = 1 and d = 63); the start command loads the run. */
static void aug_configure(uint32_t src, uint32_t n, uint32_t buf, uint32_t n_left, uint32_t n_right) {
    unsigned me = tile_index();
    wait_tables();
    PTR[(me - 1) & 63] = buf;
    PTR[(me + 1) & 63] = buf + 2 * n_left;
    DMA_RECORD_BASE_ADDRESS = buf;
    set_stream(0, STREAM_LO(src, 0), STREAM_HI(n, 1, MODE_AUG, SIZE_64, 0));
    set_stream(1, STREAM_LO(src, 0), STREAM_HI(n, 63, MODE_AUG, SIZE_64, 0));
    DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 2);
    expect = n_left + n_right;
}
/* Start (the next command, straight after waking: the same code on every tile up to the start store, spec 6.3). */
static void aug_start(void) {
    DMA_SRAM_REMAINING = expect;                       /* the counter first, */
    start_run();                                   /* then the start */
    wfi();                                         /* both neighbours' records are in */
}
```

- **The ordinal is (landing chunk − `DMA_RECORD_BASE_ADDRESS`) / 2:** 0, 1, 2, … across the buffer, whichever sender a record came from.
- **Read size 32** sends a chunk in the payload's low half with the high half 0, so a buffer of raw keys becomes (key, *j*) records in one pass at one key per cycle.
- **Modes 2 and 4 may mix in one phase** (both land at `PTR[src]`); mode 3 never shares a phase with them.

## 6. A gather

**What it does.** Reorders values by an index: for each (key, ordinal) record of the index, it fetches value[ordinal] and writes the values in index order.
**Relies on:** 4.4 (the GATHER engine), 8.5 (`DMA_CFG_KM1`), 4.9 (the values land at `PTR[me]`).

<!-- recipe: gather (rcp_gather.c) -->
```c
/* For each of the n (key, ordinal) records of the index at idx, fetch value[ordinal], k records, from vals (value j is
 * at vals + 2k j), and write the values in index order from out. One mode-1 request per index record goes to the tile
 * itself; the GATHER engine sends the k value packets as mode 2 to PTR[me]. Only the values count: n * k. */
static void gather(uint32_t idx, uint32_t vals, uint32_t out, uint32_t n, unsigned k) {
    wait_tables();
    DMA_RECORD_BASE_ADDRESS = vals;
    PTR[tile_index()] = out;
    set_stream(0, STREAM_LO(idx, 0), STREAM_HI(n, 0, MODE_GATHER, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1) | DMA_CFG_KM1(k - 1), n * k);
}
```

- **Only the value packets count** (n · k in `DMA_SRAM_REMAINING`); the mode-1 requests do not.
- **Three areas:** index, values and output in different areas keep each to one access per cycle.
- **Rate:** about k + 1 cycles per value through the one injection point and eject (4.4).

## 7. The one-word exchange, and several words to one tile

**What it does.** Every tile sends one word to every tile (counts, cuts, probe keys), as mode-7 words from the send port into data memory. And: n words to one tile with one `ROUTER_SET_HDR`.
**Relies on:** 4.11 (the send port, auto-increment), 4.12 (`DMA_REPLY_REMAINING`), `schedule-4.md` (the 16-group order).

<!-- recipe: xchg (rcp.h) -->
```c
/* The one-word exchange (spec 4.11): out[d] goes to tile d and lands at in[me] there, so afterwards in[s] holds tile s's
 * word to this tile. Mode 7 from the send port, one destination per step in the 16-group schedule's order, so every
 * step is a permutation of the tiles. Call it first in a command, by identical code on every tile: DMA_REPLY_REMAINING must
 * be stored before any word can arrive. */
static void xchg(const uint32_t *out, volatile uint32_t *in) {
    unsigned me = tile_index();
    DMA_REPLY_REMAINING = 64;                          /* including this tile's word to itself */
    uint32_t at = CM_WORD((uint32_t)&in[me]);
    for (int g = 0; g < 16; g++)
        for (int i = 0; i < 4; i++) {
            unsigned d = (me + SCHED[g][i]) & 63;
            ROUTER_SET_HDR = ROUTER_HDR(d, MODE_REPLY, at);
            ROUTER_SEND_LO = out[d];
        }
    wfi();                                         /* every word to this tile is in */
}
```

<!-- recipe: send_words (rcp_xchg.c) -->
```c
/* n words to one tile: one ROUTER_SET_HDR, then one send per word. The address advances by 1 per send in mode 7 (by 2 per
 * 64-bit packet in mode 5), so the words land at at[0..n-1]. The receiver counts n in DMA_REPLY_REMAINING. */
static void send_words(unsigned dst, volatile uint32_t *at, const uint32_t *w, unsigned n) {
    ROUTER_SET_HDR = ROUTER_HDR(dst, MODE_REPLY, CM_WORD((uint32_t)at));
    for (unsigned i = 0; i < n; i++) send32(w[i]);
}
```

- **Schedule order** makes each step a permutation of the tiles, so no link carries more than its share.
- **"First in a command":** a fast sender's word can arrive a few cycles after GO, so the counter store must come before any other work, by the same code on every tile. 
- **The sort's `xchg` is the same loop** with the header precomputed; most of the ≈ 15 cycles per word here are the loop and `ROUTER_SET_HDR`.

## 8. A vector exchange with the neighbours

**What it does.** Sends a short vector (an even number of words) to the four neighbours as mode-5 packets into their data memory.
**Relies on:** 4.8 (mode 5 at the director), 4.11 (port B, auto-increment +2), 4.12 (`DMA_CM_REMAINING`).

<!-- recipe: xvec (rcp_xvec.c) -->
```c
/* Send v[0..W-1] (W even) to the four neighbours, as mode-5 packets into their data memory: the vector lands in the
 * neighbour's nb[k], k naming this tile as the neighbour sees it: 0 from its left (me - 1), 1 from its right, 2 from
 * above (me - 8), 3 from below. The address advances by 2 per 64-bit packet. Call it first in a command. */
static void xvec(const uint32_t *vec) {
    static const int8_t to[4] = { 1, -1, 8, -8 };  /* to me + 1, this tile is its left neighbour: nb[0] */
    unsigned me = tile_index();
    DMA_CM_REMAINING = 4 * (W / 2);                    /* four vectors of W / 2 packets will arrive */
    for (int k = 0; k < 4; k++) {
        ROUTER_SET_HDR = ROUTER_HDR((me + to[k]) & 63, MODE_CMEM, CM_WORD((uint32_t)&nb[k][0]));
        for (int i = 0; i < W; i += 2) send64(vec[i + 1], vec[i]);
    }
    wfi();
}
```

- **Each 64-bit packet carries two words** (`send64(hi, lo)`); `ROUTER_SET_HI` is cleared by every send.
- The receive buffers are at the same address on every tile, so the sender names them by its own symbol.

## 9. An all-to-all on the 16-group schedule

**What it does.** Every tile sends each destination its run of records, in a group run that keeps every tile in lockstep. Three commands: the counts, the configuration (R by a reduction), the start.
**Relies on:** 4.2 (group runs, `GROUP`, the timetable, read-ahead), 6.3 (configure and start), 4.9 (`PTR[src]` from the counts), `schedule-4.md`.

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
 * order. The tables stay as they are until the run ends; the start command loads the run. */
static void a2a_configure(void) {
    unsigned me = tile_index();
    uint32_t first[64], f = 0;
    for (int d = 0; d < 64; d++) { first[d] = f; f += cnt[d]; }
    wait_tables();
    for (int g = 0; g < 16; g++) {
        for (int i = 0; i < 4; i++) {
            unsigned off = SCHED[g][i], d = (me + off) & 63;
            set_stream(4 * g + i, STREAM_LO(SRC + 2 * first[d], 0), STREAM_HI(cnt[d], off, MODE_SRC, SIZE_64, 0));
        }
        set_group(g, GROUP_LO_R(R[g]), GROUP_HI(4 * g, 4, 0));     /* gap 0 (spec 4.2) */
    }
    uint32_t at = INBOX;
    expected = 0;
    for (int s = 0; s < 64; s++) { PTR[s] = at; at += 2 * inc[s]; expected += inc[s]; }
    DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, 0, 16);
}
/* Start (the next command): from waking to the start store, the same code on every tile (spec 6.3). */
static void a2a_start(void) {
    DMA_SRAM_REMAINING = expected;                     /* the counter first, */
    start_run();                                   /* then the start; */
    if (expected) wfi();                           /* a count of 0 makes no edge */
}
```

- **R[g] is the global maximum** count in group g: every tile runs every group for the same R rotations, which is what keeps the timetable identical everywhere. Hence the reduction (section 10).
- **The receiver decides where each source lands:** `PTR[s]` from the counts it received, source after source.
- **Counters are never set in the configure command** (done with a counter expecting arrivals breaks invariant 4). They are stored first in the start command, then the start store.
- **Measured:** the start command takes 463 cycles against a timetable of Σ 4·R_g = 416 [meas: RecipeTest]; the rest is the pipeline, the links and the WFI.
- **The sort does more:** count-on-arrival of the next sort's digit (`DMA_CFG_HISTDIG(1)`), mode 4 for its keys, gap 0, and the two-hop paths for skewed data.

## 10. A mesh reduction of a small vector

**What it does.** The maximum of a vector over all 64 tiles, every tile getting the result: a fixed tree along the rows toward column 0 and up column 0 to tile 0, then a broadcast.
**Relies on:** 4.11 (the send port), 4.12 (`DMA_CM_REMAINING`, the local wake), 2.1 (broadcast, 126).

<!-- recipe: reduce (rcp.h) -->
```c
/* Max-reduce a vector of RW words (RW even) over all 64 tiles; every tile gets the result. A fixed tree: along each row
 * toward column 0 (me + 1 is a child), up column 0 toward tile 0 (me + 8 is a child), then tile 0 broadcasts. The
 * vectors travel as mode-5 packets into the parent's data memory. reduce_init() once at start-up; reduce_max() first
 * in a command (a leaf sends at once, so a parent's DMA_CM_REMAINING must already be stored). */
#define RW 16
static uint32_t red_in[2][RW];                 /* from me + 1, from me + 8 */
static uint32_t red_out[RW];                   /* tile 0's broadcast */
static uint32_t red_kids, red_parent;
static void reduce_init(void) {
    unsigned me = tile_index(), col = me & 7, row = me >> 3;
    red_kids = (col < 7) + (col == 0 && row < 7);
    red_parent = col ? me - 1 : row ? me - 8 : DST_BROADCAST;
}
static void reduce_max(uint32_t *v, uint32_t *out) {
    unsigned me = tile_index();
    if (red_kids) {
        DMA_CM_REMAINING = red_kids * (RW / 2);        /* RW / 2 packets per child */
        wfi();                                     /* the children's vectors are in */
        for (int i = 0; i < RW; i++) {
            if ((me & 7) < 7 && red_in[0][i] > v[i]) v[i] = red_in[0][i];
            if ((me & 7) == 0 && me < 56 && red_in[1][i] > v[i]) v[i] = red_in[1][i];
        }
    }
    uint32_t *at = (me & 7) ? red_in[0] : me ? red_in[1] : red_out;
    if (me == 0) DMA_CM_REMAINING = RW / 2;            /* tile 0 receives its own broadcast */
    ROUTER_SET_HDR = ROUTER_HDR(red_parent, MODE_CMEM, CM_WORD((uint32_t)at));
    for (int i = 0; i < RW; i += 2) send64(v[i + 1], v[i]);
    if (me != 0) DMA_CM_REMAINING = RW / 2;            /* the broadcast can't come before this: it needs this vector */
    wfi();                                         /* the broadcast is in */
    for (int i = 0; i < RW; i++) out[i] = red_out[i];
}
```

- **Tile 0 receives its own broadcast,** so it stores its counter before sending. Every other tile stores it after sending: the broadcast needs its vector first.
- **A tile with no children doesn't WFI for them** (a count of 0 makes no edge).
- **The sort's `tree_reduce` is about half as deep:** rows split at column 4 (columns 1–4 pass left, 5–7 pass right, over the ring link) and column 0 the same way, so up to four children per tile. This recipe's chains are 14 hops deep: 4,412 cycles for 16 words [meas: RecipeTest].

## 11. Reading big SRAM through the read port

**What it does.** Reads chunks of big SRAM into data memory without stalling: one read, and the search loop that advances many binary searches together.
**Relies on:** 4.3 (the read port, results through the loopback as mode 7), 4.12 (`DMA_REPLY_REMAINING`); CONVENTIONS 9.

<!-- recipe: read_one (rcp_rdport.c) -->
```c
/* One 32-bit chunk of big SRAM into data memory: queue the read, and its result comes back as a mode-7 word through the
 * loopback, counting in DMA_REPLY_REMAINING (spec 4.3). Not while packets are arriving for this tile (CONVENTIONS 9). */
static uint32_t sram_read32(uint32_t chunk) {
    static volatile uint32_t result;
    DMA_REPLY_REMAINING = 1;
    SRAM_RD_DEST_ADDR = CM_WORD((uint32_t)&result);
    SRAM_RD_ADDR_QUEUE = chunk;
    wfi();                                         /* the result has landed (and the latch is consumed) */
    return result;
}
```

<!-- recipe: search (rcp_rdport.c) -->
```c
/* Lower bounds (the first i with keys[i] >= q) of nq <= 32 queries in n sorted 32-bit keys at chunk keys: every active
 * search issues one read per step, then one WFI for all the results (spec 4.3's loop; at most 63 reads in a step).
 * A step of its own: no packets arrive for this tile meanwhile, and every result has landed before the next exchange.
 * The sort's split does this faster: 8 searches in registers with a shared step, about 12 cycles a step. */
static void search(uint32_t keys, uint32_t n, const uint32_t *q, volatile uint32_t *lo, unsigned nq) {
    static volatile uint32_t res[32];
    uint32_t l[32], h[32];
    unsigned act[32], na = 0;
    for (unsigned i = 0; i < nq; i++) { l[i] = 0; h[i] = n; if (n) act[na++] = i; }
    while (SRAM_RD_ADDR_QUEUE < READ_PORT_DEPTH) ;  /* once: never waits, since earlier phases ended with a WFI */
    while (na) {
        DMA_REPLY_REMAINING = na;
        SRAM_RD_DEST_ADDR = CM_WORD((uint32_t)&res[0]);       /* result a lands in res[a] */
        for (unsigned a = 0; a < na; a++) { unsigned i = act[a]; SRAM_RD_ADDR_QUEUE = keys + ((l[i] + h[i]) >> 1); }
        wfi();
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

- **Results land in issue order** from `SRAM_RD_DEST_ADDR`, which advances by 1 per queued read.
- **The rule on incoming traffic:** read-port results share the loopback and eject with arrivals, and the network has no flow control. Read-port reads run in steps of their own: every expected arrival has landed before a search step, and every result has landed (its counter done, after a `WFI`) before the next exchange. meshsim flags an outside packet accepted while results are outstanding.
- **The sort's split is faster:** worker 4's loop keeps 8 searches in registers with a shared step, about 12 cycles per step; this loop costs about 390 cycles per step of 8 searches [meas: RecipeTest: 3,531 cycles for 9 steps over 300–363 keys].

## 12. Status and conditions

**What it does.** Reads `TILE_STATUS`'s fields, and `TILE_CONDITIONS`, the errors: what a phase caused is what it finds there at its end that wasn't there at its start.
**Relies on:** 8.3 (`TILE_STATUS`), 8.3a and 4.13 (`TILE_CONDITIONS`), 6.6 (the error line).

<!-- recipe: status (rcp_status.c) -->
```c
/* TILE_STATUS (spec 8.3) is what a program steers by: the tile index, the big-SRAM size and the live bits (engines,
 * idle bits, done). TILE_CONDITIONS (8.3a, 4.13) holds the errors: 0 in a correct program, read-only, cleared only by
 * entering HALT, and any bit raises the tile's error line to C&C. A phase that wants to know what it caused reads the
 * conditions at its start and compares at its end. */
static uint32_t at_start;
static inline void phase_start(void) { at_start = conditions(); }
static inline uint32_t phase_conditions(void) { return conditions() & ~at_start; }   /* COND_* bits set in this phase */
```

**`TILE_STATUS`:** live bits [9:0], tile index [26:21], big-SRAM size in 512 KB areas − 1 [31:29]; [20:10] and [28:27] are spare and read 0. `tile_index()` and `sram_chunks()` read them. Status computed at *t* is readable at *t* + 1.

**`TILE_CONDITIONS`** (`conditions()`, `COND_*`): a condition means the software did something wrong, or the hardware is faulty. A correct program on correct hardware reads 0 here for ever. They are read-only: nothing in RUN clears them (`DMA_PULSE` [3:2] are reserved), and entering HALT clears them all. Any bit raises the tile's error line, ORed into one sticky error bit at C&C, which wakes C&C (section 20).

| bit | condition | set when | what the program did wrong |
|---|---|---|---|
| 0 `TILE_COND_DRIFT` | drift | in a group run, the SRAM read queue refused an address-generator request | too much traffic on one area: placement. Zero-length groups and group starts are timetable, not drift |
| 1 `TILE_COND_ROUTER_OVF` | router overflow | a link, eject or C&C FIFO overflowed | too much traffic to one tile: fan-in, pacing |
| 2 `TILE_COND_WE_IN_OVF` | write-engine input overflow | the write engine fell behind its arrivals | writes split across rows, or an area oversubscribed |
| 3 `TILE_COND_RDPORT_OVF` | read-port overflow | a read queued with no free entry | more than `READ_PORT_DEPTH` reads outstanding |
| 4 `TILE_COND_UNEXPECTED` | unexpected arrival | a packet arrived with its counter at 0 | a counter stored too late, or too small |
| 5 `TILE_COND_TABLE_OVF` | table request overflow | the `PTR` or `HIST` request FIFO overflowed | nothing a program can do: a hardware fault (spec edit 4) |
| 6 `TILE_COND_BUS_ERROR`, 7 `TILE_COND_ILLEGAL` | bus error, illegal instruction | the core halts | a bad address or instruction; HALT → RUN restarts it |
| 8 `TILE_COND_GATHER_OVF` | GATHER queue overflow | the GATHER request queue overflowed | in meshsim, a hardware fault (spec edit 4; the in-flight bound is open) |
| 9 `TILE_COND_BAD_MODE` | bad mode | a packet with mode 0 or 8–15 arrived | `ROUTER_SET_HDR` with a wrong mode |
| 10 `TILE_COND_DONE_EARLY` | done early | `done` pulsed with a down-counter non-zero | the phase declared itself finished with arrivals outstanding (rule 1, section 15) |

- **A crashed tile** shows at C&C as a missing READY, and its error line; it can't answer REPORT.
- **The test, tile by tile, is `conditions() == 0`.** No mask is needed.
- **Measured** [CondTest conditions]: each condition, provoked from C, is set on exactly the tiles that provoked it, the program reads it, and C&C's error bit sets `error_latency` cycles later.

## 13. A synchronised start by identical code

**What it does.** Starts a run on the same cycle on every tile (within each tile's 0/+1 skew) after set-up of any length. The set-up is one command; the start is the next. GO wakes every tile on the same cycle, and from that `WFI` to the start store every tile runs the same instructions, so the start stores land together.
**Relies on:** 6.3 (the synchronised start: identical code from GO), 8.2 (the start and its lead-in), 4.12 (the counters).

<!-- recipe: sync (rcp_sync.c) -->
```c
uint32_t w = wait_for_command();          /* every tile leaves WFI on GO's cycle (+ its 0/+1 skew) */
if (CMD_TYPE(w) == 2) {                    /* the start: identical code from WFI on every tile */
    DMA_SRAM_REMAINING = m;                /* the counter first, */
    start_run();                           /* then the start store */
    if (m) wfi();                          /* the arrivals (a count of 0 makes no edge) */
    continue;
}
if (CMD_TYPE(w) != 1) return 0;
/* the set-up: any length, per tile; it ends in wait_for_command() */
spin(2 * me);
wait_tables();
set_stream(1, STREAM_LO(PARAMS, CM_WORD((uint32_t)&par)), STREAM_HI(1, 0, MODE_CMEM, SIZE_64, 0));
DMA_CM_REMAINING = 1;
DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 1, 1);
start_run();
wfi();                                     /* the parameters are in */
wait_tables();
PTR[(me - 1) & 63] = DST;
set_stream(0, STREAM_LO(SRC, 0), STREAM_HI(par.n, 1, MODE_SRC, SIZE_64, 0));
DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
m = par.m;
```

**The rules of the start command** (spec 6.3, rule 3 in section 15):
- **No data-dependent path from the `WFI` to the start store.** The counter store and the start store are the only work; everything else belongs in the set-up command.
- **Store the counter first,** then the start: a fast sender's packets can arrive a few cycles after its start.
- **The lead-in is the same everywhere.** The first request goes out 4 `ts_engine_latency` + 3 cycles after the start store for a group run (4 L + 2 after the cycle the start takes effect), 2 L + 2 for a stream run (2 L + 1); it depends only on the run kind, the tables and L, so the runs stay in lockstep.

**Measured** [RecipeTest sync]: all 64 runs start on the same cycle after each tile's skew, though the set-up command's lengths differ by tile. From C&C's GO store: the start store 5–6 cycles later (`go_latency` 1, the 0/+1 skew, the mailbox load, the type test and the counter store), the first request 4 cycles after that (a stream run, 2*L* + 2), so 9–10 cycles in all; a group run's first request comes 7 cycles after its start store (4*L* + 3) [RecipeTest a2a: 17–18 from GO with that recipe's longer start path].

## 14. Cycles

Measured on the default machine [meas: RecipeTest]: cycles from C&C's GO to all 64 READY unless stated.

| recipe | what | cycles |
|---|---|---:|
| cmdloop | a 3-argument command: GO → all 64 READY | 207 |
| cmdloop | a 31-argument command: GO → all 64 READY | 243 |
| copy | 100 records (tile 0): start store → first instruction after WFI | 252 |
| cmem | 256 words into instruction memory: start store → wake | 241 |
| bucket | 200-263 records: count copy + 4 bucket passes + PTR[64]: GO → all READY | 10,894 |
| aug | local form, 64 keys (size 32, mode 4): GO → all READY | 176 |
| aug | remote form, start command (two neighbours, 20-35 records each): GO → all READY | 105 |
| gather | k = 1, 50-113 values per tile: GO → all READY | 324 |
| gather | k = 2, 50-113 values per tile: GO → all READY | 437 |
| xchg | one-word exchange, 64 words per tile: GO → all READY | 987 |
| xchg | four words to one tile: GO → all READY | 41 |
| xvec | 6 words to each of 4 neighbours: GO → all READY | 149 |
| a2a | counts exchange + group maxima: GO → all READY | 1,926 |
| a2a | reduction of R + configure: GO → all READY | 7,973 |
| a2a | start: GO → all READY (timetable sum R_g * 4 = 416) | 468 |
| a2a | start (a group run): GO store → start store, every tile | 10-11 |
| a2a | start (a group run): start store → first request (the lead-in) | 7 |
| a2a | start (a group run): GO store → first request, every tile | 17-18 |
| reduce | 16 words, max over 64 tiles, broadcast back: GO → all READY | 4,412 |
| rdport | one read: GO → all READY | 49 |
| rdport | 8 searches over 300-363 keys: GO → all READY | 3,531 |
| rules | 3 wrong: spread of the start cycles across tiles (right: 0) | 56 |
| sync | set-up command (per-tile lengths): GO → all READY | 1,054 |
| sync | start command: GO → the start taking effect (tile 0, after its skew) | 6 |
| sync | start command: GO → all READY | 102 |
| sync | start (a stream run): GO store → start store, every tile | 5-6 |
| sync | start (a stream run): start store → first request (the lead-in) | 4 |
| sync | start (a stream run): GO store → first request, every tile | 9-10 |
| write_block | 64 records: GO → all READY | 500 |
| write_block | 256 records: GO → all READY | 1,843 |
| fill | 256 records, C&C's seed: GO → all READY | 6,044 |
| fill | 1024 records: GO → all READY | 23,707 |
| hash | 256 records (512 words): GO → all READY | 9,258 |
| hash | 1024 records: GO → all READY | 36,857 |
| selftest | fill (1024 records, index seed): GO → all READY | 23,746 |
| selftest | configure: GO → all READY | 1,619 |
| selftest | start (timetable 16 x 4 x 16 = 1024): GO → all READY | 1,118 |
| selftest | hash of the received 1024 records: GO → all READY | 36,857 |
| selftest | signatures to C&C: GO → all READY | 333 |
| selftest | signatures to C&C: first to last arrival | 258 |
| report | error bit set → REPORT sent (C&C waits for all READY) | 70 |
| report | REPORT sent → GO (the settle, 2 x 37) | 76 |
| report | GO → last reply at C&C | 292 |
| report | first to last reply | 258 |

## 15. The rules that bite

Each rule is a command of `rcp_rules.c`, run in its right form (the code below) and its wrong form; `RecipeTest` checks that the right form works and the wrong form fails as described.

**Rule 1: a phase that sets counters ends with a `WFI` before done** (CONVENTIONS 5): the `WFI` proves every arrival is in.

<!-- recipe: rule_wfi (rcp_rules.c) -->
```c
/* A phase that sets a counter ends with a WFI before done (CONVENTIONS 5): the WFI proves every arrival is in. Done
 * with an arrival outstanding is a program error: the done-early condition (spec 4.13). The done gate keeps the late
 * arrival from waking wait_for_command(), but the phase has declared itself finished too soon. */
static void self_reply(void) {
    DMA_REPLY_REMAINING = 1;
    ROUTER_SET_HDR = ROUTER_HDR(tile_index(), MODE_REPLY, CM_WORD((uint32_t)&rbox));
    send32(7);
    wfi();                                         /* the local wake: the reply is in */
}
```

*Wrong:* without the `WFI`, `done` comes with the reply still on its way: **done early** on every tile, and invariant 4. The done gate (spec 4.12) keeps the reply's late edge from waking `wait_for_command()`, so every tile still waits for GO; but the phase was declared finished too soon [meas: RecipeTest rules: done early on 64 of 64, 64 late edges ignored]. (Until M14 the wrong form was a polled reply without the `WFI`, whose leftover latch made `wait_for_command()` fall through without GO; the done gate now clears that latch.)

**Rule 2: disjoint buffers per engine run** (4.1).

<!-- recipe: rule_disjoint (rcp_rules.c) -->
```c
/* One engine run never reads and writes the same buffer (spec 4.1: no coherence between a run's reads and its
 * writes). To shift records within a buffer, copy out and back: two runs, each between disjoint buffers. */
static void shift_up(uint32_t buf, uint32_t tmp, uint32_t n, uint32_t by) {
    wait_tables(); PTR[tile_index()] = tmp;
    set_stream(0, STREAM_LO(buf, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);       /* buf -> tmp */
    wait_tables(); PTR[tile_index()] = buf + 2 * by;
    set_stream(0, STREAM_LO(tmp, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));
    local_run(DMA_CFG_RUN(RUN_STREAMS, 0, 1), n);       /* tmp -> buf + by records */
}
```

*Wrong:* one run copying a buffer onto itself shifted by 8 records reads chunks it has already written; meshsim flags it (invariant 2).

**Rule 3: from the `WFI` to the start store, identical code on every tile** (6.3).

<!-- recipe: rule_start (rcp_rules.c) -->
```c
/* From the WFI that GO ends to the start store, every tile runs the same instructions (spec 6.3): no data-dependent
 * branch or loop, no stall (a ROUTER_SEND_LO to a full holding register, a multi-cycle wait). Counter values may differ: a
 * store costs one cycle whatever its value. Anything else belongs in the configure command. */
static void ring_start(void) {
    DMA_SRAM_REMAINING = expect;
    start_run();
    wfi();
}
```

*Wrong:* a delay of `me & 7` loop iterations before the start store spreads the tiles' starts over 56 cycles [meas: RecipeTest]. Nothing is flagged here, but in a group run the timetable assumes every tile starts within its 0/+1 skew.

**Rule 4: one read size per group** (4.2).

<!-- recipe: rule_size (rcp_rules.c) -->
```c
/* All the streams of one group have the same read size (spec 4.2): it sets the group's period, one visit per cycle,
 * or two for 128 bits. A 128-bit stream and a 64-bit one belong in different groups. */
static void two_streams_one_group(uint32_t n) {
    wait_tables();
    PTR[tile_index()] = C;
    set_stream(0, STREAM_LO(A, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));
    set_stream(1, STREAM_LO(B, 0), STREAM_HI(n, 0, MODE_SRC, SIZE_64, 0));    /* the same size as stream 0 */
    set_group(0, GROUP_LO_R(n), GROUP_HI(0, 2, 0));
    local_run(DMA_CFG_RUN(RUN_GROUPS, 0, 1), 2 * n);
}
```

*Wrong:* a 32-bit stream in a group of 64-bit ones; meshsim reports it.

**Rule 5: staging below the sort scratch; `PTR[64]` restored after mode 3** (6.2, 4.9).

<!-- recipe: rule_stage (rcp_rules.c) -->
```c
/* C&C stages data (mode 2 from src 64) at PTR[64], which the loader points at this tile's staging area: the 64 KB
 * below the top 128 KB of sort scratch (stage_base(), spec 6.2). A program keeps its buffers out of both, and a phase
 * with mode 3 uses all 256 PTR entries, PTR[64] among them: restore it before the phase ends. */
static void bucket_phase(uint32_t n) {
    one_bucket_pass(n);
    wait_tables();
    PTR[64] = stage_base();
}
```

*Wrong:* without the restore, C&C's staged words land where the bucket pass left `PTR[64]`, inside the program's own buffer, not at `stage_base()`.

**And the rules covered by the recipes themselves:** the counter stored first in a command that receives (sections 7–10); read-port reads in steps of their own (section 11); the start command (section 13).

## 16. Gaps and awkwardness found

Two remain:
- **REPORT can't be told it has landed:** it replaces a command whose READY has already fired. C&C waits a fixed settle, twice the worst-case broadcast latency (section 20).
- **Two conditions can't be caused by a program** in meshsim: table request overflow and GATHER queue overflow. They stay as hardware-fault detectors (spec edit 4).

## 17. Write a block from data memory into big SRAM

**What it does.** Copies records the controller has in data memory into big SRAM: mode 2 from the send port to the tile itself, two stores per 64-bit record.
**Relies on:** 4.9 (mode 2 lands at `PTR[src]`), 4.11 (the send port), 4.12 (`DMA_SRAM_REMAINING`).

<!-- recipe: write_block (rcp_selftest.c) -->
```c
/* n 64-bit records from data memory (w[2i] the low word, w[2i + 1] the high) into big SRAM at chunk dst: mode 2 from the
 * send port to the tile itself, two stores per record (ROUTER_SET_HI, then ROUTER_SEND_LO, which sends). They land at PTR[me], which
 * advances by 2 per packet (spec 4.9, 4.11). PTR[me] is this tile's own landing pointer: set it only when no run uses it. */
static void write_block(uint32_t dst, const uint32_t *w, uint32_t n) {
    unsigned me = tile_index();
    wait_tables();
    PTR[me] = dst;
    DMA_SRAM_REMAINING = n;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_SRC, 0);
    for (uint32_t i = 0; i < n; i++) send64(w[2 * i + 1], w[2 * i]);
    if (n) wfi();                                  /* every record written: DMA_SRAM_REMAINING = 0 and mem_idle */
}
```

- **`PTR[me]`** is the tile's own landing pointer; it advances by 2 per packet. Set it only when no run uses it (`wait_tables()`).
- **Cost** [meas: RecipeTest blocks]: 7.0 (64 records: 500 cycles GO → all READY; 256: 1,843) cycles per record, marginal (the send loop at two stores per record, `-O2`).

## 18. Fill a block with pseudo-random data

**What it does.** Fills big SRAM with a reproducible pseudo-random sequence, generated in registers and sent as in section 17, so nothing passes through data memory. The generator, a 64-bit xorshift on the 32-bit core, and the seeding are in `sw/tests/selftest.h`, which RecipeTest reads for its constants, so the Java check can't disagree with the tiles.
**Relies on:** 4.9, 4.11, 4.12; `selftest.h`.

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
    unsigned me = tile_index();
    uint32_t h = *hi, l = *lo;
    wait_tables();
    PTR[me] = dst;
    DMA_SRAM_REMAINING = n;
    ROUTER_SET_HDR = ROUTER_HDR(me, MODE_SRC, 0);
    for (uint32_t i = 0; i < n; i++) { st_next(&h, &l); send64(h, l); }
    *hi = h; *lo = l;
    if (n) wfi();
}
```

Two ways to seed: a seed per tile from C&C (one unicast command each, the 64-bit seed as two arguments), or a seed made from the tile index, optionally mixed with a base seed C&C broadcasts:

<!-- recipe: seeds (rcp_selftest.c) -->
```c
/* The two ways to seed. From C&C: one unicast command per tile, the 64-bit seed as two arguments. From the tile index:
 * one broadcast command whose two arguments are a base seed (0 for none), mixed with the index by st_tile_seed(). */
static void seed_from_cnc(volatile uint32_t *arg, uint32_t *hi, uint32_t *lo) {
    *lo = arg[0]; *hi = arg[1];
    st_seed(hi, lo);
}
static void seed_from_index(volatile uint32_t *arg, uint32_t *hi, uint32_t *lo) {
    st_tile_seed(tile_index(), arg[1], arg[0], hi, lo);
}
```

- **Checked** [meas: RecipeTest blocks]: every tile's records equal Java's from both seedings, with and without a base seed; a zero seed is taken as 1; the 64 index seeds give 64 different sequences.
- **Cost:** 23.0 (256 records: 6,044 cycles; 1,024: 23,707) cycles per record, marginal (`-O2`).

## 19. Hash a block of big SRAM

**What it does.** Reads a block through the controller read port and folds it into an order-sensitive 64-bit hash: two 32-bit lanes, the low and the high word of each record, h = rotl((h ^ w) × M, R) (`selftest.h`'s `st_mix`).
**Relies on:** 4.3 (the read port: results through the loopback, `DMA_REPLY_REMAINING`), CONVENTIONS 9.

<!-- recipe: hash (rcp_selftest.c) -->
```c
/* The 64-bit hash of n records of big SRAM at chunk src (selftest.h: two 32-bit lanes, the low and the high words,
 * h = rotl((h ^ w) * M, R)), read through the read port (spec 4.3) in batches of 32 words, two buffers: while one batch
 * is folded the next is in flight. Each batch counts in DMA_REPLY_REMAINING; the store for the next batch comes after the
 * WFI that consumed the last one. A step of its own: no packets may arrive for this tile meanwhile (CONVENTIONS 9). */
static void hash_block(uint32_t src, uint32_t n, uint32_t *hi, uint32_t *lo) {
    static volatile uint32_t buf[2][32];
    uint32_t h0 = ST_HASH_H0, h1 = ST_HASH_H1, words = 2 * n, issued = 0, folded = 0;
    unsigned k = 0;
    while (folded < words) {
        uint32_t cur = issued - folded;            /* the batch in flight to buf[k] (0 the first time) */
        if (cur) wfi();                            /* it has landed */
        if (issued < words) {                      /* the next batch, to the other buffer */
            uint32_t m = words - issued < 32 ? words - issued : 32;
            DMA_REPLY_REMAINING = m;
            SRAM_RD_DEST_ADDR = CM_WORD((uint32_t)&buf[cur ? k ^ 1 : k][0]);
            for (uint32_t i = 0; i < m; i++) SRAM_RD_ADDR_QUEUE = src + issued + i;
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

- **Double-buffered:** batches of 32 words; the next batch is queued before the last is folded, so its reads overlap the fold.
- **`st_mix` keeps M in a register**, so the compiler emits one `mul` (Zmmul) rather than a shift-and-add chain for the constant (which cost 31 cycles a word).
- **Cost** [meas: RecipeTest blocks]: 35.9 cycles per record, that is 18.0 (256 records: 9,258 cycles; 1,024: 36,857) per 32-bit word read (`-O2`).

## 20. REPORT

**What it does.** When C&C's error bit is set, it finds out which tiles hold which conditions: it stops issuing GOs, waits for every tile to finish its phase, broadcasts `CMD_REPORT`, collects one (tile index, `TILE_CONDITIONS`) packet per tile, logs them and halts the machine, which clears every condition (spec 6.6). A debugging flow, opt-in (`CnC.stopOnError`); a correct program never needs it.
**Relies on:** 6.6 (the error line, REPORT, C&C's receive FIFO), 4.11 (the core's deterministic timing), 4.13.

The tile side, in the command loop as `case CMD_REPORT: report(); break;`:

<!-- recipe: report (rcp.h) -->
```c
/* REPORT (spec 6.6): C&C's debugging command. When its error bit is set, C&C stops issuing GOs, waits until every tile
 * is in wait_for_command() with its conditions intact, then broadcasts CMD_REPORT. Each tile answers with one packet to
 * C&C: its tile index and TILE_CONDITIONS. A delay loop of (me + 1) x 4 cycles paces the answers, one every 4 cycles in
 * tile order, so nothing fans in: the core's timing is deterministic and every tile reaches the loop by identical code.
 * Call it first in the command; paced_to_cnc() also serves other results. */
static void paced_to_cnc(uint32_t hi, uint32_t lo) {
    delay4(tile_index() + 1);                      /* (me + 1) x 4 cycles: the same code on every tile */
    ROUTER_SET_HDR = ROUTER_HDR(DST_CNC, MODE_REPLY, 0);         /* C&C's port ignores the mode and the address */
    send64(hi, lo);
}
static void report(void) { paced_to_cnc(tile_index(), conditions()); }
```

The C&C side (meshsim's Java sequencer, `src/CnC.java`): `go()` calls `reportOnError` when `stopOnError` is set and the error bit is up.

<!-- recipe: cnc_stop (CnC.java) -->
```java
private boolean reportOnError(long t) {
    if (errorSeenAt < 0) errorSeenAt = t;
    if (!allReady(t) && t < errorSeenAt + watchdog) return false;
    steps.clear();
    report();
    return false;
}
```

<!-- recipe: cnc_report (CnC.java) -->
```java
CnC report() {
    steps.add((Instant) (c, t) -> {
        reportStartedAt = t;
        for (Tile p : chip.tiles) p.cmem.discardNext = true;
        return true;
    });
    command(LinkWord.BROADCAST, Registers.CMD_REPORT << 24);
    final long[] sentAt = { -1 };
    steps.add((Instant) (c, t) -> { sentAt[0] = t; return true; });
    steps.add((c, t) -> t >= sentAt[0] + reportSettle());
    steps.add((c, t) -> { clearReady(t); storeGo(t); reportGoAt = t; return true; });
    if (cfg.cncGoSettle) steps.add((c, t) -> t >= lastGoAt + cfg.goLatency + 1);
    collect(64, 64 * 4 + 1000, reports);           // the receive FIFO is C&C's wake source; one read per cycle
    steps.add((c, t) -> allReady(t) || t >= reportGoAt + watchdog);
    steps.add((Instant) (c, t) -> { reportDoneAt = t; return true; });
    halt();
    steps.add((c, t) -> t >= lastRunAt + cfg.runLatency + 2 + Math.max(1, cfg.errorLatency()));   // the cleared line reaches C&C
    steps.add((c, t) -> { clearError(t); return true; });
    return this;
}
```

- **Pacing:** every tile reaches the delay loop by identical code after waking, and the loop takes exactly 4 (*k* + 1) cycles on tile *k* (the core's timing is deterministic), so the tiles send one packet every 4 cycles in tile order, about 260 cycles for 64; C&C's receive FIFO never holds more than 2 [meas: RecipeTest selftest].
- **The settle.** REPORT lands on the command C&C sent right after the last GO, which the tiles haven't read; its READY has already fired, so READY can't say when REPORT has landed. C&C waits a fixed settle instead: **twice the worst-case latency of a one-packet broadcast from C&C to its farthest tile** (tile 36, 8 hops from tile 0), 2 + `intra_tile_latency` + `cm_latency` + 8 × (`link_latency` + 1) = **37 cycles at the defaults, so a settle of 74** [meas: CondTest broadcast, exact at link latency 2–4, `cm_latency` 2–4, `intra_tile_latency` 1–2]. The mesh is idle then: every tile has reported READY. The discarded command never runs; meshsim counts it as discarded, not as a command overwritten unread.
- **A crashed tile** never reaches `wait_for_command()`: it sends no READY and no reply. C&C's watchdog bounds both waits [meas: CondTest report: 63 replies, tile 63 identified by its missing reply].
- **Measured** [RecipeTest report]: error bit → REPORT sent 70 cycles (C&C waits for every tile's READY); REPORT sent → GO 76 (the settle of 74, plus its own step); GO → last reply 292; first to last reply 258.

## 21. The self-test

**What it does.** Seeds and fills a block on every tile (section 18), runs an operation on it (here a uniform all-to-all on the 16-group schedule), hashes what arrived (section 19), and sends the signature to C&C paced as REPORT is (`paced_to_cnc()`). The simulator computes every tile's expected signature from the generator and the all-to-all's permutation alone.
**Relies on:** sections 9, 18–20.

<!-- recipe: selftest (rcp_selftest.c) -->
```c
/* The self-test: seed and fill a block (command 1 or 2), run an operation on it (here the uniform all-to-all on the
 * 16-group schedule: n / 64 records to each tile, configure then start), hash what arrived (command 5), and send the
 * signature to C&C (command 6), which compares it with the simulator's. The all-to-all's configure and start: */
static void a2a_uniform_configure(uint32_t n) {
    unsigned me = tile_index();
    m_per = n >> 6;
    wait_tables();
    for (int g = 0; g < 16; g++) {
        for (int i = 0; i < 4; i++) {
            unsigned off = SCHED[g][i], d = (me + off) & 63;
            set_stream(4 * g + i, STREAM_LO(SRC + 2 * m_per * d, 0), STREAM_HI(m_per, off, MODE_SRC, SIZE_64, 0));
        }
        set_group(g, GROUP_LO_R(m_per), GROUP_HI(4 * g, 4, 0));   /* every count is m_per: R needs no reduction */
    }
    for (int s = 0; s < 64; s++) PTR[s] = DST + 2 * m_per * s;   /* source s's block lands at DST + s blocks */
    expected = 64 * m_per;
    DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, 0, 16);
}
static void a2a_uniform_start(void) {
    DMA_SRAM_REMAINING = expected;                     /* identical code from waking to the start store (6.3) */
    start_run();
    if (expected) wfi();
}
```

The command sequence, from C&C: fill (`2`, base seed, n), configure (`3`), start (`4`), hash (`5`, the receive buffer, n), signature (`6`), then collect 64 replies.

- **Checked** [meas: RecipeTest selftest]: all 64 signatures equal the simulator's; with one record of tile 17's block corrupted after the fill, 63 still match and the one that differs is tile 22's, which received it.
- **Cost** (1,024 records per tile): fill 23,746 cycles, configure 1,632, start 1,112 (timetable 1,024), hash 36,857, signatures 336 (GO → all READY each).
