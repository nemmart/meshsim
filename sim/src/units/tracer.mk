# tracer.mk: WORKER14's rules (the Tracer, its memory system and the pieces it shares), included by sim/Makefile.
TRACER_LINK := $(B)/src/units/file_store.o $(B)/src/units/replay_tables.o
$(B)/test_tracer_parts: $(TRACER_LINK) $(B)/tests/sha256.o
