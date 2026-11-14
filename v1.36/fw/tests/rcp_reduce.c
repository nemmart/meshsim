/* Recipe: a mesh reduction (max) of a small vector. Command 1 (argument a): every tile's vector is
 * v[i] = (me * 37 + i * 11 + a) & 0xFFF; the result lands in res. Command 0: exit. */
#define RCP_REDUCE
#include "rcp.h"
uint32_t res[RW];
static uint32_t v[RW];
int main(void) {
    reduce_init();
    unsigned me = tile_index();
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) != 1) return 0;
        for (int i = 0; i < RW; i++) v[i] = (me * 37 + i * 11 + CMD_ARG(w)) & 0xFFF;
        reduce_max(v, res);
    }
}
