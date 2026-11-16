/* port.c -- the port layer's C (../PORTING.md): the input-load stand-in, the tile's report to the master tile when
 * main returns, and the buffers' layout table for cargo xtask. */
#include "sort.h"

/* The input-load stand-in, in place of SortTest's poke of S.n_in (sort.h: "INPUT-LOAD STAND-IN, placed by the
 * harness"): the records each tile holds are the image's argument ARG_RECORDS, which the master tile writes into
 * the arguments' row in its load (mesh.h; rtl/tile/base/spec.md 6.5). The records themselves are in the scratchpad
 * from the first cycle: the chip's test writes them into each tile's scratchpad SRAMs in the model before the run,
 * as SortTest's loadInput pokes them (rtl/chip/test/src/sort.rs). */
void port_start(void) {
    S.n_in = ARGS[ARG_RECORDS];
}

/* The tile's report on main's return, in place of meshsim's exit word: one request to the master tile (11.5), op
 * REQ_OP_RESULT, the master's firmware's (cc/cc1.c): word 1 main's return value, word 2 S.n_recv, the records the tile holds at the
 * end, word 3 ERRORS; word 0 names its reply, a 32-bit WRITE to the master's pair of the reply block, counted on
 * CTR_REPLY, armed first (11.4). The tile waits for the reply before the image ends: a request that asks for none would
 * stay outstanding until the master pops it (11.3), and the boot ROM's LOAD, which the image's end sends, would then
 * be a second to the master tile, past its per-tile limit of one. The request tree paces the 64 tiles' reports, which
 * all leave main in the same cycle. */
_Static_assert(REPLY_WORD(DST_CC1) % 16 == 0 && REPLY_WORD(DST_CC1) >> 4 <= 0x7FF,
               "REQ_CC1's argument holds the reply's address");
void port_exit(int value) {
    tile->WRITE_COUNTER[CTR_REPLY - 1] = 1;
    request_send(tile->ERRORS, S.n_recv, (uint32_t)value,
                 REQ_CC1(REQ_OP_RESULT, CTR_REPLY, REPLY_WORD(DST_CC1) >> 4));
    wait_all(EV_CTR(CTR_REPLY));
}

/* The layout the chip's test writes the input into and reads the expected results from (rtl/chip/test/src/sort.rs):
 * the buffers' word addresses, from sort.h, in a section no tile loads (sort.ld: .sort_layout, not allocated). */
__attribute__((section(".sort_layout"), used)) static const uint32_t sort_layout[] = {
    0x4C41594Fu,   /* "LAYO" */
    BUF_IN, BUF_VIN, BUF_Y, BUF_VS,
};
