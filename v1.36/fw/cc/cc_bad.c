/* A tile image that fails while the boot ROM's handler is still installed (WORKER14; CcCoresTest's two-report path,
 * DECISIONS-v1.25 8d): tile 12 loads from peripheral 3's window, which is absent on a mesh tile: a bus error (ERRORS
 * [9], 4.11) and a trap to mtvec, still the ROM's report-and-park handler, since this image never installs its own. The
 * ROM sends its two-report dump (sequence 24 and 25) and parks; the error line takes C&C 1 into its error path. Every
 * other tile waits for commands. */
#include <stdint.h>
int main(void) {
    if (tile_index() == 12) (void)*(volatile uint32_t *)0xFFFFFE00u;   /* peripheral 3's window, word 0 */
    for (;;) (void)wait_for_command();
}
