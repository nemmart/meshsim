# tracer.mk: the rules (the Tracer, its memory system and the pieces it shares), included by the Makefile.
# (v1.37: with the Updater black box, which the peripheral slot can hold and which works on the file store)
# v1.37 stage 4 (the INTEGRATOR): WORKER8's link, the library of ../updater_wip/link built in place from its sources
# with the simulator's flags (its interface hostlink.h alone is included; nothing of it is copied): Ingress/Egress's
# link ends (ingress_egress.h), and the host's end in the harnesses.
LINK_DIR := ../updater_wip/link
LINK_OBJ := $(B)/link/hostlink.o $(B)/link/link_end.o $(B)/link/node.o
$(B)/link/%.o: $(LINK_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -I$(LINK_DIR) -c $< -o $@
# v1.37 stage 6: WORKER10's wire (../updater_wip/wire), built in place from its sources with the simulator's flags, as
# the link is (wire.h alone is included): the chip's ports on a chain of processes (tests/wire_ends.h, in the machine
# harness), the driver, the launcher.
WIRE_DIR := ../updater_wip/wire
WIRE_OBJ := $(B)/wire/wire.o $(B)/wire/wire_args.o
$(B)/wire/%.o: $(WIRE_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -DWIRE_CHECK -I$(WIRE_DIR) -c $< -o $@
TRACER_LINK := $(B)/src/units/file_store.o $(B)/src/units/replay_tables.o $(B)/src/units/tracer.o $(B)/src/units/updater.o \
               $(B)/src/units/cache_ctl.o $(B)/src/units/axi_arbiter.o $(B)/src/units/lpddr_model.o \
               $(LINK_OBJ)   # (updater_wip WORKER5's: cache.mk; WORKER8's: LINK_OBJ)
$(B)/test_tracer_parts: $(TRACER_LINK) $(B)/tests/sha256.o
$(B)/test_tracer: $(B)/tests/tracer_rig.o $(B)/tests/tracer_block.o $(TRACER_LINK) $(B)/tests/statefp.o
# the Tracer in the slot unit (test_periph_slot's pattern), against SlotCore driven directly
$(B)/test_tracer_slot: $(B)/src/units/periph_slot.o $(B)/src/units/replayer.o $(TRACER_LINK) $(B)/tests/statefp.o
# the request bus (D34): ReqConf's replay
$(B)/test_reqbus: $(B)/tests/statefp.o
