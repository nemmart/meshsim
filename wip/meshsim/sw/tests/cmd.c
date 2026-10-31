/* Command phases (spec 6.3): a command loop on wait_for_command().
 *   CMD 1 (one-command phase):  sum += argument + slot 1
 *   CMD 2 "configure":          a stream run copying N 64-bit records from chunk 0 to PTR[me] = 0x20000 (DMA_CONFIG);
 *                               then wait for the next command
 *   CMD 3 "start":              WRITE_COUNTER_1 = N, start the run (identical code from WFI on every tile), WFI for
 *                               counters done
 *   CMD 4:                      exit (SIM_EXIT = the sum) */
#include "mesh.h"
#define N 512
volatile uint32_t sum, phases, cmd_words[8], arg1s[8];
int main(void) {
    unsigned me = tile_index();
    for (;;) {
        uint32_t w = wait_for_command(), a1 = CMD_ARGS(w)[0];
        if (phases < 8) { cmd_words[phases] = w; arg1s[phases] = a1; }
        phases++;
        switch (CMD_TYPE(w)) {
        case 1:
            sum += CMD_ARG(w) + a1;
            break;
        case 2:
            set_stream(0, STREAM_SRC_W(0, 0), 0, STREAM_COUNT_W(N, SIZE_64, CTR_SRAM, MODE_SRC, 0));
            tables->PTR[me] = 0x20000;
            tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, 0, 1);
            break;
        case 3:
            tile->WRITE_COUNTER[CTR_SRAM - 1] = N;
            start_run();
            wfi();
            break;
        default:
            return (int)sum;
        }
    }
}
