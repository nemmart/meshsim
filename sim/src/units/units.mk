# units.mk: WORKER5's rules (the controller's units in src/units/, the golden-trace harness), included by sim/Makefile.
#
#   make golden       every golden trace (tests/golden/RUNS), reported one by one
#
# controller.cpp needs W3's engine to link: until it lands it is compiled in every build (like the sketches), not linked.

UNITS_LINK  := $(B)/src/units/trace_log.o
UNITS_CHECK := $(B)/src/units/controller.o
GOLDEN_OBJ  := $(B)/tests/golden_run.o $(B)/tests/sha256.o

$(B)/test_rv32 $(B)/test_regblock $(B)/test_golden: $(UNITS_LINK) $(GOLDEN_OBJ)

$(B)/golden: $(B)/tests/golden_main.o $(GOLDEN_OBJ) $(UNITS_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)

all: $(UNITS_CHECK) $(B)/golden
run: $(UNITS_CHECK)

.PHONY: golden
golden: $(B)/golden
	./$(B)/golden
