#include "../../wip/sw/mesh-v1.2-draft.h"
uint32_t t(void) { tile->WRITE_COUNTER[0] = 5; start_run(); return wait_all(EV_CTR(1) | EV_RUN_DONE) + wait_for_command(); }
