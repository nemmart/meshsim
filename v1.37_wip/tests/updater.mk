# updater.mk: the rules (v1.37: the Updater black box's and the host link's tests and tools), included by the Makefile.
UPDATER_BENCH := $(B)/tests/updater_bench.o $(B)/tests/frames.o $(B)/tests/sha256.o $(B)/src/units/updater.o $(B)/src/units/file_store.o
$(B)/test_updater: $(UPDATER_BENCH)
$(B)/test_file_store: $(B)/src/units/file_store.o
# gold's frames fed to the Updater directly (tests/updater_frames_main.cpp)
$(B)/updater_frames: $(B)/tests/updater_frames_main.o $(UPDATER_BENCH) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/updater_frames
# the link's packets: the tests' provisional writer and Ingress's reader (tests/packetize_main.cpp)
$(B)/packetize: $(B)/tests/packetize_main.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/packetize
# Ingress's host link in the master tile's slot (tests/test_ingress_link.cpp)
$(B)/test_ingress_link: $(SLOT_LINK)
# Ingress's host link against gold's packetizer's vectors and packet files (tests/test_packet_vectors.cpp)
$(B)/test_packet_vectors: $(SLOT_LINK)
