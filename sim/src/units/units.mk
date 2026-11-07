# units.mk: WORKER5's rules (the controller's units in src/units/, the golden-trace harness), included by sim/Makefile.
#
#   make bench_controller   the controller's speed (tests/bench_controller.cpp): pin it (taskset -c 0)
#   make golden       every golden trace (tests/golden/RUNS) on the engine, with the debug checks, and on the direct driver

UNITS_LINK  := $(B)/src/units/trace_log.o $(B)/src/units/controller.o
GOLDEN_OBJ  := $(B)/tests/golden_run.o $(B)/tests/sha256.o

$(B)/test_rv32 $(B)/test_regblock $(B)/test_golden: $(UNITS_LINK) $(GOLDEN_OBJ)

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
