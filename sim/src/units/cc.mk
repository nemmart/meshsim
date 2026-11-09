# cc.mk: WORKER16's rules (G7: the C&C units and the chip's integration, M3), included by sim/Makefile.
#
#   tests/ref/cnc1conf.sh --check     C&C 1's peripheral's conformance fixtures regenerated from ref (needs a JDK)
#   tests/m3-images/build.sh --check  M3's firmware images rebuilt and compared (needs gcc-riscv64-unknown-elf)

$(B)/test_cc_units: $(NET_LINK) $(UNITS_LINK) $(GOLDEN_OBJ)
$(B)/test_cnc1_periph: $(SLOT_LINK)
# the T2 harness (tests/t2_harness.h): its pieces' test; the T2 tool comes with the slot's C&C 1
T2_LINK := $(B)/tests/t2_harness.o $(NET_LINK) $(UNITS_LINK) $(GOLDEN_OBJ) $(TRACER_LINK)
$(B)/test_t2_harness: $(T2_LINK)
