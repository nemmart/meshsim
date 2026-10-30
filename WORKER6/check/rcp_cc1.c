/* Recipes 22 (C&C 1's side of the boot) and 27 (C&C 1's side of a phase). C&C 1 is a behavioural Java model in
 * meshsim v1.2 (D36); this is its firmware's shape for later, against D28's registers. Compiled, not run. */
#include "rcp.h"
#define GO_LATENCY   1      /* go_latency (param) */
#define STAGE_DELAY  32     /* stage_delay (param, spec 6.2) */
void cc1_toast(uint32_t mcause, uint32_t mepc);   /* recipe 27's toast path, below */
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
/* C&C 1's side of a phase (6.3; D28, D36, D38). The next command was sent right after the last GO; C&C 1 sleeps until every
 * expected tile is READY, then GO. "All tiles READY" is live: (READY & EXPECT) == EXPECT, EXPECT != 0. PULSE_GO clears
 * READY 0-63, drives GO and restarts the watchdog, in one store. Requests that aren't errors (C&C 2's and 3's "started",
 * tiles' results) arrive meanwhile, and C&C 1 answers each tile's, since the tile waits for that reply before its
 * next request (D38). An error never comes this way: it raises C&C 1's hard interrupt (recipe 27's toast path).
 * Software re-checks after every wake: the live bit may lag a store by a cycle (D10). C&C 1 replies to the address
 * and counter each request names (D40). */
static void drain_requests(void) {
    while (tile->EVENTS_PEEK & EV_REQ_PENDING) {    /* popping an empty queue would be receive underflow, an error */
        uint32_t w0 = tile->REQUEST_GET_W0, w1 = tile->REQUEST_GET_W1, w2 = tile->REQUEST_GET_W2;
        uint32_t w3 = tile->REQUEST_RECEIVE;        /* the last word pops */
        if (REQ_OP(w0) != REQ_OP_RESULT) continue;
        unsigned t = REQ_REQUESTOR(w0) & 63;
        results[t][0] = w2; results[t][1] = w3;
        send_to(ROUTER_HDR_W(t, MODE_WRITE, REQ_REPLY_CTR(w0)), w1);   /* to the reply address it named (D40) */
        send32(1);                                  /* the reply ends the tile's outstanding request (D38) */
    }
}
static void wait_all_ready(void) {
    cc1->EVENTS_MASK = CC1_EV_ALL_READY | CC1_EV_WATCHDOG;
    tile->EVENTS_MASK = EV_PERIPH(1) | EV_REQ_PENDING;
    for (;;) {
        __asm__ volatile ("wfi" ::: "memory");
        drain_requests();
        uint32_t pev = cc1->EVENTS;                 /* consumes the watchdog's event; ALL_READY is live */
        if (pev & CC1_EV_WATCHDOG) cc1_toast(0, 0); /* toast is latched (D36); no error traps C&C 1, so dump what it has */
        if (pev & CC1_EV_ALL_READY) return;
    }
}
static uint32_t go(uint32_t watchdog_limit) {
    cc1->WATCHDOG = watchdog_limit;                 /* above this phase's expected length, in cycles (D42); a write restarts it */
    cc1->PULSE_GO = 1;
    uint32_t t = mcycle();                          /* about the GO store's cycle (D41: mcycle) */
    while (mcycle() - t <= GO_LATENCY) ;            /* go_latency + 1 before anything else is sent (6.6) */
    return t;
}
static void phase(uint32_t type, uint32_t arg, const uint32_t *args, unsigned n, uint32_t limit) {
    wait_all_ready();                               /* every tile has finished and its next command has landed */
    go(limit);
    command(DST_BROADCAST, type, arg, args, n);     /* the command after this one, right after GO */
}
/* END */

/* RECIPE cc1_boot */
/* After the reset (D31, D36, D37, D41): every unit sleeps in its ROM. C&C 1 (itself started by the outside world) boots
 * C&C 2 and C&C 3 first and waits for their "started" requests; then the tiles; then boot part 2, the first program's
 * big-SRAM data. Every image is in C&C 1's big SRAM (put there by the host, or the harness), 4-chunk aligned, and goes
 * out from C&C 1's address generator at a packet per cycle: a stream may name the broadcast, d = 126, or a controller
 * absolutely, d = 65 or 66 (D41). The first command is the run's last stream, one 128-bit packet at slot 0 (the
 * command word, then the entry address in slot 1): from one source, after every overlay packet (S2), it lands last
 * (3.2). Sent from the send port instead, it could overtake the run's last packets (FINDINGS F17). */
typedef struct { uint32_t pmem, pmem_packets, dmem, dmem_packets, cmd; } Boot;   /* chunk addresses in C&C 1's big SRAM */
static void put_command(uint32_t chunk, uint32_t entry) {   /* the boot command's packet, into C&C 1's own big SRAM */
    tile->WRITE_COUNTER[CTR_SRAM - 1] = 1;
    send_to(ROUTER_HDR_W(DST_CC1, MODE_WRITE, CTR_SRAM), SRAM_ADDR(chunk));
    send128(0, 0, entry, CMD_WORD(CMD_JUMP, 0, 0));    /* slots 0-3; even: the entry in slot 1 (D36) */
    wait_all(EV_CTR(CTR_SRAM));
}
static void boot_unit(unsigned d, const Boot *b) {  /* overlays and command, uncounted, one kind (D36), one run */
    set_stream(0, STREAM_SRC_W(b->pmem, 0), REGION_PMEM, STREAM_COUNT_W(b->pmem_packets, SIZE_128, CTR_NONE, MODE_WRITE, d));
    set_stream(1, STREAM_SRC_W(b->dmem, 0), REGION_DMEM + DMEM_FIRST, STREAM_COUNT_W(b->dmem_packets, SIZE_128, CTR_NONE, MODE_WRITE, d));
    set_stream(2, STREAM_SRC_W(b->cmd, 0), COMMAND_BASE, STREAM_COUNT_W(1, SIZE_128, CTR_NONE, MODE_WRITE, d));
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 3);
    start_run();
    wait_all(EV_RUN_DONE);                          /* C&C 1's tables are free for the next run */
    if (d == DST_BROADCAST) for (unsigned t = 0; t < 64; t++) parity[t] = 1;   /* the next command is odd */
    else parity[d] = 1;
}
static void cc1_boot(const Boot *fs, const Boot *pager, const Boot *tiles, uint32_t data, uint32_t packets, uint32_t at) {
    boot_unit(DST_CC2, fs);                         /* unicast: d absolute */
    boot_unit(DST_CC3, pager);
    tile->EVENTS_MASK = EV_REQ_PENDING;             /* their start-up sends C&C 1 "started" (D31) */
    for (unsigned started = 0; started < 2; ) {
        __asm__ volatile ("wfi" ::: "memory");
        while (tile->EVENTS_PEEK & EV_REQ_PENDING) {
            uint32_t w0 = tile->REQUEST_GET_W0;
            (void)tile->REQUEST_RECEIVE;
            started += REQ_OP(w0) == REQ_OP_STARTED;   /* a controller's request: no reply (D38) */
        }
    }
    boot_unit(DST_BROADCAST, tiles);                /* d = 126 */
    cc1->READY_EXPECT[0] = ~0u;                     /* all 64 here; one at a time works too (D37.1) */
    cc1->READY_EXPECT[1] = ~0u;
    phase(CMD_STAGE, packets, 0, 0, 0);             /* boot GO; then CMD_STAGE, the images' first command */
    wait_all_ready();                               /* CMD_STAGE has landed and the images wait in wait_for_command() */
    uint32_t t = go(0);
    while (mcycle() - t < STAGE_DELAY) ;            /* no sooner than stage_delay after the GO store (6.2) */
    set_stream(0, STREAM_SRC_W(data, 0), SRAM_ADDR(at), STREAM_COUNT_W(packets, SIZE_128, CTR_SRAM, MODE_WRITE, DST_BROADCAST));
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
    start_run();                                    /* counted on counter 1, which every tile armed */
    wait_all(EV_RUN_DONE);
}
/* END */

/* RECIPE fence */
/* The fence (D43): something must follow a run's packets but leave by another sending unit, here the send port. DMA run
 * done says only that the run's last table read is done; ordering holds per sending unit (D44), not per tile. So the
 * run ends with a count-only packet (mode 5) to the tile itself on a spare counter: a stream run injects its packets
 * in order, so when that counter reaches 0 every earlier packet of the run has been injected, and what the send port
 * sends next follows them to each destination (of the same kind, D36). Here C&C 1 streams an image to a unit and then
 * sends its first command from the send port: the general form of recipe 22, which instead puts the command in the
 * run. The fence packet reads a chunk as usual (any will do) and its payload is discarded. */
static void boot_unit_fenced(unsigned d, const Boot *b, uint32_t entry) {
    set_stream(0, STREAM_SRC_W(b->pmem, 0), REGION_PMEM, STREAM_COUNT_W(b->pmem_packets, SIZE_128, CTR_NONE, MODE_WRITE, d));
    set_stream(1, STREAM_SRC_W(b->dmem, 0), REGION_DMEM + DMEM_FIRST, STREAM_COUNT_W(b->dmem_packets, SIZE_128, CTR_NONE, MODE_WRITE, d));
    set_stream(2, STREAM_SRC_W(b->cmd, 0), 0, STREAM_COUNT_W(1, SIZE_32, CTR_FENCE, MODE_COUNT, DST_CC1));   /* the fence */
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 3);
    tile->WRITE_COUNTER[CTR_FENCE - 1] = 1;         /* before the start: the fence comes from this run */
    start_run();
    wait_all(EV_CTR(CTR_FENCE) | EV_RUN_DONE);      /* every overlay packet has left; the tables are free */
    command(d, CMD_JUMP, 0, &entry, 1);             /* from the send port: it follows them, one kind, to each unit */
}
/* END */

/* RECIPE cc1_toast */
/* C&C 1's error path (D35, D36, D37), from its trap handler: a unit's error (its line, through the unit-error bit) or its
 * own. MIE is 0 and stays 0; toast is already latched in hardware. Best effort: start the file system's flush, drain
 * the reports into data memory as they arrive (so the tree's rows don't stall behind C&C 1's 128-entry queue, D38),
 * then send the dump out: down LVDS when an FPGA is in the loop, which waits as long as it needs before resetting, or
 * over the UART when a person resets by hand (D37). The host words are deferred, so the UART stands for both here.
 * Bounded by mcycle (D41). C&C 2's flush request format is a placeholder. */
#define REQ_OP_FLUSH 0x10
#define MAX_REPORTS  (66 * 24)                      /* every unit but C&C 1 failing at once, 24 reports each (recipe 24) */
#define DRAIN_QUIET  100000                         /* cycles with nothing new before the dump starts */
static uint32_t reports[MAX_REPORTS][4];            /* about 25 KB of C&C 1's 32 KB data memory */
static void uart_hex(uint32_t v) {
    for (int i = 28; i >= 0; i -= 4) cc1->UART_TX = "0123456789abcdef"[(v >> i) & 15];   /* stalls while TX is full */
    cc1->UART_TX = ' ';
}
void cc1_toast(uint32_t mcause, uint32_t mepc) {
    request_send(0, 0, 0, REQ_TARGET(TARGET_CC2) | (REQ_OP_FLUSH << 10));   /* 1. flush the logs to LPDDR (D35) */
    uint32_t n = 0;
    for (uint32_t last = mcycle(); mcycle() - last < DRAIN_QUIET && n < MAX_REPORTS; ) {   /* 2. drain */
        if (!(tile->EVENTS_PEEK & EV_REQ_PENDING)) continue;   /* never pop an empty queue */
        reports[n][0] = tile->REQUEST_GET_W0;       /* requestor [6:0], then the op and sequence */
        reports[n][1] = tile->REQUEST_GET_W1;
        reports[n][2] = tile->REQUEST_GET_W2;
        reports[n][3] = tile->REQUEST_RECEIVE;      /* pops */
        n++;
        last = mcycle();
    }
    uart_hex(mcause); uart_hex(mepc);               /* 3. what C&C 1 knows, then the reports (LVDS or UART) */
    for (int i = 0; i < 3; i++) uart_hex(cc1->ERROR_LINES[i]);
    for (uint32_t r = 0; r < n; r++) {
        for (int i = 0; i < 4; i++) uart_hex(reports[r][i]);
        cc1->UART_TX = '\n';
    }
    for (;;) ;                                      /* 4. park: the outside world resets the chip */
}
/* END */

int main(void) {
    static const Boot b = { 0x10000, 64, 0x11000, 16, 0x12000 };
    put_command(b.cmd, 0x01000000);
    cc1_boot(&b, &b, &b, 0x20000, 256, 0x30000);
    boot_unit_fenced(DST_CC3, &b, 0x01000000);
    for (;;) phase(1, 0, 0, 0, 1000000);
}
