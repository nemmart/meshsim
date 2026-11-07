# units.mk: WORKER5's rules (the controller's units in src/units/, the golden-trace harness), included by sim/Makefile.
#
#   make bench_controller   the controller's speed (tests/bench_controller.cpp): pin it (taskset -c 0)
#   make tsan-units   the threads test (test_controllers) under TSan, gcc and clang; also run by make check
#   make golden       every golden trace (tests/golden/RUNS) on the engine, with the debug checks, and on the direct driver

UNITS_LINK  := $(B)/src/units/trace_log.o $(B)/src/units/controller.o
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
