/* Controller faults (spec 4.11, 4.13): tile i takes fault i mod 8, unless command slot 2 holds 0x600D (the restart
 * after HALT -> RUN: the marker's second argument), when it records its errors and exits. PHASE-2 INTERIM: a fault
 * halts the core (phase 4 makes it a terminal trap). */
#include "mesh.h"
volatile uint32_t ok, st;
int main(void) {
    unsigned me = tile_index();
    if (CMD_ARGS(COMMAND[0])[1] == 0x600D) { st = tile->ERRORS; ok = 0x600D0000 + me; return 0; }
    switch (me & 7) {
    case 0: *(volatile uint32_t *)&tile->STATUS = 1; break;            /* store to a read-only register */
    case 1: *(volatile uint32_t *)(REGION_PMEM + 0x100) = 1; break;    /* store to program memory */
    case 2: *(volatile uint8_t *)&tile->ROUTER_SET_W1 = 1; break;     /* sub-word store to a register */
    case 3: __asm__ volatile (".word 0x02c5c533"); break;               /* div a0, a1, a2: no divider */
    case 4: __asm__ volatile (".word 0xc0002573"); break;               /* rdcycle a0: PHASE-2 INTERIM, no CSRs yet */
    case 5: __asm__ volatile ("ecall"); break;
    case 6: ok = tables->PTR[3]; break;                                 /* a load from PTR: bus error */
    default: __asm__ volatile ("lw a0, 2(%0)" :: "r"(REGION_DMEM + 0x100) : "a0"); break;   /* misaligned load */
    }
    return 1;
}
