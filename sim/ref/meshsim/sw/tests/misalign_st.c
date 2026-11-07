/* v1.25 (DECISIONS-v1.25 s8b): a misaligned store is undefined behavior: meshsim stops, naming it (CtrlTest). Tile 12
 * stores a halfword at an odd address; the others exit. */
#include "mesh.h"
int main(void) {
    if (tile_index() == 12) __asm__ volatile ("sh zero, 1(%0)" :: "r"(REGION_DMEM + 0x100) : "memory");
    return 0;
}
