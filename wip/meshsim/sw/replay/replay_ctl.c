/* The replay controller (Stage B): one tile's controller program for replay passes, spec 4.19 (mesh-sorter-v1.1-26n).
 *
 * Everything starts resident in big SRAM (replay_layout.h). The program:
 *   1. copies the layout header and the template index into data memory (mode-6 address-generator runs to itself),
 *      and builds a hash of the index keyed by the exact instruction word (4.19.5's template numbers, item 114);
 *   2. loads the decode table and the ROM into the replayer with one mode-9 run (4.19.10: 768 words at 0x00400);
 *   3. copies the window's state record into data memory and loads region 0 through the send port from byte 16 of the
 *      record (4.19.2's state record, 4.19.10's recommended practice);
 *   4. runs passes with the 4.19.9 controller loop: claim the service lines with one load of WAKE_EVENTS, answer each
 *      low-water line with a local mode-9 stream of min(half, entries left), each new expansion with its template from
 *      the next offset (min(128, words)), each expansion low-water line with the next words; squelch to
 *      DMA_PERIPH_REMAINING done while the run streams, then restore the mask;
 *   5. ruling N3 (4.19.4): on a mid-sequence start the first word sent carries the template's rd0 (bits [47:46]), and
 *      under a NOP policy with rd = x0 the controller sends the single NOP word (ROM word 1) carrying the template's
 *      rd0 instead of the template. Both go through the send port, and are waited for before any address-generator
 *      run feeds the same queue (one source at a time, invariant 18).
 * Results go to rp_results[] in data memory; main returns (crt0: ebreak, the simulator's exit).
 *
 * Final for this milestone (replayer-cp14): runs on meshsim's v1.1 tile (StageB.java) and matches the tracer's rows byte
 * for byte; the new-expansion fast path is described in README.md.
 */
#include <stdint.h>
#include "replay_regs.h"
#include "replay_layout.h"

#define HALF_CODE   128
#define HALF_LOG    256
#define HALF_EXP    128
#define TPL_STREAM0 4            /* STREAM entries 0-3: refills; 4 + k: template k's first chunk, preloaded at start-up */

uint32_t rp_results[16];

static uint32_t hdr[RL_H_WORDS] __attribute__((aligned(16)));
static uint32_t ix[128 * 4] __attribute__((aligned(16)));      /* the template index: word, offset, length, rd0/class */
static uint8_t  hashtab[256];                                  /* the general lookup (slow path): number + 1, 0 = empty */
/* The direct-mapped table (fast path), 16 B per slot, slot = (w * dm_mult) >> 25: {word (0 = empty), DMA_CONFIG for the
 * template's preloaded stream, its first chunk's words, the template number}. At start-up the same space holds the
 * state record (67 words) while region 0 is sent. */
static uint32_t dmtab[128 * 4] __attribute__((aligned(16)));
#define record dmtab
static uint32_t dm_mult;
#define DM_SLOT(w, m)  (((w) * (m)) >> 25)
static uint32_t nopword[4] __attribute__((aligned(16)));       /* ROM word 1, the shared NOP row (4.19.6) */
static uint32_t scratch[4] __attribute__((aligned(16)));
static uint32_t me;

/* the pass's stream positions (128-bit entries sent so far) */
static uint32_t code_next, rlog_next, wlog_next;
/* the active controller-fed expansion: the chunk of its next unsent word and the words left to send */
static uint32_t exp_src, exp_left;
static uint32_t nstreams, total;

static inline uint32_t min32(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* Sleep until `ev` is pending, consume it, then set the mask to `restore`. Keeps GO in the mask (4.12, item 79). */
static void wait_event(uint32_t ev, uint32_t restore) {
    WAKE_MASK = EV_GO | ev;
    wfi();
    (void)WAKE_EVENTS;
    WAKE_MASK = restore;
}

/* The STREAM tables may be written only while the address generator isn't running (4.10). */
static inline void wait_ag_idle(void) { while (TILE_STATUS & ST_RD_RUNNING) {} }

static inline void set_stream(uint32_t i, uint32_t src, uint32_t dest_word, uint32_t n, uint32_t mode) {
    STREAM_SRC[i] = src;
    STREAM_DEST[i] = STREAM_DEST_ENTRY(dest_word, 0);                 /* d = 0: the tile itself */
    STREAM_COUNT[i] = STREAM_COUNT_ENTRY(n, mode, SIZE_128, 0);
}

static inline void start_run(uint32_t first, uint32_t nstr) {
    DMA_CONFIG = CONFIG_STREAM_RUN(first, nstr);
    TILE_PULSE = PULSE_START;
}

/* Copy n 128-bit words from big SRAM into data memory: a mode-6 run to the tile itself (4.11, 6.5). */
static void copy_to_dm(uint32_t chunk, uint32_t *dst, uint32_t n, uint32_t restore) {
    wait_ag_idle();
    set_stream(0, chunk, PACKED_WORD(dst), n, MODE_CMEM);
    DMA_CM_REMAINING = n;                                             /* a write adds and arms (8.7) */
    start_run(0, 1);
    wait_event(EV_CM_DONE, restore);
}

/* One 128-bit packet through the send port; ROUTER_SET_HDR is set by the caller (addr advances by 4, 8.4). */
static inline void send128(const uint32_t *w) {
    ROUTER_SET_W0 = w[0];
    ROUTER_SET_W1 = w[1];
    ROUTER_SET_W2 = w[2];
    ROUTER_SEND_128 = w[3];
}

static inline uint32_t hash8(uint32_t w) { return (w * 2654435761u) >> 24; }

static void build_hash(uint32_t n) {
    for (uint32_t k = 0; k < n; k++) {
        uint32_t h = hash8(ix[4 * k + RL_IX_WORD]);
        while (hashtab[h]) h = (h + 1) & 255;
        hashtab[h] = (uint8_t)(k + 1);
    }
}

static int lookup(uint32_t w) {
    uint32_t h = hash8(w);
    while (hashtab[h]) {
        uint32_t k = hashtab[h] - 1u;
        if (ix[4 * k + RL_IX_WORD] == w) return (int)k;
        h = (h + 1) & 255;
    }
    return -1;
}

static inline int nop_policy(uint32_t k) { uint32_t r = RL_IX_RD0(ix[4 * k + RL_IX_META]); return r == 1 || r == 3; }
/* A word the fast path can't serve: N3's NOP policy applies (a NOP-policy template and the word's rd field is x0). The
 * index is keyed by the exact word, so this is known at start-up. */
static inline int needs_nop(uint32_t k) { return nop_policy(k) && ((ix[4 * k + RL_IX_WORD] >> 7) & 31) == 0; }
static inline uint32_t tpl_base(uint32_t k) { return hdr[RL_H_TEMPLATES] + (ix[4 * k + RL_IX_OFFSET] << 2); }

/* Start-up: one preloaded STREAM entry per template (its first min(128, length) words to the expansion port), and the
 * direct-mapped table. Its slot is a multiplicative hash, (w * m) >> 25, with m searched (odd multipliers from an LCG)
 * until the program's words land in distinct slots: a few tries for a few dozen words (EVM Zone 1, Cyclone 8). If none
 * is found in 4,096 tries, the one with the fewest collisions is kept: a word that loses its slot takes the slow path.
 * Words that N3's NOP policy applies to stay out of the table, so the fast path never needs the N3 check. */
static void build_fast(uint32_t nt) {
    for (uint32_t k = 0; k < nt; k++) {
        uint32_t len = ix[4 * k + RL_IX_LENGTH];
        set_stream(TPL_STREAM0 + k, tpl_base(k), RP_EXP_PORT, min32(HALF_EXP, len), MODE_PERIPH);
    }
    uint32_t m = 0x9E3779B1u, best = m, best_coll = ~0u, tries = 0;
    while (best_coll && tries < 4096) {
        uint32_t used[4] = { 0, 0, 0, 0 }, coll = 0;
        for (uint32_t k = 0; k < nt; k++) {
            if (needs_nop(k)) continue;
            uint32_t slot = DM_SLOT(ix[4 * k + RL_IX_WORD], m);
            if (used[slot >> 5] & (1u << (slot & 31))) coll++;
            used[slot >> 5] |= 1u << (slot & 31);
        }
        tries++;
        if (coll < best_coll) { best_coll = coll; best = m; }
        m = (m * 1664525u + 1013904223u) | 1u;
    }
    dm_mult = best;
    for (uint32_t i = 0; i < 128 * 4; i++) dmtab[i] = 0;
    for (uint32_t k = 0; k < nt; k++) {
        if (needs_nop(k)) continue;
        uint32_t w = ix[4 * k + RL_IX_WORD], *e = &dmtab[DM_SLOT(w, best) << 2];
        if (e[0]) continue;                                           /* a collision: the first word keeps the slot */
        e[0] = w;
        e[1] = CONFIG_STREAM_RUN(TPL_STREAM0 + k, 1);
        e[2] = min32(HALF_EXP, ix[4 * k + RL_IX_LENGTH]);
        e[3] = k;
    }
    rp_results[RL_R_DM_MULT] = best;
    rp_results[RL_R_DM_TRIES] = tries;
    rp_results[RL_R_DM_COLLISIONS] = best_coll;
}

/* After an expansion's first chunk has started: the rest, if any, for the expansion low-water line. */
static inline void set_active(uint32_t k, uint32_t sent) {
    uint32_t len = ix[4 * k + RL_IX_LENGTH];
    exp_left = len - sent;
    exp_src = tpl_base(k) + (sent << 2);
}

/* One word into the expansion queue through the send port, waited for before anything else feeds that queue. */
static void send_exp_word(const uint32_t *w, uint32_t restore) {
    WAKE_MASK = EV_GO | EV_PERIPH_DONE;
    ROUTER_SET_HDR = HDR(me, MODE_PERIPH, RP_EXP_PORT);
    DMA_PERIPH_REMAINING = 1;
    send128(w);
    wait_event(EV_PERIPH_DONE, restore);
}

/* The slow path: the pass's first expansion (it may start mid-sequence), a word N3's NOP policy applies to, or a word
 * the direct-mapped table misses. Returns 0 if the instruction has no template, 2 if it left an address-generator run
 * going (the caller waits for its arrivals), 1 otherwise. */
static int new_expansion_slow(uint32_t w, uint32_t restore) {
    uint32_t off = PERIPH_STATUS_0 >> 16;
    int k = lookup(w);
    rp_results[RL_R_SLOW]++;
    if (k < 0) { rp_results[RL_R_MISSING] = w; return 0; }
    uint32_t len = ix[4 * k + RL_IX_LENGTH], rd0 = RL_IX_RD0(ix[4 * k + RL_IX_META]);
    exp_left = 0;
    if (((w >> 7) & 31) == 0 && (rd0 == 1 || rd0 == 3)) {
        /* N3: a NOP policy with rd = x0. The single NOP word, carrying the template's rd0, is the whole expansion. */
        nopword[1] = (nopword[1] & ~(3u << 14)) | (rd0 << 14);
        send_exp_word(nopword, restore);
        return 1;
    }
    if (off >= len) return 1;                                         /* nothing to send (a malformed offset) */
    if (off == 0) {                                                   /* the template's preloaded stream */
        uint32_t n = min32(HALF_EXP, len);
        DMA_PERIPH_REMAINING = n;
        start_run(TPL_STREAM0 + k, 1);
        set_active((uint32_t)k, n);
        return 2;
    }
    /* N3: a mid-sequence start. The first word sent carries the template's rd0 (it lives in word 0). */
    uint32_t base = tpl_base((uint32_t)k), n = min32(HALF_EXP, len - off);
    copy_to_dm(base + (off << 2), scratch, 1, restore);
    scratch[1] = (scratch[1] & ~(3u << 14)) | (rd0 << 14);
    send_exp_word(scratch, restore);
    if (n > 1) {
        set_stream(0, base + ((off + 1) << 2), RP_EXP_PORT, n - 1, MODE_PERIPH);
        DMA_PERIPH_REMAINING = n - 1;
        start_run(0, 1);
        set_active((uint32_t)k, off + n);
        return 2;
    }
    set_active((uint32_t)k, off + n);
    return 1;
}

/* One refill stream for a stream queue: min(half, entries left) from its next entry. */
static void refill(uint32_t *next, uint32_t base, uint32_t entries, uint32_t half, uint32_t port) {
    uint32_t n = min32(half, entries - *next);
    if (!n) return;                                                   /* the stream is all sent: the claim credits nothing real */
    set_stream(nstreams++, base + (*next << 2), port, n, MODE_PERIPH);
    total += n;
    *next += n;
}

/* One pass (4.19.9's controller loop, with the new-expansion fast path). Returns 0 on a missing template. */
static int run_pass(void) {
    const uint32_t loop_mask = EV_GO | EV_PERIPH_LINES;
    const uint32_t mult = dm_mult;
    const uint32_t *const tab = dmtab;
    uint32_t first = 1;                                               /* the pass's first expansion takes the slow path */
#ifdef REPLAY_LAST_WORD
    uint32_t last_w = 0; const uint32_t *last_e = tab;
#endif
    code_next = rlog_next = wlog_next = 0;
    exp_left = 0;
    WAKE_MASK = loop_mask;                                            /* only a service line can come */
    TILE_PULSE = PULSE_PERIPH_RUN;                                    /* a pass; empties the queues: low-water at once */
    for (;;) {
        wfi();
        uint32_t ev = WAKE_EVENTS;                                    /* claims the lines: credits taken */
        if (ev & EV_NEW_EXP) {
            /* ---- the fast path: the address generator is idle here (every run is waited for below) */
            uint32_t w = PERIPH_STATUS_1;
            const uint32_t *e;
#ifdef REPLAY_LAST_WORD
            if (w == last_w) e = last_e; else
#endif
            e = &tab[DM_SLOT(w, mult) << 2];
            if (e[0] == w && !first) {
                DMA_PERIPH_REMAINING = e[2];
                DMA_CONFIG = e[1];
                TILE_PULSE = PULSE_START;
                /* ---- the stream is running: everything else is off the critical path */
#ifdef REPLAY_LAST_WORD
                if (w == last_w) rp_results[RL_R_LAST_HITS]++;
                last_w = w; last_e = e;
#endif
                set_active(e[3], e[2]);
                rp_results[RL_R_NEWEXP]++;
                wait_event(EV_PERIPH_DONE, loop_mask);                /* the done event is sticky: no race */
            } else {
                first = 0;
                int r = new_expansion_slow(w, loop_mask);
                if (!r) return 0;
                rp_results[RL_R_NEWEXP]++;
                if (r == 2) wait_event(EV_PERIPH_DONE, loop_mask);
            }
        }
        if (ev & EV_PERIPH_SVC_DONE) return 1;
        /* ---- refills, one run: the stream queues, then the active expansion's next chunk */
        nstreams = 0; total = 0;
        if (ev & EV_CODE_LOW) refill(&code_next, hdr[RL_H_CODE], hdr[RL_H_CODE_N], HALF_CODE, RP_CODE_PORT);
        if (ev & EV_RLOG_LOW) refill(&rlog_next, hdr[RL_H_RLOG], hdr[RL_H_RLOG_N], HALF_LOG, RP_RLOG_PORT);
        if (ev & EV_WLOG_LOW) refill(&wlog_next, hdr[RL_H_WLOG], hdr[RL_H_WLOG_N], HALF_LOG, RP_WLOG_PORT);
        if ((ev & EV_EXP_LOW) && exp_left) {
            uint32_t n = min32(HALF_EXP, exp_left);
            set_stream(nstreams++, exp_src, RP_EXP_PORT, n, MODE_PERIPH);
            total += n;
            exp_src += n << 2;
            exp_left -= n;
        }
        if (total) {
            WAKE_MASK = EV_GO | EV_PERIPH_DONE;                       /* squelch: only the copy's completion can wake it */
            DMA_PERIPH_REMAINING = total;
            start_run(0, nstreams);
            wfi();
            (void)WAKE_EVENTS;                                        /* consumes [3] */
            WAKE_MASK = loop_mask;                                    /* lines raised meanwhile fall through */
            rp_results[RL_R_REFILLS]++;
        }
    }
}

int main(void) {
    me = ST_ENDPOINT_ID(TILE_STATUS);
    const uint32_t base_mask = EV_GO;
    copy_to_dm(0, hdr, RL_H_WORDS / 4, base_mask);
    if (hdr[RL_H_MAGIC] != RL_MAGIC || hdr[RL_H_VERSION] != RL_VERSION) { rp_results[RL_R_STATUS] = 4; return 4; }

    uint32_t nt = hdr[RL_H_NTEMPLATES];
    if (nt > 128) nt = 128;
    if (nt) copy_to_dm(hdr[RL_H_INDEX], ix, nt, base_mask);
    build_hash(nt);

    /* ROM word 1, the shared NOP row: the tables region holds 256 decode-table words, then the ROM */
    copy_to_dm(hdr[RL_H_TABLES] + ((256 + 1) << 2), nopword, 1, base_mask);

    /* the decode table and the ROM: one mode-9 run of 768 words to 0x00400 (the replayer is stopped) */
    wait_ag_idle();
    set_stream(0, hdr[RL_H_TABLES], RP_DECODE_TABLE, 768, MODE_PERIPH);
    DMA_PERIPH_REMAINING = 768;
    start_run(0, 1);
    wait_event(EV_PERIPH_DONE, base_mask);

    /* region 0 through the send port, from byte 16 of the record (word 1 onward) */
    copy_to_dm(hdr[RL_H_RECORD], record, 67, base_mask);
    WAKE_MASK = EV_GO | EV_PERIPH_DONE;
    ROUTER_SET_HDR = HDR(me, MODE_PERIPH, RP_STATE_WORD);
    DMA_PERIPH_REMAINING = 66;
    for (uint32_t i = 1; i < 67; i++) send128(&record[4 * i]);
    wait_event(EV_PERIPH_DONE, base_mask);

    /* the fast path's tables (dmtab reuses the record's space) */
    wait_ag_idle();
    build_fast(nt);

    rp_results[RL_R_STATUS] = 1;
    for (uint32_t p = 0; p < hdr[RL_H_PASSES]; p++) {
        if (!run_pass()) { rp_results[RL_R_STATUS] = 2; break; }
        rp_results[RL_R_PASSES] = p + 1;
        if (TILE_CONDITIONS) { rp_results[RL_R_STATUS] = 3; break; }
    }
    rp_results[RL_R_CONDITIONS] = TILE_CONDITIONS;
    WAKE_MASK = EV_GO | EV_COUNTERS_DONE;                             /* the reset mask, as found */
    return (int)rp_results[RL_R_STATUS];
}
