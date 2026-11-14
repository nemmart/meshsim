# cc.mk: the rules (G7: the C&C units and the chip's integration, M3), included by sim/Makefile.
#
#   oracle/ref/cnc1conf.sh --check     C&C 1's peripheral's conformance fixtures regenerated from ref (needs a JDK)
#   tests/m3-images/build.sh --check  M3's firmware images rebuilt and compared (needs gcc-riscv64-unknown-elf)

$(B)/test_cc_units: $(NET_LINK) $(UNITS_LINK) $(GOLDEN_OBJ)
$(B)/test_cnc1_periph: $(SLOT_LINK)
# test_cc_req (pending.mk) was written on T2's harness (tests/t2_harness.h), which left the tree with T2: its port brings
# what it needs of it.
$(B)/test_cc_req: $(NET_LINK) $(UNITS_LINK) $(GOLDEN_OBJ) $(TRACER_LINK)
