# units.mk: the rules (the tile's units in src/units/ and their tests), included by the Makefile.

# the RTL's own core programs on the core alone (tests/core-programs/, tests/core_programs.h)
$(B)/test_core_programs: $(B)/src/units/trace_log.o $(B)/tests/sha256.o
$(B)/test_core_tile: $(B)/src/units/trace_log.o

# the canonical state fingerprint's C++ side (tests/statefp.*), and its test.
FP_OBJ := $(B)/tests/statefp.o
$(B)/test_statefp: $(FP_OBJ)

# the replayer (src/units/replayer.*), its tests and the conformance tool (tests/replayconf_main.cpp: a replayconf
# script made, or replayed on the model; the RTL's side is tests/replayer/README.md's).
REPLAYER_LINK := $(B)/src/units/replayer.o
$(B)/test_replayer $(B)/test_replayconf: $(REPLAYER_LINK) $(B)/tests/statefp.o
$(B)/test_replayer: $(B)/src/units/replay_tables.o
$(B)/replayconf: $(B)/tests/replayconf_main.o $(REPLAYER_LINK) $(B)/tests/statefp.o $(B)/src/units/replay_tables.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/replayconf
# Stage A's C++ driver (tests/replay_delivery_main.cpp): a delivery's windows on the C++ replayer, ref's lines.
$(B)/replay_delivery: $(B)/tests/replay_delivery_main.o $(REPLAYER_LINK) $(B)/src/units/replay_tables.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/replay_delivery
# the peripheral slot (src/units/periph_slot.*), with the replayer and the test peripheral it dispatches to.
SLOT_LINK := $(B)/src/units/periph_slot.o $(REPLAYER_LINK) $(TRACER_LINK)   # TRACER_LINK: tracer.mk, included first
$(B)/test_periph_slot: $(SLOT_LINK) $(B)/tests/statefp.o
# Ingress/Egress in the master tile's slot (tests/test_ingress_egress.cpp)
$(B)/test_ingress_egress: $(SLOT_LINK)

# the RW engine in the memory tile's slot (tests/test_rw_engine.cpp; tests/rw_engine_teeth.sh plants its bugs)
$(B)/test_rw_engine: $(SLOT_LINK)
