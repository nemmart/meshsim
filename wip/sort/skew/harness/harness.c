/* harness.c -- host harness for the skew planner (RESULTS.md 1).
 *
 * For each data set (a 64 x 64 stream matrix from data/), it plays the 64
 * tiles' part of the sort from XB to the end of the values all-to-all:
 *  - every planner function runs natively on each tile's Tile and, in step,
 *    on the RV32 image in an emulator with the controller's timing; the two
 *    resulting states must be byte-identical, and the emulator's cycles are
 *    the measured planner cycles;
 *  - exchanges and reductions are done by the harness, as the machine would;
 *  - the chosen two-hop path is executed at record level on a model of every
 *    tile's big SRAM, from the streams, GROUP and PTR the planner wrote: every
 *    record must land where the direct all-to-all would put it (keys with
 *    their ordinal, values in the same order), with one run per pair per
 *    phase, no read beyond R, exact SRAM_REMAINING, no staging overwrite and
 *    disjoint read and write areas per phase;
 *  - costs follow the walk-through's rules (RESULTS.md, "Cost rules").
 *
 *   ./harness [--fmt 128|32] [--set SUBSTR] [--version 0|1] [--force th|bt] [--L n] [--noemu] [--tune] [-v] [--pcprof]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../planner/skew.h"
#include <stddef.h>
#include "rv32emu.h"

#define MEMCH (1u << 20)                 /* chunks per tile */
static Tile tiles[NT];
static uint32_t S[NT][NT];
static uint32_t *mem;                     /* [64][MEMCH] */
static uint8_t *wbit;                     /* [64][MEMCH / 8] written bitmap */
static int use_emu = 1, verbose = 0;
static uint32_t nkind = 2;              /* 2: 128-bit values; 1: 32-bit values (--fmt 32) */
static int fail = 0;

/* ------------------------------------------------------------ RV32 image */
typedef struct { char name[64]; uint32_t addr, size; } Sym;
static Sym syms[256]; static int nsyms;
static uint32_t g_tile_addr;

static uint32_t sym(const char *n)
{
    for (int i = 0; i < nsyms; i++) if (!strcmp(syms[i].name, n)) return syms[i].addr;
    fprintf(stderr, "no symbol %s\n", n); exit(1);
}

static void load_image(void)
{
    FILE *f = fopen("planner_rv.bin", "rb");
    if (!f) { perror("planner_rv.bin"); exit(1); }
    size_t n = fread(rv_mem, 1, RV_MEM, f); fclose(f);
    if (n >= 0x10000) { fprintf(stderr, "image too large\n"); exit(1); }
    f = fopen("planner_rv.syms", "r");
    char line[256];
    while (fgets(line, sizeof line, f) && nsyms < 256) {
        unsigned a, s; char t[64];
        if (sscanf(line, "%x %x %63s", &a, &s, t) == 3) {
            strcpy(syms[nsyms].name, t); syms[nsyms].addr = a; syms[nsyms].size = s; nsyms++;
        }
    }
    fclose(f);
    g_tile_addr = sym("g_tile");
    for (int i = 0; i < nsyms; i++)
        if (!strcmp(syms[i].name, "g_tile") && syms[i].size != sizeof(Tile)) {
            fprintf(stderr, "Tile size differs: host %zu, RV32 %u\n", sizeof(Tile), syms[i].size); exit(1);
        }
}

static struct { const char *n; uint64_t max, sum, calls; } prof[32]; static int nprof;
static void prof_add(const char *n, uint64_t c)
{
    int i; for (i = 0; i < nprof; i++) if (!strcmp(prof[i].n, n)) break;
    if (i == nprof) { prof[nprof].n = n; nprof++; }
    if (c > prof[i].max) prof[i].max = c;
    prof[i].sum += c; prof[i].calls++;
}
static void prof_print(void)
{
    for (int i = 0; i < nprof; i++)
        printf("    %-16s calls %6llu  max %7llu  mean %7llu\n", prof[i].n, (unsigned long long)prof[i].calls,
               (unsigned long long)prof[i].max, (unsigned long long)(prof[i].sum / prof[i].calls));
    nprof = 0;
}
typedef void (*F0)(Tile *); typedef void (*F1)(Tile *, uint32_t); typedef void (*F2)(Tile *, uint32_t, uint32_t);

/* run fn on tile t natively and on the emulator; compare; return cycles */
static uint64_t call(const char *name, int nargs, void *fp, int t, uint32_t a1, uint32_t a2)
{
    if (use_emu) memcpy(rv_mem + g_tile_addr, &tiles[t], sizeof(Tile));
    if (nargs == 0) ((F0)fp)(&tiles[t]); else if (nargs == 1) ((F1)fp)(&tiles[t], a1);
    else ((F2)fp)(&tiles[t], a1, a2);
    if (!use_emu) return 0;
    uint64_t c = rv_call(sym(name), g_tile_addr, a1, a2, NULL);
    prof_add(name, c);
    if (memcmp(rv_mem + g_tile_addr, &tiles[t], sizeof(Tile))) {
        const uint8_t *a = rv_mem + g_tile_addr, *b = (const uint8_t *)&tiles[t];
        size_t o = 0; while (a[o] == b[o]) o++;
        fprintf(stderr, "%s on tile %d: RV32 and host states differ at byte %zu\n", name, t, o); exit(1);
    }
    return c;
}
#define CALL0(fn, t)        call(#fn, 0, (void *)fn, t, 0, 0)
#define CALL1(fn, t, a)     call(#fn, 1, (void *)fn, t, a, 0)
#define CALL2(fn, t, a, b)  call(#fn, 2, (void *)fn, t, a, b)

/* every tile: returns the maximum cycles (the phase waits for the slowest) */
static uint64_t all0(const char *n, void *fp)
{ uint64_t mx = 0; for (int t = 0; t < NT; t++) { uint64_t c = call(n, 0, fp, t, 0, 0); if (c > mx) mx = c; } return mx; }
static uint64_t all1(const char *n, void *fp, uint32_t a)
{ uint64_t mx = 0; for (int t = 0; t < NT; t++) { uint64_t c = call(n, 1, fp, t, a, 0); if (c > mx) mx = c; } return mx; }
static uint64_t all2(const char *n, void *fp, uint32_t a, uint32_t b)
{ uint64_t mx = 0; for (int t = 0; t < NT; t++) { uint64_t c = call(n, 2, fp, t, a, b); if (c > mx) mx = c; } return mx; }
#define ALL0(fn)       all0(#fn, (void *)fn)
#define ALL1(fn, a)    all1(#fn, (void *)fn, a)
#define ALL2(fn, a, b) all2(#fn, (void *)fn, a, b)

#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "CHECK FAILED: " __VA_ARGS__); fprintf(stderr, "\n"); fail++; } } while (0)

/* -------------------------------------------------------------- memory */
static inline uint32_t *M_(int t, uint32_t a) { return &mem[(size_t)t * MEMCH + (a & (MEMCH - 1))]; }
static inline uint32_t rec_id(uint32_t s, uint32_t i) { return (s << 17) | i; }
static void val_of(uint32_t s, uint32_t i, uint32_t v[4])
{
    uint32_t id = rec_id(s, i);
    v[0] = id; v[1] = ~id; v[2] = id * 2654435761u; v[3] = 0xC0DE0000u | s;
}

static void init_memory(void)
{
    memset(mem, 0, (size_t)NT * MEMCH * 4);
    for (int s = 0; s < NT; s++) {
        uint32_t n = tiles[s].n[s];
        for (uint32_t i = 0; i < n; i++) {
            *M_(s, A3 + 2 * i) = rec_id(s, i); *M_(s, A3 + 2 * i + 1) = rec_id(s, i) * 2654435761u;  /* the 32-bit value */
            uint32_t v[4]; val_of(s, i, v);
            for (int c = 0; c < 4; c++) *M_(s, A5 + 4 * i + c) = v[c];
        }
    }
}

static void clear_wbits(void) { memset(wbit, 0, (size_t)NT * MEMCH / 8); }

/* -------------------------------------------------------- phase executor */
typedef struct { uint64_t xfer, setup; int phases; uint64_t xferK, setupK; } Cost;

/* one group run on every tile; areas: allowed read and write area masks */
static void exec_phase(const char *label, uint32_t kind, uint32_t streamb, uint32_t cntb,
                       uint32_t rdok, uint32_t wrok, int aug, Cost *cost)
{
    uint32_t P = kind ? 8 : 4, step = kind ? 4 : 2;
    uint32_t R[NG];
    for (int g = 0; g < NG; g++) {
        R[g] = tiles[0].hw.group_lo[cntb + g];
        for (int t = 1; t < NT; t++) CHECK(tiles[t].hw.group_lo[cntb + g] == R[g], "%s: R differs between tiles", label);
    }
    static uint32_t cur[NT][NT]; static uint32_t got[NT], rmask[NT], wmask[NT];
    for (int r = 0; r < NT; r++) { got[r] = 0; rmask[r] = wmask[r] = 0; for (int s = 0; s < NT; s++) cur[r][s] = tiles[r].hw.ptr[s]; }
    for (int s = 0; s < NT; s++) {
        int seen[NT] = {0};
        for (int g = 0; g < NG; g++)
            for (int i = 0; i < 4; i++) {
                uint32_t lo = tiles[s].hw.stream_lo[streamb + 4 * g + i], hi = tiles[s].hw.stream_hi[streamb + 4 * g + i];
                uint32_t src = lo & 0xFFFFF, cnt = hi & 0x3FFFF, d = (hi >> 18) & 63, mode = (hi >> 24) & 7, sz = (hi >> 27) & 3;
                if (!cnt) continue;
                CHECK(d == GROUP_OFF[4 * g + i], "%s: stream %d of tile %d has offset %u", label, 4 * g + i, s, d);
                /* meshsim M12 (9 October): augmenting is the sender's packet mode, 4, on hop 2's key streams with 128-bit values */
                CHECK(mode == (aug ? 4u : 2u) && sz == (kind ? 3u : 2u), "%s: stream mode/size", label);
                CHECK(cnt <= R[g], "%s: tile %d stream %d count %u > R %u (records lost)", label, s, 4 * g + i, cnt, R[g]);
                uint32_t r = (s + d) & 63;
                CHECK(!seen[r], "%s: two runs from %d to %d", label, s, r); seen[r] = 1;
                for (uint32_t e = 0; e < cnt; e++) {
                    uint32_t a = src + e * step;
                    rmask[s] |= 1u << (a >> 17); rmask[s] |= 1u << ((a + step - 1) >> 17);
                    for (uint32_t pk = 0; pk < step / 2; pk++) {
                        uint32_t tg = cur[r][s];
                        uint32_t c0 = *M_(s, a + 2 * pk), c1 = *M_(s, a + 2 * pk + 1);
                        for (int c = 0; c < 2; c++) {
                            uint32_t ta = (tg + c) & (MEMCH - 1);
                            uint8_t *wb = &wbit[(size_t)r * MEMCH / 8 + ta / 8];
                            CHECK(!(*wb & (1 << (ta & 7))), "%s: tile %d chunk %05x written twice", label, r, ta);
                            *wb |= 1 << (ta & 7);
                            wmask[r] |= 1u << (ta >> 17);
                        }
                        *M_(r, tg) = c0;
                        *M_(r, tg + 1) = mode == 4 ? (tg - tiles[r].hw.augment_base) >> 1 : c1;
                        cur[r][s] = tg + 2;
                        got[r]++;
                    }
                }
            }
    }
    for (int t = 0; t < NT; t++) {
        CHECK(got[t] == tiles[t].hw.sram_remaining, "%s: tile %d got %u packets, expected %u", label, t, got[t], tiles[t].hw.sram_remaining);
        CHECK(!(rmask[t] & ~rdok), "%s: tile %d reads areas %02x outside %02x", label, t, rmask[t], rdok);
        CHECK(!(wmask[t] & ~wrok), "%s: tile %d writes areas %02x outside %02x", label, t, wmask[t], wrok);
        CHECK(!(rmask[t] & wmask[t]), "%s: tile %d reads and writes area set %02x", label, t, rmask[t] & wmask[t]);
    }
    cost->xfer += run_cost(R, P, 0, 0);
    cost->phases++;
    if (verbose) printf("    %-10s R %u..  xfer %u\n", label, R[0], run_cost(R, P, 0, 0));
}

static void verify_inboxes(void)
{
    int bad = 0;
    for (int d = 0; d < NT && bad < 5; d++) {
        uint32_t x = 0;
        for (int s = 0; s < NT; s++)
            for (uint32_t t = 0; t < S[s][d]; t++, x++) {
                uint32_t i = tiles[s].rowpos[d] + t, v[4]; val_of(s, i, v);
                uint32_t hi = nkind == 2 ? x : rec_id(s, i) * 2654435761u;   /* ordinal, or the value */
                if (*M_(d, A4 + 2 * x) != rec_id(s, i) || *M_(d, A4 + 2 * x + 1) != hi) { bad++; break; }
                if (nkind == 2) for (int c = 0; c < 4; c++) if (*M_(d, A1 + 4 * x + c) != v[c]) { bad++; break; }
            }
    }
    CHECK(!bad, "inbox differs from the direct all-to-all's");
}

/* --------------------------------------------------------------- the run */
typedef struct {
    uint32_t path, costD, costLB, costTH, costBT, floor2, bt_est, w, L, L1, L2, stage;
    uint64_t plan, plan_parts[8], setup, xfer, total;
    uint64_t sweep, round_max, desc_max, footprint, xferK, setupK, empty;
    int phases;
} Result;

static const char *PN[] = { "direct", "LB", "TH", "BT", "plan" };

/* the fixed command sequence of skew-v2.md 5: K, V (direct or LB), XC, TH, SWEEP,
 * DESC, R3, then LMAX step-1 and LMAX step-2 layer phases for keys and for values;
 * a tile finishes a command that is empty for its path at once, ~30 cycles */
#define C_EMPTY 30
static uint64_t empties(uint32_t path, uint32_t L1, uint32_t L2)
{
    uint32_t n, k = nkind;                                             /* K and V, or K only */
    switch (path) {
    case PATH_DIRECT: case PATH_LB: n = 4 + 2 * k * LMAX; break;      /* XC, TH, SWEEP, DESC; all layers */
    case PATH_TH:  n = k + 2 + 2 * k * (LMAX - 1); break;             /* K (V); SWEEP, DESC; layers 1.. */
    default:       n = k + k * (LMAX - L1) + k * (LMAX - L2); break;  /* K (V); unused layers */
    }
    return (uint64_t)n * C_EMPTY;
}

static void load_set(const char *fn)
{
    char p[256]; snprintf(p, sizeof p, "data/%s", fn);
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    if (fread(S, 4, NT * NT, f) != NT * NT) { fprintf(stderr, "short %s\n", p); exit(1); }
    fclose(f);
}

static void init_tiles(void)
{
    uint32_t n[NT] = {0}, M[NT] = {0};
    for (int p = 0; p < NT; p++) for (int q = 0; q < NT; q++) { n[p] += S[p][q]; M[q] += S[p][q]; }
    memset(tiles, 0, sizeof tiles);
    for (int t = 0; t < NT; t++) {
        Tile *T = &tiles[t];
        T->me = t; T->nkind = nkind;
        for (int q = 0; q < NT; q++) { T->n[q] = n[q]; T->M[q] = M[q]; T->row[q] = S[t][q]; T->col[q] = S[q][t]; }
        for (int l = 0; l < LMAX; l++) for (int s = 0; s < NT; s++) T->held[l][s] = NONE32;
    }
}

static void same(const char *what, size_t off, size_t len)
{
    for (int t = 1; t < NT; t++)
        CHECK(!memcmp((uint8_t *)&tiles[t] + off, (uint8_t *)&tiles[0] + off, len), "%s differs on tile %d", what, t);
}
#define SAME(f) same(#f, offsetof(Tile, f), sizeof(((Tile *)0)->f))

/* force: 0 = the rule, 1 = TH, 2 = BT; Lforce: BT at a given L (0: selected) */
static Result run(uint32_t version, int force, uint32_t Lforce)
{
    Result res; memset(&res, 0, sizeof res);
    init_tiles();
    /* ---- stage 1: RED with the two LB words ---- */
    uint64_t c_prep = ALL0(sw_prepare);
    uint32_t red[NG + 2] = {0};
    for (int t = 0; t < NT; t++) for (int i = 0; i < NG + 2; i++) if (tiles[t].red_own[i] > red[i]) red[i] = tiles[t].red_own[i];
    for (int t = 0; t < NT; t++) {
        memcpy(tiles[t].Rdir, red, NG * 4); tiles[t].Rp15 = red[NG]; tiles[t].Rloop = red[NG + 1];
    }
    uint64_t c_s1 = ALL0(sw_stage1);
    SAME(path); SAME(costD); SAME(costLB); SAME(floor2);
    Tile *T0 = &tiles[0];
    res.costD = T0->costD; res.costLB = T0->costLB; res.floor2 = T0->floor2; res.costTH = res.costBT = NONE32;
    res.plan_parts[0] = (red_cost(NG + 2) - red_cost(NG)) + c_s1;      /* stage 1's extra: two RED words, the rule */
    (void)c_prep;                                                          /* sw_prepare is XB's tail (as in the walk-through) */
    uint32_t path = T0->path;
    if (path != PATH_PLAN && !force) {
        res.path = path; res.plan = res.plan_parts[0];
        res.empty = empties(path, 0, 0);
        res.total = (path == PATH_DIRECT ? res.costD : res.costLB) + res.plan + res.empty;
        return res;
    }
    /* ---- stage 2: XC, the TH table, R2 ---- */
    for (int p = 0; p < NT; p++) for (int q = 0; q < NT; q++) tiles[p].offo[q] = tiles[q].inpos[p];
    uint64_t c_th = ALL0(th_build);
    uint32_t r2[NG + 3] = {0};
    for (int t = 0; t < NT; t++) for (int i = 0; i < NG + 3; i++) if (tiles[t].r2_own[i] > r2[i]) r2[i] = tiles[t].r2_own[i];
    for (int t = 0; t < NT; t++) memcpy(tiles[t].r2, r2, sizeof r2);
    uint64_t c_s2 = ALL1(sw_stage2, version);
    SAME(path); SAME(costTH); SAME(bt_w); SAME(bt_L); SAME(bt_est);
    res.costTH = T0->costTH; res.bt_est = T0->bt_est;
    res.plan_parts[1] = C_XCH + c_th + red_cost(NG + 3) + c_s2;
    path = T0->path;
    if (force == 1) {
        CHECK(r2[NG] == 1, "TH forced with %u layers", r2[NG]);
        path = PATH_TH;
    }
    if (force == 2) path = PATH_BT;
    if (path == PATH_TH) {
        uint64_t c_d = ALL0(th_desc);
        for (int m = 0; m < NT; m++) for (int s = 0; s < NT; s++) tiles[m].desc_in[s] = tiles[s].th_desc_out[m];
        res.plan_parts[2] = c_d + C_XCH;
        Cost cost = {0};
        for (uint32_t kind = 0; kind < nkind; kind++) {
            clear_wbits();
            cost.setup += ALL1(th_hop1, kind) + C_B;
            exec_phase(kind ? "TH V1" : "TH K1", kind, kind ? 64 : 0, kind ? 16 : 0, kind ? 0x60 : 0x08, kind ? 0x89 : 0x03, 0, &cost);
            clear_wbits();
            cost.setup += ALL1(th_hop2, kind) + C_B;
            exec_phase(kind ? "TH V2" : "TH K2", kind, kind ? 64 : 0, kind ? 16 : 0, kind ? 0x89 : 0x03, kind ? 0x06 : 0x10, !kind && nkind == 2, &cost);
            if (!kind) { cost.xferK = cost.xfer; cost.setupK = cost.setup; }
        }
        verify_inboxes();
        res.path = PATH_TH; res.L = 1; res.L1 = res.L2 = 1;
        res.setup = cost.setup; res.xfer = cost.xfer; res.phases = cost.phases;
        res.xferK = cost.xferK; res.setupK = cost.setupK;
        for (int i = 0; i < 8; i++) res.plan += res.plan_parts[i];
        res.empty = empties(PATH_TH, 1, 1);
        res.total = res.plan + res.setup + res.xfer + res.empty;
        return res;
    }
    if (path != PATH_BT) {
        res.path = path;
        for (int i = 0; i < 8; i++) res.plan += res.plan_parts[i];
        res.empty = empties(path, 0, 0);
        res.total = (path == PATH_DIRECT ? res.costD : res.costLB) + res.plan + res.empty;
        return res;
    }
    /* ---- stage 3: BT ---- */
    if (Lforce) { for (int t = 0; t < NT; t++) bt_force(&tiles[t], Lforce); }
    res.w = T0->bt_w; res.L = T0->bt_L;
    uint64_t c_si = ALL0(bt_sweep_init);
    static uint8_t tok[NT][VL * 8];
    memset(tok, 0xFF, sizeof tok);
    static uint64_t cyc[NT][2 * NT];
    for (uint32_t pass = 0; pass < 2; pass++)
        for (uint32_t t = 0; t < NT; t++)
            for (int s = 0; s < NT; s++) {
                int col = (s + t) & 63;
                memcpy(tiles[s].u.sw.cmask, tok[col], VL * 8);
                cyc[s][pass * NT + t] = CALL2(bt_sweep_cell, s, pass, t);
                memcpy(tok[col], tiles[s].u.sw.cmask, VL * 8);
            }
    /* the token ring as a pipeline: round r on tile s waits for its own round r-1
     * and for tile s+1's round r-1 plus the token's trip (8 mode-5 packets of
     * 3 stores, latency 7, CM_REMAINING and the wake: 45 [estimate]) */
    {
        static uint64_t tm[NT], nt_[NT];
        uint64_t rmax = 0;
        for (int s = 0; s < NT; s++) tm[s] = 0;
        for (int r = 0; r < 2 * NT; r++) {
            for (int s = 0; s < NT; s++) {
                uint64_t ready = r ? (tm[(s + 1) & 63] + 45 > tm[s] ? tm[(s + 1) & 63] + 45 : tm[s]) : 0;
                nt_[s] = ready + cyc[s][r];
                if (cyc[s][r] > rmax) rmax = cyc[s][r];
            }
            memcpy(tm, nt_, sizeof tm);
        }
        uint64_t mx = 0; for (int s = 0; s < NT; s++) if (tm[s] > mx) mx = tm[s];
        res.sweep = mx; res.round_max = rmax;
    }
    res.plan_parts[3] += c_si + ALL0(bt_sweep_done);
    /* bt_sweep_done finds the own L1; a one-word reduction makes it global;
     * then one descriptor exchange per step-1 layer */
    uint32_t L1 = 0;
    {
        uint64_t cd = 0, dmax = 0;
        for (uint32_t layer = 0; ; layer++) {
            uint64_t c = ALL1(bt_desc, layer);
            if (layer == 0) for (int t = 0; t < NT; t++) if (tiles[t].L1own > L1) L1 = tiles[t].L1own;
            for (int m = 0; m < NT; m++) for (int s = 0; s < NT; s++) tiles[m].held[layer][s] = tiles[s].u.desc_out[m];
            cd += c + C_XCH; if (c > dmax) dmax = c;
            if (layer + 1 >= L1 || layer + 1 >= LMAX) break;
        }
        res.desc_max = dmax;
        res.plan_parts[3] += res.sweep + red_cost(1) + cd;
    }
    uint64_t c_hs = ALL0(bt_held_stats);
    if (verbose) {
        uint32_t e = 0, st = 0, pm = 0, l2 = 0, mv = 0;
        for (int t = 0; t < NT; t++) { e |= tiles[t].err; if (tiles[t].stage_own > st) st = tiles[t].stage_own;
            if (tiles[t].pcur > pm) pm = tiles[t].pcur;
            if (tiles[t].L2own > l2) l2 = tiles[t].L2own;
            }
        printf("    errors %x  staging max %u  pieces max %u  L1 %u  L2 %u\n", e, st, pm, L1, l2); (void)mv;
    }
    uint32_t r3[3 + 2 * (LMAX + 1)] = {0};
    for (int t = 0; t < NT; t++) {
        Tile *T = &tiles[t];
        uint32_t v[3 + 2 * (LMAX + 1)];
        v[0] = T->L1own; v[1] = T->L2own; v[2] = T->err ? NONE32 : T->stage_own;
        for (int l = 0; l <= LMAX; l++) { v[3 + l] = T->R1own[l]; v[3 + LMAX + 1 + l] = T->R2own[l]; }
        for (int i = 0; i < 3 + 2 * (LMAX + 1); i++) if (v[i] > r3[i]) r3[i] = v[i];
    }
    for (int t = 0; t < NT; t++) memcpy(tiles[t].r3, r3, sizeof r3);
    uint64_t c_dec = ALL0(bt_decide);
    SAME(path); SAME(costBT);
    res.plan_parts[4] = c_hs + red_cost(3 + 2 * (LMAX + 1)) + c_dec;
    res.costBT = T0->costBT; res.L1 = r3[0]; res.L2 = r3[1]; res.stage = r3[2];
    for (int i = 0; i < 8; i++) res.plan += res.plan_parts[i];
    /* footprint of the BT working state */
    {
        uint32_t pmax = 0; for (int t = 0; t < NT; t++) if (tiles[t].pcur > pmax) pmax = tiles[t].pcur;
        res.footprint = 8 + pmax + NT + NT + 8 * VL + 256 + 4 * NT * (L1 > LMAX ? LMAX : L1) + 12;
    }
    if (T0->path != PATH_BT && !force) {
        res.path = T0->path;
        res.empty = empties(res.path, 0, 0);
        res.total = (res.path == PATH_DIRECT ? res.costD : res.costLB) + res.plan + res.empty;
        return res;
    }
    if (T0->costBT == NONE32) {
        fprintf(stderr, "  BT infeasible: L1 %u L2 %u staging %u\n", r3[0], r3[1], r3[2]);
        res.path = PATH_BT; res.total = NONE32;
        return res;
    }
    Cost cost = {0};
    uint32_t L2 = r3[1];
    for (uint32_t kind = 0; kind < nkind; kind++) {
        char lb[32];
        clear_wbits();
        for (uint32_t l = 0; l < L1; l++) {
            if (l % NBANK == 0) cost.setup += ALL2(bt_hop1_src, l, kind);
            cost.setup += ALL2(bt_hop1_m, l, kind) + C_B;
            snprintf(lb, sizeof lb, "BT %c1.%u", kind ? 'V' : 'K', l);
            exec_phase(lb, kind, 64 * (l % NBANK), 16 * (l % NBANK), kind ? 0x60 : 0x08, kind ? 0x89 : 0x03, 0, &cost);
        }
        clear_wbits();
        for (uint32_t l = 0; l < L2; l++) {
            uint64_t a = ALL2(bt_hop2_m, l, kind);
            for (int d = 0; d < NT; d++) for (int m = 0; m < NT; m++) tiles[d].u.h2.in2[m] = tiles[m].u.h2.out2[d];
            /* in2 and out2 share the buffer with out2 first: deliver after every m has built out2 */
            uint64_t b = ALL2(bt_hop2_d, l, kind);
            cost.setup += a + C_XCH + b + C_B;
            snprintf(lb, sizeof lb, "BT %c2.%u", kind ? 'V' : 'K', l);
            exec_phase(lb, kind, 0, 0, kind ? 0x89 : 0x03, kind ? 0x06 : 0x10, !kind && nkind == 2, &cost);
        }
        if (!kind) { cost.xferK = cost.xfer; cost.setupK = cost.setup; }
    }
    verify_inboxes();
    res.path = PATH_BT;
    res.setup = cost.setup; res.xfer = cost.xfer; res.phases = cost.phases;
    res.xferK = cost.xferK; res.setupK = cost.setupK;
    res.empty = empties(PATH_BT, L1, L2);
    res.total = res.plan + res.setup + res.xfer + res.empty;
    return res;
}

static void kfmt(char *b, uint64_t x) { if (x == NONE32) strcpy(b, "-"); else sprintf(b, "%.1fK", x / 1000.0); }

int main(int argc, char **argv)
{
    const char *only = NULL; uint32_t version = 1, L = 0; int force = 0, tune = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--set")) only = argv[++i];
        else if (!strcmp(argv[i], "--version")) version = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--force")) { i++; force = !strcmp(argv[i], "th") ? 1 : 2; }
        else if (!strcmp(argv[i], "--L")) L = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--noemu")) use_emu = 0;
        else if (!strcmp(argv[i], "--tune")) tune = 1;
        else if (!strcmp(argv[i], "--fmt")) nkind = atoi(argv[++i]) == 32 ? 1 : 2;
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "--pcprof")) rv_prof = 1;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 1; }
    }
    if (use_emu) load_image();
    mem = malloc((size_t)NT * MEMCH * 4); wbit = malloc((size_t)NT * MEMCH / 8);
    if (!mem || !wbit) { fprintf(stderr, "out of memory\n"); return 1; }
    FILE *lf = fopen("data/list.txt", "r");
    char line[256];
    while (fgets(line, sizeof line, lf)) {
        char fn[128], name[128];
        if (sscanf(line, "%127[^\t]\t%127[^\n]", fn, name) != 2) continue;
        if (only && !strstr(name, only)) continue;
        load_set(fn);
        if (tune) {
            /* BT at every L, and the selections of both estimate versions */
            printf("%s\n", name);
            for (uint32_t l = 2; l <= LMAX; l++) {
                init_tiles(); init_memory();
                Result r = run(version, 2, l);
                char a[16], b[16], c[16];
                kfmt(a, r.total); kfmt(b, r.plan); kfmt(c, r.xfer + r.setup);
                printf("  L=%u w=%u layers %u/%u  total %s (plan %s, transfers+set-up %s) footprint %llu %s\n",
                       l, r.w, r.L1, r.L2, a, b, c, (unsigned long long)r.footprint, fail ? "FAIL" : "ok");
            }
            for (uint32_t v = 0; v < 2; v++) {
                init_tiles(); init_memory();
                Result r = run(v, 2, 0);
                char a[16], e[16]; kfmt(a, r.total); kfmt(e, r.bt_est);
                printf("  version %u selects L=%u w=%u (estimate %s): total %s\n", v, r.L, r.w, e, a);
            }
            continue;
        }
        init_tiles(); init_memory();
        Result r = run(version, force, L);
        char d[16], lb[16], th[16], bt[16], fl[16], pl[16], tot[16], sw[16];
        kfmt(d, r.costD); kfmt(lb, r.costLB); kfmt(th, r.costTH); kfmt(bt, r.costBT); kfmt(fl, r.floor2);
        kfmt(pl, r.plan); kfmt(tot, r.total); kfmt(sw, r.sweep);
        printf("%-26s %-6s total %8s  plan %7s | D %8s LB %8s TH %8s BT %8s floor %7s", name, PN[r.path], tot, pl, d, lb, th, bt, fl);
        if (r.path == PATH_BT || r.w)
            printf(" | w %u L %u layers %u/%u sweep %s round<=%llu desc<=%llu stage %u mem %llu",
                   r.w, r.L, r.L1, r.L2, sw, (unsigned long long)r.round_max, (unsigned long long)r.desc_max,
                   r.stage, (unsigned long long)r.footprint);
        if (r.phases) printf(" | xfer %.1fK setup %.1fK (%d phases; keys %.1fK + %.1fK)", r.xfer / 1000.0, r.setup / 1000.0,
                             r.phases, r.xferK / 1000.0, r.setupK / 1000.0);
        printf(" | empty cmds %.2fK | plan parts", r.empty / 1000.0);
        for (int i = 0; i < 5; i++) printf(" %.1fK", r.plan_parts[i] / 1000.0);
        printf("  %s\n", fail ? "FAIL" : "ok");
        if (verbose) prof_print(); else nprof = 0;
        fflush(stdout);
    }
    fclose(lf);
    if (rv_prof) {
        FILE *f = fopen("pcprof.txt", "w");
        for (int i = 0; i < 0x2000; i++) if (rv_hist[i]) fprintf(f, "%x %llu\n", i * 4, (unsigned long long)rv_hist[i]);
        fclose(f);
    }
    if (fail) { fprintf(stderr, "%d checks failed\n", fail); return 1; }
    return 0;
}
