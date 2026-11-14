# Startup and the ends of the rebuilt tile's programs (tile_map.h has their protocol
# with the test), linked into every program.
#
# The boot ROM jumps to IMEM_BASE: _start, the first word of .text.start, which
# link.ld places there (6.5). _start jumps to the program's main, mtvec left at the
# ROM's handler, so that every trap sends reports 24 and 25. The programs are assembly
# with no nested calls, so they use no stack and no global pointer; their arguments
# are in DMEM, at ARGUMENTS (tile_map.h).
#
# A program ends by jumping to pass, with its words in a0 to a2, or to fail, with a
# check's number, its expected value and its actual value in a0 to a2 (tile_map.h's
# expect): each sends its report, then ends: on a compute tile, a jump to the reset
# PC, its counters at 0, so that the boot ROM asks for its next load (6.5); on a
# special tile, the park: WFI with mie 0, which nothing but the NMI wakes. A program
# that waits for an error's NMI parks there too (errors.S).

#include "tile_map.h"

    .section .text.start, "ax"

# At IMEM_BASE.
    .globl _start
_start:
    j       main

# ---- The ends ----

    .globl pass
pass:
    report  REPORT_PASS, a0, a1, a2
    j       end

    .globl fail
fail:
    report  REPORT_FAIL, a0, a1, a2

end:
    lw      t0, PARAMS(zero)
    andi    t0, t0, PARAMS_TILE_ID
    li      t1, TILE_COUNT
    bgeu    t0, t1, park                # a special tile
    li      t0, RESET_PC
    jr      t0

    .globl park
park:
    csrw    mie, zero
1:
    wfi
    j       1b
