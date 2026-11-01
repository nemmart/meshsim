/* C&C 1's firmware (WORKER14; meshsim with Config.ccCores): the boot (spec 6.2 steps 4-5, recipe 22's way), then a
 * loop on the harness's commands (W14-04). Built against the generated mesh.h by sw/build_cc.sh.
 *
 * The outside world (the harness, W14-02) lands this image and its first command (CMD_JUMP) through the host port,
 * having first placed every image C&C 1 sends in C&C 1's big SRAM and filled boot_desc in this image's data (W14-03).
 *  1. Each unit's first command (CMD_JUMP, its entry in slot 1) is put into C&C 1's own big SRAM as one 128-bit row,
 *     counted on counter 1, so it can go out as the last stream of that unit's run (recipe 22's put_command).
 *  2. C&C 2 and C&C 3 in one stream run, d = 65 and 66 absolute (W14-05): for each, program memory, data memory, the
 *     command, all uncounted and of one kind, from one sending unit, so its command lands last (3.2, 6.2 step 4).
 *  3. Both "started" requests (REQ_OP_STARTED, 6.2 step 4), from their start-up, through C&C 1's request port.
 *  4. The tiles: one stream run, d = 126 (the broadcast); READY_EXPECT = all 64; when all are READY, PULSE_GO, then
 *     go_latency + 1 cycles before anything else is sent (6.6).
 * Then the loop: C&C 1 waits for "command landed" (EVENTS [9], as the ROM's controller branch), serving requests
 * meanwhile, and runs the command:
 *  CC_PHASE (arg [2:0] n, [3] unicast, [10:4] its tile, [22:16] repeats, 0 meaning 1): the tile command word in slot
 *            +0 and n <= 6 arguments after it; C&C 1 sends the command, waits for all READY, GO (the Java model's
 *            command().go()), as many times as asked, back to back (the phase measurement);
 *  CC_GO:    wait for all READY, GO (the model's go() alone);
 *  CC_EXPECT: READY_EXPECT_0-1 = slots +0, +1;
 *  CC_EXIT:  SIM_EXIT with the argument;
 *  CC_WATCHDOG: WATCHDOG = the argument (each GO restarts it; 0 off), for the phases after;
 *  CC_UART:  UART_DIVISOR = the argument (the host's baud rate; the harness speeds the dump up with 1).
 * After the boot C&C 1 points mtvec at its own handler and enables its hard interrupt: a unit's error line (through
 * ERROR_LINES and the unit-error bit, its ERRORS [11]) or its own error traps it into the error path (cc1_toast,
 * below); so does the watchdog's expiry while it waits for READY.
 * Each command's completion is posted in mbox (W14-04): mbox[0] commands completed, [1] mcycle at the last all-READY,
 * [2] mcycle at the last GO store, [3] the boot's state (1 C&C 2/3 started, 2 tiles running), [4]-[5] the requestors
 * of the two "started" requests, [6] requests served (results answered), [7] other requests seen. */
#include <stdint.h>

#define CC_PHASE  0x50   /* 'P' */
#define CC_GO     0x47   /* 'G' */
#define CC_EXPECT 0x45   /* 'E' */
#define CC_EXIT   0x58   /* 'X' */
#define CC_WATCHDOG 0x57 /* 'W' */
#define CC_UART   0x55   /* 'U' */

typedef struct { uint32_t pmem, pmem_packets, dmem, dmem_packets, cmd, entry; } Image;   /* chunk addresses in C&C 1's big SRAM */
typedef struct { uint32_t magic; Image unit[3]; uint32_t go_latency, uart_divisor; } BootDesc;   /* unit: C&C 2, C&C 3, the tiles; divisor 0: the reset's */
/* in .data: the harness fills it in the image before landing it (W14-03) */
volatile BootDesc boot_desc __attribute__((section(".data"))) = { 0xB007DE5Cu, { { 0 } }, 1, 0 };
volatile uint32_t mbox[8] __attribute__((section(".data"))) = { 0 };
static uint32_t parity[2] __attribute__((section(".data"))) = { 0, 0 };   /* bit t: tile t's next command's parity (in .data: no .bss to zero) */

/* ---------------------------------------------------------------- requests (6.7; recipe 27's drain_requests) */
static void drain_requests(void) {
    while (tile->EVENTS_PEEK & EV_REQ_PENDING) {    /* never pop an empty queue (receive underflow is an error) */
        uint32_t w0 = tile->REQUEST_GET_W0, w1 = tile->REQUEST_GET_W1;
        (void)tile->REQUEST_GET_W2;
        (void)tile->REQUEST_RECEIVE;                /* the last word pops */
        uint32_t op = REQ_OP(w0), who = REQ_REQUESTOR(w0);
        if (op == REQ_OP_STARTED) { if (mbox[3] < 1) { if (!mbox[4]) mbox[4] = who; else mbox[5] = who; } }
        else if (op == REQ_OP_RESULT && who < 64) {  /* a tile's result: the reply ends its outstanding request (D38, D40) */
            send_to(ROUTER_HDR_W(who, MODE_WRITE, REQ_REPLY_CTR(w0)), w1);
            send32(1);
            mbox[6]++;
        } else mbox[7]++;
    }
}
static unsigned started(void) { return (mbox[4] != 0) + (mbox[5] != 0); }

/* ---------------------------------------------------------------- the phase cycle (6.3, 6.6; recipe 27) */
void cc1_toast(uint32_t mcause, uint32_t mepc) __attribute__((noreturn));
static void wait_all_ready(void) {
    cc1->EVENTS_MASK = CC1_EV_ALL_READY | CC1_EV_WATCHDOG;
    tile->EVENTS_MASK = EV_PERIPH(1) | EV_REQ_PENDING;
    for (;;) {                                      /* the hot path first: all READY ends the wait at once */
        uint32_t pev = cc1->EVENTS_PEEK;
        if (pev & CC1_EV_ALL_READY) break;          /* live: re-checked after every wake */
        if (pev & CC1_EV_WATCHDOG) cc1_toast(0, 0); /* the toast pin is latched (6.1); dump what there is */
        if (tile->EVENTS_PEEK & EV_REQ_PENDING) { drain_requests(); continue; }
        __asm__ volatile ("wfi" ::: "memory");
    }
    mbox[1] = mcycle();
    tile->EVENTS_MASK = EV_RESET_MASK;
}
static void go(void) {
    cc1->PULSE_GO = 1;
    uint32_t t = mcycle();
    mbox[2] = t;
    while (mcycle() - t <= boot_desc.go_latency) ;  /* go_latency + 1 before anything else is sent (6.6) */
}
/* A command to every tile (dst DST_BROADCAST) or one: the arguments into the block its parity names, then the word
 * into slot 0, last; every packet of it of one kind (3.2). */
static void command(unsigned dst, uint32_t word, const uint32_t *args, unsigned n) {
    unsigned u = dst == DST_BROADCAST ? 0 : dst, par = parity[u >> 5] >> (u & 31) & 1;   /* a broadcast: every tile at tile 0's */
    send_to(ROUTER_HDR_W(dst, MODE_WRITE, CTR_NONE), COMMAND_BASE + 4 * (par ? ARGS_ODD : ARGS_EVEN));
    for (unsigned i = 0; i < n; i++) send32(args[i]);
    tile->ROUTER_ADDR = COMMAND_BASE;
    send32((word & ~(1u << 23)) | par << 23);
    if (dst == DST_BROADCAST) parity[0] = parity[1] = par ? 0 : ~0u;
    else parity[u >> 5] ^= 1u << (u & 31);
}

/* ---------------------------------------------------------------- the boot (6.2; recipe 22) */
static void put_commands(void) {                   /* each unit's first command, into C&C 1's own big SRAM */
    tile->WRITE_COUNTER[CTR_SRAM - 1] = 3;
    for (unsigned u = 0; u < 3; u++) {
        send_to(ROUTER_HDR_W(DST_CC1, MODE_WRITE, CTR_SRAM), SRAM_ADDR(boot_desc.unit[u].cmd));
        send128(0, 0, boot_desc.unit[u].entry, CMD_WORD(CMD_JUMP, 0, 0));
    }
    wait_all(EV_CTR(CTR_SRAM));
}
/* Unit image b to d as streams 3k..3k + 2 of the next run: program memory, data memory, then the command, one kind */
static void unit_streams(unsigned k, unsigned d, const volatile Image *b) {
    set_stream(3 * k, STREAM_SRC_W(b->pmem, 0), REGION_PMEM, STREAM_COUNT_W(b->pmem_packets, SIZE_128, CTR_NONE, MODE_WRITE, d));
    set_stream(3 * k + 1, STREAM_SRC_W(b->dmem, 0), REGION_DMEM + DMEM_FIRST, STREAM_COUNT_W(b->dmem_packets, SIZE_128, CTR_NONE, MODE_WRITE, d));
    set_stream(3 * k + 2, STREAM_SRC_W(b->cmd, 0), COMMAND_BASE, STREAM_COUNT_W(1, SIZE_128, CTR_NONE, MODE_WRITE, d));
}
static void run_streams(unsigned n) {
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, n);
    start_run();
    wait_all(EV_RUN_DONE);                          /* the tables are free for the next run */
}
static void boot(void) {
    if (boot_desc.uart_divisor) cc1->UART_DIVISOR = boot_desc.uart_divisor;   /* the host's baud rate (W14-03) */
    put_commands();
    unit_streams(0, DST_CC2, &boot_desc.unit[0]);   /* C&C 2 and C&C 3 in one run: each unit's packets in order */
    unit_streams(1, DST_CC3, &boot_desc.unit[1]);
    run_streams(6);
    tile->EVENTS_MASK = EV_REQ_PENDING;             /* their start-up sends "started" (6.2 step 4) */
    while (started() < 2) { drain_requests(); if (started() < 2) __asm__ volatile ("wfi" ::: "memory"); }
    tile->EVENTS_MASK = EV_RESET_MASK;
    mbox[3] = 1;
    unit_streams(0, DST_BROADCAST, &boot_desc.unit[2]);
    run_streams(3);
    parity[0] = parity[1] = ~0u;                    /* the tiles' next command is odd */
    cc1->READY_EXPECT[0] = ~0u;
    cc1->READY_EXPECT[1] = ~0u;
    wait_all_ready();                               /* the ROMs pulsed done; the landed command raised READY */
    go();                                           /* the ROMs jump */
    mbox[3] = 2;
}

/* ---------------------------------------------------------------- the harness's commands (W14-04) */
static uint32_t wait_own_command(void) {
    cc1->EVENTS_MASK = CC1_EV_WATCHDOG;             /* a phase running past its watchdog is noticed here too */
    tile->EVENTS_MASK = EV_COMMAND | EV_REQ_PENDING | EV_PERIPH(1);
    for (;;) {
        drain_requests();
        if (cc1->EVENTS_PEEK & CC1_EV_WATCHDOG) cc1_toast(0, 0);
        if (tile->EVENTS & EV_COMMAND) break;       /* consumes [9]; [12] is live */
        __asm__ volatile ("wfi" ::: "memory");
    }
    tile->EVENTS_MASK = EV_RESET_MASK;
    return COMMAND[0];
}
/* ---------------------------------------------------------------- the error path (6.1, 4.13; recipe cc1_toast;
 * DECISIONS-v1.25 8d). MIE is 0 from the trap on and stays 0; the toast pin is already latched in hardware. Best effort:
 * drain the reports into data memory as they arrive, counting each unit's against its set, told by its first report's
 * sequence number: 0, the image's handler's 24 (0-23, recipe 24); 24, the boot ROM's two-report dump (24-25). The drain
 * ends when every unit that began a set has finished it and nothing more has come for SETTLE cycles, or when nothing
 * has come for DRAIN_QUIET (a unit whose set never completes; a unit with its error line up but no handler running).
 * Then the summary in toast_info and the dump over the UART, then park. Bounded by mcycle (4.11). */
#define REQ_SEQ(w0)  (((w0) >> 21) & 0x7FFu)
#define REPORTS      ((volatile uint32_t (*)[4])0x1000)   /* 0x1000-0x4FFF: 1,024 reports, above the image's data */
#define MAX_REPORTS  1024
#define SETTLE       1000
#define DRAIN_QUIET  50000
/* [0] mcause, [1] mepc, [2]-[4] ERROR_LINES_0-2, [5] reports drained, [6] units with a set begun, [7] units whose set
 * completed, [8] flags: bit 0 a set began with a sequence number other than 0 or 24, bit 1 a sequence number out of
 * its set or repeated, bit 2 a request other than a report, [9] the drain's end (mcycle), [10] the dump's end */
volatile uint32_t toast_info[12] __attribute__((section(".data"))) = { 0 };
static void uart_hex(uint32_t v) {
    for (int i = 28; i >= 0; i -= 4) cc1->UART_TX = "0123456789abcdef"[(v >> i) & 15];   /* stalls while TX is full */
    cc1->UART_TX = ' ';
}
void cc1_toast(uint32_t mcause, uint32_t mepc) {
    uint8_t want[67], got[67];
    uint32_t seen[67][1];
    for (int u = 0; u < 67; u++) { want[u] = 0; got[u] = 0; seen[u][0] = 0; }
    uint32_t n = 0, begun = 0, done = 0, flags = 0;
    for (uint32_t last = mcycle(); ; ) {
        uint32_t quiet = mcycle() - last;
        if (!(tile->EVENTS_PEEK & EV_REQ_PENDING)) {   /* never pop an empty queue */
            if ((begun && done == begun && quiet >= SETTLE) || quiet >= DRAIN_QUIET) break;
            continue;
        }
        uint32_t w0 = tile->REQUEST_GET_W0, w1 = tile->REQUEST_GET_W1, w2 = tile->REQUEST_GET_W2;
        uint32_t w3 = tile->REQUEST_RECEIVE;        /* pops */
        if (n < MAX_REPORTS) { REPORTS[n][0] = w0; REPORTS[n][1] = w1; REPORTS[n][2] = w2; REPORTS[n][3] = w3; }
        n++;
        last = mcycle();
        uint32_t u = REQ_REQUESTOR(w0), seq = REQ_SEQ(w0);
        if (REQ_OP(w0) != REQ_OP_REPORT || u > 66) { flags |= 4; continue; }
        if (!want[u]) {                             /* the first report says which set follows */
            want[u] = seq == 24 ? 2 : 24;
            if (seq != 0 && seq != 24) flags |= 1;
            begun++;
        }
        uint32_t first = want[u] == 2 ? 24 : 0;
        if (seq < first || seq >= first + want[u] || (seen[u][0] >> (seq - first) & 1)) flags |= 2;
        else seen[u][0] |= 1u << (seq - first);
        if (got[u] < want[u] && ++got[u] == want[u]) done++;
    }
    toast_info[0] = mcause; toast_info[1] = mepc;
    for (int i = 0; i < 3; i++) toast_info[2 + i] = cc1->ERROR_LINES[i];
    toast_info[5] = n; toast_info[6] = begun; toast_info[7] = done; toast_info[8] = flags; toast_info[9] = mcycle();
    /* the dump (LVDS or UART, D37; the UART stands for both): what C&C 1 knows, then each unit's count, then the reports */
    for (int i = 0; i < 9; i++) uart_hex(toast_info[i]);
    cc1->UART_TX = '\n';
    for (uint32_t u = 0; u < 67; u++) if (want[u]) { uart_hex(u); uart_hex(want[u]); uart_hex(got[u]); cc1->UART_TX = '\n'; }
    for (uint32_t r = 0; r < n && r < MAX_REPORTS; r++) {
        for (int i = 0; i < 4; i++) uart_hex(REPORTS[r][i]);
        cc1->UART_TX = '\n';
    }
    while (cc1->UART_STATUS & 0x1F) ;               /* the TX FIFO has emptied into the shifter */
    toast_info[10] = mcycle();
    for (;;) ;                                      /* park: the outside world resets the chip */
}
/* mtvec's target: MIE is now 0 and stays 0 (the handler never returns) */
void cc1_trap(void) __attribute__((noreturn, aligned(4)));
void cc1_trap(void) {
    uint32_t c, e;
    __asm__ volatile ("csrr %0, mcause" : "=r"(c));
    __asm__ volatile ("csrr %0, mepc" : "=r"(e));
    cc1_toast(c, e);
}

int main(void) {
    boot();
    __asm__ volatile ("csrw mtvec, %0" :: "r"(cc1_trap));   /* C&C 1's own handler, then the hard interrupt (6.1) */
    __asm__ volatile ("csrs mie, %0" :: "r"(1u << 11));      /* mie.MEIE */
    __asm__ volatile ("csrsi mstatus, 8");                   /* mstatus.MIE */
    for (;;) {
        uint32_t w = wait_own_command();
        const volatile uint32_t *a = CMD_ARGS(w);
        uint32_t arg = CMD_ARG(w);
        switch (CMD_TYPE(w)) {
        case CC_PHASE: {
            unsigned n = arg & 7, dst = arg & 8 ? (arg >> 4) & 63 : DST_BROADCAST, reps = (arg >> 16) & 0x7F;
            uint32_t cw[7];                         /* the tile command, copied out of C&C 1's own command block once */
            if (n > 6) n = 6;
            for (unsigned i = 0; i <= n; i++) cw[i] = a[i];
            do {
                command(dst, cw[0], cw + 1, n);
                wait_all_ready();
                go();
            } while (reps-- > 1);
            break;
        }
        case CC_GO: wait_all_ready(); go(); break;
        case CC_EXPECT: cc1->READY_EXPECT[0] = a[0]; cc1->READY_EXPECT[1] = a[1]; break;
        case CC_EXIT: mbox[0]++; sim_exit(arg); break;
        case CC_WATCHDOG: cc1->WATCHDOG = arg; break;
        case CC_UART: cc1->UART_DIVISOR = arg; break;
        default: mbox[7] |= 0x80000000u; break;   /* an unknown command: flagged in the mailbox */
        }
        mbox[0]++;
    }
}
