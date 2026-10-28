/* split.c -- design v1's bisection split (Sorter v1.0: S1 with carried brackets,
 * early exit), ported to design v2's controller read port (spec 4.3).  See split.h
 * for the interface and BINDING.md for the phases.
 *
 * Owner q (tile q, 1..63) looks for K* = max{v : L(v) <= R_q}, L(v) = #records with
 * key < v; tile 0 owns nothing.  Every round, in lockstep:
 *   barrier; probe exchange (32-bit replies, landing by source): owner q sends its probe v_q to every tile,
 *       or, once resolved, its previous probe again (the done marker: a live
 *       bisection never repeats a probe);
 *   counting: each tile, for every owner whose probe is new, finds lb(v) and
 *       ub(v) in its run inside the bracket it carries for that owner, with the
 *       spec 4.3 search loop (all active searches' midpoints, WFI, update);
 *       identical (v, bracket) pairs are searched once (v1's COPY);
 *   barrier; counts exchange (64-bit replies): tile p sends (lb, ub) to each owner as one
 *       64-bit reply (exact, no saturation), landing at REPLY[2p], REPLY[2p + 1];
 *   owner update: L = sum less, E = sum eq.  L = R: done (c_p = less_p).  L > R:
 *       hi = v - 1.  L < R < L + E: done, the tie step at v.  Else lo = v + 1.
 *       If lo = hi, K* = lo is known without probing it: lb_p(K*) is the ub at the
 *       last probe below, ub_p(K*) the lb at the last probe above (both kept),
 *       and the tie step resolves it.  So at most 32 probes, and the round count
 *       is fixed at 32 (lead, 3 Oct).
 * Then barrier; XA (32-bit replies): owner q sends c[p][q] to tile p.
 * Spec 2026-10-26n: replies land by source in the reply block (REPLY[2 s]), so each exchange's words are read there
 * (or copied out) before the next exchange, which a barrier guarantees; read-port results count in DMA_CM_REMAINING.
 */
#include "split.h"
#include "split_regs.h"
#ifndef SPLIT_SEED
#define SPLIT_SEED 1            /* the seed bracket (balanced targets) */
#endif
#ifndef SEED_T
#define SEED_T 26               /* largest seed width used, in bits (measured best: RESULTS 4) */
#endif

static const uint8_t SCHED[64] = {
    1, 29, 35, 63,  2, 30, 34, 62,  3, 31, 33, 61,  5, 23, 41, 59,
    6, 22, 42, 58,  7, 21, 43, 57,  8, 28, 36, 56,  9, 27, 37, 55,
   10, 26, 38, 54, 11, 25, 39, 53, 12, 24, 40, 52, 13, 15, 49, 51,
   14, 18, 46, 50, 16, 20, 44, 48, 17, 19, 45, 47,  0,  4, 32, 60 };

volatile uint32_t split_steps[SPLIT_ROUNDS + 1];
volatile uint32_t split_resolved_round;

/* receive buffers: the reply block, by source.  One is enough: every exchange
 * starts after a barrier, which a tile reaches only after it has used the
 * previous exchange's words.  Owner q's probe at REPLY[2q]; tile p's seed sample
 * for me at REPLY[2p] (SEED); tile p's (lb, ub) at REPLY[2p], REPLY[2p + 1]. */
#define RB SR_REPLY

/* tile side, per owner: the last probe, the carried bracket, its lb and ub */
/* O5 (design v1's layout): the per-owner state is one struct-of-arrays block, so one
 * pointer p = SA + q reaches every field at a constant offset */
static uint32_t SA[6 * 64];
enum { F_TV = 0, F_PLO = 64, F_PHI = 128, F_LB = 192, F_UB = 256, F_K = 320 };
#define tv  (SA + F_TV)                     /* the owner's last probe */
#define plo (SA + F_PLO)                    /* the carried bracket [plo, phi] */
#define phi (SA + F_PHI)
#define tlb (SA + F_LB)                     /* lb and ub of the last probe */
#define tub (SA + F_UB)
#define tk  (SA + F_K)                      /* bits(phi - plo), counted down as the bracket shrinks */
static uint8_t trivial[64];
static const uint8_t kbits[8] = { 0, 1, 2, 2, 3, 3, 3, 3 };   /* bits(y), y < 8 (design v1) */

/* searches: the owners to search for (inputs are rebuilt from the owner arrays);
 * read-port results land in got[] */
static uint32_t got[64];

/* owner side: ub at the last probe below, lb at the last probe above, the cuts */
static uint32_t ubA[64], lessB[64], cq[64];

/* The barrier (patch, worker 8, 3 Oct 2026, lead's agreement): by default worker 4's own (done, WFI); a caller may
 * supply its own with -DSPLIT_BARRIER=fn, a function of no arguments that pulses done, WFIs and reads the command word
 * in command slot 0, as mesh.h's wait_for_command() does (lead, 3 Oct: every wake from a command reads slot 0). */
#ifdef SPLIT_BARRIER
void SPLIT_BARRIER(void);
static void barrier(void) { SPLIT_BARRIER(); }
#else
static void barrier(void) { TILE_PULSE = TILE_PULSE_DONE; wfi(); }
#endif

/* The spec 4.3 search loop over the m searches in s_* (s_lo = plo, s_len = the
 * bracket's width w: the answer is in [plo, plo + w]).  Lower bounds (upper = 0:
 * the first record >= key) or upper bounds (1: the first record > key).
 * Design v1's shared step (BS_SHARED): search j takes K_j = bits(w_j) steps; sorted
 * by K, they all use one step size h per step, and a search joins when h reaches
 * 2^(K_j - 1), so the searches active at a step are a prefix and need no
 * compaction.  A joining search's first step is Shar's: probe plo + h - 1; if that
 * record is below the key, the answer is in [plo + h, plo + w], which fits the
 * window of h answers ending at plo + w, so base = plo + w + 1 - h, else base =
 * plo.  Every later step: probe base + h - 1; below the key: base += h.  So the
 * state per search is base (and its key); every probe stays inside the bracket,
 * hence inside the run.  Results go to res[id]. */
static uint32_t s_base[64], s_top[64], s_k[64];
static uint8_t s_cnt[34], s_ord[64];

/* The step loop over the searches already placed in s_base / s_top / s_k / s_ord,
 * sorted by depth (cnt[k] of depth k, kmax the largest), lower or upper bounds.
 * See the comment above for the shared step and Shar's first step.  Returns the
 * search steps; s_base[i] ends as the bound. */
static uint32_t step_loop(uint32_t kmax, const uint8_t *cnt, uint32_t run, int upper)
{
    uint32_t steps = 0, act = 0, gw = SR_RD_DEST32(CM_WORD(got));
    TRACE(0x600);
    for (uint32_t k = kmax; k >= 1; k--) {
        uint32_t h = 1u << (k - 1), old = act;
        act += cnt[k];
        DMA_CM_REMAINING = act;                                  /* a write adds: the count is 0 here */
        SRAM_RD_DEST_ADDR = gw;
        uint32_t a = run + ((h - 1) << 1);
        const uint32_t *bp = s_base, *b4 = s_base + (act & ~3u), *be = s_base + act;
        for (; bp != b4; bp += 4) {                              /* issue, unrolled by 4 */
            SRAM_RD_ADDR_QUEUE = a + (bp[0] << 1); SRAM_RD_ADDR_QUEUE = a + (bp[1] << 1);
            SRAM_RD_ADDR_QUEUE = a + (bp[2] << 1); SRAM_RD_ADDR_QUEUE = a + (bp[3] << 1);
        }
        for (; bp != be; bp++) SRAM_RD_ADDR_QUEUE = a + (bp[0] << 1);
        wfi();
        steps += act;
        uint32_t *b = s_base;
        const uint32_t *g = got, *kk = s_k, *g4 = got + (old & ~3u), *go = got + old;
        if (upper) {
            for (; g != g4; g += 4, kk += 4, b += 4) {
                if (g[0] <= kk[0]) b[0] += h;
                if (g[1] <= kk[1]) b[1] += h;
                if (g[2] <= kk[2]) b[2] += h;
                if (g[3] <= kk[3]) b[3] += h;
            }
            for (; g != go; g++, kk++, b++) if (g[0] <= kk[0]) b[0] += h;
            for (uint32_t i = old; i < act; i++) if (got[i] <= s_k[i]) s_base[i] = s_top[i] - h;
        } else {
            for (; g != g4; g += 4, kk += 4, b += 4) {
                if (g[0] < kk[0]) b[0] += h;
                if (g[1] < kk[1]) b[1] += h;
                if (g[2] < kk[2]) b[2] += h;
                if (g[3] < kk[3]) b[3] += h;
            }
            for (; g != go; g++, kk++, b++) if (g[0] < kk[0]) b[0] += h;
            for (uint32_t i = old; i < act; i++) if (got[i] < s_k[i]) s_base[i] = s_top[i] - h;
        }
    }
    TRACE(0x601);
    return steps;
}

/* place the m searches listed in s_qk (with s_cnt counted) into depth
 * order; lower bounds take their bracket from plo / phi, upper bounds from
 * tlb + 1 / phi.  Returns kmax; clears s_cnt[0] (depth-0 entries are not placed). */
static uint8_t start_[34];
static uint16_t s_qk[64];                  /* O5b: a search to place, owner | depth << 8 */
static uint32_t place(uint32_t m, int upper, uint32_t kmax)
{
    uint32_t pos = 0;
    for (uint32_t k = kmax; k >= 1; k--) { start_[k] = (uint8_t)pos; pos += s_cnt[k]; }
    start_[0] = (uint8_t)pos;                   /* depth 0 (no step): after all the others */
    for (uint32_t i = 0; i < m; i++) {
        uint32_t x = s_qk[i], k = x >> 8, j = start_[k]++, q = x & 0xFF;
        const uint32_t *p = SA + q;
        s_base[j] = upper ? p[F_LB] + 1 : p[F_PLO];
        s_top[j] = p[F_PHI] + 1;
        s_k[j] = p[F_TV];
        s_ord[j] = (uint8_t)q;
    }
    s_cnt[0] = 0;
    return kmax;
}

/* the active-owner list (not trivial, no done marker seen yet), in order */
static uint8_t alist[64], nalist;

/* the counting step for one round: every owner with a new probe.  One pass over
 * the active owners (one pointer each: O5) carries the bracket, drops done owners,
 * deduplicates, counts the depth down (v1's 3-bit table) and buckets the search;
 * then the lb steps; then one read of key[min(lb, n - 1)] per search, all issued
 * without a branch (v1), and the ub steps for the matches. */
static uint32_t count_round(const volatile uint32_t *pv, uint32_t run, uint32_t n_me, int first)
{
    static uint8_t copy_of[64], cpy[64];
    uint32_t m = 0, pvv = 0, pplo = 0, pphi = 0, prev = 64, ncp = 0, na = 0, kmax = 0;
    for (uint32_t i = 0; i < nalist; i++) {
        uint32_t q = alist[i], v = pv[2 * q];                   /* owner q's probe: REPLY[2q] */
        uint32_t *p = SA + q;
        uint32_t lo = p[F_PLO], hi = p[F_PHI];
        if (!first) {
            uint32_t bv = p[F_TV];
            if (v == bv) continue;                               /* done marker: drop the owner */
            if (v > bv) p[F_PLO] = lo = p[F_UB]; else p[F_PHI] = hi = p[F_LB];
        }
        alist[na++] = (uint8_t)q;
        p[F_TV] = v;
        if (v == pvv && lo == pplo && hi == pphi && prev < 64) { copy_of[q] = (uint8_t)prev; cpy[ncp++] = (uint8_t)q; continue; }
        prev = q; pvv = v; pplo = lo; pphi = hi;
        uint32_t w = hi - lo, k = p[F_K], sh = k > 3 ? k - 3 : 0, y = w >> sh;
        if (y || !sh) k = sh + kbits[y];
        else { k = sh; while (k && !(w >> (k - 1))) k--; }        /* shrank more than 8x: count down */
        p[F_K] = k;
        if (k > kmax) kmax = k;
        s_qk[m++] = (uint16_t)(q | k << 8); s_cnt[k]++;
    }
    nalist = (uint8_t)na;
    uint32_t steps = 0;
    if (m) {
        place(m, 0, kmax);
        steps = kmax ? step_loop(kmax, s_cnt, run, 0) : 0;
        for (uint32_t k = 1; k <= kmax; k++) s_cnt[k] = 0;
        /* key[min(lb, n - 1)] for every search, no branch (lb = n reads key[n - 1] < v);
         * an empty run has every bound 0 and nothing to read */
        if (!n_me) { for (uint32_t i = 0; i < m; i++) { uint32_t *p = SA + s_ord[i]; p[F_LB] = p[F_UB] = 0; } m = 0; }
        TRACE(0x600);
        uint32_t last = run + ((n_me - 1) << 1);
        if (m) {
        DMA_CM_REMAINING = m;
        SRAM_RD_DEST_ADDR = SR_RD_DEST32(CM_WORD(got));
        for (uint32_t i = 0; i < m; i++) {
            uint32_t a = run + (s_base[i] << 1);
            SRAM_RD_ADDR_QUEUE = a < last ? a : last;
        }
        wfi();
        }
        TRACE(0x601);
        steps += m;
        uint32_t e = 0, kmax2 = 0;
        for (uint32_t i = 0; i < m; i++) {
            uint32_t q = s_ord[i], b = s_base[i];
            uint32_t *p = SA + q;
            p[F_LB] = p[F_UB] = b;
            if (got[i] != s_k[i] || b >= n_me) continue;
            uint32_t w = p[F_PHI] - b - 1, k = 0;                  /* ub in [lb + 1, phi] */
            uint32_t q_ = q;
            if (w >> 16) { k = 16; w >>= 16; }
            if (w >> 8) { k += 8; w >>= 8; }
            if (w >> 4) { k += 4; w >>= 4; }
            if (w >> 2) { k += 2; w >>= 2; }
            k += w > 1 ? 2 : w;
            if (!k) { p[F_UB] = b + 1; continue; }
            if (k > kmax2) kmax2 = k;
            s_qk[e++] = (uint16_t)(q_ | k << 8); s_cnt[k]++;
        }
        if (e) {
            place(e, 1, kmax2);
            steps += step_loop(kmax2, s_cnt, run, 1);
            for (uint32_t k = 1; k <= kmax2; k++) s_cnt[k] = 0;
            for (uint32_t i = 0; i < e; i++) tub[s_ord[i]] = s_base[i];
        }
    }
    for (uint32_t i = 0; i < ncp; i++) {                     /* copies */
        uint32_t q = cpy[i], src = copy_of[q];
        tlb[q] = tlb[src]; tub[q] = tub[src];
    }
    return steps;
}

/* the tie step: c_p = lb_p + clamp(R - L - sum_{p' < p} eq_p', 0, eq_p), with
 * lb_p = lbv[p * st], eq_p = ubv[p * st] - lbv[p * st] */
static void tie(const volatile uint32_t *lbv, const volatile uint32_t *ubv, uint32_t st, uint32_t L, uint32_t R)
{
    uint32_t before = 0, want = R - L;
    for (uint32_t p = 0; p < 64; p++) {
        uint32_t eq = ubv[p * st] - lbv[p * st], take = 0;
        if (want > before) { take = want - before; if (take > eq) take = eq; }
        cq[p] = lbv[p * st] + take;
        before += eq;
    }
}

void split_get_cuts(uint32_t run, uint32_t n_me, uint32_t shaped, uint32_t c[65], uint32_t n[64])
{
    uint32_t me = ME();
    while (SRAM_RD_ADDR_QUEUE < 64) ;                           /* spec 4.3: once, before the first step */

    /* n[]: every tile's run length */
    DMA_REPLY_REMAINING = 64;
    for (uint32_t s = 0; s < 64; s++) {
        uint32_t dst = (me + SCHED[s]) & 63;
        ROUTER_SET_HDR = ROUTER_HDR(dst, SR_MODE_REPLY, 0);
        ROUTER_SEND_32 = n_me;
    }
    wfi();
    for (uint32_t p = 0; p < 64; p++) n[p] = RB[2 * p];
    TRACE(0x100);
    uint32_t N = 0, acc = 0, R = 0, k0 = 0;
    while (n_me >> k0) k0++;                                     /* bits(n_me): every owner's first depth */
    for (uint32_t p = 0; p < 64; p++) N += n[p];
    for (uint32_t q = 0; q < 64; q++) {
        uint32_t rq = shaped ? acc : (uint32_t)(((uint64_t)q * N) >> 6);
        if (q == me) R = rq;
        acc += n[q];
        trivial[q] = (q == 0 || rq == 0 || rq == N);
        plo[q] = 0; phi[q] = n_me; tlb[q] = tub[q] = 0; tk[q] = k0;
    }
    nalist = 0;
    for (uint32_t q = 1; q < 64; q++) if (!trivial[q]) alist[nalist++] = (uint8_t)q;
    static uint32_t hp[64];                                      /* exchange headers (replies), schedule order */
    static uint8_t so[64];                                       /* the owner (destination) of schedule step s */
    for (uint32_t s = 0; s < 64; s++) {
        uint32_t dst = (me + SCHED[s]) & 63;
        so[s] = (uint8_t)dst;
        hp[s] = ROUTER_HDR(dst, SR_MODE_REPLY, 0);
    }
    uint32_t lo = 0, hi = 0xFFFFFFFFu, done = trivial[me], last = 0;
    uint32_t lo_known = 1, hi_known = 1;                         /* ubA / lessB hold lo's lb / hi + 1's lb */
#if SPLIT_SEED
    /* The seed bracket (balanced targets): tile p's key at j_p = (q n_p) >> 6 for owner q.
     * sum_p j_p <= R_q (floors) and sum over non-empty p of (j_p + 1) > q N / 64 >= R_q,
     * so with m and M the smallest and largest of owner q's samples, L(m) <= R_q < L(M + 1):
     * K* is in [m, M].  One read batch, one exchange, one barrier. */
    if (!shaped) {
        uint32_t *mine = got;                                    /* free until round 1 */
        if (n_me) {
            DMA_CM_REMAINING = 64;
            SRAM_RD_DEST_ADDR = SR_RD_DEST32(CM_WORD(mine));
            uint32_t acc = 0;
            for (uint32_t q = 0; q < 64; q++, acc += n_me) SRAM_RD_ADDR_QUEUE = run + ((acc >> 6) << 1);
            wfi();
        }
        barrier();
        DMA_REPLY_REMAINING = 64;
        for (uint32_t s = 0; s < 64; s++) { ROUTER_SET_HDR = hp[s]; ROUTER_SEND_32 = mine[so[s]]; }
        wfi();
        if (!done) {
            uint32_t mn = 0xFFFFFFFFu, mx = 0;
            for (uint32_t p = 0; p < 64; p++) {
                if (!n[p]) continue;
                uint32_t x = RB[2 * p];                          /* tile p's seed sample for me */
                if (x < mn) mn = x;
                if (x > mx) mx = x;
            }
            /* only a narrow seed (bits(mx - mn) <= SEED_T) is used: a wide one keeps the full
             * key space, where v1's dedup (one search for all owners on the same path) is worth
             * more than the rounds the seed would save (RESULTS 4) */
            uint32_t x = mx - mn, k = 0;
            while (k < 32 && (x >> k)) k++;
            if (k <= SEED_T) { lo = mn; hi = mx; lo_known = hi_known = 0; }
        }
    }
#endif
    split_resolved_round = 0;
    for (uint32_t p = 0; p < 64; p++) {
        ubA[p] = 0; lessB[p] = n[p];
        cq[p] = (me == 0 || R == 0) ? 0 : (R == N ? n[p] : 0);
    }

    /* Early exit (lead, 3 Oct): every tile sees every owner's probe or done marker each
     * round, so the owner list (alist) empties on the same round on every tile.  From
     * then on a tile only answers the remaining round barriers (C&C's sequence stays
     * fixed: 2 per round), then XA.  An empty list before round 1 (every owner
     * trivial) skips every round. */
    uint32_t nb = 0;                                             /* round barriers taken */
    for (uint32_t r = 1; r <= SPLIT_ROUNDS && nalist; r++) {
        barrier(); nb++;
        /* probe exchange */
        if (!done) last = lo + ((hi - lo) >> 1);
        DMA_REPLY_REMAINING = 64;
        for (const uint32_t *h = hp, *e = hp + 64; h != e; h += 4) {     /* unrolled by 4 */
            ROUTER_SET_HDR = h[0]; ROUTER_SEND_32 = last; ROUTER_SET_HDR = h[1]; ROUTER_SEND_32 = last;
            ROUTER_SET_HDR = h[2]; ROUTER_SEND_32 = last; ROUTER_SET_HDR = h[3]; ROUTER_SEND_32 = last;
        }
        wfi();
        TRACE(0x200 | r);
        split_steps[r] = count_round(RB, run, n_me, r == 1);
        TRACE(0x300 | r);
        if (!nalist) break;                                      /* every owner has sent its done marker */
        barrier(); nb++;
        /* counts exchange: one 64-bit reply (lb, ub) per owner, landing at REPLY[2 me], REPLY[2 me + 1] there */
        DMA_REPLY_REMAINING = 64;
        /* to owners whose probe was a done marker: stale, ignored.  Unrolled by 4;
         * hp[s] = the header, so[s] = the owner's word offset */
        for (uint32_t s = 0; s < 64; s += 4) {
            uint32_t o0 = so[s], o1 = so[s + 1], o2 = so[s + 2], o3_ = so[s + 3];
            ROUTER_SET_HDR = hp[s];     ROUTER_SET_W0 = tlb[o0];  ROUTER_SEND_64 = tub[o0];
            ROUTER_SET_HDR = hp[s + 1]; ROUTER_SET_W0 = tlb[o1];  ROUTER_SEND_64 = tub[o1];
            ROUTER_SET_HDR = hp[s + 2]; ROUTER_SET_W0 = tlb[o2];  ROUTER_SEND_64 = tub[o2];
            ROUTER_SET_HDR = hp[s + 3]; ROUTER_SET_W0 = tlb[o3_]; ROUTER_SEND_64 = tub[o3_];
        }
        wfi();
        TRACE(0x400 | r);
        if (done) continue;
        /* owner update: sum lb; sum ub only when it decides something (L < R) */
        const volatile uint32_t *cc = RB;                        /* tile p's (lb, ub) at [2p], [2p + 1] */
        uint32_t L = 0, v = last;
        for (const volatile uint32_t *p = cc, *e = cc + 128; p != e; p += 8) L += p[0] + p[2] + p[4] + p[6];
        if (L == R) {
            for (uint32_t p = 0; p < 64; p++) cq[p] = cc[2 * p];
            done = 1;
        } else if (L > R) {
            hi = v - 1; hi_known = 1;
            for (uint32_t p = 0; p < 64; p += 4) {
                lessB[p] = cc[2 * p]; lessB[p + 1] = cc[2 * p + 2]; lessB[p + 2] = cc[2 * p + 4]; lessB[p + 3] = cc[2 * p + 6];
            }
        } else {
            uint32_t U = 0;
            for (uint32_t i = 1; i < 128; i += 8) U += cc[i] + cc[i + 2] + cc[i + 4] + cc[i + 6];
            if (R < U) {                                             /* L < R < L + E: the tie step at v */
                tie(cc, cc + 1, 2, L, R);
                done = 1;
            } else {
                lo = v + 1; lo_known = 1;
                for (uint32_t p = 0; p < 64; p += 4) {
                    ubA[p] = cc[2 * p + 1]; ubA[p + 1] = cc[2 * p + 3]; ubA[p + 2] = cc[2 * p + 5]; ubA[p + 3] = cc[2 * p + 7];
                }
            }
        }
        if (!done && lo == hi && lo_known && hi_known) {          /* else: the next probe is lo itself */                                 /* K* = lo, known without probing it */
            uint32_t Ls = 0;
            for (uint32_t p = 0; p < 64; p++) Ls += ubA[p];
            tie(ubA, lessB, 1, Ls, R);
            done = 1;
        }
        if (done) split_resolved_round = r;
    }

    while (nb < 2 * SPLIT_ROUNDS) { barrier(); nb++; }           /* the rounds not needed: barriers only */
    barrier();
    /* XA: owner me sends c[p][me] to tile p */
    DMA_REPLY_REMAINING = 64;
    for (uint32_t s = 0; s < 64; s++) {
        uint32_t dst = (me + SCHED[s]) & 63;
        ROUTER_SET_HDR = ROUTER_HDR(dst, SR_MODE_REPLY, 0);
        ROUTER_SEND_32 = cq[dst];
    }
    wfi();
    for (uint32_t q = 0; q < 64; q++) c[q] = RB[2 * q];
    c[64] = n_me;
    TRACE(0x500);
}
