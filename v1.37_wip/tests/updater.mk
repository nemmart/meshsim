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
# the whole chip from a packet file: run_chip with the master tile's program for v1.37 (tests/proof_bench.h); the master
# tile's program on gold's fixtures (tests/test_master.cpp)
PROOF_LINK := $(B)/tests/proof_bench.o $(UPDATER_BENCH) $(SEGMENT_LINK)
$(B)/test_master: $(PROOF_LINK)
# the memory system, the memory tile's peripheral 1, in the chip (tests/test_memory_system.cpp; v1.37 stage 1)
$(B)/test_memory_system: $(PROOF_LINK)
# one frame file, N and s on the whole chip, with its figures (tests/proof_run_main.cpp)
$(B)/proof_run: $(B)/tests/proof_run_main.o $(PROOF_LINK) $(LIB_OBJ) $(WIRE_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/proof_run
# v1.37 stage 6 (the INTEGRATOR): the chain of processes on WORKER10's wire (WIRE_OBJ, tracer.mk): its launcher, the driver, and a stand-in ASIC made of the link's
# Node alone (tests/driver_main.cpp, tests/standin_asic_main.cpp); proof_run is the chip's process
$(B)/wire_launch: $(B)/wire/launch.o $(WIRE_OBJ)
	$(CXX) $(FLAGS) $^ -o $@
$(B)/driver: $(B)/tests/driver_main.o $(B)/tests/frames.o $(LINK_OBJ) $(WIRE_OBJ) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
$(B)/standin_asic: $(B)/tests/standin_asic_main.o $(B)/tests/frames.o $(LINK_OBJ) $(WIRE_OBJ) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/wire_launch $(B)/driver $(B)/standin_asic
