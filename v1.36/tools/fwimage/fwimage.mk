# tools/fwimage: fwimage makes the sort runs' key files, in the RTL's format (fwimage.cpp's header; fw/README.md), and
# dumps what a loader takes from an ELF file. fw/check_images.sh and fw/build.sh check its output against the committed
# keys; run_tests.sh builds the 64K key file with it.
#   build/<cc>-<mode>/fwimage keys RECORDS OUT; ... loaded ELF OUT
$(B)/fwimage: $(B)/tools/fwimage/fwimage.o
	$(CXX) $(FLAGS) $^ -o $@
all: $(B)/fwimage
