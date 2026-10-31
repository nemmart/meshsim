/* rv32emu.c -- a minimal RV32I + Zmmul interpreter for cycle counts of the
 * planner, with design v2's controller timing (machine-spec 4.11): one
 * instruction per cycle, a taken branch or jump +1, a multiply 3 cycles,
 * loads and stores 1 cycle.  Divide, CSR, compressed and atomic instructions
 * are rejected, so a build that uses them fails loudly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "rv32emu.h"

uint8_t rv_mem[RV_MEM];
uint64_t rv_hist[0x2000];
int rv_prof = 0;

static uint32_t rd32(uint32_t a)
{
    if (a + 4 > RV_MEM || (a & 3)) { fprintf(stderr, "rv32: bad load32 %08x\n", a); exit(2); }
    uint32_t v; memcpy(&v, rv_mem + a, 4); return v;
}
static void wr(uint32_t a, uint32_t v, int n)
{
    if (a + n > RV_MEM || (a & (n - 1))) { fprintf(stderr, "rv32: bad store %08x\n", a); exit(2); }
    memcpy(rv_mem + a, &v, n);
}

uint64_t rv_call(uint32_t pc, uint32_t a0, uint32_t a1, uint32_t a2, uint64_t *instret)
{
    uint32_t x[32] = {0};
    x[1] = RV_SENTINEL; x[2] = RV_STACK_TOP; x[10] = a0; x[11] = a1; x[12] = a2;
    uint64_t cyc = 0, ins = 0;
    while (pc != RV_SENTINEL) {
        uint32_t i = rd32(pc), op = i & 0x7F, rd = (i >> 7) & 31, f3 = (i >> 12) & 7;
        uint32_t rs1 = (i >> 15) & 31, rs2 = (i >> 20) & 31, f7 = i >> 25;
        int32_t immI = (int32_t)i >> 20;
        int32_t immS = ((int32_t)i >> 25 << 5) | (int32_t)((i >> 7) & 31);
        int32_t immB = ((int32_t)i >> 31 << 12) | (int32_t)(((i >> 7) & 1) << 11)
                     | (int32_t)(((i >> 25) & 63) << 5) | (int32_t)(((i >> 8) & 15) << 1);
        int32_t immJ = ((int32_t)i >> 31 << 20) | (int32_t)(i & 0xFF000) | (int32_t)(((i >> 20) & 1) << 11)
                     | (int32_t)(((i >> 21) & 1023) << 1);
        uint32_t a = x[rs1], b = x[rs2], v = 0, npc = pc + 4;
        int wb = 1;
        cyc++; ins++;
        if (rv_prof && pc < 0x8000) rv_hist[pc >> 2]++;
        switch (op) {
        case 0x37: v = i & 0xFFFFF000u; break;                                   /* lui */
        case 0x17: v = pc + (i & 0xFFFFF000u); break;                            /* auipc */
        case 0x6F: v = pc + 4; npc = pc + immJ; cyc++; break;                    /* jal */
        case 0x67: v = pc + 4; npc = (a + immI) & ~1u; cyc++; break;             /* jalr */
        case 0x63: {                                                              /* branches */
            int t;
            switch (f3) {
            case 0: t = a == b; break; case 1: t = a != b; break;
            case 4: t = (int32_t)a < (int32_t)b; break; case 5: t = (int32_t)a >= (int32_t)b; break;
            case 6: t = a < b; break; case 7: t = a >= b; break;
            default: goto bad;
            }
            if (t) { npc = pc + immB; cyc++; }
            wb = 0; break;
        }
        case 0x03: {                                                              /* loads */
            uint32_t ad = a + immI, w;
            switch (f3) {
            case 0: w = rd32(ad & ~3u) >> (8 * (ad & 3)); v = (uint32_t)(int32_t)(int8_t)w; break;
            case 1: if (ad & 1) goto bad; w = rd32(ad & ~3u) >> (8 * (ad & 2)); v = (uint32_t)(int32_t)(int16_t)w; break;
            case 2: v = rd32(ad); break;
            case 4: w = rd32(ad & ~3u) >> (8 * (ad & 3)); v = w & 0xFF; break;
            case 5: if (ad & 1) goto bad; w = rd32(ad & ~3u) >> (8 * (ad & 2)); v = w & 0xFFFF; break;
            default: goto bad;
            }
            break;
        }
        case 0x23: {                                                              /* stores */
            uint32_t ad = a + immS;
            if (f3 == 0) wr(ad, b & 0xFF, 1); else if (f3 == 1) wr(ad, b & 0xFFFF, 2);
            else if (f3 == 2) wr(ad, b, 4); else goto bad;
            wb = 0; break;
        }
        case 0x13: {                                                              /* op-imm */
            uint32_t sh = rs2;
            switch (f3) {
            case 0: v = a + immI; break;
            case 1: v = a << sh; break;
            case 2: v = (int32_t)a < immI; break;
            case 3: v = a < (uint32_t)immI; break;
            case 4: v = a ^ immI; break;
            case 5: v = (f7 & 0x20) ? (uint32_t)((int32_t)a >> sh) : a >> sh; break;
            case 6: v = a | immI; break;
            case 7: v = a & immI; break;
            }
            break;
        }
        case 0x33:                                                                /* op */
            if (f7 == 1) {                                                        /* Zmmul */
                cyc += 2;
                switch (f3) {
                case 0: v = a * b; break;
                case 1: v = (uint32_t)(((int64_t)(int32_t)a * (int32_t)b) >> 32); break;
                case 2: v = (uint32_t)(((int64_t)(int32_t)a * (uint64_t)b) >> 32); break;
                case 3: v = (uint32_t)(((uint64_t)a * b) >> 32); break;
                default: fprintf(stderr, "rv32: divide at %08x\n", pc); exit(2);
                }
                break;
            }
            switch (f3) {
            case 0: v = (f7 & 0x20) ? a - b : a + b; break;
            case 1: v = a << (b & 31); break;
            case 2: v = (int32_t)a < (int32_t)b; break;
            case 3: v = a < b; break;
            case 4: v = a ^ b; break;
            case 5: v = (f7 & 0x20) ? (uint32_t)((int32_t)a >> (b & 31)) : a >> (b & 31); break;
            case 6: v = a | b; break;
            case 7: v = a & b; break;
            }
            break;
        case 0x0F: wb = 0; break;                                                 /* fence */
        default:
        bad:
            fprintf(stderr, "rv32: illegal instruction %08x at %08x\n", i, pc); exit(2);
        }
        if (wb && rd) x[rd] = v;
        pc = npc;
    }
    if (instret) *instret = ins;
    return cyc;
}
