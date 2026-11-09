# net.mk: WORKER9's rules (the network's units and the chip's assembly in src/units/), included by sim/Makefile.
#
#   tests/ref/routerconf.sh --check   the router's conformance fixtures regenerated from ref and compared (needs a JDK)
#   tests/ref/cncconf.sh --check      C&C's likewise

NET_LINK := $(B)/src/units/net.o $(B)/src/units/router.o $(B)/src/units/inject.o $(B)/src/units/cnc.o $(B)/src/units/chip.o

$(B)/test_router $(B)/test_net_headers: $(NET_LINK)
$(B)/test_cnc: $(NET_LINK) $(B)/tests/statefp.o $(B)/tests/statefp_g2.o
$(B)/test_chip: $(NET_LINK) $(B)/tests/statefp.o $(B)/tests/statefp_g1.o $(B)/tests/statefp_g2.o $(UNITS_LINK) $(GOLDEN_OBJ)

# The chip on several threads under ThreadSanitizer (W4's build/<cc>-tsan/, its rules in threads.mk): make tsan-chip;
# in make check. Only form 2's test (the others are one thread).
NTB := build/$(CC_NAME)-tsan
NTB_LINK := $(addprefix $(NTB)/src/units/,net.o router.o inject.o cnc.o chip.o trace_log.o controller.o tile_ctl.o \
              periph_slot.o replayer.o file_store.o replay_tables.o tracer.o) \
            $(addprefix $(NTB)/tests/,statefp.o statefp_g1.o statefp_g2.o golden_run.o sha256.o)
$(NTB)/test_chip: $(NTB_LINK)
.PHONY: tsan-chip tsan-chip-one
tsan-chip-one: $(NTB)/test_chip
	TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1" ./$(NTB)/test_chip form2
tsan-chip:
	@set -e; for c in $(COMPILERS); do echo "==== $$c tsan (chip)"; $(MAKE) --no-print-directory CC_NAME=$$c tsan-chip-one; done
check: tsan-chip
