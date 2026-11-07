/* skew.h -- skew handling for design v2: the per-tile planner (skew-v2.md).
 *
 * Portable C99 for RV32I + Zmmul (no divide): no '/' or '%' by a variable,
 * no pointers in the state, only fixed-width integers, so the struct has the
 * same layout on the host (harness) and on the RV32 build (cycle counts).
 *
 * One Tile holds what one tile's controller holds.  Fields marked [global]
 * are identical on every tile; the harness fills them as the exchanges and
 * reductions would.  Fields under "BT working state" are the data-memory
 * footprint budgeted at <= 2 KB (skew-v2.md section 8).
 */
#ifndef SKEW_H
#define SKEW_H
#include <stdint.h>

#define NT        64
#define NG        16
#define WCAP      1024u          /* TH staging slot per destination (W_q <= 1024: M_q <= 65,536) */
#define LMAX      5              /* BT layers held (step 1 and step 2) */
#define PMAX      300            /* BT pieces per source row: with LMAX 5 the working state is 2,048 B */
#define VL        8              /* BT sweep: count levels kept as masks (counts per pair < VL) */
#define NBANK     4              /* STREAM banks of 64: four step-1 layers written at once */
#define NONE32    0xFFFFFFFFu
#define NONE16    0xFFFFu

/* big-SRAM placement, chunk addresses (walk-through 3, skew-v2.md 4) */
#define A0        0x00000u       /* keys staging: A0 + A1 (contiguous); values staging segment 1 */
#define A1        0x20000u       /* VR (values received) = A1-A2 */
#define A3        0x60000u       /* sort 1's output (key, j): the keys' send buffer; values staging seg 2 */
#define A4        0x80000u       /* KR: keys received */
#define A5        0xA0000u       /* VS: values in sorted-key order, the values' send buffer */
#ifndef A7VALUES
#define A7VALUES  8192u          /* values in A7's top 128 KB, sort scratch (lead, 2 Oct) */
#endif
#define A7TOP     (0x100000u - 4u * A7VALUES)   /* values staging, segment 3 */
#define SEG_A0    32768u         /* values staging index space: [0,32768) A0, [32768,65536) A3, */
#define SEG_A3    65536u         /*   [65536, 65536 + A7VALUES) A7 top */
#define SEG_END   (SEG_A3 + A7VALUES)

/* paths */
enum { PATH_DIRECT = 0, PATH_LB = 1, PATH_TH = 2, PATH_BT = 3, PATH_PLAN = 4 };
enum { KIND_K = 0, KIND_V = 1 };          /* keys (64-bit, period 4), values (128-bit, period 8) */

/* --- cost constants for the switch: the measured set-up costs (RESULTS.md 4), rounded up --- */
#define C_TAIL      60
#define C_PREFETCH  9
#define C_B         25
#define C_SEND      9            /* one send-port step of a one-word exchange */
#define C_XCH       (C_SEND * 64 + C_TAIL + C_B)   /* one 64-step exchange */
#ifndef C_TH1
#define C_TH1       3900         /* TH hop 1, one kind: streams, PTR, counters, configure, barrier [meas] */
#endif
#ifndef C_TH2
#define C_TH2       3800         /* TH hop 2, one kind [meas] */
#endif
#ifndef C_TH_LOCAL
#define C_TH_LOCAL  5000         /* least TH table (th_build on any set) [meas >= 5.5K] */
#endif
#ifndef C_BT1
#define C_BT1       2300         /* BT step-1 layer, one kind, at m (PTR) and barrier [meas] */
#endif
#ifndef C_BT1SRC
#define C_BT1SRC    12000        /* BT step-1 source streams, one kind, up to NBANK layers [meas] */
#endif
#ifndef C_BT2
#define C_BT2       11800        /* BT step-2 layer, one kind: m's streams, exchange, d's PTR, barrier [meas] */
#endif

/* ---- the hardware tables the planner writes (patch: worker 6, 3 Oct 2026, with the lead's agreement) ----
 * The planner only ever WRITES STREAM, the GROUP low words (R) and PTR, through the four HW_* macros below.
 *   -DSKEW_HW_SHADOW  (the host harness and its RV32 cycle-count image): into a shadow HW in the Tile, as before.
 *   otherwise         (the tile, the sort program): straight into the table window (mesh.h 1.2, spec 4.10). The
 *                     caller must hold the table-access rule around every planner call that writes tables: the tile's
 *                     run and the arrivals that update PTR are done (v1.2: waited for by events, D17).
 * sram_remaining and augment_base stay fields in both builds: the phase code loads them into the registers. */
#ifdef SKEW_HW_SHADOW
typedef struct {
    uint32_t stream_lo[256], stream_hi[256];
    uint32_t group_lo[64];
    uint32_t ptr[256];
    uint32_t sram_remaining;
    uint32_t augment_base;
} HW;
#define HW_STREAM(T, s, lo, hi)  ((T)->hw.stream_lo[s] = (lo), (T)->hw.stream_hi[s] = (hi))
#define HW_STREAM_HI(T, s, hi)   ((T)->hw.stream_hi[s] = (hi))
#define HW_GROUP_R(T, g, R)   ((T)->hw.group_lo[g] = (R))
#define HW_PTR(T, i, v)        ((T)->hw.ptr[i] = (v))
#else
#include "mesh.h"
typedef struct {
    uint32_t sram_remaining;
    uint32_t augment_base;
} HW;
/* mesh.h 1.2 (v1.2 port: WORKER8): STREAM is three arrays. HW_STREAM3 writes a stream's source, destination and count
 * words; HW_STREAM_HI (a count word only) empties a stream: count 0 sends nothing. */
#define HW_STREAM3(T, s, src, dest, cnt)  ((void)(T), set_stream((s), (src), (dest), (cnt)))
#define HW_STREAM_HI(T, s, hi)   ((void)(T), tables->STREAM_COUNT[s] = (hi))
#define HW_GROUP_R(T, g, R)   ((void)(T), tables->GROUP[g][0] = (R))
#define HW_PTR(T, i, v)        ((void)(T), tables->PTR[i] = (v))
#endif

typedef struct {
    /* ---- [global] after the n exchange, the split targets and RED ---- */
    uint32_t nkind;                    /* record format: 2 = 128-bit values (keys + values all-to-alls),
                                          1 = 32-bit values (one all-to-all of 64-bit (key, value)) */
    uint32_t n[NT], M[NT];             /* records per tile in, rank-target sizes out */
    uint32_t nmax, Mmax, Wmax;
    uint32_t Rdir[NG];                 /* RED: direct group maxima */
    uint32_t Rp15, Rloop;              /* RED (LB): group 15 without the loopback; max loopback */
    /* ---- own ---- */
    uint32_t me;
    uint32_t row[NT];                  /* S[me][q] (XB) */
    uint32_t col[NT];                  /* S[p][me] (XB) */
    uint32_t rowpos[NT];               /* c[me][q]: start of slice q in the sorted run */
    uint32_t inpos[NT];                /* off[p][me]: start of p's slice in the inbox */
    uint32_t offo[NT];                 /* off[me][q] (XC, stage 2) */
    /* ---- stage 1 / 2 results ---- */
    uint32_t red_own[NG + 2];          /* own RED contribution: 16 maxima, R'15, loopback */
    uint32_t costD, costLB, costTH, costBT, floor2;
    uint32_t path;
    /* ---- TH (stage 2) ---- */
    struct {
        uint8_t  th_cnt[NT];           /* pieces per intermediate */
        uint8_t  th_q[NT];             /* the (largest) piece per intermediate: destination */
        uint16_t th_src[NT], th_stage[NT], th_size[NT];
    } t_th;
    uint32_t r2_own[NG + 3];           /* R2 words: layer-0 maxima, L, z, Delta(W*) */
    uint32_t r2[NG + 3];               /* [global] R2 */
    /* ---- BT selection [global] ---- */
    uint32_t bt_w, bt_L, bt_magic, bt_est;
    /* ---- BT working state: the <= 2 KB footprint (skew-v2.md 8) ---- */
    uint32_t pcur, ncell;              /* own pieces so far (sweep order); nonempty cells */
    uint8_t  piece_m[PMAX];            /* intermediate of each own piece, in sweep order */
    uint8_t  r[NT];                    /* sweep: own pieces per intermediate; later a counter */
    uint8_t  kc[NT];                   /* pieces in cell d, minus 1 (w >= 256: k <= 256) */
    union {
        uint32_t rmask[VL][2];         /* sweep: {m : r[m] <= v} for each level v */
        uint8_t  order[NT];            /* after: the nonempty cells in sweep order */
    } v;
    union {                            /* one 256-byte buffer, used by one stage at a time */
        struct { uint32_t cmask[VL][2]; } sw;   /* sweep: the column token, {m : c[m] <= v} */
        uint32_t desc_out[NT];         /* one step-1 layer's descriptors to each m */
        struct { uint16_t out2[NT]; uint16_t in2[NT]; } h2; /* step 2: (s | j << 6) out and in */
    } u;
    uint32_t held[LMAX][NT];           /* at m: step-1 descriptor from s, per layer */
    /* ---- BT results ---- */
    uint32_t L1own, L2own, stage_own;  /* own layer counts and staging high-water mark */
    uint32_t R1own[LMAX + 1], R2own[LMAX + 1];
    uint32_t r3[3 + 2 * (LMAX + 1)];   /* [global] R3: L1, L2, stage max, R1[], R2[] */
    uint32_t stc[3];                   /* staging cursors while the step-1 layers land */
    uint32_t th_desc_out[NT], desc_in[NT];  /* TH: descriptors out and in */
    uint32_t err;                      /* planner error flags */
    HW hw;
} Tile;

/* error flags */
#define ERR_PIECES   1u   /* more than PMAX pieces in the row */
#define ERR_LAYERS   2u   /* more than LMAX layers */
#define ERR_STAGE    4u   /* staging past the third segment */
#define ERR_COUNT    8u   /* a count past VL - 1 */

/* ---- skew_common.c ---- */
uint32_t udiv32(uint32_t a, uint32_t b);                 /* shift-subtract, no divide */
uint32_t run_cost(const uint32_t *R, uint32_t P, uint32_t extraR, uint32_t extraP);
uint32_t run_cost_flat(uint32_t R, uint32_t P);          /* every group at R */
uint32_t red_cost(uint32_t words);
void     sw_prepare(Tile *T);                            /* prefixes, own RED words */
void     sw_stage1(Tile *T);                             /* after RED: D, LB, floor -> path */
/* mode: 2 (write at PTR[src]), or 4 (augmented: the keys of hop 2 with 128-bit values, hop2_mode; meshsim M12, spec
 * 3.1 and 4.9 of 9 October: augmenting is the sender's packet mode, no longer the receiver's attach flag) */
/* packets per 128-bit value: one in v1.1 (a 128-bit element is one packet); the host harness keeps design v1.0's two */
#ifdef SKEW_HW_SHADOW
#define VALUE_PACKETS 2u
#else
#define VALUE_PACKETS 1u
#endif
#ifdef SKEW_HW_SHADOW
/* the host harness's shadow keeps design v1.0's two-word encoding (its expected results are in it): count [17:0],
 * d [23:18], mode [26:24] (2 or 4: write by source or augmented), size [28:27] (2 64-bit, 3 128-bit) */
static inline void write_stream(Tile *T, uint32_t stream, uint32_t src, uint32_t count, uint32_t d, uint32_t kind, uint32_t mode)
{
    HW_STREAM(T, stream, src & 0xFFFFFu, (count & 0x3FFFFu) | (d << 18) | (mode << 24) | ((kind ? 3u : 2u) << 27));
}
/* the same with the mode and read size given as one STREAM_HI constant (stream_hi_fixed), for loops that hold it */
static inline uint32_t stream_hi_fixed(uint32_t mode, uint32_t kind) { return (mode << 24) | ((kind ? 3u : 2u) << 27); }
static inline void write_stream_hi(Tile *T, uint32_t stream, uint32_t src, uint32_t count, uint32_t d, uint32_t hi0)
{
    HW_STREAM(T, stream, src & 0xFFFFFu, (count & 0x3FFFFu) | (d << 18) | hi0);
}
/* the packet mode of a hop-2 stream (design v1.0 numbering): 4 augmented for the keys of the 128-bit format, else 2 */
static inline uint32_t hop2_mode(const Tile *T, uint32_t kind) { return 2u + 2u * ((T->nkind - 1u) & ~kind & 1u); }   /* nkind 1 or 2, kind 0 (K) or 1 (V) */
#else
/* the tile (mesh.h 1.2, spec 4.2): three words per stream. mode is mesh.h's (MODE_SRC or MODE_AUGMENT); kind 0 keys
 * (64-bit), 1 values (128-bit). STREAM_DEST is 0 (modes 1-3 ignore it); d is in STREAM_COUNT; every packet counts on
 * counter 1 at its receiver (CONVENTIONS 5). */
static inline void write_stream(Tile *T, uint32_t stream, uint32_t src, uint32_t count, uint32_t d, uint32_t kind, uint32_t mode)
{
    HW_STREAM3(T, stream, STREAM_SRC_W(src, 0), 0, STREAM_COUNT_W(count, kind ? SIZE_128 : SIZE_64, CTR_SRAM, mode, d));
}
/* the count word's read size, counter and mode as one constant (stream_hi_fixed), for loops that hold it */
static inline uint32_t stream_hi_fixed(uint32_t mode, uint32_t kind) { return STREAM_COUNT_W(0, kind ? SIZE_128 : SIZE_64, CTR_SRAM, mode, 0); }
static inline void write_stream_hi(Tile *T, uint32_t stream, uint32_t src, uint32_t count, uint32_t d, uint32_t hi0)
{
    HW_STREAM3(T, stream, STREAM_SRC_W(src, 0), 0, STREAM_COUNT_W(count, 0, 0, 0, d) | hi0);
}
/* the packet mode of a hop-2 stream: the keys of the 128-bit format are augmented (MODE_AUGMENT), else MODE_SRC */
static inline uint32_t hop2_mode(const Tile *T, uint32_t kind) { return MODE_SRC + (MODE_AUGMENT - MODE_SRC) * ((T->nkind - 1u) & ~kind & 1u); }
#endif
/* the write-by-source mode in each build's numbering (v1.2 port, WORKER8): design v1.0's 2 for the host shadow (its
 * expected results are in that numbering), mesh.h 1.2's MODE_SRC = 1 on the tile, where 2 is now the bucket write.
 * Hop 1's streams (TH and BT) and the empty streams (EMPTY_HI, skew_bt.c) use it. */
#ifdef SKEW_HW_SHADOW
#define SK_MODE_SRC 2u
#else
#define SK_MODE_SRC MODE_SRC
#endif
uint32_t stage_addr(uint32_t pos, uint32_t kind);        /* staging index -> chunk address */
extern const uint8_t GROUP_OFF[64];                      /* stream 4g+i -> offset */
extern const uint8_t STREAM_OF[64];                        /* offset -> stream index 4g+i */

/* ---- skew_th.c ---- */
void th_build(Tile *T);                                  /* stage 2: TH table and R2 words */
void sw_stage2(Tile *T, uint32_t version);               /* after R2: TH, BT selection, or stop */
void th_desc(Tile *T);                                   /* th_desc_out[m]: TH hop-1 descriptors */
void th_hop1(Tile *T, uint32_t kind);                    /* source streams and m's PTR (from desc_in) */
void th_hop2(Tile *T, uint32_t kind);                    /* m's streams and the destination's PTR */

/* ---- skew_bt.c ---- */
uint32_t bt_estimate(const Tile *T, uint32_t L, uint32_t *w_out, uint32_t version);
void bt_select(Tile *T, uint32_t version);               /* (w, L) and the estimate; [global] */
void bt_force(Tile *T, uint32_t L);                      /* harness: a given L at the z-formula's w */
void bt_sweep_init(Tile *T);
void bt_sweep_cell(Tile *T, uint32_t pass, uint32_t t);  /* uses and updates the token u.sw.cmask */
void bt_sweep_cell_c(Tile *T, uint32_t pass, uint32_t t, uint32_t (*C)[2]);   /* the same, on token C (worker 8, M7) */
void bt_sweep_done(Tile *T);                             /* own L1, the cell order */
void bt_desc(Tile *T, uint32_t layer);                   /* u.desc_out[m] for one step-1 layer */
void bt_held_stats(Tile *T);                             /* own L1, R1; at m: L2, R2, staging */
void bt_decide(Tile *T);                                 /* after R3: exact BT cost, final path */
void bt_hop1_src(Tile *T, uint32_t first, uint32_t kind); /* source: streams of layers first.. in banks */
void bt_hop1_m(Tile *T, uint32_t layer, uint32_t kind);  /* at m: PTR[s] for one layer */
void bt_hop2_m(Tile *T, uint32_t layer, uint32_t kind);  /* at m: streams and out2, one step-2 layer */
void bt_hop2_d(Tile *T, uint32_t layer, uint32_t kind);  /* at d: PTR from in2 */

#endif
