/* Addresses and constants of the rebuilt tile's programs: the programs base_tile_test
 * boots on base_tile through its boot ROM, as the master tile does, and special, which
 * base_tile_special_test places before the reset ends (their build:
 * xtask/src/tile_tests.rs). Every address here is the base tile specification's
 * (rtl/tile/base/spec.md, 5; tile_pkg), and every field its registers' (5.5, 7-13).
 *
 * Included by every program and, through the C preprocessor, by link.ld. cargo xtask
 * defines MESH_SIZE, IMEM_BYTES, DMEM_BYTES and SCRATCHPAD_BYTES as Veryl.toml
 * configures them; there are deliberately no defaults.
 *
 * Protocol with the test (base_tile_test.rs follows the same):
 *
 * - Boot (6.5): after the tile's reset the boot ROM arms COUNTER_7 and sends the
 *   master tile a LOAD request (11.5, op 3). The test, as the master tile, answers it
 *   with the load, all on one of the router's inputs, so that it arrives in order: the
 *   program's segments by uncounted 128-bit WRITE packets, a row each, into IMEM and
 *   DMEM at their load addresses (the code's first word, _start, at IMEM_BASE), then
 *   its arguments, ARGUMENTS' two rows, likewise, then one COUNT packet on counter 7.
 *   The ROM arrives once counter 7 drains; its BARRIER_ARRIVE pulses, and the test
 *   releases it: the ROM jumps to IMEM_BASE. Argument 0 is the program's kind, 1 to 7
 *   its own, as each program's header gives them. DMEM past what the test writes is
 *   dirty; .bss is zeroed by the test.
 * - Reports (11.5): a program reports to the master tile by REPORT requests (op 1)
 *   on the request tree, words 1 to 3 its data; the number says which:
 *   REPORT_PASS, with the program's words; REPORT_FAIL, a check's number, its expected
 *   value and its actual value; REPORT_WAIT, a program waiting for the test. Every
 *   trap goes to the boot ROM's handler, mtvec's reset value, which the programs keep:
 *   it sends reports 24 and 25 (6.5) and parks.
 * - The end (6.5): after its last report a program on a compute tile ends by jumping
 *   to the reset PC, its counters at 0, and the boot ROM sends its next LOAD request;
 *   on a special tile it parks for good, in a WFI that no event wakes.
 */

#if !defined(MESH_SIZE) || !defined(IMEM_BYTES) || !defined(DMEM_BYTES) || !defined(SCRATCHPAD_BYTES)
#error "MESH_SIZE, IMEM_BYTES, DMEM_BYTES and SCRATCHPAD_BYTES must be defined: cargo xtask passes Veryl.toml's"
#endif

/* ---- The map (5.1) ---- */

#define DMEM_BASE 0x00000000
#define IMEM_BASE 0x01000000
#define SCRATCHPAD_BASE 0x03000000
#define PERIPHERAL_1_BASE 0x04000000 /* peripheral n's region: 0x03 + n */

#define RESET_PC 0x02000000 /* the boot ROM's first word (6.5) */

/* DMEM's first 16 bytes are reserved (6.3). The programs' arguments, which the load
 * writes, are the 8 words after them (6.5: data at addresses the image fixes), each
 * an x0-relative address; argument 0 is the program's kind. */
#define DMEM_RESERVED 16
#define ARGUMENTS (DMEM_BASE + DMEM_RESERVED)
#define ARGUMENT_BYTES 32
#define ARGUMENT(n) (ARGUMENTS + 4 * (n))

/* ---- The tile ids (4.1) ---- */

#define TILE_COUNT (MESH_SIZE * MESH_SIZE)
#if TILE_COUNT + 5 <= 8
#define TILE_ID_BITS 3
#elif TILE_COUNT + 5 <= 16
#define TILE_ID_BITS 4
#elif TILE_COUNT + 5 <= 32
#define TILE_ID_BITS 5
#elif TILE_COUNT + 5 <= 64
#define TILE_ID_BITS 6
#else
#define TILE_ID_BITS 7
#endif

/* ---- Region 0xFF (5.2, 5.5): the tile window's registers, each an x0-relative
 * offset from 0xFFFF_F800, so one lw or sw each; the tables need a base register ---- */

#define PARAMS (-0x800) /* block 0 */
#define STATUS (-0x7FC)
#define ERRORS (-0x7F8)
#define EVENTS (-0x7F4)
#define EVENTS_PEEK (-0x7F0)
#define EVENTS_MASK (-0x7EC)
#define ARRIVE (-0x7E8)
#define DMA_CONFIG (-0x7C0) /* block 1 */
#define DMA_START (-0x7BC)
#define WRITE_HDR (-0x780) /* block 2 */
#define WRITE_DEST_ADDR (-0x77C)
#define WRITE_W0 (-0x778)
#define WRITE_W1 (-0x774)
#define WRITE_W2 (-0x770)
#define WRITE_SEND_32 (-0x76C)
#define WRITE_SEND_64 (-0x768)
#define WRITE_SEND_128 (-0x764)
#define READ_HDR (-0x740) /* block 3 */
#define READ_DEST_ADDR (-0x73C)
#define READ_SEND (-0x738)
#define READ_FREE (-0x734)
#define REQUEST_OUT_W0 (-0x700) /* block 4 */
#define REQUEST_OUT_W1 (-0x6FC)
#define REQUEST_OUT_W2 (-0x6F8)
#define REQUEST_SEND (-0x6F4)
#define REQUEST_IN_W0 (-0x6F0)
#define REQUEST_IN_W1 (-0x6EC)
#define REQUEST_IN_W2 (-0x6E8)
#define REQUEST_RECEIVE (-0x6E4)
#define RECEIVE_CONFIG (-0x6C0) /* block 5 */
#define COUNTER(n) (-0x6C0 + 4 * (n))
#define GATHER_CONFIG (-0x680) /* block 6 */
#define UNMAPPED_TILE_WORD (-0x7E4) /* block 0, word 7: unmapped (5.3) */

/* The tables' windows (5.2): entry i at +4i; GROUP[g]'s words at +8g and +8g + 4. */
#define HIST_BASE 0xFFFFE000
#define PTR_BASE 0xFFFFE400
#define STREAM_SRC_ADDR_BASE 0xFFFFE800
#define STREAM_DEST_ADDR_BASE 0xFFFFEC00
#define STREAM_HDR_BASE 0xFFFFF000
#define GROUP_BASE 0xFFFFF400

/* A peripheral's window, 0xFFFF_F800 + 0x200n (17.1), as an x0-relative offset. */
#define PERIPHERAL_WINDOW(n) (-0x800 + 0x200 * (n))

/* ---- Fields ---- */

/* PARAMS (7.1) */
#define PARAMS_TILE_ID 0x7F
/* STATUS (7.1) */
#define STATUS_ARRIVED 0x1
#define STATUS_DMA_BUSY 0x2
#define STATUS_GATHER_BUSY 0x4
#define STATUS_REQUEST_FULL 0x8
/* EVENTS (7.2) */
#define EVENT_RELEASE 0x1
#define EVENT_DMA_DONE 0x2
#define EVENT_ALL_COUNTERS_DONE 0x4
#define EVENT_REQUEST_PENDING 0x8
#define EVENT_PERIPHERAL(n) (0x8 << (n))
#define EVENT_COUNTER_DONE(n) (0x40 << (n))
#define EVENTS_MASK_RESET (EVENT_RELEASE | EVENT_ALL_COUNTERS_DONE)
/* ERRORS (7.4) */
#define ERROR_ROUTER_OVERFLOW 0x1
#define ERROR_BAD_PACKET 0x2
#define ERROR_COUNTER_UNDERFLOW 0x4
#define ERROR_DRIFT 0x8
#define ERROR_GATHER_OVERFLOW 0x10
#define ERROR_READ_OVERFLOW 0x20
#define ERROR_EARLY_ARRIVAL 0x40
#define ERROR_ACCESS_FAULT 0x80
#define ERROR_ILLEGAL_INSTRUCTION 0x100
#define ERROR_RECEIVE_UNDERFLOW 0x200

/* The packet's header block (4.1, 5.4): size [1:0], counter [4:2], mode [7:5]; a
 * header register's dst at [14:8]. */
#define SIZE_32 0
#define SIZE_64 1
#define SIZE_128 2
#define MODE_WRITE 0
#define MODE_APPEND 1
#define MODE_BUCKET 2
#define MODE_AUGMENT 3
#define MODE_GATHER 4
#define MODE_COUNT 5
#define HEADER(mode, counter, size) ((mode) << 5 | (counter) << 2 | (size))
#define HDR_DST 8
#define DST_MASK (((1 << TILE_ID_BITS) - 1) << HDR_DST)

/* A scratchpad word address (5.4): byte address SCRATCHPAD_BASE | w << 2. */
#define WORD_ADDRESS(offset) ((offset) >> 2)

/* ---- The reports (11.5): word 0, op REPORT (1) at [17:10] and the number at
 * [25:18], the requester and the target (the master tile's, 0) left 0 ---- */

#define OP_REPORT 1
#define REPORT_WORD_0(number) ((number) << 18 | OP_REPORT << 10)
#define REPORT_PASS 0
#define REPORT_FAIL 1
#define REPORT_WAIT 2

#ifdef __ASSEMBLER__

/* ---- A report, for the programs (not the link script) ----
 *
 * One REPORT request (11.1): its words 0 to 2, then REQUEST_SEND, its word 3, which
 * stalls while the request queue is full. Clobbers t6.
 */
.macro report number, word1, word2, word3
    li      t6, REPORT_WORD_0(\number)
    sw      t6, REQUEST_OUT_W0(zero)
    sw      \word1, REQUEST_OUT_W1(zero)
    sw      \word2, REQUEST_OUT_W2(zero)
    sw      \word3, REQUEST_SEND(zero)
.endm

/* ---- Checks, for the programs ----
 *
 * Each ends the program with a failed check's report unless its condition holds:
 * REPORT_FAIL, with the check's number, the expected value and the actual value
 * (start.S, fail). They clobber t5 and t6; their operands must not be t5, t6, a0,
 * a1 or a2.
 */

/* register == expected, a constant */
.macro expect register, expected, check
    li      t6, \expected
    beq     \register, t6, .Lexpect_\@
    li      a0, \check
    mv      a1, t6
    mv      a2, \register
    j       fail
.Lexpect_\@:
.endm

/* first == second, both registers (second is the expected value) */
.macro expect_equal first, second, check
    beq     \first, \second, .Lexpect_equal_\@
    li      a0, \check
    mv      a1, \second
    mv      a2, \first
    j       fail
.Lexpect_equal_\@:
.endm

/* ---- Waits ----
 *
 * Until every event in `bits` has been seen in EVENTS: EVENTS_MASK set to them, then
 * a WFI (the wake is (EVENTS & EVENTS_MASK) != 0, 7.2) and a load of EVENTS, which
 * consumes what it returns of them, after every wake (17.2), until the loads have
 * returned each. Leaves EVENTS_MASK at `bits`. Clobbers t4, t5 and t6.
 */
.macro wait_events bits
    li      t6, \bits
    sw      t6, EVENTS_MASK(zero)
    li      t4, 0
.Lwait_events_\@:
    wfi
    lw      t5, EVENTS(zero)
    and     t5, t5, t6
    or      t4, t4, t5
    bne     t4, t6, .Lwait_events_\@
.endm

#endif
