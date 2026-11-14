/* The digit all-to-all on the 16-group schedule (schedule-4.md) as a two-command phase, programmed from C (spec 6.3; v1.2).
 * The tile's parameters arrive in big SRAM at PARAMS (the test puts them there): cnt[64] records for each destination
 * (the send buffer at chunk 0 is sorted by destination), inc[64] records from each source, R[16], the group gap.
 * Start-up copies them into data memory with the 6.5 block copy. Commands:
 *   1 configure: STREAM, GROUP, PTR (receive buffer at 0x40000, sources in order), DMA_CONFIG
 *   2 start:     WRITE_COUNTER_1 = records expected, start, WFI for counters done
 *   3 exit */
#include "mesh.h"
#define PARAMS 0xF0000
#define SRC 0
#define INBOX 0x40000
static const uint8_t G4[16][4] = { { 1, 29, 35, 63 }, { 2, 30, 34, 62 }, { 3, 31, 33, 61 }, { 5, 23, 41, 59 }, { 6, 22, 42, 58 },
    { 7, 21, 43, 57 }, { 8, 28, 36, 56 }, { 9, 27, 37, 55 }, { 10, 26, 38, 54 }, { 11, 25, 39, 53 }, { 12, 24, 40, 52 },
    { 13, 15, 49, 51 }, { 14, 18, 46, 50 }, { 16, 20, 44, 48 }, { 17, 19, 45, 47 }, { 0, 4, 32, 60 } };
struct params { uint32_t cnt[64], inc[64], R[16], gap, pad; } __attribute__((aligned(8)));
static struct params P;
volatile uint32_t phases;
int main(void) {
    unsigned me = tile_index();
    /* the parameters: a block copy from big SRAM (6.5) */
    set_stream(255, STREAM_SRC_W(PARAMS, 0), (uint32_t)&P, STREAM_COUNT_W(sizeof P / 8, SIZE_64, CTR_CM, MODE_WRITE, 0));
    tile->WRITE_COUNTER[CTR_CM - 1] = sizeof P / 8;
    tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 255, 1);
    start_run();
    wfi();
    uint32_t expected = 0;
    for (;;) {
        uint32_t w = wait_for_command();
        phases++;
        if (CMD_TYPE(w) == 1) {
            uint32_t first[64], f = 0;
            for (int d = 0; d < 64; d++) { first[d] = f; f += P.cnt[d]; }
            for (int g = 0; g < 16; g++) {
                for (int i = 0; i < 4; i++) {
                    unsigned off = G4[g][i], d = (me + off) & 63;
                    set_stream(4 * g + i, STREAM_SRC_W(SRC + 2 * first[d], 0), 0, STREAM_COUNT_W(P.cnt[d], SIZE_64, CTR_SRAM, MODE_SRC, off));
                }
                set_group(g, GROUP_LO_R(P.R[g]), GROUP_HI(4 * g, 4, P.gap));
            }
            uint32_t at = INBOX;
            expected = 0;
            for (int s = 0; s < 64; s++) { tables->PTR[s] = at; at += 2 * P.inc[s]; expected += P.inc[s]; }
            tile->DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, 0, 16);
        } else if (CMD_TYPE(w) == 2) {
            tile->WRITE_COUNTER[CTR_SRAM - 1] = expected;   /* the counters first, */
            start_run();                     /* then the start pulse */
            wfi();
        } else return 0;
    }
}
