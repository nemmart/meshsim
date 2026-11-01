/* C&C 2's and C&C 3's firmware (WORKER14; meshsim with Config.ccCores): one image for both, its id from PARAMS. C&C 1
 * lands it and its first command (spec 6.2 step 4); its start-up sends C&C 1 a "started" request (REQ_OP_STARTED, W3
 * its id), and then it serves its receive queue (target 1 or 2) as a stub: every request popped and counted in mbox,
 * none answered (the file system and the pager are their engines', deferred; the engines are stubs, CcCores.Stub). */
#include <stdint.h>
volatile uint32_t mbox[4] __attribute__((section(".data"))) = { 0 };   /* [0] started sent at (mcycle), [1] requests popped */
int main(void) {
    mbox[0] = mcycle();
    request_send(tile_index(), 0, 0, REQ_CC1(REQ_OP_STARTED, 0, 0));   /* a controller's request: no reply (D38) */
    tile->EVENTS_MASK = EV_REQ_PENDING;
    for (;;) {
        while (tile->EVENTS_PEEK & EV_REQ_PENDING) {
            (void)tile->REQUEST_GET_W0;
            (void)tile->REQUEST_RECEIVE;
            mbox[1]++;
        }
        __asm__ volatile ("wfi" ::: "memory");
    }
}
