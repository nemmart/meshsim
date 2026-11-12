# tests/p7b.mk (WORKER25, P7b: Stage B and the replay acceptance kit): the rules for P7b's tests and tools (one
# `include` line in the Makefile).
# The replayer's error lists against ref's (tests/replayer/lists.txt; oracle/ref/replaylists.sh).
$(B)/test_replay_lists: $(REPLAYER_LINK) $(B)/tests/statefp.o
