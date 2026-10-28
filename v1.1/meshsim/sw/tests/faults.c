/* Controller faults (spec 4.11, 4.13, 6.1): tile i takes fault i mod 8, unless command slot 2 holds 0x600D (the
 * restart after HALT -> RUN: the marker's second argument), when it records its conditions and exits. */
#include "mesh.h"
volatile uint32_t ok, st;
int main(void) {
    unsigned me = tile_index();
    if (CMD_ARGS(COMMAND[0])[1] == 0x600D) { st = conditions(); ok = 0x600D0000 + me; return 0; }   /* M14: was status() */
    switch (me & 7) {
    case 0: TILE_STATUS = 1; break;                                   /* store to a read-only register */
    case 1: ok = *(volatile uint32_t *)0x100; break;                    /* load from instruction memory */
    case 2: *(volatile uint8_t *)ROUTER_SET_W1_ADDR = 1; break;                /* sub-word store to a register */
    case 3: __asm__ volatile (".word 0x02c5c533"); break;               /* div a0, a1, a2: no divider */
    case 4: __asm__ volatile (".word 0xc0002573"); break;               /* rdcycle a0: no CSRs */
    case 5: __asm__ volatile ("ecall"); break;
    case 6: ok = *(volatile uint32_t *)0x3200; break;                   /* the table window's unmapped part */
    default: __asm__ volatile ("lw a0, 2(%0)" :: "r"(DMEM_BASE) : "a0"); break;   /* misaligned load */
    }
    return 1;
}
