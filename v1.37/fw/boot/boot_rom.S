/* The boot ROM's program (rtl/tile/base/spec.md 6.5): every tile's, from the reset
 * PC, the ROM's first byte, 0x0200_0000 (5.1). The ROM's contents are its halfwords
 * in boot_rom_pkg.veryl, written there by hand from this file as the GNU assembler
 * encodes it; boot_rom_test checks that they are equal, word for word, at every
 * configuration it runs at (mise run core-images assembles this file into
 * build/core-tests/boot_rom.elf, linked at the ROM's base).
 *
 * It depends on no configuration: it branches on the straps alone, never on the
 * tile id (6.5), so its words are the same at every configuration.
 *
 * The steps, 6.5's:
 *   1. mtvec to the handler, below, whatever an image left there (6.2); mie.MEIE,
 *      the wake (6.2), mstatus.MIE staying 0, as the reset leaves it and every
 *      image keeps it, so the wake releases a WFI and never traps; and EVENTS_MASK
 *      to its reset value, release and all_counters_done (18).
 *   2. PARAMS (7.1): with boot_local 1 (the master tile and the memory tile), a
 *      jump to IMEM's base at once, to the image placed there before the reset
 *      ended.
 *   3. Otherwise, the load (a compute tile and the tracer tile):
 *      1. arms COUNTER_7 with 1 (12.2), which clears and arms all_counters_done
 *         (7.2), then sends the master tile a LOAD request (11.5): word 0 its op,
 *         3 at [17:10], with the requester and the target, the master tile's 0,
 *         left 0 (11.2, 11.3), and words 1-3 0, stored to REQUEST_OUT_W0-W2 and
 *         then REQUEST_SEND (11.1), whatever an image left in them. The arming
 *         store takes effect before the request is sent (6.4). PARAMS.barrier is
 *         kept in a1 for step 3.3, taken while the load is in flight.
 *      2. waits for the load in a WFI, re-checking after every wake (17.2): its
 *         load of EVENTS consumes all_counters_done, which EVENTS_MASK selects,
 *         once the load's COUNT has drained counter 7, the only one armed.
 *      3. with PARAMS.barrier 1 (a compute tile), stores to ARRIVE and waits in a
 *         WFI, the canonical wait (7.3), for the release, which the arrival gate
 *         makes its only wake; its load of EVENTS consumes it. With 0 (the tracer
 *         tile), no arrival: an ARRIVE there is UB (7.3).
 *      4. jumps to IMEM's base, 0x0100_0000, where the load placed its image.
 *
 * So every image starts with mtvec the handler, mie.MEIE set and mstatus.MIE 0,
 * EVENTS_MASK at its reset value and, on a loaded tile, all_counters_done
 * consumed and every counter at 0, and with the barrier the release consumed and
 * counter_7_done cleared by the ARRIVE (7.2), without it counter_7_done still set.
 * No register holds a value an image may use. A loaded image ends by jumping to
 * the reset PC, its counters at 0 (6.5), and the ROM asks for the next load. The ROM never writes DMEM, and never writes
 * ra (x1), which report 25 carries.
 *
 * The handler, at mtvec's reset value (6.2; boot_rom_pkg's HANDLER), sends the
 * master tile reports 24 and 25 (6.5): each a REPORT request (11.5), word 0 its
 * op, 1 at [17:10], and its number at [25:18], stored as the LOAD request is,
 * its words 1-3 in ascending order (11.1). Then it parks in a jump to itself.
 * Every trap is terminal (6.2), so it keeps nothing but ra.
 *
 * Registers: the tile window's, 0xFFFF_F800 up (5.5), each an x0-relative
 * offset, so one lw or sw each (5.2).
 */

/* ---- The tile window's registers (5.5): 0xFFFF_F800 + offset, as x0 + offset ---- */
#define PARAMS          (-0x800)        /* block 0, word 0: 0xFFFF_F800 */
#define STATUS          (-0x7FC)        /* block 0, word 1 */
#define ERRORS          (-0x7F8)        /* block 0, word 2 */
#define EVENTS          (-0x7F4)        /* block 0, word 3 */
#define EVENTS_MASK     (-0x7EC)        /* block 0, word 5 */
#define ARRIVE          (-0x7E8)        /* block 0, word 6 */
#define REQUEST_OUT_W0  (-0x700)        /* block 4, word 0: 0xFFFF_F900 */
#define REQUEST_OUT_W1  (-0x6FC)        /* block 4, word 1 */
#define REQUEST_OUT_W2  (-0x6F8)        /* block 4, word 2 */
#define REQUEST_SEND    (-0x6F4)        /* block 4, word 3 */
#define COUNTER_7       (-0x6A4)        /* block 5, word 7: 0xFFFF_F95C */

/* ---- Fields ---- */
#define MIE_MEIE_BIT            11      /* mie's MEIE (the core specification, CSRs) */
#define PARAMS_BARRIER_BIT      17      /* PARAMS [17] (7.1) */
#define PARAMS_BOOT_LOCAL_BIT   18      /* PARAMS [18] */
#define EVENT_RELEASE           (1 << 0) /* EVENTS [0] (7.2) */
#define EVENT_ALL_COUNTERS_DONE (1 << 2) /* EVENTS [2] */
#define EVENTS_MASK_RESET       (EVENT_RELEASE | EVENT_ALL_COUNTERS_DONE) /* (18) */
#define OP_REPORT               1       /* the master tile's REPORT (11.5) */
#define OP_LOAD                 3       /* the master tile's LOAD (11.5) */
#define OP_BIT                  10      /* word 0's op, [17:10] (11.5) */
#define REPORT(number)          ((number) << 18 | OP_REPORT << OP_BIT) /* word 0 (11.5) */
#define IMEM_BASE               0x01000000 /* region 0x01 (5.1) */

    .option norelax                     /* every offset final here, none left to the linker */
    .text
    .globl _start
_start:
    /* ---- 1. mtvec, mie.MEIE and EVENTS_MASK ---- */
1:  auipc   a0, %pcrel_hi(handler)      /* the handler's address, from the reset PC */
    addi    a0, a0, %pcrel_lo(1b)
    csrw    mtvec, a0
    li      a0, 1
    slli    a0, a0, MIE_MEIE_BIT
    csrs    mie, a0
    li      a0, EVENTS_MASK_RESET
    sw      a0, EVENTS_MASK(zero)

    /* ---- 2. The boot strap: boot_local, the image already in IMEM ---- */
    lw      a1, PARAMS(zero)
    srli    a2, a1, PARAMS_BOOT_LOCAL_BIT
    andi    a2, a2, 1
    bnez    a2, image

    /* ---- 3. The load: COUNTER_7 armed, the LOAD request, the load, the release ---- */
    li      a0, 1                       /* COUNTER_7's count and ARRIVE's bit 0 (1.2) */
    sw      a0, COUNTER_7(zero)
    li      a2, OP_LOAD
    slli    a2, a2, OP_BIT
    sw      a2, REQUEST_OUT_W0(zero)
    sw      zero, REQUEST_OUT_W1(zero)
    sw      zero, REQUEST_OUT_W2(zero)
    sw      zero, REQUEST_SEND(zero)    /* stalls while the request queue is full (11.1) */
    srli    a1, a1, PARAMS_BARRIER_BIT  /* a1: PARAMS.barrier, for step 3.3 */
    andi    a1, a1, 1
    .globl wait_load
wait_load:
    wfi
    lw      a2, EVENTS(zero)            /* consumes all_counters_done */
    andi    a2, a2, EVENT_ALL_COUNTERS_DONE
    beqz    a2, wait_load
    beqz    a1, image                   /* no barrier: no arrival (7.3) */
    sw      a0, ARRIVE(zero)
    .globl wait_release
wait_release:
    wfi                                 /* the canonical wait: the gate lets only the release wake it */
    lw      a2, EVENTS(zero)            /* consumes the release */
    andi    a2, a2, EVENT_RELEASE
    beqz    a2, wait_release
    .globl image
image:
    lui     a0, %hi(IMEM_BASE)
    jr      a0

    /* ---- The handler: reports 24 and 25, then the park ---- */
    .balign 4                           /* mtvec's base is a word's (the core specification, CSRs) */
    .globl handler
handler:
    lui     a0, %hi(REPORT(24))         /* report 24: ERRORS, mcause, mepc */
    addi    a0, a0, %lo(REPORT(24))
    lw      a1, ERRORS(zero)
    csrr    a2, mcause
    csrr    a3, mepc
    sw      a0, REQUEST_OUT_W0(zero)
    sw      a1, REQUEST_OUT_W1(zero)
    sw      a2, REQUEST_OUT_W2(zero)
    sw      a3, REQUEST_SEND(zero)      /* stalls while the request queue is full (11.1) */
    lui     a0, %hi(REPORT(25))         /* report 25: mtval, STATUS, ra */
    addi    a0, a0, %lo(REPORT(25))
    csrr    a1, mtval
    lw      a2, STATUS(zero)
    sw      a0, REQUEST_OUT_W0(zero)
    sw      a1, REQUEST_OUT_W1(zero)
    sw      a2, REQUEST_OUT_W2(zero)
    sw      ra, REQUEST_SEND(zero)
    .globl park
park:
    j       park                        /* only a reset restarts the core */
