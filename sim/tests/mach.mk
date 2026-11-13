# mach.mk: the rules (the machine harness, tests/mach.h; the suites ported onto it), included by sim/Makefile.
MACH_OBJ  := $(B)/tests/mach.o $(B)/tests/checker.o $(B)/tests/phase_log.o $(B)/tests/suite.o
MACH_LINK := $(MACH_OBJ) $(B)/tests/statefp.o $(B)/tests/statefp_g1.o $(B)/tests/statefp_g2.o $(NET_LINK) $(UNITS_LINK) $(GOLDEN_OBJ)

$(B)/test_mach: $(MACH_LINK)
# test_checkself: oracle/ref/CheckSelf.java's sequence (the checker), apart from test_mach
$(B)/test_checkself: $(MACH_LINK)
