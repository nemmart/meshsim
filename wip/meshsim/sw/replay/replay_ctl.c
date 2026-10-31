/* The replay controller (Stage B): one tile's controller program for replay passes, spec 4.19 (v1.25).
 *
 * v1.25 (WORKER11): the universal tables (wip/tables/replayer, DECISIONS-v1.25 s5). The template index is keyed by
 * key(w), the normalized template-index key (tkey below): a presented word is normalized, then looked up. The fast
 * path's direct-mapped table holds only the templates the window's bitmap (state record word 0) names, so the 75
 * universal templates don't crowd it; the slow path serves any template. The NOP policy (N3) now depends on the
 * presented word's rd, which a key doesn't keep: a fast-path entry records the template's policy and the fast path
 * hands rd = x0 words with a NOP policy to the slow path.
 *
 * Everything starts resident in big SRAM (replay_layout.h). The program:
 *   1. copies the layout header and the template index into data memory (mode-0 address-generator runs to itself, on
 *      counter 2), and builds a hash of the index keyed by the exact instruction word (4.19.9's template numbers);
 *   2. loads the decode table and the ROM into the replayer with one mode-0 run into its region (4.19.10: 768 words at
 *      byte offset 0x1000), counted on counter 4;
 *   3. copies the window's state record into data memory and loads part 0 through the send port from byte 16 of the
 *      record (4.19.2's state record, 4.19.10's recommended practice);
 *   4. runs passes with the 4.19.9 controller loop on counted credits: one load of the replayer's EVENTS consumes one
 *      credit from every masked queue bit; each credit taken is answered with a local refill stream of min(half,
 *      entries left); a queue with nothing left leaves the mask; each new expansion is served first (its first chunk
 *      on an explicitly taken expansion credit); the run is squelched to counter 4's done and DMA run done together;
 *   5. ruling N3 (4.19.4): on a mid-sequence start the first word sent carries the template's rd0 (bits [47:46]), and
 *      under a NOP policy with rd = x0 the controller sends the single NOP word (ROM word 1) carrying the template's
 *      rd0 instead of the template. Both go through the send port, and are waited for before any address-generator
 *      run feeds the same queue (one source at a time, invariant 18).
 * Results go to rp_results[] in data memory; main returns its status (crt0: the simulator-only exit word, 8.15).
 *
 * v1.2 port (WORKER4): mesh.h 1.2 replaces v1.1's local replay_regs.h; byte addresses (the replayer's region 0x04, data
 * memory); the counted credits replace v1.1's service lines and claims.
 */
#include <stdint.h>
#include "mesh.h"
#include "replay_layout.h"

#define RP_DECODE_TABLE (REGION_PERIPH(1) + 0x1000u)   /* the decode table, then the ROM: 768 x 16 bytes (4.19.10) */
#define RP_EV_QUEUES    (RP_EV_CODE | RP_EV_RLOG | RP_EV_WLOG | RP_EV_EXP)
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
#define DM_SLOTS 512            /* v1.25: 512 slots (8 KB), so a window's bitmap templates usually land collision-free */
#define DM_TRIES 256            /* multipliers tried at most; a key that loses its slot takes the slow path */
static uint32_t dmtab[DM_SLOTS * 4] __attribute__((aligned(16)));
#define record dmtab
static uint32_t dm_mult;
#define DM_SLOT(w, m)  (((w) * (m)) >> 23)
static uint32_t nopword[4] __attribute__((aligned(16)));       /* ROM word 1, the shared NOP row (4.19.6) */
static uint32_t scratch[4] __attribute__((aligned(16)));
static uint32_t me, pmask;                                     /* pmask: the replayer's EVENTS_MASK in a pass */
static uint32_t bitmap[4];                                     /* the window's template bitmap (state record word 0) */

/* key(w) (wip/tables/README.md): every bit of w except register fields. An inline (custom-0): rd, rs1, rs2 cleared.
 * CSRRW/CSRRS: rd and rs1 renamed by first appearance (x0 0; a non-zero rd 1; rs1 0 if x0, 1 if equal to a non-zero
 * rd or if rd is x0, else 2). Any other word is its own key. */
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

/* the pass's stream positions (128-bit entries sent so far) */
static uint32_t code_next, rlog_next, wlog_next;
/* the active controller-fed expansion: the chunk of its next unsent word and the words left to send */
static uint32_t exp_src, exp_left;
static uint32_t nstreams, total;

static inline uint32_t min32(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* Sleep until every event in `ev` is pending, consume them, then set the mask to `restore`. Keeps GO in the mask
 * (4.12). EVENTS_REQUIRED makes the WFI wait for all of them (a counter's done and DMA run done, 4.10). */
static void wait_event(uint32_t ev, uint32_t restore) {
    tile->EVENTS_MASK = EV_GO | ev;
    tile->EVENTS_REQUIRED = ev;
    uint32_t e;
    do { __asm__ volatile ("wfi" ::: "memory"); e = tile->EVENTS; } while ((e & ev) != ev);
    tile->EVENTS_REQUIRED = 0;
    tile->EVENTS_MASK = restore;
}

static inline void stream(uint32_t i, uint32_t src, uint32_t dest, uint32_t n, uint32_t ctr) {
    set_stream(i, STREAM_SRC_W(src, 0), dest, STREAM_COUNT_W(n, SIZE_128, ctr, MODE_WRITE, 0));   /* d = 0: the tile itself */
}
static inline void start_runs(uint32_t first, uint32_t nstr) {
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, first, nstr);
    start_run();
}

/* Copy n 128-bit words from big SRAM into data memory: a mode-0 run to the tile itself on counter 2 (4.11, 6.5). */
static void copy_to_dm(uint32_t chunk, uint32_t *dst, uint32_t n, uint32_t restore) {
    wait_rd_idle();
    stream(0, chunk, (uint32_t)dst, n, CTR_CM);
    tile->WRITE_COUNTER[CTR_CM - 1] = n;                              /* a write adds and arms (8.7) */
    start_runs(0, 1);
    wait_event(EV_CTR(CTR_CM) | EV_RUN_DONE, restore);
}

/* One 128-bit packet through the send port; ROUTER_HDR and ROUTER_ADDR are set by the caller (+ 16 per send, 8.4). */
static inline void sendw(const uint32_t *w) { send128(w[3], w[2], w[1], w[0]); }

static inline uint32_t hash8(uint32_t w) { return (w * 2654435761u) >> 24; }

static void build_hash(uint32_t n) {
    for (uint32_t k = 0; k < n; k++) {
        uint32_t h = hash8(ix[4 * k + RL_IX_KEY]);
        while (hashtab[h]) h = (h + 1) & 255;
        hashtab[h] = (uint8_t)(k + 1);
    }
}

static int lookup(uint32_t key) {
    uint32_t h = hash8(key);
    while (hashtab[h]) {
        uint32_t k = hashtab[h] - 1u;
        if (ix[4 * k + RL_IX_KEY] == key) return (int)k;
        h = (h + 1) & 255;
    }
    return -1;
}

static inline int nop_policy(uint32_t k) { uint32_t r = RL_IX_RD0(ix[4 * k + RL_IX_META]); return r == 1 || r == 3; }
static inline int in_bitmap(uint32_t k) { return (bitmap[k >> 5] >> (k & 31)) & 1u; }
#define DM_NOP (1u << 31)       /* in a fast-path entry's template number: the template has a NOP policy (N3) */
static inline uint32_t tpl_base(uint32_t k) { return hdr[RL_H_TEMPLATES] + (ix[4 * k + RL_IX_OFFSET] << 2); }

/* Start-up: one preloaded STREAM entry per template (its first min(128, length) words to the expansion port), and the
 * direct-mapped table over the templates in the window's bitmap. Its slot is a multiplicative hash of the key,
 * (key * m) >> 25, with m searched (odd multipliers from an LCG) until those keys land in distinct slots. If none is
 * found in DM_TRIES tries, the one with the fewest collisions is kept: a key that loses its slot takes the slow path.
 * (v1.2's 128 slots and 4,096 tries took millions of cycles at start-up for a window naming ~40 templates.) */
static void build_fast(uint32_t nt) {
    for (uint32_t k = 0; k < nt; k++) {
        uint32_t len = ix[4 * k + RL_IX_LENGTH];
        stream(TPL_STREAM0 + k, tpl_base(k), RP_Q_EXP, min32(HALF_EXP, len), CTR_PERIPH);
    }
    uint32_t m = 0x9E3779B1u, best = m, best_coll = ~0u, tries = 0;
    while (best_coll && tries < DM_TRIES) {
        uint32_t used[DM_SLOTS / 32], coll = 0;
        for (uint32_t i = 0; i < DM_SLOTS / 32; i++) used[i] = 0;
        for (uint32_t k = 0; k < nt; k++) {
            if (!in_bitmap(k)) continue;
            uint32_t slot = DM_SLOT(ix[4 * k + RL_IX_KEY], m);
            if (used[slot >> 5] & (1u << (slot & 31))) coll++;
            used[slot >> 5] |= 1u << (slot & 31);
        }
        tries++;
        if (coll < best_coll) { best_coll = coll; best = m; }
        m = (m * 1664525u + 1013904223u) | 1u;
    }
    dm_mult = best;
    for (uint32_t i = 0; i < DM_SLOTS * 4; i++) dmtab[i] = 0;
    uint32_t held = 0;
    for (uint32_t k = 0; k < nt; k++) {
        if (!in_bitmap(k)) continue;
        uint32_t key = ix[4 * k + RL_IX_KEY], *e = &dmtab[DM_SLOT(key, best) << 2];
        held++;
        if (e[1]) continue;                                           /* a collision: the first key keeps the slot */
        e[0] = key;
        e[1] = DMA_CFG_RUN(RUN_STREAMS, TPL_STREAM0 + k, 1);
        e[2] = min32(HALF_EXP, ix[4 * k + RL_IX_LENGTH]);
        e[3] = k | (nop_policy(k) ? DM_NOP : 0u);
    }
    rp_results[RL_R_FAST_TEMPLATES] = held;
    rp_results[RL_R_DM_MULT] = best;
    rp_results[RL_R_DM_TRIES] = tries;
    rp_results[RL_R_DM_COLLISIONS] = best_coll;
}

/* After an expansion's first chunk has started: the rest, if any, for the expansion's credits (its bit joins the mask
 * while words are left). */
static inline void set_active(uint32_t k, uint32_t sent) {
    uint32_t len = ix[4 * k + RL_IX_LENGTH];
    exp_left = len - sent;
    exp_src = tpl_base(k) + (sent << 2);
    pmask = exp_left ? pmask | RP_EV_EXP : pmask & ~RP_EV_EXP;
}
/* A new expansion sets its credits to 2 (4.19.8); the first chunk the controller sends at once takes one, explicitly:
 * a consuming load with only the expansion's bit in the window's mask. */
static inline void take_exp_credit(void) {
    replayer->EVENTS_MASK = RP_EV_EXP;
    (void)replayer->EVENTS;
    replayer->EVENTS_MASK = pmask;
}

/* One word into the expansion queue through the send port, waited for before anything else feeds that queue. */
static void send_exp_word(const uint32_t *w, uint32_t restore) {
    send_to(ROUTER_HDR_W(me, MODE_WRITE, CTR_PERIPH), RP_Q_EXP);
    tile->WRITE_COUNTER[CTR_PERIPH - 1] = 1;
    sendw(w);
    wait_event(EV_CTR(CTR_PERIPH), restore);
}

/* The slow path: the pass's first expansion (it may start mid-sequence), a word N3's NOP policy applies to, or a word
 * the direct-mapped table misses. Returns 0 if the instruction has no template, 2 if it left an address-generator run
 * going (the caller waits for its arrivals), 1 otherwise. */
static int new_expansion_slow(uint32_t w, uint32_t restore) {
    uint32_t off = replayer->EXPANSION_OFFSET;
    int k = lookup(tkey(w));
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
        tile->WRITE_COUNTER[CTR_PERIPH - 1] = n;
        start_runs(TPL_STREAM0 + k, 1);
        set_active((uint32_t)k, n);
        return 2;
    }
    /* N3: a mid-sequence start. The first word sent carries the template's rd0 (it lives in word 0). */
    uint32_t base = tpl_base((uint32_t)k), n = min32(HALF_EXP, len - off);
    copy_to_dm(base + (off << 2), scratch, 1, restore);
    scratch[1] = (scratch[1] & ~(3u << 14)) | (rd0 << 14);
    send_exp_word(scratch, restore);
    if (n > 1) {
        stream(0, base + ((off + 1) << 2), RP_Q_EXP, n - 1, CTR_PERIPH);
        tile->WRITE_COUNTER[CTR_PERIPH - 1] = n - 1;
        start_runs(0, 1);
        set_active((uint32_t)k, off + n);
        return 2;
    }
    set_active((uint32_t)k, off + n);
    return 1;
}

/* One refill stream for a stream queue: min(half, entries left) from its next entry. A queue with nothing left behind
 * its credits leaves the mask (4.19.8). */
static void refill(uint32_t *next, uint32_t base, uint32_t entries, uint32_t half, uint32_t port, uint32_t bit) {
    uint32_t n = min32(half, entries - *next);
    if (n) { stream(nstreams++, base + (*next << 2), port, n, CTR_PERIPH); total += n; *next += n; }
    if (*next >= entries) pmask &= ~bit;
}

/* One pass (4.19.9's controller loop on counted credits, with the new-expansion fast path). Returns 0 on a missing
 * template. */
static uint32_t store_log;
static int run_pass(void) {
    const uint32_t loop_mask = EV_GO | EV_PERIPH(1);
    const uint32_t mult = dm_mult;
    const uint32_t *const tab = dmtab;
    uint32_t first = 1;                                               /* the pass's first expansion takes the slow path */
    code_next = rlog_next = wlog_next = 0;
    exp_left = 0;
    pmask = PEV_RUN_DONE | RP_EV_NEWEXP | (hdr[RL_H_CODE_N] ? RP_EV_CODE : 0) | (hdr[RL_H_RLOG_N] ? RP_EV_RLOG : 0)
          | (store_log && hdr[RL_H_WLOG_N] ? RP_EV_WLOG : 0);         /* no write log without the store log */
    replayer->EVENTS_MASK = pmask;
    tile->EVENTS_MASK = loop_mask;                                    /* only the replayer can come */
    replayer->PULSE_RUN = 1;                                          /* a pass: empties the queues, two credits each */
    for (;;) {
        __asm__ volatile ("wfi" ::: "memory");
        uint32_t ev = replayer->EVENTS & pmask;                       /* consumes: one credit from each set queue bit */
        if (!ev) continue;                                            /* a spurious wake (4.12) */
        if (ev & RP_EV_NEWEXP) {
            /* ---- the fast path: the address generator is idle here (every run is waited for below) */
            uint32_t w = replayer->EXPANSION_OPCODE, key = tkey(w);
            const uint32_t *e = &tab[DM_SLOT(key, mult) << 2];
            if (!(ev & RP_EV_EXP)) take_exp_credit();                 /* the first chunk's credit: taken by this load, or now */
            ev &= ~RP_EV_EXP;
            if (e[1] && e[0] == key && !first && !((e[3] & DM_NOP) && !((w >> 7) & 31u))) {
                tile->WRITE_COUNTER[CTR_PERIPH - 1] = e[2];
                tile->DMA_CONFIG = e[1];
                start_run();
                /* ---- the stream is running: everything else is off the critical path */
                set_active(e[3] & ~DM_NOP, e[2]);
                replayer->EVENTS_MASK = pmask;
                rp_results[RL_R_NEWEXP]++;
                wait_event(EV_CTR(CTR_PERIPH) | EV_RUN_DONE, loop_mask);
            } else {
                first = 0;
                int r = new_expansion_slow(w, loop_mask);
                if (!r) return 0;
                replayer->EVENTS_MASK = pmask;
                rp_results[RL_R_NEWEXP]++;
                if (r == 2) wait_event(EV_CTR(CTR_PERIPH) | EV_RUN_DONE, loop_mask);
            }
        }
        if (ev & PEV_RUN_DONE) break;
        /* ---- refills, one run: the stream queues, then the active expansion's next chunk */
        nstreams = 0; total = 0;
        if (ev & RP_EV_CODE) refill(&code_next, hdr[RL_H_CODE], hdr[RL_H_CODE_N], HALF_CODE, RP_Q_CODE, RP_EV_CODE);
        if (ev & RP_EV_RLOG) refill(&rlog_next, hdr[RL_H_RLOG], hdr[RL_H_RLOG_N], HALF_LOG, RP_Q_RLOG, RP_EV_RLOG);
        if (ev & RP_EV_WLOG) refill(&wlog_next, hdr[RL_H_WLOG], hdr[RL_H_WLOG_N], HALF_LOG, RP_Q_WLOG, RP_EV_WLOG);
        if ((ev & RP_EV_EXP) && exp_left) {
            uint32_t n = min32(HALF_EXP, exp_left);
            stream(nstreams++, exp_src, RP_Q_EXP, n, CTR_PERIPH);
            total += n;
            exp_src += n << 2;
            exp_left -= n;
            if (!exp_left) pmask &= ~RP_EV_EXP;
        }
        replayer->EVENTS_MASK = pmask;                                /* before the next WFI */
        if (total) {
            tile->WRITE_COUNTER[CTR_PERIPH - 1] = total;
            start_runs(0, nstreams);
            wait_event(EV_CTR(CTR_PERIPH) | EV_RUN_DONE, loop_mask);  /* the squelch: credits returned meanwhile fall through */
            rp_results[RL_R_REFILLS]++;
        }
    }
    replayer->EVENTS_MASK = 0;
    tile->EVENTS_MASK = EV_RESET_MASK;
    return 1;
}

int main(void) {
    me = tile_index();
    const uint32_t base_mask = EV_RESET_MASK;
    copy_to_dm(0, hdr, RL_H_WORDS / 4, base_mask);
    if (hdr[RL_H_MAGIC] != RL_MAGIC || hdr[RL_H_VERSION] != RL_VERSION) { rp_results[RL_R_STATUS] = 4; return 4; }

    uint32_t nt = hdr[RL_H_NTEMPLATES];
    if (nt > 128) nt = 128;
    if (nt) copy_to_dm(hdr[RL_H_INDEX], ix, nt, base_mask);
    build_hash(nt);

    /* ROM word 1, the shared NOP row: the tables region holds 256 decode-table words, then the ROM */
    copy_to_dm(hdr[RL_H_TABLES] + ((256 + 1) << 2), nopword, 1, base_mask);

    /* the decode table and the ROM: one mode-0 run of 768 words into the replayer's region at 0x1000 (it is stopped) */
    wait_rd_idle();
    stream(0, hdr[RL_H_TABLES], RP_DECODE_TABLE, 768, CTR_PERIPH);
    tile->WRITE_COUNTER[CTR_PERIPH - 1] = 768;
    start_runs(0, 1);
    wait_event(EV_CTR(CTR_PERIPH) | EV_RUN_DONE, base_mask);

    /* part 0 through the send port, from byte 16 of the record (word 1 onward): the state word, the stream word, the image */
    copy_to_dm(hdr[RL_H_RECORD], record, 67, base_mask);
    for (uint32_t i = 0; i < 4; i++) bitmap[i] = record[i];          /* word 0: the template bitmap (dmtab reuses record) */
    store_log = (record[7] >> 19) & 1;                                /* the state word's flag [115]: includes the store log */
    send_to(ROUTER_HDR_W(me, MODE_WRITE, CTR_PERIPH), RP_STATE);
    tile->WRITE_COUNTER[CTR_PERIPH - 1] = 66;
    for (uint32_t i = 1; i < 67; i++) sendw(&record[4 * i]);
    wait_event(EV_CTR(CTR_PERIPH), base_mask);

    /* the fast path's tables (dmtab reuses the record's space) */
    wait_rd_idle();
    build_fast(nt);

    rp_results[RL_R_STATUS] = 1;
    for (uint32_t p = 0; p < hdr[RL_H_PASSES]; p++) {
        if (!run_pass()) { rp_results[RL_R_STATUS] = 2; break; }
        rp_results[RL_R_PASSES] = p + 1;
        if (tile->ERRORS) { rp_results[RL_R_STATUS] = 3; break; }
    }
    rp_results[RL_R_CONDITIONS] = tile->ERRORS;
    tile->EVENTS_MASK = EV_RESET_MASK;                                /* the reset mask, as found */
    return (int)rp_results[RL_R_STATUS];
}
