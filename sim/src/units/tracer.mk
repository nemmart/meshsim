# tracer.mk: WORKER14's rules (the Tracer, its memory system and the pieces it shares), included by sim/Makefile.
TRACER_LINK := $(B)/src/units/file_store.o $(B)/src/units/replay_tables.o $(B)/src/units/tracer.o
$(B)/test_tracer_parts: $(TRACER_LINK) $(B)/tests/sha256.o
$(B)/test_tracer: $(TRACER_LINK) $(B)/tests/statefp.o
# the Tracer in W15's slot unit (test_periph_slot's pattern), against SlotCore driven directly
$(B)/test_tracer_slot: $(B)/src/units/periph_slot.o $(B)/src/units/replayer.o $(TRACER_LINK) $(B)/tests/statefp.o
# the request bus (D34): ReqConf's replay
$(B)/test_reqbus: $(B)/tests/statefp.o
