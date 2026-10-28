/* Boot test: every tile writes its index, a constant and its big-SRAM size, then exits (ebreak). */
#include "mesh.h"
volatile uint32_t result[4];
const uint32_t magic = 0xC0FFEE00;
int main(void) {
    result[0] = tile_index();
    result[1] = magic + tile_index();
    result[2] = sram_chunks();
    result[3] = 0;                                 /* PTR[64] is write-only: test/CtrlTest reads it through the simulator */
    return 7;
}
