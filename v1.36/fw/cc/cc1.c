/* v1.36: C&C 1 is the master tile (64), its peripheral 1 Ingress/Egress (17.6), and the hardware has no commands
 * (6.5, 7.3): the tiles pull their images by LOAD and meet at the barrier, arrive and release. What changed, and only
 * that (fw/README.md, "What changed"): the harness places this image in the master's IMEM and DMEM before the reset
 * ends (boot_local, 6.5), with its images in the master's scratchpad, boot_desc filled, and its commands, which the
 * host port no longer carries, in boot_desc.script, run in order; the boot waits for every LOAD (the compute tiles'
 * and the tracer tile's), sends the tracer tile its image if boot_desc.unit[1] has one (the memory tile, C&C 2's
 * place, starts from its placed image), and ends each image's run with a COUNT on counter 7, the boot ROM's, in place
 * of the command; READY is ARRIVALS and GO the RELEASE (port/mesh.h); no tile command is sent (CC_PHASE and
 * CC_SEGMENT keep their barriers alone), no "started" request comes, a tile's result is the request its image's end
 * sends (crt0.S), answered at the address its argument names; an error is the NMI, with mstatus.MIE kept 0 (6.2); a
 * report's number is word 0 [25:18]; v1.35's exit word is sim_exit_word (crt0.S). Two commands are new: CC_WAIT,
 * the wait for the tiles' results, which v1.35's harness saw in the exit word, and CC_FAULT, a bad packet to every
 * compute tile and the memory tile, whose reports take C&C 1 into its error path.
 *
 * C&C 1's firmware (WORKER14; meshsim with Config.ccCores): the boot (spec 6.2 steps 4-5, recipe 22's way), then a
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
 *  CC_SEGMENT (v1.3, B1; DECISIONS-v1.3 section 24, 9544e8e and 7c53209): a traced segment's two barriers. Slot +0 the
 *            tiles' command word, +1 C&C 3's start word. The tiles' command; all READY (each tile booted, nothing
 *            armed); GO; C&C 3's start, into its command block (it sends nothing sooner than stage_delay after it); the
 *            tiles' second command; all READY again (each tile's window has landed, its counters at 0); the final GO,
 *            the replay.
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
#define CC_SEGMENT 0x54  /* 'T' */
#define CC_UART   0x55   /* 'U' */
#define CC_WAIT   0x52   /* 'R' (v1.36): wait for the argument's results, and as many LOADs after them (6.5) */
#define CC_FAULT  0x46   /* 'F' (v1.36): a bad packet to every compute tile and the memory tile */
#define CMD_TYPE(w)  ((uint32_t)(w) >> 24)        /* a command's word: [31:24] type, [22:0] argument */
#define CMD_ARG(w)   ((uint32_t)(w) & 0x7FFFFFu)
#define TILES_LOADING 65                          /* the LOADs of the boot: the compute tiles' and the tracer tile's */
#define SCRIPT_COMMANDS 32

typedef struct { uint32_t pmem, pmem_packets, dmem, dmem_packets, cmd, entry; } Image;   /* chunk addresses in C&C 1's big SRAM */
typedef struct { uint32_t magic; Image unit[3]; uint32_t go_latency, uart_divisor; uint32_t script[SCRIPT_COMMANDS][4]; } BootDesc;   /* unit: C&C 2, C&C 3, the tiles; divisor 0: the reset's; v1.36: script, the commands, each a word and its arguments */
/* in .data: the harness fills it in the image before landing it (W14-03) */
volatile BootDesc boot_desc __attribute__((section(".data"))) = { 0xB007DE5Cu, { { 0 } }, 1, 0, { { 0 } } };
volatile uint32_t mbox[8] __attribute__((section(".data"))) = { 0 };
static uint32_t next_command __attribute__((section(".data"))) = 0;   /* v1.36: the script's next (in .data: no .bss to zero) */

/* ---------------------------------------------------------------- requests (6.7; recipe 27's drain_requests) */
static void drain_requests(void) {
    while (tile->EVENTS_PEEK & EV_REQ_PENDING) {    /* never pop an empty queue (receive underflow is an error) */
        uint32_t w0 = tile->REQUEST_GET_W0;
        (void)tile->REQUEST_GET_W1;
        (void)tile->REQUEST_GET_W2;
        (void)tile->REQUEST_RECEIVE;                /* the last word pops */
        uint32_t op = REQ_OP(w0), who = REQ_REQUESTOR(w0);
        if (op == REQ_OP_LOAD) mbox[4]++;          /* v1.36: a boot ROM's LOAD (6.5); answered by the boot alone */
        else if (op == REQ_OP_RESULT && who <= DST_CC3) {  /* a tile's result (v1.36: the tracer tile's too): the reply ends its outstanding request (D38, D40) */
            send_to(ROUTER_HDR_W(who, MODE_WRITE, REQ_REPLY_CTR(w0)), REQ_ARG(w0) << 4);
            send32(1);
            mbox[6]++;
        } else mbox[7]++;
    }
}

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

/* ---------------------------------------------------------------- the boot (6.2; recipe 22) */
/* Unit image b to d as streams 3k..3k + 2 of the next run: program memory, data memory, then (v1.36) the load's COUNT
 * on counter 7, from the row at b->cmd, for the command, one kind (6.5); b->entry is unused (the ROM jumps to IMEM's
 * base) */
static void unit_streams(unsigned k, unsigned d, const volatile Image *b) {
    set_stream(3 * k, STREAM_SRC_W(b->pmem, 0), REGION_PMEM, STREAM_COUNT_W(b->pmem_packets, SIZE_128, CTR_NONE, MODE_WRITE, d));
    set_stream(3 * k + 1, STREAM_SRC_W(b->dmem, 0), REGION_DMEM + DMEM_FIRST, STREAM_COUNT_W(b->dmem_packets, SIZE_128, CTR_NONE, MODE_WRITE, d));
    set_stream(3 * k + 2, STREAM_SRC_W(b->cmd, 0), 0, STREAM_COUNT_W(1, SIZE_128, CTR_LOAD, MODE_COUNT, d));
}
static void run_streams(unsigned n) {
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, n);
    start_run();
    wait_all(EV_RUN_DONE);                          /* the tables are free for the next run */
}
static void boot(void) {
    if (boot_desc.uart_divisor) cc1->UART_DIVISOR = boot_desc.uart_divisor;   /* the host's baud rate (W14-03) */
    tile->EVENTS_MASK = EV_REQ_PENDING;             /* v1.36: every LOAD first (6.5) */
    while (mbox[4] < TILES_LOADING) { drain_requests(); if (mbox[4] < TILES_LOADING) __asm__ volatile ("wfi" ::: "memory"); }
    tile->EVENTS_MASK = EV_RESET_MASK;
    if (boot_desc.unit[1].pmem_packets) {           /* C&C 3's image, the tracer tile's (its LOAD answered) */
        unit_streams(0, DST_CC3, &boot_desc.unit[1]);
        run_streams(3);
    }
    mbox[3] = 1;
    unit_streams(0, DST_BROADCAST, &boot_desc.unit[2]);
    run_streams(3);
    cc1->READY_EXPECT[0] = ~0u;
    cc1->READY_EXPECT[1] = ~0u;
    wait_all_ready();                               /* the ROMs arrived, the load landed */
    go();                                           /* the ROMs jump */
    mbox[3] = 2;
}

/* ---------------------------------------------------------------- the harness's commands (W14-04) */
static uint32_t wait_own_command(void) {          /* v1.36: the script's next; past its end, CC_EXIT */
    drain_requests();
    if (cc1->EVENTS_PEEK & CC1_EV_WATCHDOG) cc1_toast(0, 0);   /* a phase running past its watchdog is noticed here too */
    return next_command < SCRIPT_COMMANDS ? boot_desc.script[next_command++][0] : (uint32_t)CC_EXIT << 24;
}
/* v1.36: CC_WAIT's wait: n results answered, and as many LOADs after them, each its image's end (6.5) */
static void wait_results(uint32_t n) {
    cc1->EVENTS_MASK = CC1_EV_WATCHDOG;
    tile->EVENTS_MASK = EV_REQ_PENDING | EV_PERIPH(1);
    for (;;) {
        drain_requests();
        if (mbox[6] >= n && mbox[4] >= TILES_LOADING + n) break;
        if (cc1->EVENTS_PEEK & CC1_EV_WATCHDOG) cc1_toast(0, 0);
        __asm__ volatile ("wfi" ::: "memory");
    }
    tile->EVENTS_MASK = EV_RESET_MASK;
}
/* v1.36: CC_FAULT's bad packets, a WRITE to the boot ROM (4.2), one to every compute tile and one to the memory tile,
 * in one transfer, which goes on whatever the core does; the first error line raises C&C 1's NMI (6.2, 17.6) */
static void fault_all(void) {
    set_stream(0, STREAM_SRC_W(boot_desc.unit[2].cmd, 0), RESET_PC, STREAM_COUNT_W(1, SIZE_32, CTR_NONE, MODE_WRITE, DST_BROADCAST));
    set_stream(1, STREAM_SRC_W(boot_desc.unit[2].cmd, 0), RESET_PC, STREAM_COUNT_W(1, SIZE_32, CTR_NONE, MODE_WRITE, DST_CC2));
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 2);
    start_run();
    tile->EVENTS_MASK = 0;
    for (;;) __asm__ volatile ("wfi" ::: "memory");  /* the NMI ends it */
}
/* ---------------------------------------------------------------- the error path (6.1, 4.13; recipe cc1_toast;
 * DECISIONS-v1.25 8d). MIE is 0 from the trap on and stays 0; the toast pin is already latched in hardware. Best effort:
 * drain the reports into data memory as they arrive, counting each unit's against its set, told by its first report's
 * sequence number: 0, the image's handler's 24 (0-23, recipe 24); 24, the boot ROM's two-report dump (24-25). The drain
 * ends when every unit that began a set has finished it and nothing more has come for SETTLE cycles, or when nothing
 * has come for DRAIN_QUIET (a unit whose set never completes; a unit with its error line up but no handler running).
 * Then the summary in toast_info and the dump over the UART, then park. Bounded by mcycle (4.11). */
#define REQ_SEQ(w0)  REQ_NUMBER(w0)                  /* v1.36: a report's number, word 0 [25:18] (11.5) */
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
    __asm__ volatile ("csrw mtvec, %0" :: "r"(cc1_trap));   /* C&C 1's own handler; v1.36: an error is the NMI, which */
    __asm__ volatile ("csrs mie, %0" :: "r"(1u << 11));      /* mie.MEIE: the wake (6.2); mstatus.MIE stays 0 */
    for (;;) {
        uint32_t w = wait_own_command();
        const volatile uint32_t *a = &boot_desc.script[next_command - 1][1];   /* v1.36: the script's arguments */
        uint32_t arg = CMD_ARG(w);
        switch (CMD_TYPE(w)) {
        case CC_PHASE: {
            unsigned reps = (arg >> 16) & 0x7F;     /* v1.36: no tile command, so no [10:0] */
            do {
                wait_all_ready();
                go();
            } while (reps-- > 1);
            break;
        }
        case CC_GO: wait_all_ready(); go(); break;
        case CC_SEGMENT: {                          /* B1's barriers (Q15, Q17): C&C 1 owns the segment's READY/GO */
            wait_all_ready();                       /* each tile's READY (nothing armed) */
            go();                                   /* the tiles arm their record's counters on waking; v1.36: no
                                                       command, no C&C 3 start: its handshakes are the tiles' requests */
            wait_all_ready();                       /* every tile's window has landed */
            go();                                   /* the replay, all 64 together */
            break;
        }
        case CC_EXPECT: cc1->READY_EXPECT[0] = a[0]; cc1->READY_EXPECT[1] = a[1]; break;
        case CC_EXIT: mbox[0]++; sim_exit(arg); break;
        case CC_WATCHDOG: cc1->WATCHDOG = arg; break;
        case CC_UART: cc1->UART_DIVISOR = arg; break;
        case CC_WAIT: wait_results(arg); break;
        case CC_FAULT: fault_all(); break;
        default: mbox[7] |= 0x80000000u; break;   /* an unknown command: flagged in the mailbox */
        }
        mbox[0]++;
    }
}
