/* Recipes 25 and 26: using a peripheral window through its struct, and the replayer's refill loop on counted credits
 * (D10, D14, W3-REVIEW-2 section 5). The replayer is peripheral 1 on a tile that has one. */
#include "rcp.h"
/* the template index (spec 4.19.9; v1.25: universal, keyed by key(w)): a key to its expansion's words in big SRAM */
extern void template_lookup(uint32_t key, uint32_t offset, uint32_t *chunk, uint32_t *entries);

/* RECIPE periph_window */
/* A peripheral is reached through its window's struct (D14): its standard words first (D10), then its own. Check the
 * tile's PARAMS [2:0] before touching a window: an absent peripheral's window is a bus error. Read EVENTS_PEEK to look;
 * a load of EVENTS consumes one count from every set bit in the window's EVENTS_MASK. */
static int replayer_present(void) {
    return (PARAMS_PERIPH_PRESENT(tile->PARAMS) & 1) && PARAMS_TYPE(replayer->PARAMS) == PTYPE_REPLAYER;
}
/* Load the state and stream words (two 128-bit words, st[0..7]) into region 0x04 while the replayer is stopped
 * (STATUS [0] = 0: its part 0 is written only then, spec 4.19.10): mode-0 writes to the tile's own peripheral region, 16-byte
 * aligned, counted on counter 4 like any write (they land within K, D10). */
static void replayer_load_state(const uint32_t *st) {
    while (replayer->STATUS & PST_RUN) ;           /* only between passes; the loop below ends a pass by its event */
    tile->WRITE_COUNTER[CTR_PERIPH - 1] = 2;
    send_to(ROUTER_HDR_W(tile_index(), MODE_WRITE, CTR_PERIPH), RP_STATE);
    send128(st[3], st[2], st[1], st[0]);
    send128(st[7], st[6], st[5], st[4]);
    wait_all(EV_CTR(CTR_PERIPH));
}
/* A peripheral with a DMA (not the replayer): send words 32-bit words from it to dst at addr, counted there on ctr.
 * Only DMA done is in the window's mask while it waits, so the consuming load claims nothing else (D10). A transfer
 * goes out as 128-bit packets, then 64 and/or 32: keep words a multiple of 4 unless it is the last into that buffer,
 * or DMA_ADDR is left misaligned for the next transfer's 128-bit packets (D26, W3-29). */
static void periph_dma(unsigned n, unsigned dst, uint32_t addr, uint32_t words, unsigned queue, unsigned ctr) {
    volatile PeripheralStd *p = peripheral(n);
    uint32_t mask = p->EVENTS_MASK;
    p->EVENTS_MASK = PEV_DMA_DONE;
    p->DMA_HDR = DMA_HDR_W(words, queue, dst, MODE_WRITE, ctr);
    p->DMA_ADDR = addr;
    p->PULSE_DMA = 1;                               /* a count of 0 still posts DMA done (D10) */
    tile->EVENTS_MASK = EV_GO | EV_PERIPH(n);
    while (!(p->EVENTS & PEV_DMA_DONE))             /* the consuming load; a spurious wake reads 0 and waits again */
        __asm__ volatile ("wfi" ::: "memory");
    tile->EVENTS_MASK = EV_RESET_MASK;
    p->EVENTS_MASK = mask;
}
/* END */

/* RECIPE replay_loop */
/* One pass of the replayer (spec 4.19.9 on D10's counted credits). Each queue's credit bit holds up to 2 credits, set
 * to 2 at PULSE_RUN (the expansion queue's again at each new expansion); a credit is permission to push half a queue.
 * One load of the window's EVENTS consumes one credit from every set bit in its mask, so the loop pushes for every bit
 * the load returns, in one refill run of up to four streams, and waits for that run before it looks again. A queue
 * with nothing more to push leaves the mask, or its spare credit would keep the tile awake (W3-24). Resident streams
 * only: data staged from LPDDR is fetched into big SRAM first, on another counter (spec 4.19.13). */
typedef struct { uint32_t chunk, left, port, half, bit; } Feed;   /* the next half in big SRAM (4-chunk aligned), entries left */
/* key(w) (spec 4.19.9, v1.25): the index's key for a presented word, every bit of w but its register fields. An inline
 * (custom-0) has rd, rs1 and rs2 cleared; CSRRW/CSRRS has rd and rs1 renamed by first appearance (x0 stays 0, a
 * non-zero rd becomes 1; rs1 is 0 if x0, 1 if it equals a non-zero rd or if rd is x0, else 2), the CSR kept. */
static uint32_t template_key(uint32_t w) {
    uint32_t op = (w | 1) & 0x7F, f3 = (w >> 12) & 7, rd = (w >> 7) & 31, rs1 = (w >> 15) & 31;
    if (op == 0x0B) return w & ~(31u << 7 | 31u << 15 | 31u << 20);
    if (op == 0x73 && (f3 == 1 || f3 == 2))
        return (w & ~(31u << 7 | 31u << 15)) | (rd ? 1u : 0u) << 7 | (!rs1 ? 0u : (!rd || rs1 == rd) ? 1u : 2u) << 15;
    return w;
}
static unsigned refill(Feed *f, unsigned s, uint32_t *pmask, uint32_t *armed) {
    uint32_t n = f->left < f->half ? f->left : f->half;    /* every refill but a stream's last is exactly a half */
    set_stream(s, STREAM_SRC_W(f->chunk, 0), f->port, STREAM_COUNT_W(n, SIZE_128, CTR_PERIPH, MODE_WRITE, 0));
    f->chunk += 4 * n;                              /* 128-bit entries: 4 chunks each */
    f->left -= n;
    *armed += n;
    if (f->left == 0) *pmask &= ~f->bit;            /* nothing behind its credits now: out of the mask */
    return s + 1;
}
static void replay_pass(Feed *code, Feed *rlog, Feed *wlog, int store_log) {
    Feed exp = { 0, 0, RP_Q_EXP, RP_HALF_EXP, RP_EV_EXP };
    uint32_t pmask = PEV_RUN_DONE | RP_EV_NEWEXP | (code->left ? RP_EV_CODE : 0) | (rlog->left ? RP_EV_RLOG : 0)
                   | (store_log && wlog->left ? RP_EV_WLOG : 0);   /* no write log without the store log */
    replayer->EVENTS_MASK = pmask;                  /* the expansion's bit joins at the first new expansion */
    tile->EVENTS_MASK = EV_GO | EV_PERIPH(1);       /* GO can't come mid-pass; the tile's [13] is live */
    replayer->PULSE_RUN = 1;                        /* empties the queues; every credit counter to 2 */
    for (;;) {
        __asm__ volatile ("wfi" ::: "memory");
        uint32_t ev = replayer->EVENTS;             /* consumes: one credit from each set bit under pmask */
        if (ev & PEV_RUN_DONE) break;               /* the pass has ended; no refill is in flight here */
        if (ev & RP_EV_NEWEXP) {                    /* present an expansion: its credits are 2 again */
            template_lookup(template_key(replayer->EXPANSION_OPCODE), replayer->EXPANSION_OFFSET, &exp.chunk, &exp.left);
            if (exp.left) pmask |= RP_EV_EXP;       /* its first credit is claimed on the next wake */
        }
        unsigned s = 0;
        uint32_t armed = 0;
        if (ev & RP_EV_CODE) s = refill(code, s, &pmask, &armed);
        if (ev & RP_EV_RLOG) s = refill(rlog, s, &pmask, &armed);
        if (ev & RP_EV_WLOG) s = refill(wlog, s, &pmask, &armed);
        if (ev & RP_EV_EXP)  s = refill(&exp, s, &pmask, &armed);
        replayer->EVENTS_MASK = pmask;              /* before the next WFI */
        if (!s) continue;                           /* a spurious wake (D10), or a new expansion alone */
        tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, s);
        tile->WRITE_COUNTER[CTR_PERIPH - 1] = armed;
        start_run();
        wait_all(EV_CTR(CTR_PERIPH) | EV_RUN_DONE); /* the squelch: [13] is out of the mask while the run streams */
        tile->EVENTS_MASK = EV_GO | EV_PERIPH(1);   /* credits returned meanwhile fall through at once */
    }
    replayer->EVENTS_MASK = 0;
    tile->EVENTS_MASK = EV_RESET_MASK;
}
/* END */

static uint32_t state[8];
int main(void) {
    Feed code = { 0x10000, 300, RP_Q_CODE, RP_HALF_CODE, RP_EV_CODE };
    Feed rlog = { 0x20000, 900, RP_Q_RLOG, RP_HALF_LOG, RP_EV_RLOG };
    Feed wlog = { 0x30000, 0, RP_Q_WLOG, RP_HALF_LOG, RP_EV_WLOG };
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) == 1 && replayer_present()) { replayer_load_state(state); replay_pass(&code, &rlog, &wlog, 0); }
        else if (CMD_TYPE(w) == 2) periph_dma(2, DST_CC1, REGION_DMEM + 0x100, 8, 0, CTR_CM);
        else sim_exit(0);
    }
}
