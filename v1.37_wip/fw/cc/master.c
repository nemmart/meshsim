/* master.c: the master tile's program for v1.37 (updater_wip, WORKER3): the proof arrives as the host's frames over
 * the link, and the master builds the memory images from them with the Updater and hands the tracer tile its segment.
 * Written for v1.37 on v1.36's interface (include/mesh.h) and v1.37's additions (include/host.h); cc1.c, v1.36's
 * master program, is untouched and is what every v1.36 test runs. The machine must be built with the host link
 * (MachConfig::host_link): Ingress with its two streams and its DMA (INGRESS-INTERFACE.md), and the Updater as the
 * master tile's peripheral 2 (UPDATER-INTERFACE.md). The frames are HOST-FRAMES.md's.
 *
 * What the harness places before the reset ends (boot_local, 6.5), as for cc1.c: this image; in the master's scratchpad
 * the two images it loads and the load's COUNT row, where boot_desc says; boot_desc and system_config (N and s, which
 * no frame carries). In the tracer tile's scratchpad its layout's header, the tables and the templates, as today; the
 * record, the advice and the expected end state come from here.
 *
 * THE BOOT, cc1.c's: every LOAD awaited (the compute tiles' and the tracer tile's); the compute tiles' image sent, and
 * the release when all have arrived; then the segment's first barrier (each tile's program has started, nothing
 * armed), whose release lets the tiles arm for their records. THE TRACER TILE'S LOAD IS HELD BACK: it sits in its boot
 * ROM until its segment is ready.
 *
 * THE PAGE MAP is the master's (UPDATER-INTERFACE.md, Open): 128 KB in the scratchpad, a bit a 4 KB page of LPDDR. The
 * core can't load the scratchpad, so the row the cursor is in, 128 pages, is kept in DMEM, written back by a WRITE to
 * the tile itself and the next fetched with the read unit. Page 0 is never given out. Pages are taken in order by a
 * cursor that only moves up; v1.37 frees nothing.
 *
 * THE FRAME LOOP (INGRESS-INTERFACE.md, "Frames" and "the threshold"): the frame's header word into DMEM; by its type
 * the rest goes
 *   1, 3, 4   to the parser, through a 32-word buffer in DMEM, as a stream of 32-bit words;
 *   2, 5, 6   to the Updater's segment queue, in a run of the Updater;
 *   7         record s and record s + 1 to the staging area, the others to scrap; record s's word 66 to DMEM;
 *   8         the counts to the parser, log s to the staging area, the others to scrap;
 *   9         to the Updater, in three runs.
 * Words are read out to the Updater only as far as the allowance covers them: a segment_credit adds 256 words, a
 * transfer takes its words off. The free page queue gets exactly PAGE_COUNT pages for each run, from the map, under its
 * own credits. Every root is a page from the map, made by the Updater at the run's start (RUN_CONFIG.root, v1.37).
 *
 * THE THRESHOLD (INGRESS-INTERFACE.md, "Waking for more than a packet"; the owner's sequence; WORKER4): Ingress posts the
 * background flag when the words waiting reach BACKGROUND_THRESHOLD, and none after it until the words its count showed
 * are read out. The threshold is 1 between frames; after a frame's header word it is the smaller of 256 (a credit:
 * system_config's threshold word) and the words left in the frame, and as the frame's tail comes it is lowered to what
 * is left; at the frame's end it is 1 again. It is stored only when it changes. The program knows nothing of packets:
 * `avail` is the words it knows are waiting and has not started to read; it loads BACKGROUND_WORDS only after a flag,
 * with no transfer of its own in flight and `avail` used up, so the count is exact and covers what Ingress owes. A
 * transfer may run across packets, so a frame's header word need not be a packet's first word: nothing checks it.
 *
 * THE RUNS, UPDATER-INTERFACE.md's ("The runs"), IMAGE_ROOT and what it starts as:
 *   frame 2          the program image, empty
 *   frame 5          a base table (frame 4 with a path), empty; the proof's table, empty or a copy of the base
 *   frame 6          the cycle-0 image, a copy of the program image
 *   frame 9, to s    the start image, a copy of the cycle-0 image             WORD_COUNT: the segments' words before s
 *   frame 9, s       the final image, a copy of the start image, continue,     segment s's
 *                    with the Tracer's copy (TRACER_ROOT, tracer_copy)
 *   frame 9, past s  the final image, continue                                 the rest
 * PAGE_COUNT is frame 1's, frame 4's, frame 3's data image pages, and for the three: ASIC s's start count, twice its
 * Tracer's copy count, its final count less its Tracer's copy count.
 *
 * THE HAND-OVER, at the second of frame 9's runs' done (the Tracer's copy is complete), with the third started and
 * the stream going on: the tracer tile's load, held back until now, as one transfer of the DMA unit, uncounted and of
 * one kind so that it lands in order: record s (word 66 with the Tracer's copy's root and depth and the table's) to
 * where the tracer tile's layout has its record; record s + 1 as the expected end state (for the last segment, the
 * header's word for it 0: no check); advice log s; the header's HASH_MUL, HASH_STEP, HASH_LINES and ADVICE_N; then
 * the image and the load's COUNT, last. tracer_ctl.c finds its segment in place when it starts: it is unchanged.
 * Then the segment's second barrier (every window has landed), whose release is the replay; the 64 tiles' results and
 * the tracer tile's; the third run's done; the exit.
 *
 * ERRORS: any error bit is the NMI (6.2), and so are Ingress's (a packet that fails a check) and the Updater's through
 * the tile's ERRORS.peripheral_n. The handler writes what there is into master_results and exits. What a frame says
 * that cannot be (master_layout.h, MR_FRAME) ends the same way.
 *
 * Waiting: every wait sleeps in WFI and, on each wake, takes every event there is into the program's own state
 * (poll), serves the requests and moves the frame's data on (the pump), so nothing waits on anything else. */
#include "host.h"
#include "master_layout.h"
#include "../tracer/tracer_layout.h"

/* ---- the firmware's own conventions, v1.35's (port/mesh.h's names for them, which this file does not include) */
#define OP_RESULT            2             /* a tile's result: its reply, a 32-bit WRITE, at RESULT_ADDR on RESULT_COUNTER */
#define RESULT_COUNTER(w0)   (((w0) >> 18) & 0x7u)
#define RESULT_ADDR(w0)      (((w0) >> 21) << 4)
#define EXIT_DONE            0x45584954u   /* "EXIT": crt0.S's sim_exit_word[0] once [1] is written */
extern volatile uint32_t sim_exit_word[2];
#define DMEM_FIRST           0x10
#define TILES_LOADING        65            /* the LOADs of the boot: the compute tiles' and the tracer tile's */

/* ---- the master's counters, each with one user */
#define CTR_LINK             1             /* the words Ingress's DMA sends here */
#define CTR_READ             2             /* the read unit's rows */
#define CTR_SELF             3             /* the write unit's rows to the master's own scratchpad */
#define TILE_MASK   (EVENT_REQUEST_PENDING | EVENT_PERIPHERAL(1) | EVENT_PERIPHERAL(2) | EVENT_DMA_DONE | \
                     EVENT_COUNTER_DONE(CTR_LINK) | EVENT_COUNTER_DONE(CTR_READ) | EVENT_COUNTER_DONE(CTR_SELF))
#define LINK_MASK   (HL_EVENT_BACKGROUND_SERVICE | P_EVENT_DMA_DONE)
#define UPD_MASK    (P_EVENT_DONE | UPD_EVENT_SEGMENT_CREDIT | UPD_EVENT_FREE_PAGE_CREDIT)
#define SELF(counter, size)  (HDR_DST(TILE_MASTER) | HEADER(MODE_WRITE, counter, size))

/* ---- what the harness fills (master_layout.h), and what it reads */
volatile uint32_t boot_desc[MD_H_WORDS] __attribute__((section(".data"))) = { MD_MAGIC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
volatile uint32_t system_config[MS_H_WORDS] __attribute__((section(".data"))) = { MS_MAGIC, 64, 0, 0 };
volatile uint32_t master_results[MR_WORDS];
#define R master_results

/* ---- the program's state */
static uint32_t N, S;                       /* the system's configuration */
static uint32_t tile_seen, link_seen;       /* counted events taken by a load of EVENTS and not yet used */
static uint32_t upd_done;                   /* the Updater's done, taken */
static uint32_t avail;                      /* the words waiting in Ingress that the program knows of and has not started to read */
static uint32_t flag_seen;                  /* a background flag taken, its count not yet loaded */
static uint32_t in_flight;                  /* a transfer of Ingress's DMA has started and not yet landed */
static uint32_t credit_threshold;           /* system_config's threshold word: 256 by default */
static uint32_t threshold;                  /* BACKGROUND_THRESHOLD as last stored */
static uint32_t frame_open;                 /* a frame's header word is in, and its end not yet reached */
static uint32_t wakes, reads;               /* the WFIs returned from; the transfers started */
static uint32_t allowance, page_allowance;  /* the segment queue's words and the free page queue's entries the credits taken cover */
static uint32_t pump_left, pump_busy;       /* the frame's words still to go to the Updater; a transfer of them is in flight */
static uint32_t pages_due;                  /* pages still to queue for the runs started */
static uint32_t frame_left;                 /* the frame's words not yet read out of Ingress (but the pump's) */
#define PARSE_ROWS           32             /* the parser's buffer, in 128-bit words */
static uint32_t frames, runs;

static volatile uint32_t frame_header[4] __attribute__((aligned(16)));
static volatile uint32_t buffer[4 * PARSE_ROWS] __attribute__((aligned(16)));        /* the parser's */
static volatile uint32_t files_word[4] __attribute__((aligned(16)));                 /* record s's word 66 */
static volatile uint32_t map_row[4] __attribute__((aligned(16)));                    /* the page map's row the cursor is in */
static uint32_t map_at, map_cursor, map_dirty;                                       /* that row; the next page to look at */

/* what the frames said */
static uint32_t image_depth, root_program, root_cycle0, root_tracer;
static uint32_t header_seen;                /* the header frame the next data frame belongs to: 1, 4 or 0 */
static uint32_t program_words64, program_words128, program_pages;
static struct { uint32_t is_base, from_base, k, p1, p3, words64, words128, pages; } table_header;
static uint32_t have_base, root_base, root_table, table_depth, table_k, table_p1, table_p3;
static uint32_t have_proof, data_words64, data_words128, data_pages;
static uint32_t words_before, words_mine, words_after, segments_words128, pages_start, pages_final, pages_tracer;
static uint32_t advice_values, advice_rows;

static inline uint32_t min32(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* ---------------------------------------------------------------- the end, and the error path */
static __attribute__((noreturn)) void finish(uint32_t status, uint32_t detail) {
    R[MR_TILE_ERRORS] = tile->ERRORS; R[MR_INGRESS_ERRORS] = host_link->ERRORS; R[MR_UPDATER_ERRORS] = updater->ERRORS;
    R[MR_ERROR_LINES] = ingress_egress->ERROR_LINES_0; R[MR_ERROR_LINES + 1] = ingress_egress->ERROR_LINES_1;
    R[MR_ERROR_LINES + 2] = ingress_egress->ERROR_LINES_2;
    R[MR_DETAIL] = detail; R[MR_END] = mcycle();
    R[MR_WAKES] = wakes; R[MR_LINK_READS] = reads;
    R[MR_STATUS] = status;                          /* last of the results */
    sim_exit_word[1] = status;
    sim_exit_word[0] = EXIT_DONE;
    __asm__ volatile ("csrw mie, zero" ::: "memory");
    for (;;) __asm__ volatile ("wfi" ::: "memory");  /* park: the outside world resets the chip */
}
static __attribute__((noreturn)) void bad_frame(uint32_t type, uint32_t check) { finish(MR_FRAME, (type & 0xFFu) | check << 8); }
/* mtvec's target: an error bit's NMI, or a trap of the program's own. MIE stays 0; the handler never returns. */
void master_trap(void) __attribute__((noreturn, aligned(4)));
void master_trap(void) {
    uint32_t cause, epc;
    __asm__ volatile ("csrr %0, mcause" : "=r"(cause));
    __asm__ volatile ("csrr %0, mepc" : "=r"(epc));
    R[MR_MCAUSE] = cause; R[MR_MEPC] = epc;
    finish(MR_TRAP, 0);
}

/* ---------------------------------------------------------------- events, requests and the pump */
/* The requests (11.5): a LOAD is counted (the boot and the hand-over answer them); a tile's result is answered where
 * its word 0 says, which ends the tile's image (crt0.S); anything else is counted. Never pops an empty queue. */
static void drain_requests(void) {
    while (tile->EVENTS_PEEK & EVENT_REQUEST_PENDING) {
        uint32_t w0 = tile->REQUEST_IN_W0;
        (void)tile->REQUEST_RECEIVE;                /* the last word pops */
        uint32_t op = REQUEST_OP_OF(w0), who = REQUEST_REQUESTER(w0);
        if (op == OP_LOAD) R[MR_LOADS]++;
        else if (op == OP_RESULT && who <= TILE_TRACER) {
            write_to(HDR_DST(who) | HEADER(MODE_WRITE, RESULT_COUNTER(w0), SIZE_32), RESULT_ADDR(w0));
            write32(1);
            R[MR_RESULTS]++;
        } else R[MR_OTHERS]++;
    }
}
/* Ingress's DMA (INGRESS-INTERFACE.md): the next n words waiting on the background stream, n at most `avail`, to an
 * address of the master's own (DMEM, the scratchpad, the Updater's segment queue), counted on CTR_LINK, armed first. */
static void link_read(uint32_t n, uint32_t address) {
    tile_seen &= ~EVENT_COUNTER_DONE(CTR_LINK); link_seen &= ~P_EVENT_DMA_DONE;
    *counter(CTR_LINK) = n;
    host_link->DMA_HDR = SELF(CTR_LINK, SIZE_128) | DMA_HDR_COUNT(n) | DMA_HDR_QUEUE(HL_QUEUE_BACKGROUND);
    host_link->DMA_DEST_ADDR = address;
    host_link->DMA_START = 1;
    avail -= n; in_flight = 1; reads++;
    R[MR_LINK_WORDS] += n;
}
static inline int link_read_done(void) { return (tile_seen & EVENT_COUNTER_DONE(CTR_LINK)) && (link_seen & P_EVENT_DMA_DONE); }
static void set_threshold(uint32_t v) {
    if (v == threshold) return;
    host_link->BACKGROUND_THRESHOLD = v;
    threshold = v;
    R[MR_THRESHOLD_STORES]++;
}
/* With nothing known waiting and nothing in flight: the frame's tail lowers the threshold to what is left of it; after
 * a flag, the count is loaded. 1 if words became known (no event comes for them: the caller must not sleep). */
static int link_refill(void) {
    if (avail || in_flight) return 0;
    if (frame_open) {
        uint32_t left = frame_left + pump_left;     /* the frame's words not yet read out */
        if (left && left < threshold) set_threshold(left);
    }
    if (!flag_seen) return 0;
    flag_seen = 0;
    avail = host_link->BACKGROUND_WORDS;
    return avail != 0;
}
/* The pump: the frame's data on to the Updater's segment queue, a packet (or what is left of one) at a time, only
 * when the allowance covers it (INGRESS-INTERFACE.md, "Credits for the Updater"). Never waits. */
static void pump(void) {
    if (pump_busy) {
        if (!link_read_done()) return;
        pump_busy = 0;
    }
    uint32_t n = min32(avail, pump_left);
    if (allowance < n) n = allowance;               /* what the allowance covers; the rest stays in Ingress */
    if (!n) return;
    allowance -= n; pump_left -= n;
    link_read(n, UPD_QUEUE_SEGMENT);
    pump_busy = 1;
}
/* Every event there is, into the program's state: one load of the tile's EVENTS, and of each peripheral's that has
 * one (a load consumes one count of each counted event its mask selects, 7.2: a queue's second credit is the next
 * wake's). Then the requests and the pump. Never waits. */
static void poll(void) {
    uint32_t e = tile->EVENTS;
    tile_seen |= e & (EVENT_DMA_DONE | EVENT_COUNTER_DONE(CTR_LINK) | EVENT_COUNTER_DONE(CTR_READ) | EVENT_COUNTER_DONE(CTR_SELF));
    if (e & EVENT_REQUEST_PENDING) drain_requests();
    if (e & EVENT_PERIPHERAL(1)) {
        uint32_t l = host_link->EVENTS;
        if (l & HL_EVENT_BACKGROUND_SERVICE) flag_seen = 1;   /* cleared by the load; its count is loaded when it is exact */
        link_seen |= l & P_EVENT_DMA_DONE;
    }
    if (e & EVENT_PERIPHERAL(2)) {
        uint32_t u = updater->EVENTS;
        if (u & UPD_EVENT_SEGMENT_CREDIT) allowance += UPD_CREDIT;
        if (u & UPD_EVENT_FREE_PAGE_CREDIT) page_allowance += UPD_FREE_PAGE_CREDIT;
        if (u & P_EVENT_DONE) upd_done = 1;
    }
    if (in_flight && link_read_done()) in_flight = 0;
    link_refill();
    pump();
}
/* The one sleep: WFI, then poll. Whoever waits re-checks what it waits for after it. The pump first: what the
 * program has set since the last poll (a frame for the Updater) may let it start, and nothing else would wake it. */
static void idle(void) {
    if (link_refill()) return;                      /* words known: the caller looks again before any sleep */
    pump();
    __asm__ volatile ("wfi" ::: "memory");
    wakes++;
    poll();
}
static void wait_counter(unsigned n) {
    while (!(tile_seen & EVENT_COUNTER_DONE(n))) idle();
    tile_seen &= ~EVENT_COUNTER_DONE(n);
}

/* ---------------------------------------------------------------- the page map */
/* The cached row back to the scratchpad, landed. */
static void map_flush(void) {
    if (!map_dirty) return;
    tile_seen &= ~EVENT_COUNTER_DONE(CTR_SELF);
    *counter(CTR_SELF) = 1;
    write_to(SELF(CTR_SELF, SIZE_128), SCRATCHPAD_ADDRESS(ML_MAP + 4 * map_at));
    write128(map_row[3], map_row[2], map_row[1], map_row[0]);
    wait_counter(CTR_SELF);
    map_dirty = 0;
}
/* Every page free but page 0, LPDDR's first (the superblock's): the 128 KB written as zeros, landed. */
static void map_format(void) {
    tile_seen &= ~EVENT_COUNTER_DONE(CTR_SELF);
    *counter(CTR_SELF) = ML_MAP_ROWS;
    write_to(SELF(CTR_SELF, SIZE_128), SCRATCHPAD_ADDRESS(ML_MAP));
    for (uint32_t row = 0; row < ML_MAP_ROWS; row++) write128(0, 0, 0, 0);
    wait_counter(CTR_SELF);
    map_row[0] = 1; map_row[1] = 0; map_row[2] = 0; map_row[3] = 0;
    map_at = 0; map_cursor = 1; map_dirty = 1;
}
/* The next free page, marked taken. May sleep, for a row of the map. */
static uint32_t map_take(void) {
    for (;;) {
        if (map_cursor >= LPDDR_PAGES) finish(MR_ROOM, 0);
        uint32_t row = map_cursor >> 7;
        if (row != map_at) {
            map_flush();
            tile_seen &= ~EVENT_COUNTER_DONE(CTR_READ);
            *counter(CTR_READ) = 1;
            tile->READ_HDR = HEADER(0, CTR_READ, SIZE_128);
            tile->READ_DEST_ADDR = (uint32_t)map_row;
            tile->READ_SEND = ML_MAP + 4 * row;
            wait_counter(CTR_READ);
            map_at = row;
        }
        uint32_t page = map_cursor++, word = (page >> 5) & 3, bit = 1u << (page & 31);
        if (map_row[word] & bit) continue;
        map_row[word] |= bit;
        map_dirty = 1;
        R[MR_PAGES_TAKEN]++;
        return page;
    }
}
static uint32_t new_root(void) { return PAGE_ADDRESS(map_take()); }
/* The free page queue, for the runs started: a page of the map for each entry the credits cover. 1 if it pushed any. */
static int feed_pages(void) {
    int pushed = 0;
    while (pages_due && page_allowance) {
        uint32_t page = map_take();                 /* may sleep, and the requests answered then use the write unit */
        write_to(SELF(0, SIZE_128), UPD_QUEUE_FREE_PAGES);
        write128(0, 0, 0, PAGE_ADDRESS(page));
        pages_due--; page_allowance--; pushed = 1;
        R[MR_PAGES_QUEUED]++;
    }
    return pushed;
}
/* What a wait at the top does between its checks: the free page queue, or a sleep. */
static void work(void) { if (!feed_pages()) idle(); }

/* ---------------------------------------------------------------- the barrier and the DMA unit */
/* Every compute tile's arrival, then the release (7.3), and go_latency + 1 cycles before anything else is sent (6.6). */
static void barrier(void) {
    host_link->EVENTS_MASK = LINK_MASK | IE_EVENT_ALL_ARRIVED;     /* live: it wakes the wait */
    while (!(host_link->EVENTS_PEEK & IE_EVENT_ALL_ARRIVED)) work();
    host_link->EVENTS_MASK = LINK_MASK;
    ingress_egress->RELEASE = 1;
    uint32_t t = mcycle();
    R[MR_RELEASED] = t;
    while (mcycle() - t <= boot_desc[MD_H_GO_LATENCY]) ;
}
static inline void stream(unsigned i, uint32_t source, uint32_t dest, uint32_t count, uint32_t size, uint32_t counter, uint32_t mode, uint32_t dst) {
    set_stream(i, source, dest, STREAM_HDR_COUNT(count) | HDR_DST(dst) | HEADER(mode, counter, size));
}
/* An image to dst as three streams from i: its code, its data, then the load's COUNT on the boot ROM's counter (6.5). */
static unsigned image_streams(unsigned i, uint32_t dst, const volatile uint32_t *image) {
    stream(i, image[0], IMEM_BASE, image[1], SIZE_128, 0, MODE_WRITE, dst);
    stream(i + 1, image[2], DMEM_BASE + DMEM_FIRST, image[3], SIZE_128, 0, MODE_WRITE, dst);
    stream(i + 2, image[4], 0, 1, SIZE_128, LOAD_COUNTER, MODE_COUNT, dst);
    return i + 3;
}
/* Streams 0 to n - 1 as one transfer, to its end. */
static void transfer(unsigned n) {
    tile_seen &= ~EVENT_DMA_DONE;
    tile->DMA_CONFIG = DMA_CONFIG_KIND(DMA_KIND_STREAM) | DMA_CONFIG_START(0) | DMA_CONFIG_COUNT(n);
    dma_start();
    while (!(tile_seen & EVENT_DMA_DONE)) work();
    tile_seen &= ~EVENT_DMA_DONE;
}
static void boot(void) {
    while (R[MR_LOADS] < TILES_LOADING) idle();     /* every LOAD first (6.5) */
    R[MR_BOOT] = 1;
    transfer(image_streams(0, TILE_BROADCAST, &boot_desc[MD_H_TILES]));
    ingress_egress->ARRIVE_EXPECT_0 = ~0u;
    ingress_egress->ARRIVE_EXPECT_1 = ~0u;
    barrier();                                      /* the ROMs arrived, the load landed: they jump */
    R[MR_BOOT] = 2;
    barrier();                                      /* the segment's first barrier: each tile's program, nothing armed */
    R[MR_BOOT] = 3;                                 /* the tiles arm for their records, and say so to the tracer tile */
}

/* ---------------------------------------------------------------- a frame's words */
static void packet_wait(void) { while (!avail) work(); }
/* n words of the packet at the front to an address of the master's own, landed. Not while the pump runs. */
static void take(uint32_t n, uint32_t address) {
    link_read(n, address);
    while (!link_read_done()) idle();
    frame_left -= n;
}
/* The frame's next n words to the scratchpad from `word` on, or, with word 0, to scrap. */
static void route(uint32_t n, uint32_t word) {
    while (n) {
        packet_wait();
        uint32_t c = min32(n, avail);
        if (!word) c = min32(c, ML_SCRAP_ROWS);
        take(c, SCRATCHPAD_ADDRESS(word ? word : ML_SCRAP));
        if (word) word += 4 * c;
        n -= c;
    }
}
/* The parser's words: a section of the frame, `rows` 128-bit words, as 32-bit words, a packet at a time. */
static uint32_t parse_left, parse_words, parse_at, parse_type;
static void parse_begin(uint32_t type, uint32_t rows) { parse_type = type; parse_left = rows; parse_words = parse_at = 0; }
static uint32_t next32(void) {
    if (parse_at == parse_words) {
        if (!parse_left) bad_frame(parse_type, MR_D_SHORT);
        packet_wait();
        uint32_t n = min32(min32(avail, parse_left), PARSE_ROWS);
        take(n, (uint32_t)buffer);
        parse_left -= n; parse_words = 4 * n; parse_at = 0;
    }
    return buffer[parse_at++];
}
/* A string (HOST-FRAMES.md): its length in bytes, then the bytes, padded to 32-bit words. v1.37 keeps no names. */
static uint32_t skip_string(void) {
    uint32_t bytes = next32();
    for (uint32_t words = (bytes + 3) >> 2; words; words--) (void)next32();
    return bytes;
}
static void parse_end(void) { if (parse_left) bad_frame(parse_type, MR_D_LONG); }
/* A file's depth from its length in pages: the smallest d >= 1 with 1024^d >= pages (gold gives every file an index level). */
static uint32_t depth_of(uint32_t pages) { return pages <= 1024 ? 1 : pages <= (1u << 20) ? 2 : 3; }

/* ---------------------------------------------------------------- the Updater's runs */
static uint32_t run_config(uint32_t lru, uint32_t carry_on, uint32_t tracer_copy, uint32_t depth, uint32_t root) {
    return (lru ? UPD_RUN_CONFIG_LRU : 0) | (carry_on ? UPD_RUN_CONFIG_CONTINUE : 0) | (tracer_copy ? UPD_RUN_CONFIG_TRACER_COPY : 0) |
           UPD_RUN_CONFIG_DEPTH(depth) | UPD_RUN_CONFIG_ROOT(root);
}
static void run_start(uint32_t config, uint32_t words, uint32_t pages, uint32_t image, uint32_t tracer_root, uint32_t source) {
    if (runs == MR_MAX_RUNS) finish(MR_ROOM, 1);
    updater->RUN_CONFIG = config; updater->WORD_COUNT = words; updater->PAGE_COUNT = pages;
    updater->IMAGE_ROOT = image; updater->TRACER_ROOT = tracer_root; updater->SOURCE_ROOT = source;
    upd_done = 0;
    pages_due += pages;
    R[MR_RUN_START + runs] = mcycle();
    updater->START = 1;
}
static void run_wait(void) {
    while (!upd_done) work();
    R[MR_PAGE_COUNT + runs] = updater->PAGE_COUNT;  /* the pages not used: 0 with the frames' counts */
    R[MR_RUN_DONE + runs] = mcycle();
    R[MR_RUNS] = ++runs;
}
/* A frame of update words as one run: its data to the segment queue as the allowance lets it, to the run's done. */
static void update_frame(uint32_t config, uint32_t words, uint32_t pages, uint32_t image, uint32_t source) {
    pump_left = frame_left; frame_left = 0;
    run_start(config, words, pages, image, 0, source);
    run_wait();
    while (pump_left || pump_busy) work();
}

/* ---------------------------------------------------------------- the hand-over */
static void hand_over(void) {
    /* record s's word 66 (17.9.2; frame 7 leaves the roots and depths 0): the Tracer's copy's root [30:0] and depth
     * [32:31], the table's root [63:33] and depth [65:64] */
    uint32_t w0 = root_tracer | (image_depth & 1) << 31;
    uint32_t w1 = (image_depth >> 1 & 1) | root_table << 1;
    uint32_t w2 = (files_word[2] & ~3u) | table_depth, w3 = files_word[3];
    tile_seen &= ~EVENT_COUNTER_DONE(CTR_SELF);
    *counter(CTR_SELF) = 3;
    write_to(SELF(CTR_SELF, SIZE_128), SCRATCHPAD_ADDRESS(ML_RECORD + 4 * 66));
    write128(w3, w2, w1, w0);
    write_to(SELF(CTR_SELF, SIZE_128), SCRATCHPAD_ADDRESS(ML_HEADER));
    write128(advice_values, table_k, table_p3, table_p1);   /* HASH_MUL, HASH_STEP, HASH_LINES; ADVICE_N */
    write128(0, 0, 0, 0);
    wait_counter(CTR_SELF);
    /* the tracer tile's load: its segment, then its image, then the COUNT */
    const uint32_t d = TILE_TRACER;
    unsigned i = 0;
    stream(i++, ML_RECORD, SCRATCHPAD_ADDRESS(boot_desc[MD_H_TR_RECORD]), RECORD_WORDS, SIZE_128, 0, MODE_WRITE, d);
    if (S + 1 < N) stream(i++, ML_EXPECT, SCRATCHPAD_ADDRESS(boot_desc[MD_H_TR_EXPECT]), RECORD_WORDS, SIZE_128, 0, MODE_WRITE, d);
    else stream(i++, ML_HEADER + 4, SCRATCHPAD_ADDRESS(TL_H_EXPECT), 1, SIZE_32, 0, MODE_WRITE, d);   /* the last segment: no end state to check */
    stream(i++, ML_ADVICE, SCRATCHPAD_ADDRESS(boot_desc[MD_H_TR_ADVICE]), advice_rows, SIZE_128, 0, MODE_WRITE, d);
    stream(i++, ML_HEADER, SCRATCHPAD_ADDRESS(TL_H_HASH_MUL), 3, SIZE_32, 0, MODE_WRITE, d);
    stream(i++, ML_HEADER + 3, SCRATCHPAD_ADDRESS(TL_H_ADVICE_N), 1, SIZE_32, 0, MODE_WRITE, d);
    R[MR_HANDED] = mcycle(); R[MR_HANDED_WAKES] = wakes; R[MR_HANDED_READS] = reads;
    transfer(image_streams(i, d, &boot_desc[MD_H_TRACER]));
    R[MR_BOOT] = 4;
}

/* ---------------------------------------------------------------- the frames */
static void frame(void) {
    packet_wait();
    frame_left = 1;
    take(1, (uint32_t)frame_header);                /* the frame's header word (HOST-FRAMES.md, "Every frame") */
    const uint32_t type = frame_header[2], length = frame_header[3], compression = FRAME_COMPRESSION(frame_header[0]);
    const uint32_t lru = compression == 1;
    if (frames == MR_MAX_FRAMES) finish(MR_ROOM, 2);
    R[MR_FRAME_TYPE + frames] = type; R[MR_FRAME_START + frames] = mcycle();
    R[MR_FRAME_WAKES + frames] = wakes; R[MR_FRAME_READS + frames] = reads;
    R[MR_FRAMES] = ++frames;
    if (compression > 1) bad_frame(type, MR_D_TYPE);
    frame_left = length;
    frame_open = 1;
    if (length) set_threshold(min32(credit_threshold, length));   /* wake me when a credit's worth is there */
    switch (type) {
    case FRAME_PROGRAM_HEADER: {
        parse_begin(type, length);
        (void)skip_string();                        /* the program's stem */
        image_depth = depth_of(next32());           /* the file's length in pages: every image's depth */
        R[MR_DEPTHS] = image_depth | table_depth << 8;
        program_words64 = next32(); program_words128 = next32(); program_pages = next32();
        parse_end();
        header_seen = FRAME_PROGRAM_HEADER;
        break;
    }
    case FRAME_PROGRAM_DATA:
        if (header_seen != FRAME_PROGRAM_HEADER) bad_frame(type, MR_D_ORDER);
        if (length != program_words128) bad_frame(type, MR_D_LENGTH);
        root_program = new_root();
        R[MR_ROOT_PROGRAM] = root_program;
        update_frame(run_config(lru, 0, 0, image_depth, UPD_ROOT_EMPTY), program_words64, program_pages, root_program, 0);
        header_seen = 0;
        break;
    case FRAME_PROOF_HEADER: {
        parse_begin(type, length);
        if (next32() != 1) bad_frame(type, MR_D_VERSION);
        (void)skip_string();                        /* the program's stem */
        (void)skip_string();                        /* the working directory */
        for (uint32_t fields = next32(); fields; fields--) { (void)next32(); (void)next32(); }   /* the proof's parameters, 64 bits each */
        data_words64 = next32(); data_words128 = next32(); data_pages = next32();
        words_before = words_mine = words_after = 0;
        for (uint32_t k = 0; k < N; k++) {          /* each segment's update words: before s, s, after s */
            uint32_t words = next32();
            if (k < S) words_before += words; else if (k == S) words_mine = words; else words_after += words;
        }
        segments_words128 = next32();
        for (uint32_t k = 0; k < N; k++) (void)next32();   /* the pages each segment writes */
        for (uint32_t k = 0; k < N; k++) {          /* each ASIC's pages: its start image's, its final image's, its Tracer's copy's */
            uint32_t a = next32(), b = next32(), c = next32();
            if (k == S) { pages_start = a; pages_final = b; pages_tracer = c; }
        }
        parse_end();
        if (pages_final < pages_tracer) bad_frame(type, MR_D_COUNTS);
        have_proof = 1;
        break;
    }
    case FRAME_TABLE_HEADER: {
        parse_begin(type, length);
        table_header.is_base = skip_string() != 0;  /* a path: a base table, sent on its own */
        table_header.from_base = skip_string() != 0 && !table_header.is_base;   /* a base to add to: the last one established */
        table_header.k = next32(); table_header.p1 = next32(); table_header.p3 = next32();
        table_header.words64 = next32(); table_header.words128 = next32(); table_header.pages = next32();
        parse_end();
        header_seen = FRAME_TABLE_HEADER;
        break;
    }
    case FRAME_TABLE_DATA: {
        if (header_seen != FRAME_TABLE_HEADER || (table_header.from_base && !have_base)) bad_frame(type, MR_D_ORDER);
        if (length != table_header.words128) bad_frame(type, MR_D_LENGTH);
        const uint32_t k = table_header.k, pages = k > 5 ? 1u << (k - 5) : 1;   /* 2^k lines of 128 bytes, in pages */
        const uint32_t depth = depth_of(pages), root = new_root();
        update_frame(run_config(lru, 0, 0, depth, table_header.from_base ? UPD_ROOT_COPY : UPD_ROOT_EMPTY), table_header.words64,
                     table_header.pages, root, table_header.from_base ? root_base : 0);
        if (table_header.is_base) { root_base = root; have_base = 1; R[MR_ROOT_BASE] = root; }
        else {
            root_table = root; table_depth = depth; table_k = k; table_p1 = table_header.p1; table_p3 = table_header.p3;
            R[MR_ROOT_TABLE] = root; R[MR_DEPTHS] = image_depth | table_depth << 8;
        }
        header_seen = 0;
        break;
    }
    case FRAME_DATA_IMAGE:
        if (!have_proof || !root_program) bad_frame(type, MR_D_ORDER);
        if (length != data_words128) bad_frame(type, MR_D_LENGTH);
        root_cycle0 = new_root();
        R[MR_ROOT_CYCLE0] = root_cycle0;
        update_frame(run_config(lru, 0, 0, image_depth, UPD_ROOT_COPY), data_words64, data_pages, root_cycle0, root_program);
        break;
    case FRAME_REGISTERS:
        if (length != RECORD_WORDS * N) bad_frame(type, MR_D_LENGTH);
        /* N records in segment order: those before s to scrap in one run of words; record s, its word 66 to DMEM (the
         * hand-over completes it); record s + 1 as the expected end state; the rest to scrap */
        route(RECORD_WORDS * S, 0);
        route(66, ML_RECORD);
        packet_wait();
        take(1, (uint32_t)files_word);
        route(1, ML_RECORD + 4 * 67);
        if (S + 1 < N) route(RECORD_WORDS, ML_EXPECT);
        route(frame_left, 0);
        break;
    case FRAME_ADVICE: {
        uint32_t before = 0;
        parse_begin(type, N >> 2);                  /* N counts of 32 bits */
        for (uint32_t k = 0; k < N; k++) {
            uint32_t values = next32(), rows = (values + 1) >> 1;   /* a log: its count rounded up to even, halved */
            if (k < S) before += rows; else if (k == S) { advice_values = values; advice_rows = rows; }
        }
        parse_end();
        if (advice_rows > ML_ADVICE_ROWS || advice_rows > boot_desc[MD_H_TR_ADVICE_ROWS]) finish(MR_ROOM, 3);
        route(before, 0);
        route(advice_rows, ML_ADVICE);
        route(frame_left, 0);
        break;
    }
    case FRAME_SEGMENTS: {
        if (!have_proof || !root_cycle0 || !root_table) bad_frame(type, MR_D_ORDER);
        if (length != segments_words128) bad_frame(type, MR_D_LENGTH);
        pump_left = frame_left; frame_left = 0;     /* one stream for the three runs */
        const uint32_t start = new_root();
        R[MR_ROOT_START] = start;
        run_start(run_config(lru, 0, 0, image_depth, UPD_ROOT_COPY), words_before, pages_start, start, 0, root_cycle0);
        run_wait();                                 /* the start image is frozen */
        const uint32_t final = new_root(), copy = new_root();
        root_tracer = copy;
        R[MR_ROOT_FINAL] = final; R[MR_ROOT_TRACER] = copy;
        run_start(run_config(lru, 1, 1, image_depth, UPD_ROOT_COPY), words_mine, 2 * pages_tracer, final, copy, start);
        run_wait();                                 /* the Tracer's copy is complete */
        run_start(run_config(lru, 1, 0, image_depth, UPD_ROOT_EXISTS), words_after, pages_final - pages_tracer, final, 0, 0);
        hand_over();                                /* tracing begins, while the stream goes on */
        barrier();                                  /* every tile's window has landed: the replay */
        R[MR_BOOT] = 5;
        while (R[MR_RESULTS] < TILES_LOADING || R[MR_LOADS] < 2 * TILES_LOADING) work();   /* each result, and the LOAD after it */
        run_wait();
        while (pump_left || pump_busy) work();
        break;
    }
    default: bad_frame(type, MR_D_TYPE);
    }
    if (frame_left) bad_frame(type, MR_D_LONG);
    frame_open = 0;
    set_threshold(1);                               /* between frames: the next header word */
    if (type == FRAME_SEGMENTS) {                   /* the proof's last frame */
        map_flush();
        finish(MR_OK, 0);
    }
}

int main(void) {
    __asm__ volatile ("csrw mtvec, %0" :: "r"(master_trap));
    tile->EVENTS_MASK = TILE_MASK;
    host_link->EVENTS_MASK = LINK_MASK;
    updater->EVENTS_MASK = UPD_MASK;
    N = system_config[MS_H_ASICS]; S = system_config[MS_H_SEGMENT];
    credit_threshold = system_config[MS_H_THRESHOLD] ? system_config[MS_H_THRESHOLD] : UPD_CREDIT;
    threshold = 1;                                  /* BACKGROUND_THRESHOLD's reset value */
    if (boot_desc[MD_H_MAGIC] != MD_MAGIC || system_config[MS_H_MAGIC] != MS_MAGIC || (N != 64 && N != 128 && N != 256) || S >= N ||
        credit_threshold > HL_BUFFER_WORDS)
        finish(MR_CONFIG, 0);
    boot();
    map_format();
    for (;;) frame();
}
