/* v1.25 (DECISIONS-v1.25 s8b): a misaligned load is undefined behavior: meshsim stops, naming it (CtrlTest). Tile 9 loads
 * a word at an odd address; the others exit. */
#include "mesh.h"
volatile uint32_t sink;
int main(void) {
    if (tile_index() == 9) { uint32_t v; __asm__ volatile ("lw %0, 2(%1)" : "=r"(v) : "r"(REGION_DMEM + 0x100)); sink = v; }
    return 0;
}
