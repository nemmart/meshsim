# tests/p7b.mk (WORKER25, P7b: Stage B and the replay acceptance kit): the rules for P7b's tests and tools (one
# `include` line in the Makefile).
# The replayer's error lists against ref's (tests/replayer/lists.txt; oracle/ref/replaylists.sh).
$(B)/test_replay_lists: $(REPLAYER_LINK) $(B)/tests/statefp.o
# The acceptance kit's replayer (ref's ReplayShard): tests/replay_shard_main.cpp.
$(B)/replay_shard: $(B)/tests/replay_shard_main.o $(B)/tests/sha256.o $(REPLAYER_LINK) $(B)/src/units/replay_tables.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/replay_shard
# The acceptance kit's summary step (run_shard.sh's Python in v1.3): tests/acceptance/shard_summary.cpp.
$(B)/shard_summary: $(B)/tests/acceptance/shard_summary.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/shard_summary
# The summary step against v1.3's (tests/acceptance/summary/, oracle/ref/shardsummary.sh): needs shard_summary built.
$(B)/test_shard_summary: | $(B)/shard_summary
# v1.3's ReplayerTest (run_tests.sh's step replayer): W15's ported parts and ConvertDelivery's groups on convert_delivery.
$(B)/suite_replayer: $(B)/tests/suite_replayer.o $(B)/tests/sha256.o $(B)/tests/statefp.o $(REPLAYER_LINK) $(B)/src/units/replay_tables.o $(LIB_OBJ) | $(B)/convert_delivery
	$(CXX) $(FLAGS) $(filter-out $(B)/convert_delivery,$^) -o $@ $(WRAP)
$(B)/test_suite_replayer: | $(B)/suite_replayer
# Stage B (ref's StageB): tests/stageb.cpp on W20's harness (tests/mach.mk's MACH_LINK); the driver stageb.
$(B)/stageb: $(B)/tests/stageb_main.o $(B)/tests/stageb.o $(B)/tests/sha256.o $(B)/tests/statefp.o $(MACH_LINK) $(B)/src/units/replay_tables.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $(sort $^) -o $@ $(WRAP)
all: $(B)/stageb
$(B)/test_stageb: | $(B)/stageb
