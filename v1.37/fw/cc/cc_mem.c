/* cc_mem.c: a test variant of the master tile's program (v1.36; as cc_rcp.c and cc_bad.c were C&C 1's in v1.35): the
 * memory run, the master's requests to the memory tile (11.5), whose program, memory.c, serves them with the RW engine
 * (17.7). v1.35 had no memory tile; this is written for v1.36, in v1.36's names (include/mesh.h). The test
 * (tests/test_memory.cpp) places it in the master's IMEM and DMEM, and memory.c in the memory tile's, before the reset
 * ends (boot_local, 6.5), and plays LPDDR on the RW engine's lpddr port. The compute tiles' LOADs stay in the master's
 * receive queue, unanswered.
 *
 *  1. A READ of READ_BEATS beats of LPDDR from beat address READ_LPDDR into mem_summary.read, its data on counter 1,
 *     its reply, a 128-bit 0, into mem_summary.replies[0] on counter 2, both armed first (11.4).
 *  2. A WRITE of WRITE_BEATS beats to WRITE_LPDDR: its reply on counter 3, the part's address and the memory tile's
 *     counter (11.5), into mem_summary.part; then the beats, 128-bit WRITEs into the part on that counter, word w of
 *     beat b 0x57000000 | b << 8 | w.
 *  3. A READ of those beats back, as 1, into mem_summary.read_back and mem_summary.replies[1].
 *  4. CREDIT_REQUESTS WRITEs of 0 beats, more than the memory target's receive queue holds (11.3), MEMORY_LIMIT
 *     outstanding at a time, the per-tile limit, each answered by its reply alone into a slot of its own: the request
 *     tree's root has a credit for each only if the memory tile returns one as it pops each request.
 * Each READ and WRITE crosses a 4 KiB boundary of LPDDR, so that the RW engine splits it into bursts. Every wait sleeps
 * in WFI on its counters, with Ingress/Egress's watchdog set (17.6): an answer that never comes ends the run with
 * mem_summary.failure NO_ANSWER. Then mem_summary.done is DONE, written last, and the master parks. Every reply slot is
 * a 128-bit reply's: 16 bytes, 16-byte aligned. */
#include "mesh.h"

#define DONE            0x444F4E45u   /* "DONE" */
#define NONE            0xFFFFFFFFu
#define KIND_MEMORY     1u
#define NO_ANSWER       7u
#define READ_BEATS      20
#define WRITE_BEATS     24
#define READ_LPDDR      0x0FF8u       /* beat addresses: 0xFF80, 8 beats below 64 KiB */
#define WRITE_LPDDR     0x20F4u       /* 0x20F40, 12 beats below 0x21000 */
#define CREDIT_REQUESTS 300           /* more than the memory target's 256 receive queue entries at 8 x 8 (11.3) */
#define MEMORY_LIMIT    3
#define ANSWER_LIMIT    (1u << 16)    /* the watchdog's limit for each answer, in cycles */
#define MEMORY_TILE     (TILE_COUNT + 1)

/* What the test reads (its words by index: done, failure, a spare word, the kind, then the answers). In .bss, zeroed
 * by crt0.S; main sets what is not 0 at the start. */
struct summary {
    uint32_t done, failure, spare, kind;
    uint32_t part[4];                       /* the WRITE's reply: the part's address [31:0], the counter [34:32] */
    uint32_t replies[2][4];                 /* the READs' replies */
    uint32_t read[READ_BEATS][4];           /* the first READ's beats */
    uint32_t read_back[WRITE_BEATS][4];     /* the WRITE's beats, read back */
    uint32_t answers[CREDIT_REQUESTS][4];   /* each credits' request's reply */
};
volatile struct summary mem_summary __attribute__((aligned(16)));

/* Sleeps until every event of `events`, the tile's, has been seen, each consumed as a load of EVENTS returns it, or
 * the watchdog has expired: 1, or 0. */
static int wait_events(uint32_t events) {
    ingress_egress->EVENTS_MASK = IE_EVENT_WATCHDOG;
    ingress_egress->WATCHDOG = ANSWER_LIMIT;
    tile->EVENTS_MASK = events | EVENT_PERIPHERAL(1);
    uint32_t seen = 0;
    for (;;) {
        seen |= tile->EVENTS & events;
        if (seen == events) { ingress_egress->WATCHDOG = 0; return 1; }
        if (ingress_egress->EVENTS_PEEK & IE_EVENT_WATCHDOG) return 0;
        __asm__ volatile ("wfi" ::: "memory");
    }
}
static __attribute__((noreturn)) void finish(uint32_t failure) {
    mem_summary.failure = failure;
    mem_summary.done = DONE;
    __asm__ volatile ("csrw mie, zero" ::: "memory");
    for (;;) __asm__ volatile ("wfi" ::: "memory");
}
static uint32_t address_of(volatile const void *p) { return (uint32_t)p; }

/* A READ (11.5): `beats` from LPDDR beat address `lpddr` into DMEM at `data` on counter 1, its reply at `reply` on
 * counter 2, both armed first; then the wait for both. */
static void read_request(uint32_t lpddr, uint32_t beats, uint32_t data, uint32_t reply) {
    *counter(1) = beats;
    *counter(2) = 1;
    request_send(MEMORY_W3_REPLY_ADDR(reply) | MEMORY_W3_REPLY_COUNTER(2), data, lpddr,
                 REQUEST_WORD_0(TARGET_MEMORY, OP_READ) | MEMORY_BEATS(beats) | MEMORY_DATA_COUNTER(1));
    if (!wait_events(EVENT_COUNTER_DONE(1) | EVENT_COUNTER_DONE(2))) finish(NO_ANSWER);
}
/* A WRITE (11.5) of `beats` to LPDDR beat address `lpddr`, its reply at `reply` on counter 3, armed by the caller. */
static void write_request(uint32_t lpddr, uint32_t beats, uint32_t reply) {
    request_send(MEMORY_W3_REPLY_ADDR(reply) | MEMORY_W3_REPLY_COUNTER(3), 0, lpddr,
                 REQUEST_WORD_0(TARGET_MEMORY, OP_WRITE) | MEMORY_BEATS(beats));
}

int main(void) {
    mem_summary.kind = KIND_MEMORY;
    for (unsigned w = 0; w < 4; w++) { mem_summary.part[w] = NONE; mem_summary.replies[0][w] = NONE; mem_summary.replies[1][w] = NONE; }
    for (unsigned r = 0; r < CREDIT_REQUESTS; r++) for (unsigned w = 0; w < 4; w++) mem_summary.answers[r][w] = NONE;
    read_request(READ_LPDDR, READ_BEATS, address_of(mem_summary.read), address_of(mem_summary.replies[0]));
    *counter(3) = 1;
    write_request(WRITE_LPDDR, WRITE_BEATS, address_of(mem_summary.part));
    if (!wait_events(EVENT_COUNTER_DONE(3))) finish(NO_ANSWER);
    write_to(HEADER(MODE_WRITE, mem_summary.part[1], SIZE_128) | HDR_DST(MEMORY_TILE), mem_summary.part[0]);
    for (uint32_t beat = 0; beat < WRITE_BEATS; beat++) {
        uint32_t word = 0x57000000u | beat << 8;
        write128(word | 3, word | 2, word | 1, word);
    }
    read_request(WRITE_LPDDR, WRITE_BEATS, address_of(mem_summary.read_back), address_of(mem_summary.replies[1]));
    for (unsigned first = 0; first < CREDIT_REQUESTS; first += MEMORY_LIMIT) {
        unsigned n = CREDIT_REQUESTS - first < MEMORY_LIMIT ? CREDIT_REQUESTS - first : MEMORY_LIMIT;
        *counter(3) = n;
        for (unsigned r = first; r < first + n; r++) write_request(0, 0, address_of(mem_summary.answers[r]));
        if (!wait_events(EVENT_COUNTER_DONE(3))) finish(NO_ANSWER);
    }
    finish(0);
}
