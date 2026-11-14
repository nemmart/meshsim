# mach.mk: the rules (the machine harness, tests/mach.h; the suites and tools built on it), included by sim/Makefile.
# MACH_LINK: the harness, the checker, the suites' printer, and the chip (the network's units, a tile's control part with
# the peripheral slot and what it holds). v1.35's M1 bench (src/units/controller.*), the golden-trace driver and the
# fingerprints of v1.3's state (tests/statefp*) are not part of it (PENDING-v1.36.md).
MACH_OBJ  := $(B)/tests/mach.o $(B)/tests/checker.o $(B)/tests/suite.o
MACH_LINK := $(MACH_OBJ) $(NET_LINK) $(B)/src/units/trace_log.o $(B)/src/units/tile_ctl.o $(B)/src/units/periph_slot.o \
             $(B)/src/units/replayer.o $(TRACER_LINK)

$(B)/test_mach: $(MACH_LINK)
# acceptance B: the RTL's sort images on the chip (tests/test_rtl_sort.cpp)
$(B)/test_rtl_sort: $(MACH_LINK)
# test_checkself: oracle/ref/CheckSelf.java's sequence (the checker), apart from test_mach
$(B)/test_checkself: $(MACH_LINK)

# The chip on several threads under ThreadSanitizer (build/<cc>-tsan/, its rules in src/threads.mk): make tsan-chip; in
# make check. Acceptance B's 16-record run split over 2 and over 8 threads (the nets and the request tree cross
# domains), the digest at every poll against one domain's.
MTB_LINK := $(subst $(B)/,$(TB)/,$(MACH_LINK))
$(TB)/test_rtl_sort: $(MTB_LINK)
.PHONY: tsan-chip tsan-chip-one
tsan-chip-one: $(TB)/test_rtl_sort
	TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1" ./$(TB)/test_rtl_sort sort_16_kv32_split
tsan-chip:
	@set -e; for c in $(COMPILERS); do echo "==== $$c tsan (chip)"; $(MAKE) --no-print-directory CC_NAME=$$c tsan-chip-one; done
