/* events.h: the devices of the RTL's test memory map these programs use, and the event trigger
 * (tests/core-programs/README.md, "The interrupt programs"): a store of KIND | DELAY to the side-effect write device has
 * the test raise an input DELAY cycles after the cycle it sees the store counted in. */
#define RESULT_PASS   0x20000000
#define RESULT_FAIL   0x20000004
#define TRIGGER       0x20000014    /* the side-effect write device */
#define BARRIER       0x2000001C    /* a store lowers the line (MEIP) and with bit 0 arms */
#define RELEASE       0x000         /* the barrier's release: MEIP high from the cycle after */
#define NMI           0x100         /* the NMI input high from that cycle on */
#define MEIE          0x800

/* Fail with code \code (clobbers t6). */
.macro fail_with code
    li      a0, \code
    j       fail
.endm
