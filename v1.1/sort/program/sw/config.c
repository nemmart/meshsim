/* config.c -- the direct or LB path's tables and K's configure (worker 6). The XFER overlay (M7): RED's tail calls it, and when
 * stage 2 or 3 settles on direct or LB, main.c loads XFER again and calls it. */
#include "sort.h"

/* The direct or LB tables for K and V, then K's configure (the start command loads the run).
 * direct: 16 groups at R = Rdir. LB (skew-v2.md 2.2): group 15's loopback stream carries nothing (count 0) and runs at
 * R'15; a 17th group of one stream, d = 0, t = 1, carries the loopback stream at R = Rloop. */
void config_direct_lb(void) {
    Tile *T = &SK;
    unsigned me = tile_index(), k128 = (S.fmt == FMT_K128);
    unsigned lb = (T->path == PATH_LB);
    S.groups = lb ? 17 : 16;
    wait_tables();
    for (int g = 0; g < NG; g++) {
        uint32_t R = (lb && g == NG - 1) ? T->Rp15 : T->Rdir[g];
        set_group(GRP_K + g, GROUP_LO_R(R), GROUP_HI(STR_K + 4 * g, 4, 0));
        if (k128) set_group(GRP_V + g, GROUP_LO_R(R), GROUP_HI(STR_V + 4 * g, 4, 0));
    }
    if (lb) {
        uint32_t c0 = S.cut[me], cnt = T->row[me];  /* the loopback stream: this tile's own slice */
        STREAM_COUNT[STR_K + 60] = STREAM_COUNT_ENTRY(0, MODE_SRC, SIZE_64, 0);         /* group 15's d = 0 stream: empty */
        set_stream(STR_KLB, STREAM_SRC_ADDR(BUF_X + (c0 << 1)), STREAM_DEST_ENTRY(0, 0), STREAM_COUNT_ENTRY(cnt, KEY_MODE(k128), SIZE_64, 0));
        set_group(GRP_K + NG, GROUP_LO_R(T->Rloop), GROUP_HI(STR_KLB, 1, 0));
        if (k128) {
            STREAM_COUNT[STR_V + 60] = STREAM_COUNT_ENTRY(0, MODE_SRC, SIZE_128, 0);
            set_stream(STR_VLB, STREAM_SRC_ADDR(BUF_VS + (c0 << 2)), STREAM_DEST_ENTRY(0, 0), STREAM_COUNT_ENTRY(cnt, MODE_SRC, SIZE_128, 0));
            set_group(GRP_V + NG, GROUP_LO_R(T->Rloop), GROUP_HI(STR_VLB, 1, 0));
        }
    }
    DMA_RECORD_BASE_ADDRESS = BUF_Y;                  /* k = 2: received key i gets ordinal i (the key streams send mode 5) */
    DMA_CONFIG = DMA_CFG_RUN(RUN_GROUPS, GRP_K, S.groups) | DMA_CFG_HISTDIG(1);
}
