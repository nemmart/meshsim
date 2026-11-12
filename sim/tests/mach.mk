# mach.mk: WORKER20's rules (the machine harness, tests/mach.h; the suites ported onto it), included by sim/Makefile.
MACH_OBJ  := $(B)/tests/mach.o $(B)/tests/checker.o $(B)/tests/phase_log.o $(B)/tests/suite.o
MACH_LINK := $(MACH_OBJ) $(NET_LINK) $(UNITS_LINK) $(GOLDEN_OBJ)

$(B)/test_mach: $(MACH_LINK)
