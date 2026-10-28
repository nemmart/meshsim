// build: -O2
/* Search steps through the controller read port (spec 4.3), the loop as the spec gives it: B lower-bound searches over
 * this tile's sorted run of N 32-bit keys at chunk KEYS; each step queues one read per active search
 * (SRAM_RD_ADDR_QUEUE = its midpoint), waits with DMA_REPLY_REMAINING and WFI, then compares and updates; converged searches
 * (lo == hi) drop out (design v1's early exit), so a step's reads scale with the searches still active.
 * Variant 0: active searches by index (act[]), state in separate arrays; variant 1: the active searches' state kept
 * packed (lo, hi, target moved together when one drops out). Parameters (N, targets[64]) arrive by block copy from big
 * SRAM at PARAMS. ebreak marks each step's start and the end (a simulator-only hook records the cycle); nact[] holds
 * each step's active count.
 *   CMD 1 (argument: B | variant << 8): run;  CMD 2: exit */
#include "mesh.h"
#define PARAMS 0xF0000
#define KEYS 0
#define MAXB 64
struct params { uint32_t n, pad, target[MAXB]; };
static struct params P;
volatile uint32_t results[MAXB], res[MAXB], chk[64], nact[64];
static unsigned runs;
static inline void mark(void) { __asm__ volatile ("ebreak" ::: "memory"); }

static void search0(unsigned B) {
    uint32_t lo[MAXB], hi[MAXB]; uint8_t act[MAXB];
    const uint32_t rw = CM_WORD((uint32_t)&results[0]);
    for (unsigned i = 0; i < B; i++) { lo[i] = 0; hi[i] = P.n; act[i] = i; }
    unsigned na = B, step = 0;
    while (SRAM_RD_ADDR_QUEUE < READ_PORT_DEPTH) ;
    while (na) {
        mark(); nact[step++] = na;
        DMA_REPLY_REMAINING = na;
        SRAM_RD_DEST_ADDR = rw;
        for (unsigned j = 0; j < na; j++) { unsigned k = act[j]; SRAM_RD_ADDR_QUEUE = KEYS + ((lo[k] + hi[k]) >> 1); }
        wfi();
        unsigned m = 0;
        for (unsigned j = 0; j < na; j++) {
            unsigned k = act[j]; uint32_t mid = (lo[k] + hi[k]) >> 1;
            if (results[j] < P.target[k]) lo[k] = mid + 1; else hi[k] = mid;
            if (lo[k] < hi[k]) act[m++] = k;
        }
        na = m;
    }
    mark();
    uint32_t c = 0;
    for (unsigned i = 0; i < B; i++) { res[i] = lo[i]; c += lo[i] * (i + 1); }
    chk[runs++ & 63] = c;
}

struct st { uint32_t lo, hi, t, k; };
static void search1(unsigned B) {
    struct st s[MAXB];
    uint32_t out[MAXB];
    const uint32_t rw = CM_WORD((uint32_t)&results[0]);
    for (unsigned i = 0; i < B; i++) { s[i].lo = 0; s[i].hi = P.n; s[i].t = P.target[i]; s[i].k = i; }
    unsigned na = B, step = 0;
    while (SRAM_RD_ADDR_QUEUE < READ_PORT_DEPTH) ;
    while (na) {
        mark(); nact[step++] = na;
        DMA_REPLY_REMAINING = na;
        SRAM_RD_DEST_ADDR = rw;
        for (struct st *p = s, *e = s + na; p < e; p++) SRAM_RD_ADDR_QUEUE = KEYS + ((p->lo + p->hi) >> 1);
        wfi();
        struct st *q = s; const volatile uint32_t *r = results;
        for (struct st *p = s, *e = s + na; p < e; p++, r++) {
            uint32_t lo = p->lo, hi = p->hi, mid = (lo + hi) >> 1;
            if (*r < p->t) lo = mid + 1; else hi = mid;
            if (lo < hi) { q->lo = lo; q->hi = hi; q->t = p->t; q->k = p->k; q++; } else out[p->k] = lo;
        }
        na = q - s;
    }
    mark();
    uint32_t c = 0;
    for (unsigned i = 0; i < B; i++) { res[i] = out[i]; c += out[i] * (i + 1); }
    chk[runs++ & 63] = c;
}

int main(void) {
    set_stream(255, STREAM_LO(PARAMS, CM_WORD((uint32_t)&P)), STREAM_HI(sizeof P / 8, 0, MODE_CMEM, SIZE_64, 0));
    DMA_CM_REMAINING = sizeof P / 8;
    DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 255, 1);
    DMA_PULSE = DMA_PULSE_START(1);
    wfi();
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) return 0;
        unsigned B = CMD_ARG(w) & 0xFF;
        if ((CMD_ARG(w) >> 8) & 1) search1(B); else search0(B);
    }
}
