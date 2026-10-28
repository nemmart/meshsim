/* sort.h -- the six-step sort on design v2 (worker 6): constants, command set, shared state.
 * Spec: spec/machine-spec.md (v1.1, 2026-10-26n); interface: mesh.h 1.1 and sw/CONVENTIONS.md;
 * plan: design_v2/sort/sort-walkthrough.md (where it differs from the spec, the spec wins). */
#ifndef SORT_H
#define SORT_H
#include "mesh.h"

/* ---- big-SRAM areas (spec 4.1: area a = chunks a*0x20000 ...), placement as walk-through section 3 ---- */
#define AREA(a)      ((a) * 0x20000u)
#define BUF_IN       AREA(0)     /* input: raw 32-bit keys (k = 2) or 64-bit (key, value) records (32-bit values) */
#define BUF_VIN      AREA(1)     /* input values, 128 bits each (A1-A2); later VR, the values received */
#define BUF_X        AREA(3)     /* sort 1's X: ends as sorted (key, j); sort 2's Y */
#define BUF_Y        AREA(4)     /* sort 1's Y; KR, the keys received; sort 2's X: ends as the final keys */
#define BUF_VS       AREA(5)     /* values in sorted-key order (A5-A6); gather 2's output */
#define BUF_VR       AREA(1)     /* V: the values received (A1-A2, over the input values gather 1 has read) */

/* ---- C&C staging and sort scratch (spec 6.2, mesh.h v3): stage_base() = top - STAGE_CHUNKS (0xF4000 on 4 MB), 64 KB,
 * below the top 128 KB of sort scratch, scratch_base(). The loader sets PTR[64] = stage_base() at boot, and the sort
 * re-sets it after every phase with mode 3 (which overwrites PTR[64]). */

/* ---- STREAM allocation (walk-through section 2) ---- */
#define STR_K         0           /* K: keys all-to-all, group g in streams 4g .. 4g+3, schedule-4 order */
#define GRP_K         0           /* K: GROUP[0..15] */
#define STR_V         64          /* V: values all-to-all, group g in streams 64+4g .. 64+4g+3 */
#define GRP_V         32          /* V: GROUP[32..47] (+ 48, LB's 17th group); TH uses GROUP 0-31 (skew-v2.md 5) */
#define STR_KLB       128         /* LB: K's loopback stream, the 17th group's one stream (t = 1) */
#define STR_VLB       129         /* LB: V's loopback stream */
#define STR_G1        195         /* gather 1: index BUF_X -> values BUF_VIN -> BUF_VS */
#define STR_G2        196         /* gather 2: index BUF_Y -> values BUF_VR -> BUF_VS */
#define STR_IN        192         /* L1: augmenting pass (k = 2) or copy pass (32-bit values), A0 -> A3 */
#define STR_FROM_X    193         /* bucket pass reading BUF_X */
#define STR_FROM_Y    194         /* bucket pass reading BUF_Y */
#define STR_OV        197         /* an overlay load: one mode-6 loopback stream, staging -> instruction memory (6.5) */

/* ---- the sort's command set (C&C -> slot 0; [31:24] type, [23] parity, [22:0] argument) ---- */
#define CMD_L1       0x10        /* argument: FMT_* */
#define CMD_XB       0x11        /* counts to receivers; plan the transfer (PTR, K streams, local group maxima) */
#define CMD_RED      0x12        /* group maxima: mesh reduction and broadcast; GROUP R; K configured */
#define CMD_K        0x13        /* start: the keys all-to-all */
#define CMD_SPLIT    0x14        /* the split (step 3): split_get_cuts() */
#define CMD_L2       0x15
#define CMD_V        0x16        /* start: the values all-to-all (k = 2) */
/* skew handling's fixed sequence (skew-v2.md 5; lead, 3 Oct): XC TH SWEEP DESC K K1.0-4 K2.0-4 V V1.0-4 V2.0-4 L2.
 * In S1 they are empty on every tile (stage 1 chooses direct or LB): a tile finishes them at once. */
#define CMD_XC       0x20
#define CMD_TH       0x21        /* argument bit 0: TEST ONLY, force BT when feasible (SortTest force=bt) */
#define CMD_SWEEP    0x22
#define CMD_DESC     0x23
#define CMD_K1       0x24        /* argument: layer 0-4 */
#define CMD_K2       0x25
#define CMD_V1       0x26
#define CMD_V2       0x27
#define CMD_BARRIER  0x30        /* C&C's answer to one of the split's own barriers (its word is read and ignored) */
#define CMD_EXIT     0x7F        /* simulator only: main returns, the core ebreaks */
#define FMT_KV32     0           /* 32-bit key + 32-bit value: 64-bit records in */
#define FMT_K128     1           /* 32-bit key + 128-bit value (k = 2): keys and values in separate arrays */
/* The keys' packet mode (meshsim M12, spec 3.1 and 4.9 of 9 October): 4, the augmented write, with 128-bit values (the
 * receiver stamps each key's ordinal), else 2. Arithmetic, not a branch: k128 is 0 or 1. */
#define KEY_MODE(k128) (MODE_SRC + (MODE_AUGMENT - MODE_SRC) * (k128))

#ifndef __ASSEMBLER__
#include "skew.h"                /* worker 5's planner (design_v2/sort/skew/planner), tile build: tables direct */

/* The planner's per-tile state (5,352 B). The sort shares its fields rather than keeping copies: n[] (the split's
 * output), col[] (XB's counts received: copied out of the reply block by xchg), row[], rowpos[],
 * inpos[], M[], and the reduced RED vector. */
extern Tile SK;                  /* in the shared region (sort.ld), over the split's state */
enum { RB_R, RB_L, RB_D, RB_U }; /* RED's receive buffers: from me + 1, me - 1, me + 8, me - 8 */
#define RV (NG + 2)              /* RED's vector: 16 direct group maxima, group 15 without the loopback, the loopback */

/* State that lives across commands (data memory). */
struct sort_state {
    uint32_t fmt;                /* FMT_*, from CMD_L1 */
    uint32_t n_in;               /* records this tile holds at the start: INPUT-LOAD STAND-IN, placed by the harness */
    uint32_t n_recv;             /* records this tile receives in K (sort 2's size); set by XB (P1 test: the harness) */
    uint32_t red_in;             /* RED: mode-6 packets this tile receives before it sends (RV / 2 per child) */
    uint32_t path;               /* the transfer path taken (PATH_*), identical on every tile */
    uint32_t plan_wanted;        /* stage 1 said PATH_PLAN (stage 2 needed; S1 falls back to min(direct, LB)) */
    uint32_t shaped;             /* the split's targets: 0 balanced, 1 the input's shape (CMD_SPLIT's argument) */
    uint32_t groups;             /* groups in the K and V group runs: 16 direct, 17 LB */
    uint32_t bt_wanted;          /* stage 2 chose BT (the final decision, after R3, may still fall back) */
    uint32_t next_rem;           /* DMA_SRAM_REMAINING of the next two-hop layer phase, set when it is configured */
    uint32_t bt_L1, bt_L2;       /* BT: the global step-1 and step-2 layer counts (R3) */
    uint32_t force_bt;           /* TEST ONLY (TH's argument, bit 0): take BT whenever it is feasible */
    uint32_t red_children;       /* RED's tree: children, their buffers (mask of RB_*), the parent and its buffer */
    uint32_t red_mask, red_parent, red_pbuf;
    uint32_t cut[65];            /* the split's output: cut[q] = this tile's records below R_q, (key, tile, position) */
    uint32_t n[64];              /* the split's output: n[p] = tile p's record count (copied into SK.n in XB) */
};
extern struct sort_state S;

/* ---- the split (step 3), worker 4's interface (lead, 3 Oct; exact types in his BINDING.md, not yet here) ----
 * Called on every tile in CMD_SPLIT with this tile's sorted run (chunk address, record count) and the target kind
 * (shaped: 0 = balanced R_q = floor(q * N / 64), 1 = the input's shape R_q = n_0 + ... + n_(q-1)). On return, on every
 * tile: c[q] = the number of this tile's sorted records whose global rank, in (key, tile, position) order, is below
 * R_q, q = 0..64 (c[0] = 0, c[64] = n_me); n[p] = tile p's record count. c and n are at the same address on every
 * tile. This is the state after the walk-through's XA. Worker 4's split (../split-bisect/split.c, design v1's bisection
 * through the read port), an overlay; its 66 barriers (65 shaped) go through sort_barrier(), i.e. wait_for_command(). */
void split_get_cuts(uint32_t run_chunk, uint32_t n_me, uint32_t shaped, uint32_t c[65], uint32_t n[64]);

/* ---- overlays (M7; lead, 3 Oct): one region at _ov_base, images staged by C&C at boot (stage_base()), copied in
 * by ov_load() (kernel) in a command's tail, or at the start of a local-only command (L2). */
enum { OV_LOCAL = 0, OV_SPLIT, OV_XFER, OV_TH, OV_BTPLAN, OV_BTRUN, OV_N };
void ov_load(unsigned id);
void sort_barrier(void);

void xb(void);
void red(void);
void k_start(void);
void v_start(void);
void plan_transfer(void);
void decide_stage1(void);
void config_direct_lb(void);
void skew_th(void);
void skew_desc(void);
void th_phase(unsigned phase);
void zero_planner(void);
/* a one-word exchange (32-bit replies, send port, schedule order): out[d] goes to tile d, in[s] copied out of REPLY[2 s]. Call it
 * FIRST in a command (it stores DMA_REPLY_REMAINING before anything can arrive). */
void xchg(const uint32_t *out, uint32_t *in);
/* a max-reduction of 'words' (<= 20) words over RED's tree, broadcast back: the caller has stored DMA_CM_REMAINING =
 * S.red_children * packets FIRST in the command; vec is merged in place (an odd vector must have one word after it
 * that may be sent and overwritten); the global result lands in out */
void tree_reduce(uint32_t *vec, unsigned words, uint32_t *out, unsigned wait);   /* wait = 0: the children's packets
                                                                                  have landed already (a WFI covered them) */
void bt_sweep(void);             /* BT (BTPLAN): SWEEP, the 128 token rounds and the L1 reduction */
void bt_descs(void);             /* BT (BTPLAN): DESC, the descriptor exchanges, held statistics, R3, the decision */
void bt_first(void);             /* BT (BTRUN): the layer GROUP high words, K1.0 configured */
void bt_phase(unsigned kh, unsigned layer);   /* BT (BTRUN): a layer phase, kh 0 K1, 1 K2, 2 V1, 3 V2 */
/* wait_tables() (the table window may be touched only when the address generator is not running, and
 * ts_busy = 0: spec 4.10) is in mesh.h from v9 (M14); this file's copy, the same code, is gone. */

void l1(uint32_t fmt);
void l2(void);
void restore_stage_ptr(void);
#endif
#endif
