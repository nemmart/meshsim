# units.mk: WORKER5's rules (the controller's units in src/units/, the golden-trace harness), included by sim/Makefile.
#
#   make bench_controller   the controller's speed (tests/bench_controller.cpp): pin it (taskset -c $(tools/machine.sh cpus p1 1): D21)
#   make tsan-units   the threads test (test_controllers) under TSan, gcc and clang; also run by make check
#   make golden       every golden trace (tests/golden/RUNS) on the engine, with the debug checks, and on the direct driver

UNITS_LINK  := $(B)/src/units/trace_log.o $(B)/src/units/controller.o $(B)/src/units/tile_ctl.o \
               $(B)/src/units/periph_slot.o $(B)/src/units/replayer.o $(TRACER_LINK)   # W15: tile_ctl builds the slot (M3)
GOLDEN_OBJ  := $(B)/tests/golden_run.o $(B)/tests/sha256.o

$(B)/test_rv32 $(B)/test_regblock $(B)/test_golden $(B)/test_controllers $(B)/test_broken_units: $(UNITS_LINK) $(GOLDEN_OBJ)

$(B)/golden: $(B)/tests/golden_main.o $(GOLDEN_OBJ) $(UNITS_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)

$(B)/bench_controller: $(B)/tests/bench_controller.o $(GOLDEN_OBJ) $(UNITS_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)

all: $(B)/golden $(B)/bench_controller

.PHONY: golden
golden: $(B)/golden
	./$(B)/golden --direct
	./$(B)/golden --engine

.PHONY: bench_controller
bench_controller: $(B)/bench_controller
	./$(B)/bench_controller

# The threads test under ThreadSanitizer (W4's build/<cc>-tsan/ and its rules in threads.mk): make tsan-units; in make check.
UTB := build/$(CC_NAME)-tsan
$(UTB)/test_controllers: $(UTB)/src/units/trace_log.o $(UTB)/src/units/controller.o $(UTB)/tests/golden_run.o $(UTB)/tests/sha256.o
.PHONY: tsan-units tsan-units-one
tsan-units-one: $(UTB)/test_controllers
	TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1" ./$(UTB)/test_controllers
tsan-units:
	@set -e; for c in $(COMPILERS); do echo "==== $$c tsan (controllers)"; $(MAKE) --no-print-directory CC_NAME=$$c tsan-units-one; done
check: tsan-units

# WORKER8: the canonical state fingerprint's C++ side (tests/statefp.*), and its test.
FP_OBJ := $(B)/tests/statefp.o
$(B)/test_statefp: $(FP_OBJ)
$(B)/test_statefp_g1: $(FP_OBJ) $(B)/tests/statefp_g1.o
$(B)/test_statefp_core: $(FP_OBJ) $(B)/tests/statefp_g1.o $(UNITS_LINK) $(GOLDEN_OBJ)
$(B)/test_g1_tile $(B)/test_tile_slot $(B)/test_tileslotconf: $(UNITS_LINK)
$(B)/test_g1_replay: $(FP_OBJ) $(B)/tests/statefp_g1.o $(UNITS_LINK) $(GOLDEN_OBJ)

# WORKER15: the replayer (src/units/replayer.*), its tests and the conformance tool (tests/replayconf_main.cpp: a
# ReplayConf script replayed on the C++ replayer; oracle/ref/replayconf.sh writes ref's side).
REPLAYER_LINK := $(B)/src/units/replayer.o
$(B)/test_replayer $(B)/test_replayconf: $(REPLAYER_LINK) $(B)/tests/statefp.o
$(B)/test_replayer: $(B)/src/units/replay_tables.o
$(B)/replayconf: $(B)/tests/replayconf_main.o $(REPLAYER_LINK) $(B)/tests/statefp.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/replayconf
# Stage A's C++ driver (tests/replay_delivery_main.cpp): a delivery's windows on the C++ replayer, ref's lines.
$(B)/replay_delivery: $(B)/tests/replay_delivery_main.o $(REPLAYER_LINK) $(B)/src/units/replay_tables.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/replay_delivery
# WORKER15: the peripheral slot (src/units/periph_slot.*), with the replayer and the test peripheral it dispatches to.
SLOT_LINK := $(B)/src/units/periph_slot.o $(REPLAYER_LINK) $(TRACER_LINK)   # TRACER_LINK: tracer.mk, included first
$(B)/test_slotconf $(B)/test_periph_slot: $(SLOT_LINK) $(B)/tests/statefp.o
$(B)/slotconf: $(B)/tests/slotconf_main.o $(SLOT_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/slotconf
