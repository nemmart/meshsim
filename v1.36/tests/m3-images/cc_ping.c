/* cc_ping.c (test_cc_units): a controller's program for the C&C units' integration test. Run on C&C 1
 * (unit 64), booted through the host port: three uncounted mode-0 writes through its send port, each exercising one of
 * the controller router's paths: to tile 5's data memory (the loopback to the link, across tile 0's fifth port and the
 * mesh), to its own data memory (the loopback ejected at the controller, with a loopback token), and a broadcast into
 * every tile's data memory (out on the link, rooted at tile 0, never back to the controller); then SIM_EXIT. Each send's
 * cycle and the cycle its STATUS poll sees send-busy clear go to stamp[] (mcycle), for ref's cycles (CcPing.java): the
 * first poll has nothing arriving at C&C 1 to wake its register block (the missed wake fixed in TileCtl). */
#include "mesh.h"
volatile unsigned stamp[6];
static inline unsigned cyc(void) { unsigned x; __asm__ volatile("csrr %0, mcycle" : "=r"(x)); return x; }
int main(void) {
    tile->ROUTER_HDR = ROUTER_HDR_W(5, MODE_WRITE, CTR_NONE) | HDR_SIZE(SIZE_32);
    tile->ROUTER_ADDR = 0x6000;
    stamp[0] = cyc();
    send32(0xC0FFEE05);
    while (tile->STATUS & ST_SEND_BUSY) ;
    stamp[1] = cyc();
    tile->ROUTER_HDR = ROUTER_HDR_W(tile_index(), MODE_WRITE, CTR_NONE) | HDR_SIZE(SIZE_32);
    tile->ROUTER_ADDR = 0x6000;
    stamp[2] = cyc();
    send32(0xC0FFEE40);
    while (tile->STATUS & ST_SEND_BUSY) ;
    stamp[3] = cyc();
    tile->ROUTER_HDR = ROUTER_HDR_W(126, MODE_WRITE, CTR_NONE) | HDR_SIZE(SIZE_32);
    tile->ROUTER_ADDR = 0x6004;
    stamp[4] = cyc();
    send32(0xB0B0CA57);
    while (tile->STATUS & ST_SEND_BUSY) ;
    stamp[5] = cyc();
    for (int i = 0; i < 200; i++) __asm__ volatile("nop");
    return 0x5EE;
}
