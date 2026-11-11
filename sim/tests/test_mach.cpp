// test_mach.cpp (WORKER20): the machine harness (mach.h). With the header only: mach::fields against ref's Fields.java
// (the values printed by ref's own Fields.java, 2026-11-10), and the header compiling with the units it names.
#include "harness.h"
#include "mach.h"
#include "units/ag.h"
#include "units/tile_ctl.h"

using namespace mach::fields;

TEST(mach_fields_as_refs_Fields) {
    CHECK_EQ(master_config(RUN_GROUPS, 0, 16, 0, 0, 1), 0x2001u);                // A2A.load's DMA_CONFIG
    CHECK_EQ(master_config(RUN_STREAMS, 255, 511, 7, 7, 8), 0x07FFFFFEu);
    CHECK_EQ(stream_src(0x1FFFFF, 7), 0x00FFFFFFu);
    CHECK_EQ(stream_count(0x1FFFF, SIZE_64, 1, 1, 63), 0x7E4BFFFFu);              // count, size 1, counter 1, M_SRC, d 63
    CHECK_EQ(group_lo(0xFFFFFFFF), 0x3FFFFu);
    CHECK_EQ(group_hi(4, 4, 0xFF), 0xFF304u);
    CHECK_EQ(hdr_bits(SIZE_128, 7, 7, 127), 0xFFFC0000u);
}
