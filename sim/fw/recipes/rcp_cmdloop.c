/* Recipe 1: the command loop (spec 6.3; CONVENTIONS 4). The boot is recipe 22. Commands here:
 *   CMD_SUM  (argument n): sum the n words of the argument block, plus the tile index; result in res[k]
 *   CMD_EXIT: end the test (simulator only, D36) */
#include "rcp.h"
#define CMD_SUM  1
#define CMD_EXIT 2
volatile uint32_t res[8], ncmd;

/* RECIPE cmdloop */
int main(void) {
    unsigned me = tile_index();
    for (;;) {
        uint32_t w = wait_for_command();          /* pulse done, then WFI; GO wakes it; returns the command word */
        volatile uint32_t *arg = CMD_ARGS(w);     /* this command's argument block (slots 1-7 or 8-15, by parity) */
        switch (CMD_TYPE(w)) {
        case CMD_SUM: {
            uint32_t s = me;
            for (uint32_t i = 0; i < CMD_ARG(w); i++) s += arg[i];
            res[ncmd++ & 7] = s;
            break;
        }
        case CMD_EXIT:
            sim_exit(0);                          /* simulator only: a real program loops for ever */
        }
    }
}
/* END */
