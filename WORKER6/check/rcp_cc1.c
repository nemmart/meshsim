/* Recipes 22 (C&C 1's side of the boot) and 27 (C&C 1's side of a phase). C&C 1 is a behavioural Java model in
 * meshsim v1.2 (D36); this is its firmware's shape for later, against D28's registers. Compiled, not run. */
#include "rcp.h"
#define GO_LATENCY   1      /* go_latency (param) */
#define STAGE_DELAY  32     /* stage_delay (param, spec 6.2) */
typedef struct { uint32_t pmem_words, dmem_words; const uint32_t *pmem, *dmem; uint32_t entry; } Image;
static unsigned parity[67];                         /* per unit: the next command's parity; tiles move together on broadcasts */
volatile uint32_t results[64][2];

/* RECIPE cc1_command */
/* A command to dst (a unit, or DST_BROADCAST): the arguments into the block its parity names, then the command word
 * to slot 0, last, all uncounted mode-0 writes from C&C 1's send port (D6). Every packet of one command to a tile is
 * of one kind (3.2). A broadcast needs every tile at the same parity; parity[0] stands for all 64 then. */
static void command(unsigned dst, uint32_t type, uint32_t arg, const uint32_t *args, unsigned n) {
    unsigned u = dst == DST_BROADCAST ? 0 : dst, par = parity[u];
    send_to(ROUTER_HDR_W(dst, MODE_WRITE, CTR_NONE), COMMAND_BASE + 4 * (par ? ARGS_ODD : ARGS_EVEN));
    for (unsigned i = 0; i < n; i++) send32(args[i]);
    tile->ROUTER_ADDR = COMMAND_BASE;               /* slot 0 last: "command landed" follows the whole command */
    send32(CMD_WORD(type, par, arg));
    if (dst == DST_BROADCAST) for (unsigned t = 0; t < 64; t++) parity[t] ^= 1;
    else parity[u] ^= 1;
}
/* END */

/* RECIPE cc1_phase */
/* C&C 1's side of a phase (6.3; D28, D36). The next command was sent right after the last GO; C&C 1 sleeps until every
 * expected tile is READY, then GO. "All tiles READY" is live: (READY & EXPECT) == EXPECT, EXPECT != 0. PULSE_GO clears
 * READY 0-63, drives GO and restarts the watchdog, in one store. Requests that aren't errors (C&C 2's and 3's "started",
 * tiles' results) arrive meanwhile; an error never comes this way: it raises C&C 1's hard interrupt (recipe 27's toast
 * path). Software re-checks after every wake: the live bit may lag a store by a cycle (D10). */
static void drain_requests(void) {
    while (tile->EVENTS_PEEK & EV_REQ_PENDING) {    /* popping an empty queue would be receive underflow, an error */
        uint32_t w0 = tile->REQUEST_GET_W0, w1 = tile->REQUEST_GET_W1, w2 = tile->REQUEST_GET_W2;
        (void)tile->REQUEST_RECEIVE;                /* the last word pops */
        if (((w0 >> 10) & 0xFF) == REQ_OP_RESULT) { results[w0 >> 18 & 63][0] = w2; results[w0 >> 18 & 63][1] = w1; }
    }
}
static void wait_all_ready(void) {
    cc1->EVENTS_MASK = CC1_EV_ALL_READY | CC1_EV_WATCHDOG;
    tile->EVENTS_MASK = EV_PERIPH(1) | EV_REQ_PENDING;
    for (;;) {
        __asm__ volatile ("wfi" ::: "memory");
        drain_requests();
        uint32_t pev = cc1->EVENTS;                 /* consumes the watchdog's event; ALL_READY is live */
        if (pev & CC1_EV_WATCHDOG) for (;;) ;       /* toast is latched already (D36): the outside world resets */
        if (pev & CC1_EV_ALL_READY) return;
    }
}
static void go(uint32_t watchdog_limit) {
    cc1->WATCHDOG = watchdog_limit;                 /* above this phase's expected length; a write restarts it */
    cc1->PULSE_GO = 1;
    delay4((GO_LATENCY + 1 + 3) / 4 + 1);           /* go_latency + 1 before anything else is sent (6.6) */
}
static void phase(uint32_t type, uint32_t arg, const uint32_t *args, unsigned n, uint32_t limit) {
    wait_all_ready();                               /* every tile has finished and its next command has landed */
    go(limit);
    command(DST_BROADCAST, type, arg, args, n);     /* the command after this one, right after GO */
}
/* END */

/* RECIPE cc1_boot */
/* After the reset (D31, D36): every unit sleeps in its ROM. C&C 1 (itself started by the outside world) boots C&C 2
 * and C&C 3 first, and waits for their "started" requests; then the tiles, overlays and first command of one kind
 * (here all broadcast), READY, GO; then the first program's big-SRAM data by announced staging (step 5). */
static void overlay(unsigned dst, uint32_t addr, const uint32_t *w, uint32_t words) {   /* 16-byte aligned, padded */
    send_to(ROUTER_HDR_W(dst, MODE_WRITE, CTR_NONE), addr);
    for (uint32_t i = 0; i < words; i += 4) send128(w[i + 3], w[i + 2], w[i + 1], w[i]);
}
static void stage(uint32_t chunk, const uint32_t *w, uint32_t words) {   /* counted: the tiles armed for it */
    send_to(ROUTER_HDR_W(DST_BROADCAST, MODE_WRITE, CTR_SRAM), SRAM_ADDR(chunk));
    for (uint32_t i = 0; i < words; i += 4) send128(w[i + 3], w[i + 2], w[i + 1], w[i]);
}
static void boot_unit(unsigned dst, const Image *im) {
    overlay(dst, REGION_PMEM, im->pmem, im->pmem_words);
    overlay(dst, REGION_DMEM + 0x10, im->dmem, im->dmem_words);   /* word 0 stays unwritten (D27; FINDINGS F7) */
    uint32_t entry = im->entry;
    command(dst, CMD_JUMP, 0, &entry, 1);           /* the first command is even: entry in slot 1 (D36) */
}
static void cc1_boot(const Image *fs, const Image *pager, const Image *tiles,
                     const uint32_t *data, uint32_t stage_words, uint32_t stage_chunk) {
    boot_unit(DST_CC2, fs);                         /* unicast to each: overlays and command of one kind */
    boot_unit(DST_CC3, pager);
    for (unsigned started = 0; started < 2; ) {     /* their start-up sends C&C 1 "started" (D31) */
        tile->EVENTS_MASK = EV_REQ_PENDING;
        __asm__ volatile ("wfi" ::: "memory");
        while (tile->EVENTS_PEEK & EV_REQ_PENDING) {
            uint32_t w0 = tile->REQUEST_GET_W0;
            (void)tile->REQUEST_RECEIVE;
            started += ((w0 >> 10) & 0xFF) == REQ_OP_STARTED;
        }
    }
    boot_unit(DST_BROADCAST, tiles);                /* broadcast: the same kind as its first command (D36) */
    cc1->READY_EXPECT[0] = ~0u;                     /* all 64 tiles; one at a time doesn't work yet: FINDINGS F1 */
    cc1->READY_EXPECT[1] = ~0u;
    uint32_t n = stage_words / 4;                   /* step 5: announce the data, READY, GO, then the data */
    phase(CMD_STAGE, n, 0, 0, 0);                   /* waits for the boot command's READY, GOs, sends CMD_STAGE */
    wait_all_ready();                               /* CMD_STAGE has landed and the images wait in wait_for_command() */
    go(0);
    delay4(STAGE_DELAY / 4);                        /* no sooner than stage_delay after the GO store (6.2) */
    stage(stage_chunk, data, stage_words);         /* each tile waits for counter 1, then done */
}
/* END */

/* RECIPE cc1_toast */
/* C&C 1's error path (D35, D36), from its trap handler: a unit's error (its line, through the unit-error bit) or its
 * own. MIE is 0 and stays 0; toast is already latched in hardware. Best effort, nothing waits for it: start the file
 * system's flush, drain what reports arrive, push them out over the UART, park. Bounded by a loop count: C&C 1 has no
 * timer but the watchdog (FINDINGS). The request formats are placeholders: C&C 2's flush op isn't defined yet. */
#define REQ_OP_FLUSH 0x10
static void uart_hex(uint32_t v) {
    for (int i = 28; i >= 0; i -= 4) cc1->UART_TX = "0123456789abcdef"[(v >> i) & 15];   /* stalls while TX is full */
    cc1->UART_TX = ' ';
}
void cc1_toast(uint32_t mcause, uint32_t mepc) {
    request_send(0, 0, 0, REQ_TARGET(TARGET_CC2) | (REQ_OP_FLUSH << 10));   /* 1. flush the logs to LPDDR (D35) */
    uart_hex(mcause); uart_hex(mepc);
    uart_hex(cc1->ERROR_LINES[0]); uart_hex(cc1->ERROR_LINES[1]); uart_hex(cc1->ERROR_LINES[2]);
    for (uint32_t idle = 0; idle < 100000; idle++) {    /* 2. drain the reports: 23 per failed unit (recipe 24) */
        if (!(tile->EVENTS_PEEK & EV_REQ_PENDING)) continue;
        uart_hex(tile->REQUEST_GET_W0); uart_hex(tile->REQUEST_GET_W1); uart_hex(tile->REQUEST_GET_W2);
        uart_hex(tile->REQUEST_RECEIVE);
        cc1->UART_TX = '\n';
        idle = 0;
    }
    for (;;) ;                                      /* 3. park: the outside world resets the chip */
}
/* END */

int main(void) {
    static const Image im = { 0, 0, 0, 0, 0x01000000 };
    cc1_boot(&im, &im, &im, 0, 0, 0x30000);
    for (;;) phase(1, 0, 0, 0, 1000000);
}
