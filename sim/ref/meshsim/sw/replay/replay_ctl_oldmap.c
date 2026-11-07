/* replay_ctl.c built on the queue map before CL1 (test only, WORKER11): write log at offset 0x280000, expansion at
 * 0x300000. Under v1.25's map 0x280000 is in part 2 (the read log) and 0x300000 part 3 (the write log), so the pass
 * starves; StageB synthetic's "old queue map (CL4)" case checks that the window fails at once instead of hanging. */
#include <stdint.h>
#include "mesh.h"
#undef RP_Q_WLOG
#undef RP_Q_EXP
#define RP_Q_WLOG (REGION_PERIPH(1) + 0x280000u)
#define RP_Q_EXP  (REGION_PERIPH(1) + 0x300000u)
#include "replay_ctl.c"
