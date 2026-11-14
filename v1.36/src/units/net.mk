# net.mk: the rules (the network's units and the chip's assembly in src/units/), included by sim/Makefile.
#
#   oracle/ref/routerconf.sh --check   the router's conformance fixtures regenerated from ref and compared (needs a JDK)
#   oracle/ref/cncconf.sh --check      C&C's likewise

NET_LINK := $(B)/src/units/net.o $(B)/src/units/router.o $(B)/src/units/inject.o $(B)/tests/bench_master.o $(B)/src/units/chip.o

$(B)/test_router $(B)/test_net_headers: $(NET_LINK)
$(B)/test_cnc: $(NET_LINK) $(B)/tests/statefp.o $(B)/tests/statefp_g2.o
$(B)/test_chip: $(NET_LINK) $(B)/tests/statefp.o $(B)/tests/statefp_g1.o $(B)/tests/statefp_g2.o $(UNITS_LINK) $(GOLDEN_OBJ)

# The chip on several threads under ThreadSanitizer: make tsan-chip, whose rules are tests/mach.mk's (v1.36: acceptance
# B's run split over threads). v1.35's, test_chip's form 2, is pending with test_chip (pending.mk); its rules stay:
# make tsan-chip-form2-one CC_NAME=<cc>, once test_chip is ported.
NTB := build/$(CC_NAME)-tsan
NTB_LINK := $(addprefix $(NTB)/src/units/,net.o router.o inject.o chip.o trace_log.o controller.o tile_ctl.o \
              periph_slot.o replayer.o file_store.o replay_tables.o tracer.o) \
            $(addprefix $(NTB)/tests/,statefp.o statefp_g1.o statefp_g2.o golden_run.o sha256.o)
$(NTB)/test_chip: $(NTB_LINK)
.PHONY: tsan-chip-form2-one
tsan-chip-form2-one: $(NTB)/test_chip
	TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1" ./$(NTB)/test_chip form2
