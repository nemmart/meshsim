/* Recipe 22: the image's first command, boot part 2 (D31 step 5, D36, D37.2). */
#include "rcp.h"
extern void trap_handler(void);             /* recipe 24 */

/* RECIPE boot_part2 */
/* Boot part 2: the image's first command after the ROM's jump, CMD_STAGE with the number of 128-bit packets of
 * big-SRAM data C&C 1 sends, counted on counter 1, no sooner than stage_delay after its GO store (6.2). The counter
 * store comes first in the start path, so no packet can beat it. Then this image's handler and the hard interrupt
 * (D37.2: the ROM leaves interrupts off, and enabling them is the image's job), then the wait. */
static void boot_part2(uint32_t packets) {
    tile->WRITE_COUNTER[CTR_SRAM - 1] = packets;    /* first: the data may follow GO by stage_delay */
    __asm__ volatile ("csrw mtvec, %0" :: "r"(trap_handler));
    __asm__ volatile ("csrs mie, %0" :: "r"(1u << 11));   /* mie.MEIE (the bit is Q1's, the CSR list) */
    __asm__ volatile ("csrsi mstatus, 8");          /* mstatus.MIE: from here an error traps */
    if (packets) wait_all(EV_CTR(CTR_SRAM));        /* the data is in big SRAM; then done, in wait_for_command() */
}
/* END */

int main(void) {
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) == CMD_STAGE) boot_part2(CMD_ARG(w));
        else sim_exit(0);
    }
}
