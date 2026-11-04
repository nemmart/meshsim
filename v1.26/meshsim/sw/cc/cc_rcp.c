/* The units' image for rcp_cc1.c's main (WORKER14; RecipeTest's cc1 driver, C&C 1 as a core): rcp_cc1.c boots one
 * image, from the same chunks of its big SRAM, onto C&C 2, C&C 3 and the tiles, so this image is all three, and fits the
 * 64 + 16 packets rcp_cc1.c streams (build_cc.sh checks). A controller (its PARAMS id 65 or 66) sends C&C 1 "started"
 * (6.2 step 4) and then reads each command that lands (C&C 3 gets one more: recipe fence's CMD_JUMP, from the send
 * port, while it runs; its program-memory overlay is this same image, its data-memory overlay zeroes 0x10-0x10F, where
 * nothing of this image lives but `commands`). A tile runs recipe 22's tile side: CMD_STAGE is boot part 2 (as
 * rcp_boot.c's boot_part2, recipe 22: the counter, recipe 24's handler and the hard interrupt, the wait for the
 * data); any other command is a phase that does nothing. `commands` counts the commands run. */
#include <stdint.h>
extern void trap_handler(void);                     /* recipe 24 */
volatile uint32_t commands;
static void boot_part2(uint32_t packets) {           /* rcp_boot.c's, recipe 22 */
    tile->WRITE_COUNTER[CTR_SRAM - 1] = packets;
    __asm__ volatile ("csrw mtvec, %0" :: "r"(trap_handler));
    __asm__ volatile ("csrs mie, %0" :: "r"(1u << 11));
    __asm__ volatile ("csrsi mstatus, 8");
    if (packets) wait_all(EV_CTR(CTR_SRAM));
}
int main(void) {
    if (tile_index() >= 64) {
        request_send(tile_index(), 0, 0, REQ_CC1(REQ_OP_STARTED, 0, 0));
        tile->EVENTS_MASK = EV_COMMAND;
        for (;;) {
            __asm__ volatile ("wfi" ::: "memory");
            if (tile->EVENTS & EV_COMMAND) { (void)COMMAND[0]; commands++; }   /* every command word read (6.3) */
        }
    }
    for (;;) {
        uint32_t w = wait_for_command();
        if (CMD_TYPE(w) == CMD_STAGE) boot_part2(CMD_ARG(w));
        commands++;
    }
}
