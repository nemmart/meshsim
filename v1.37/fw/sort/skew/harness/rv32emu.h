/* rv32emu.h -- see rv32emu.c */
#ifndef RV32EMU_H
#define RV32EMU_H
#include <stdint.h>
#define RV_MEM        0x20000u
#define RV_STACK_TOP  0x20000u
#define RV_SENTINEL   0xFFFFFFF0u
extern uint8_t rv_mem[RV_MEM];
extern uint64_t rv_hist[0x2000];
extern int rv_prof;
/* call the function at pc with a0..a2; returns cycles (spec 4.11 timing) */
uint64_t rv_call(uint32_t pc, uint32_t a0, uint32_t a1, uint32_t a2, uint64_t *instret);
#endif
