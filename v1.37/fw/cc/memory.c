/* memory.c: the memory tile's program (v1.36; v1.35 had no memory tile: its C&C 2, cc23.c, popped its requests and
 * served none). Written for v1.36, in v1.36's names (include/mesh.h). It serves the memory tile's requests (11.5), READ and WRITE, with the RW
 * engine, its peripheral 2 (17.7; v1.37: the memory system is its peripheral 1, 17.11), one request at a time, in the order they arrive.
 *
 * It is placed in the memory tile's IMEM and DMEM before the reset ends, and the boot ROM starts it at _start
 * (crt0.S; 6.5, PARAMS.boot_local 1). mtvec stays at the boot ROM's handler, so a trap sends the master tile reports
 * 24 and 25 and parks (6.2). The program sleeps until a request is pending (EVENTS.request_pending, 11.1), pops it
 * and serves it to its end before it pops the next:
 *
 *   READ    a read job of the request's beats from its LPDDR beat address into the RW engine's output queue 0, and
 *           its DMA's transfer of them to the requester, 128-bit WRITEs from the request's address up, counted on its
 *           data counter (17.7), a packet a beat; both started together, the DMA draining the queue as the job fills
 *           it. Then, with a reply counter, the reply: a 128-bit WRITE of 0 to the reply address, from output queue 1
 *           (11.4, 11.5).
 *   WRITE   a part of the write buffer, its region 0x05, for the request's beats: the whole buffer from its offset
 *           0, since one request is served at a time. COUNTER_1 armed with the beats, then the 128-bit reply: the
 *           part's address in [31:0], the counter, 1, in [34:32], and the bits above 0 (11.5). Once COUNTER_1 has
 *           drained, the requester's beats have landed in the part (12.3), and a write job copies them to LPDDR; the
 *           request is served when LPDDR has answered its last write (17.7). So a request served after it, a READ
 *           among them, sees what it wrote.
 *   others  undefined (11.5): popped and ignored.
 *
 * Every wait sleeps in WFI on its event (7.2, 17.2): the tile's request_pending and counter_1_done, and the RW
 * engine's done and dma_done through the tile's peripheral_2, each re-checked after every wake. The program has no
 * data. */
#include "mesh.h"

/* The output queues (17.7): 0 the read's beats, 1 the replies. */
#define QUEUE_BEATS     0
#define QUEUE_REPLY     1
/* The counter that counts a WRITE request's beats into the buffer. */
#define BUFFER_COUNTER  1

/* Sleeps until any event of `events`, the tile's, is pending, and returns EVENTS_PEEK: nothing is consumed. */
static uint32_t sleep_until(uint32_t events) {
    tile->EVENTS_MASK = events;
    for (;;) {
        uint32_t pending = tile->EVENTS_PEEK;
        if (pending & events) return pending;
        __asm__ volatile ("wfi" ::: "memory");
    }
}

/* Sleeps until the RW engine has posted every event of `events` (its EVENTS_MASK holds both of its standard ones, so
 * the tile's peripheral_2 wakes the core), consuming each as its load of EVENTS returns it (17.2). */
static void wait_rw(uint32_t events) {
    uint32_t seen = 0;
    while ((seen & events) != events) {
        sleep_until(EVENT_PERIPHERAL(2));
        seen |= rw_engine->EVENTS;
    }
}

/* A DMA transfer from output queue `queue`: `packets` 128-bit packets to `dst`'s `address`, counted on
 * `counter_number` (17.4). */
static void start_dma(uint32_t queue, uint32_t counter_number, uint32_t dst, uint32_t address, uint32_t packets) {
    rw_engine->DMA_HDR = HEADER(MODE_WRITE, counter_number, 0) | HDR_DST(dst) | DMA_HDR_COUNT(packets) | DMA_HDR_QUEUE(queue);
    rw_engine->DMA_DEST_ADDR = address;
    rw_engine->DMA_START = 1;
}

/* A request's fields (11.5). */
struct request {
    uint32_t requester, beats, data_counter, lpddr, data_address, reply_address, reply_counter;
};

/* READ: the job and its DMA's transfer, then the reply, if the request asks for one. */
static void serve_read(const struct request *r) {
    rw_engine->RW_CONFIG = RW_CONFIG_KIND(RW_KIND_READ) | RW_CONFIG_BEATS(r->beats);
    rw_engine->RW_LPDDR_ADDR = r->lpddr;
    rw_engine->START = 1;
    start_dma(QUEUE_BEATS, r->data_counter, r->requester, r->data_address, r->beats);
    wait_rw(P_EVENT_DONE | P_EVENT_DMA_DONE);
    if (r->reply_counter != 0) {
        /* the value 0: RW_REPLY_W0 is 0, as every send leaves it, and the send's word 0 */
        rw_engine->RW_REPLY_SEND = 0;
        start_dma(QUEUE_REPLY, r->reply_counter, r->requester, r->reply_address, 1);
        wait_rw(P_EVENT_DMA_DONE);
    }
}

/* WRITE: the part armed, the reply, the beats' landing, then the job. */
static void serve_write(const struct request *r) {
    if (r->beats != 0) *counter(BUFFER_COUNTER) = r->beats;
    rw_engine->RW_REPLY_W0 = RW_BUFFER;
    rw_engine->RW_REPLY_SEND = BUFFER_COUNTER;
    start_dma(QUEUE_REPLY, r->reply_counter, r->requester, r->reply_address, 1);
    wait_rw(P_EVENT_DMA_DONE);
    if (r->beats == 0) return;
    /* the counter's drain: the beats are in the part; the load of EVENTS consumes counter_1_done */
    sleep_until(EVENT_COUNTER_DONE(BUFFER_COUNTER));
    (void)tile->EVENTS;
    rw_engine->RW_CONFIG = RW_CONFIG_KIND(RW_KIND_WRITE) | RW_CONFIG_BEATS(r->beats);
    rw_engine->RW_LPDDR_ADDR = r->lpddr;
    rw_engine->RW_BUFFER_ADDR = 0;
    rw_engine->START = 1;
    wait_rw(P_EVENT_DONE);
}

int main(void) {
    rw_engine->EVENTS_MASK = P_EVENT_DONE | P_EVENT_DMA_DONE;
    for (;;) {
        uint32_t words[4];
        sleep_until(EVENT_REQUEST_PENDING);
        words[0] = tile->REQUEST_IN_W0;          /* the oldest request (11.1), popped by REQUEST_RECEIVE */
        words[1] = tile->REQUEST_IN_W1;
        words[2] = tile->REQUEST_IN_W2;
        words[3] = tile->REQUEST_RECEIVE;
        struct request r = {
            .requester = REQUEST_REQUESTER(words[0]),
            .beats = MEMORY_BEATS_OF(words[0]),
            .data_counter = MEMORY_DATA_COUNTER_OF(words[0]),
            .lpddr = words[1],
            .data_address = words[2],
            .reply_address = MEMORY_W3_REPLY_ADDR_OF(words[3]),
            .reply_counter = MEMORY_W3_REPLY_COUNTER_OF(words[3]),
        };
        switch (REQUEST_OP_OF(words[0])) {
            case OP_READ: serve_read(&r); break;
            case OP_WRITE: serve_write(&r); break;
            default: break;
        }
    }
}
