/* C&C 3's firmware for the Tracer (meshsim B1, WORKER22; spec 4.21.3's controller loop, 4.21.6's drain, 4.17.9's DMA).
 *
 * One segment: everything starts in C&C 3's big SRAM as tracer_layout.h lays it out, landed by the harness standing in for
 * C&C 1 and C&C 2 (6.2, section 22: the update stream applied, the Tracer's copy and the map made). The program:
 *   1. sends C&C 1 its "started" request (as cc23.c), copies the header and the template index into data memory, and
 *      hashes the index by key(w);
 *   2. loads the decode table and the ROM into the Tracer (one 768-word run into its region at 0x1000; it is stopped),
 *      writes HASH_MUL, HASH_STEP, HASH_LINES and RUN_CONFIG, and loads the record (68 words, word 0 first);
 *   3. Q3: sends window 0's start (record words 0 and 2-66) to tile FIRST from its own copy of the record;
 *   4. runs WINDOWS windows: PULSE_RUN, then 4.21.3's loop on the Tracer's events. New expansion: EXPANSION_TEMPLATE for
 *      an inline (item 145: before the first word), then the template from EXPANSION_OFFSET on the expansion credits (the
 *      N3 rules: a NOP policy with rd = x0 sends the single NOP word carrying rd0; a mid-sequence start's first word
 *      carries rd0). The advice credit: the next half queue of advice. The service requests, one DMA transfer at a time:
 *      output queue 0 in four transfers, word 0 and words 2-66 to tile n + 1's record, word 1 and word 67 to tile n's;
 *      at the last window all 68 words to C&C 3's own memory (dst 66, Q11) and then words 1 and 67 forwarded to the last
 *      tile (Q4); queues 1-3 by their credits, 128 words a chunk and exactly the remainder last (words 37-39), each into
 *      tile n's stream area. Run done: the next window;
 *   5. checks the advice left (the last record's word 1 [124:102]) is 0 and the end state (words 0, 2-65) equals the
 *      expected one (the next segment's frame 2, if given), and returns its status (crt0: the simulator's exit word).
 * Q14: an expansion being fed when a segment's record is loaded is dropped (one segment per run here: nothing is pending).
 *
 * On C&C 3 a stream's d below 64 is relative to tile 2 (4.2: a controller stands for its tile), so the program's own
 * memories and region are reached with d = 66, its id, and tile t with d = (t - 2) mod 64.
 */
#include <stdint.h>
#include "mesh.h"
#include "tracer_layout.h"
#include "../replay/replay_layout.h"

uint32_t tr_results[16];

static uint32_t hdr[TL_H_WORDS] __attribute__((aligned(16)));
static uint32_t ix[128 * 4] __attribute__((aligned(16)));
static uint8_t  hashtab[256];
static uint32_t rec[68 * 4] __attribute__((aligned(16)));
static uint32_t expect_rec[66 * 4] __attribute__((aligned(16)));
static uint32_t nopword[4] __attribute__((aligned(16)));
static uint32_t scratch[4] __attribute__((aligned(16)));
static uint32_t me, tmask;

static inline uint32_t min32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline uint32_t sram_addr(uint32_t chunk) { return 0x03000000u | (chunk << 2); }
static inline uint32_t rel(uint32_t t) { return (t - 2u) & 63u; }            /* d for tile t from C&C 3 (4.2) */

/* key(w), the normalized template-index key (wip/tables/README.md; replay_ctl.c's tkey) */
static inline uint32_t tkey(uint32_t w) {
    uint32_t op = (w | 1u) & 0x7Fu;
    if (op == 0x0Bu) return w & ~(31u << 7 | 31u << 15 | 31u << 20);
    if (op == 0x73u) {
        uint32_t f3 = (w >> 12) & 7u;
        if (f3 == 1u || f3 == 2u) {
            uint32_t rd = (w >> 7) & 31u, rs1 = (w >> 15) & 31u;
            uint32_t nrd = rd ? 1u : 0u, nrs1 = !rs1 ? 0u : (!rd || rs1 == rd) ? 1u : 2u;
            return (w & ~(31u << 7 | 31u << 15)) | nrd << 7 | nrs1 << 15;
        }
    }
    return w;
}
static inline uint32_t hash8(uint32_t w) { return (w * 2654435761u) >> 24; }
static void build_hash(uint32_t n) {
    for (uint32_t k = 0; k < n; k++) { uint32_t h = hash8(ix[4 * k + RL_IX_KEY]); while (hashtab[h]) h = (h + 1) & 255; hashtab[h] = (uint8_t)(k + 1); }
}
static int lookup(uint32_t key) {
    uint32_t h = hash8(key);
    while (hashtab[h]) { uint32_t k = hashtab[h] - 1u; if (ix[4 * k + RL_IX_KEY] == key) return (int)k; h = (h + 1) & 255; }
    return -1;
}
static inline uint32_t tpl_base(uint32_t k) { return hdr[TL_H_TEMPLATES] + (ix[4 * k + RL_IX_OFFSET] << 2); }

/* ---- the address generator and the send port (replay_ctl.c's, with C&C 3's own d) */
static void wait_event(uint32_t ev, uint32_t restore) {
    tile->EVENTS_MASK = EV_GO | ev;
    tile->EVENTS_REQUIRED = ev;
    uint32_t e;
    do { __asm__ volatile ("wfi" ::: "memory"); e = tile->EVENTS; } while ((e & ev) != ev);
    tile->EVENTS_REQUIRED = 0;
    tile->EVENTS_MASK = restore;
}
static inline void stream_to(uint32_t i, uint32_t src, uint32_t dest, uint32_t n, uint32_t ctr, uint32_t d) {
    set_stream(i, STREAM_SRC_W(src, 0), dest, STREAM_COUNT_W(n, SIZE_128, ctr, MODE_WRITE, d));
}
/* n 128-bit words from big SRAM to an address of C&C 3's own (its data memory, its peripheral's region), landed */
static void run_self(uint32_t chunk, uint32_t dest, uint32_t n, uint32_t ctr, uint32_t restore) {
    wait_rd_idle();
    stream_to(0, chunk, dest, n, ctr, me);
    tile->WRITE_COUNTER[ctr - 1] = n;
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
    start_run();
    wait_event(EV_CTR(ctr) | EV_RUN_DONE, restore);
}
static void copy_to_dm(uint32_t chunk, uint32_t *dst, uint32_t n, uint32_t restore) { run_self(chunk, (uint32_t)dst, n, CTR_CM, restore); }
static inline void sendw(const uint32_t *w) { send128(w[3], w[2], w[1], w[0]); }

/* ---- the expansion queue */
static uint32_t exp_src, exp_left, adv_next, adv_entries;
static void take_exp_credit(void) { tracer->EVENTS_MASK = TR_EV_EXP; (void)tracer->EVENTS; tracer->EVENTS_MASK = tmask; }
static void send_exp_word(const uint32_t *w, uint32_t restore) {
    send_to(ROUTER_HDR_W(me, MODE_WRITE, CTR_PERIPH), TR_Q_EXP);
    tile->WRITE_COUNTER[CTR_PERIPH - 1] = 1;
    sendw(w);
    wait_event(EV_CTR(CTR_PERIPH), restore);
}
static void exp_chunk(uint32_t restore) {
    uint32_t n = min32(TR_HALF, exp_left);
    if (!n) return;
    run_self(exp_src, TR_Q_EXP, n, CTR_PERIPH, restore);
    exp_src += n << 2; exp_left -= n;
}
static int new_expansion(uint32_t ev, uint32_t restore) {
    uint32_t w = tracer->EXPANSION_OPCODE, off = tracer->EXPANSION_OFFSET;
    int k = lookup(tkey(w));
    tr_results[TL_R_NEWEXP]++;
    if (!(ev & TR_EV_EXP)) take_exp_credit();                         /* the first chunk's credit */
    exp_left = 0;
    if (k < 0) { tr_results[TL_R_MISSING] = w; return 0; }
    uint32_t meta = ix[4 * k + RL_IX_META], len = ix[4 * k + RL_IX_LENGTH], rd0 = RL_IX_RD0(meta);
    if (RL_IX_CLASS(meta) == 1) tracer->EXPANSION_TEMPLATE = TR_TEMPLATE_VALID | (uint32_t)k;   /* item 145: first */
    if (((w >> 7) & 31) == 0 && (rd0 == 1 || rd0 == 3)) {
        nopword[1] = (nopword[1] & ~(3u << 14)) | (rd0 << 14);       /* N3: the single NOP word, carrying rd0 */
        send_exp_word(nopword, restore);
        return 1;
    }
    if (off >= len) return 1;
    exp_src = tpl_base((uint32_t)k) + (off << 2); exp_left = len - off;
    if (off > 0) {                                                    /* N3: a mid-sequence start carries rd0 */
        copy_to_dm(exp_src, scratch, 1, restore);
        scratch[1] = (scratch[1] & ~(3u << 14)) | (rd0 << 14);
        send_exp_word(scratch, restore);
        exp_src += 4; exp_left--;
        uint32_t n = min32(TR_HALF - 1, exp_left);                   /* the rest of the first chunk */
        if (n) { run_self(exp_src, TR_Q_EXP, n, CTR_PERIPH, restore); exp_src += n << 2; exp_left -= n; }
        return 1;
    }
    exp_chunk(restore);
    return 1;
}
static void advice_chunk(uint32_t restore) {
    uint32_t n = min32(TR_HALF, adv_entries - adv_next);
    if (!n) return;
    run_self(hdr[TL_H_ADVICE] + (adv_next << 2), TR_Q_ADVICE, n, CTR_PERIPH, restore);
    adv_next += n;
    tr_results[TL_R_ADVICE_RUNS]++;
}

/* ---- the DMA (one transfer at a time) */
static uint32_t dma_busy;
static void dma(uint32_t q, uint32_t n, uint32_t dst, uint32_t addr, uint32_t ctr) {
    tracer->DMA_HDR = DMA_HDR_W(n, q, dst, MODE_WRITE, ctr);
    tracer->DMA_ADDR = addr;
    tracer->PULSE_DMA = 1;
    dma_busy = 1;
    tr_results[TL_R_TRANSFERS]++;
}

int main(void) {
    me = tile_index();
    request_send(me, 0, 0, REQ_CC1(REQ_OP_STARTED, 0, 0));            /* as cc23.c: C&C 1 waits for it (6.2) */
    const uint32_t base_mask = EV_RESET_MASK;
    copy_to_dm(0, hdr, TL_H_WORDS / 4, base_mask);
    if (hdr[TL_H_MAGIC] != TL_MAGIC || hdr[TL_H_VERSION] != TL_VERSION) { tr_results[TL_R_STATUS] = 4; return 4; }
    uint32_t nt = min32(hdr[TL_H_NTEMPLATES], 128);
    if (nt) copy_to_dm(hdr[TL_H_INDEX], ix, nt, base_mask);
    build_hash(nt);
    copy_to_dm(hdr[TL_H_TABLES] + ((256 + 1) << 2), nopword, 1, base_mask);       /* ROM word 1, the shared NOP row */

    /* 2. the tables, the constants, the record (word 0 first: its write resets the Tracer, 4.21.3) */
    run_self(hdr[TL_H_TABLES], TR_TABLES, 768, CTR_PERIPH, base_mask);
    tracer->HASH_MUL = hdr[TL_H_HASH_MUL]; tracer->HASH_STEP = hdr[TL_H_HASH_STEP]; tracer->HASH_LINES = hdr[TL_H_HASH_LINES];
    tracer->RUN_CONFIG = hdr[TL_H_RUN_CONFIG];
    exp_left = 0;                                                     /* Q14: no expansion carried into a new segment */
    run_self(hdr[TL_H_RECORD], TR_STATE, 68, CTR_PERIPH, base_mask);
    adv_entries = (hdr[TL_H_ADVICE_N] + 1) >> 1; adv_next = 0;

    /* 3. Q3: window 0's start (words 0 and 2-66) to the first tile, from C&C 3's own copy of the record */
    copy_to_dm(hdr[TL_H_RECORD], rec, 68, base_mask);
    uint32_t first = hdr[TL_H_FIRST_TILE], trec = hdr[TL_H_T_RECORD];
    send_to(ROUTER_HDR_W(first, MODE_WRITE, CTR_NONE), sram_addr(trec));
    sendw(&rec[0]);
    send_to(ROUTER_HDR_W(first, MODE_WRITE, CTR_NONE), sram_addr(trec + 8));
    for (uint32_t i = 2; i <= 66; i++) sendw(&rec[4 * i]);

    /* 4. the windows */
    const uint32_t loop_mask = EV_GO | EV_PERIPH(1);
    const uint32_t svc = TR_EV_SVC_STATE | TR_EV_SVC_CODE | TR_EV_SVC_RLOG | TR_EV_SVC_WLOG;
    tr_results[TL_R_STATUS] = 1;
    uint32_t windows = hdr[TL_H_WINDOWS], status = 1;
    for (uint32_t n = 0; n < windows && status == 1; n++) {
        uint32_t tn = first + n, last = n + 1 == windows, q0 = 0, sent[4] = { 0, 0, 0, 0 };
        dma_busy = 0;
        tmask = PEV_RUN_DONE | PEV_DMA_DONE | TR_EV_NEWEXP | svc | (exp_left ? TR_EV_EXP : 0) | (adv_next < adv_entries ? TR_EV_ADVICE : 0);
        tracer->EVENTS_MASK = tmask;
        tile->EVENTS_MASK = loop_mask;
        tracer->PULSE_RUN = 1;
        for (;;) {
            __asm__ volatile ("wfi" ::: "memory");
            uint32_t ev = tracer->EVENTS & tmask;                     /* consumes the counted bits; the service bits are live */
            if (!ev) continue;
            if (ev & TR_EV_NEWEXP) {
                if (!new_expansion(ev, loop_mask)) { status = 2; break; }
                ev &= ~TR_EV_EXP;
            }
            if ((ev & TR_EV_EXP) && exp_left) exp_chunk(loop_mask);
            if (ev & TR_EV_ADVICE) advice_chunk(loop_mask);
            if (ev & PEV_DMA_DONE) dma_busy = 0;
            if (!dma_busy) {
                if ((ev & TR_EV_SVC_STATE) && q0 < 4) {
                    if (last) {                                       /* Q4, Q11: all 68 words to C&C 3 itself */
                        tile->WRITE_COUNTER[CTR_SRAM - 1] = 68;
                        dma(0, 68, me, sram_addr(hdr[TL_H_OWN_RECORD]), CTR_SRAM);
                        q0 = 4;
                    } else {
                        static const uint8_t cnt[4] = { 1, 1, 65, 1 }, word[4] = { 0, 1, 2, 67 };
                        uint32_t to = (q0 == 0 || q0 == 2) ? tn + 1 : tn;         /* the state to n + 1, the totals to n */
                        dma(0, cnt[q0], to, sram_addr(trec + 4u * word[q0]), CTR_NONE);
                        q0++;
                    }
                } else {
                    for (uint32_t q = 1; q <= 3; q++) {
                        if (!(ev & (TR_EV_SVC_STATE << q))) continue;
                        uint32_t len = q == 1 ? tracer->CODE_LENGTH : q == 2 ? tracer->READ_LOG_LENGTH : tracer->WRITE_LOG_LENGTH;
                        uint32_t c = min32(TR_HALF, len - sent[q]);
                        if (!c) continue;
                        uint32_t base = hdr[TL_H_T_CODE + q - 1];
                        dma(q, c, tn, sram_addr(base + (sent[q] << 2)), CTR_NONE);
                        sent[q] += c;
                        break;
                    }
                }
            }
            /* while a transfer runs, only DMA done (and the Tracer's own credits) wake us: the service bits stay up */
            tmask = PEV_RUN_DONE | PEV_DMA_DONE | TR_EV_NEWEXP | (dma_busy ? 0 : svc) | (exp_left ? TR_EV_EXP : 0)
                  | (adv_next < adv_entries ? TR_EV_ADVICE : 0);
            tracer->EVENTS_MASK = tmask;
            if (ev & PEV_RUN_DONE) break;
            if (tracer->ERRORS) { status = 3; break; }
        }
        if (status == 1) tr_results[TL_R_WINDOWS] = n + 1;
    }
    tracer->EVENTS_MASK = 0;
    tile->EVENTS_MASK = base_mask;

    /* 5. the end: the last record landed in C&C 3's memory; words 1 and 67 to the last tile (Q4); the checks */
    if (status == 1) {
        wait_event(EV_CTR(CTR_SRAM), base_mask);
        copy_to_dm(hdr[TL_H_OWN_RECORD], rec, 68, base_mask);
        uint32_t tl = first + windows - 1;
        send_to(ROUTER_HDR_W(tl, MODE_WRITE, CTR_NONE), sram_addr(trec + 4));
        sendw(&rec[4 * 1]);
        send_to(ROUTER_HDR_W(tl, MODE_WRITE, CTR_NONE), sram_addr(trec + 4 * 67));
        sendw(&rec[4 * 67]);
        uint32_t left = (rec[7] >> 6) & 0x7FFFFFu;                    /* word 1 [124:102]: the advice field */
        tr_results[TL_R_ADVICE_LEFT] = left;
        if (left) status = 5;
        if (hdr[TL_H_EXPECT]) {
            copy_to_dm(hdr[TL_H_EXPECT], expect_rec, 66, base_mask);
            for (uint32_t i = 0; i < 66 && status == 1; i++) {
                if (i == 1) continue;                                 /* word 1 is the window's descriptor, not state */
                for (uint32_t j = 0; j < 4; j++) if (rec[4 * i + j] != expect_rec[4 * i + j]) { tr_results[TL_R_END_DIFF] = i + 1; status = 6; break; }
            }
        }
    }
    tr_results[TL_R_ERRORS] = tracer->ERRORS;
    tr_results[TL_R_CONDITIONS] = tile->ERRORS;
    tr_results[TL_R_STATUS] = status;
    return (int)status;
}
